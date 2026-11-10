#!/usr/bin/env python3
"""Stage an isolated actual DXE core with allocation-lifetime bookkeeping."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
BASE = 'c6d8901de2ffaabc6110c81e90a915f84795fbd5'
CORE = Path('MdeModulePkg/Core/Dxe')
PINS = {
    'Mem/Pool.c': 'f856e6071081709d3786127647736f0e3232f69bc7e4dd5b8d4eb530cb0a537a',
    'DxeMain.inf': 'a35e70b115d9616672cffd4c077afb9495063f0896a4b7536503dbe643662065',
    'DxeMain/DxeMain.c': 'bf96c08f7baae086b4cde0dc7d596b61e541f3c5f305f587358bbfa1ad62698d',
    'Mem/Page.c': '544c621cff7263653f1eced8be3dc7f71185a93ece08f241f557a73f3c005275',
}


def integrate(source):
    def replace(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise ValueError('unexpected pool anchor: ' + old)
        source = source.replace(old, new)
    replace('#include "HeapGuard.h"', '''#include "HeapGuard.h"
#include "cepheus_pool_lifetime.h"

STATIC CE_POOL_LIFETIME mCepheusPoolLifetime;''')
    replace('STATIC EFI_LOCK  mPoolMemoryLock = EFI_INITIALIZE_LOCK_VARIABLE (TPL_NOTIFY);', '''STATIC EFI_LOCK  mPoolMemoryLock = EFI_INITIALIZE_LOCK_VARIABLE (TPL_NOTIFY);

STATIC EFI_STATUS EFIAPI
CoreCepheusPoolLookup (UINT64 Address, UINT64 Bytes, UINT32 Type,
                       UINT64 Generation, CE_POOL_ALLOCATION *Output)
{
  if (Output == NULL) { return EFI_INVALID_PARAMETER; }
  *Output = (CE_POOL_ALLOCATION){0};
  if (gMemoryMapTerminated || (gEfiCurrentTpl != TPL_APPLICATION)) { return EFI_ACCESS_DENIED; }
  if (EFI_ERROR (CoreAcquireLockOrFail (&mPoolMemoryLock))) { return EFI_ACCESS_DENIED; }
  int Result = CePoolLookup (&mCepheusPoolLifetime, Address, Bytes, Type, Generation, Output);
  CoreReleaseLock (&mPoolMemoryLock);
  return Result == 0 ? EFI_SUCCESS : EFI_NOT_FOUND;
}

/* A pre-EBS metadata query. Never authorizes object reads or resource writes. */
typedef struct {
  UINT32 Version, Bytes;
  EFI_STATUS (EFIAPI *Lookup)(UINT64, UINT64, UINT32, UINT64, CE_POOL_ALLOCATION *);
} CE_POOL_QUERY;
STATIC CE_POOL_QUERY mCepheusPoolQuery = {1, sizeof (CE_POOL_QUERY), CoreCepheusPoolLookup};
STATIC EFI_GUID mCepheusPoolQueryGuid = {0x2401bd95, 0x43d7, 0x4349, {0x9e,0x0a,0xca,0x00,0xce,0x30,0x04,0x01}};

VOID
CoreCepheusPoolQueryInstall (VOID)
{
  EFI_HANDLE Handle = NULL;
  CoreInstallProtocolInterface (&Handle, &mCepheusPoolQueryGuid, EFI_NATIVE_INTERFACE, &mCepheusPoolQuery);
}

VOID
CoreCepheusPoolRangeChanged (UINT64 Start, UINT64 Pages)
{
  ASSERT_LOCKED (&gMemoryLock);
  CePoolRangeChanged (&mCepheusPoolLifetime, Start, Pages);
}''')
    replace('  BOOLEAN    PageAsPool;\n\n  ASSERT_LOCKED',
            '  BOOLEAN    PageAsPool;\n  UINTN      RequestedSize = Size;\n\n  ASSERT_LOCKED')
    replace('  return Buffer;\n}\n\n/**\n  Frees pool.', '''  if (Buffer != NULL) {
    CePoolAllocated (&mCepheusPoolLifetime, (UINTN)Buffer, RequestedSize, (UINT32)PoolType);
  }
  return Buffer;
}

/**
  Frees pool.''')
    replace('  ASSERT (Buffer != NULL);', '''  ASSERT (Buffer != NULL);
  CePoolFreed (&mCepheusPoolLifetime, (UINTN)Buffer);''')
    return source


def integrate_page(source):
    old = '  Entry         = NULL;\n  NumberOfBytes = LShiftU64 (NumberOfPages, EFI_PAGE_SHIFT);'
    if source.count(old) != 1:
        raise ValueError('unexpected page-conversion anchor')
    return source.replace(old, '''  extern VOID CoreCepheusPoolRangeChanged (UINT64 Start, UINT64 Pages);
  if ((ChangingType && (NewType == EfiConventionalMemory)) || ChangingAttributes) {
    CoreCepheusPoolRangeChanged (Start, NumberOfPages);
  }
'''+old)


def tail_replace(source, old, new):
    if source.count(old) != 1:
        raise ValueError('unexpected exit-tail anchor: ' + old)
    return source.replace(old, new)


def integrate_tail_pool(source):
    source = tail_replace(source, '#define POOL_FREE_SIGNATURE', '#include "CepheusRpmhTailCore.h"\n\n#define POOL_FREE_SIGNATURE')
    source = tail_replace(source, '  CoreInstallProtocolInterface (&Handle, &mCepheusPoolQueryGuid, EFI_NATIVE_INTERFACE, &mCepheusPoolQuery);',
                            '  CoreInstallProtocolInterface (&Handle, &mCepheusPoolQueryGuid, EFI_NATIVE_INTERFACE, &mCepheusPoolQuery);\n  extern VOID CoreCepheusTailInstall (VOID);\n  CoreCepheusTailInstall ();')
    source = tail_replace(source, '  ASSERT_LOCKED (&mPoolMemoryLock);\n\n  if ((PoolType',
                            '  ASSERT_LOCKED (&mPoolMemoryLock);\n  if (CoreCepheusTailFrozen ()) { mCeTail.Failed = TRUE; return NULL; }\n\n  if ((PoolType')
    source = tail_replace(source, '  ASSERT (Buffer != NULL);\n  CePoolFreed',
                            '  ASSERT (Buffer != NULL);\n  if (CoreCepheusTailFrozen ()) { mCeTail.Failed = TRUE; return EFI_ACCESS_DENIED; }\n  CePoolFreed')
    return source


def integrate_tail_page(source):
    source = tail_replace(source, 'MEMORY_MAP  mMapStack[MAX_MAP_DEPTH];', 'MEMORY_MAP  mMapStack[MAX_MAP_DEPTH];\n#include "CepheusRpmhTailMap.h"')
    old = '  extern VOID CoreCepheusPoolRangeChanged (UINT64 Start, UINT64 Pages);'
    source = tail_replace(source, old, '  if (CoreCepheusTailRejectMutation ()) { return EFI_ACCESS_DENIED; }\n'+old)
    old = '    CoreCepheusPoolRangeChanged (Start, NumberOfPages);'
    source = tail_replace(source, old, old+'\n    CeTailMapRangeChanged (Start, NumberOfPages);')
    source = tail_replace(source, '  if ((Start & EFI_PAGE_MASK) != 0) {',
                            '  if (CoreCepheusTailRejectMutation ()) { return; }\n  if ((Start & EFI_PAGE_MASK) != 0) {')
    source = tail_replace(source, '  RemoveEntryList (&Entry->Link);\n  Entry->Link.ForwardLink = NULL;',
                            '  if (CoreCepheusTailRejectMutation ()) { return; }\n  RemoveEntryList (&Entry->Link);\n  Entry->Link.ForwardLink = NULL;')
    source = tail_replace(source, '  if (IsListEmpty (&mFreeMemoryMapEntryList)) {',
                            '  if (CoreCepheusTailRejectMutation ()) { return NULL; }\n  if (IsListEmpty (&mFreeMemoryMapEntryList)) {')
    source = tail_replace(source, '    if (FreeDescriptorEntries != NULL) {',
                            '    if (FreeDescriptorEntries != NULL) {\n      CeTailMapAllocated ((UINTN)FreeDescriptorEntries, DEFAULT_PAGE_ALLOCATION_GRANULARITY);')
    source = tail_replace(source, '  if (mFreeMapStack != 0) {',
                            '  if (CoreCepheusTailRejectMutation ()) { return; }\n  if (mFreeMapStack != 0) {')
    return source


def integrate_tail_main(source):
    start = source.index('CoreExitBootServices (')
    end = source.index('\n}', start) + 2
    before, body, after = source[:start], source[start:end], source[end:]
    old = '  EFI_STATUS  Status;'
    body = tail_replace(body, old, old+'\n  extern VOID CoreCepheusTailAttempt (VOID);\n  CoreCepheusTailAttempt ();')
    old = '  gRuntime->AtRuntime = TRUE;'
    body = tail_replace(body, old, old+'\n\n  extern VOID CoreCepheusRpmhTail (VOID);\n  CoreCepheusRpmhTail ();')
    return before+body+after


def stage(repository, output, rpmh_tail=False):
    if output.exists():
        raise ValueError('output already exists')
    archive = subprocess.check_output(['git', '-C', str(repository), 'archive', BASE])
    output.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(archive)) as files:
        files.extractall(output, filter='data')
    # Git archives omit recursive submodules. Resolve them to existing local
    # dependencies without modifying those trees or silently fetching versions.
    tree = subprocess.check_output(['git', '-C', str(repository), 'ls-tree', '-r', '-z', BASE])
    dependencies = {}
    for entry in tree.split(b'\0'):
        if not entry:
            continue
        metadata, name = entry.split(b'\t', 1)
        mode, _, commit = metadata.split()
        if mode == b'160000':
            path = Path(name.decode())
            local = repository/path
            actual = subprocess.check_output(['git', '-C', str(local), 'rev-parse', 'HEAD']).decode().strip()
            if actual != commit.decode():
                raise ValueError('submodule commit mismatch: ' + str(path))
            target = output/path
            if target.exists():
                target.rmdir()
            target.symlink_to(local, target_is_directory=True)
            dependencies[str(path)] = actual
    supplemental = repository/'MdePkg/Include/AsmMacroLib.h'
    if supplemental.is_file():
        shutil.copyfile(supplemental, output/'MdePkg/Include/AsmMacroLib.h')
    for name, digest in PINS.items():
        if hashlib.sha256((output/CORE/name).read_bytes()).hexdigest() != digest:
            raise ValueError('pinned core source mismatch: ' + name)
    pool = output/CORE/'Mem/Pool.c'
    pool.write_text(integrate(pool.read_text()))
    page = output/CORE/'Mem/Page.c'
    page.write_text(integrate_page(page.read_text()))
    shutil.copyfile(ROOT/'firmware/cepheus_pool_lifetime.h', output/CORE/'Mem/cepheus_pool_lifetime.h')
    inf = output/CORE/'DxeMain.inf'
    text = inf.read_text()
    if text.count('  Mem/Pool.c') != 1:
        raise ValueError('unexpected core INF')
    inf.write_text(text.replace('  Mem/Pool.c', '  Mem/Pool.c\n  Mem/cepheus_pool_lifetime.h'))
    main = output/CORE/'DxeMain/DxeMain.c'
    text = main.read_text()
    anchor = '  CoreInitializeDispatcher ();'
    if text.count(anchor) != 1:
        raise ValueError('unexpected dispatcher anchor')
    main.write_text(text.replace(anchor, '  extern VOID CoreCepheusPoolQueryInstall (VOID);\n  CoreCepheusPoolQueryInstall ();\n'+anchor))
    manifest = {'base_commit': BASE, 'bookkeeping_only': not rpmh_tail, 'dependencies': dependencies, 'files': {}}
    if rpmh_tail:
        manifest['rpmh_exit_tail_diagnostic'] = True
        pool.write_text(integrate_tail_pool(pool.read_text()))
        page.write_text(integrate_tail_page(page.read_text()))
        main.write_text(integrate_tail_main(main.read_text()))
        tailfiles = ('CepheusRpmhTailCore.h', 'CepheusRpmhTailMap.h', 'CepheusRpmhTailProtocol.h', 'cepheus_rpmh_tail.h',
                     'cepheus_rpmh_snapshot.h', 'cepheus_rpmh_snapshot_reader.h', 'cepheus_rpmh_pool.h')
        for name in tailfiles:
            shutil.copyfile(ROOT/'firmware'/name, output/CORE/'Mem'/name)
        subprocess.run(['python3', str(ROOT/'scripts/generate-rpmh-snapshot-identity.py'), str(ROOT.parent),
                        str(output/CORE/'Mem/CepheusRpmhSnapshotIdentity.h'), '--omit-core'], check=True)
        text = inf.read_text()
        text = text.replace('  Mem/Pool.c', '  Mem/Pool.c\n'+''.join('  Mem/'+name+'\n' for name in
                            (*tailfiles, 'CepheusRpmhSnapshotIdentity.h')).rstrip(), 1)
        inf.write_text(text)
    if supplemental.is_file():
        manifest['supplemental_asm_header_sha256'] = hashlib.sha256(supplemental.read_bytes()).hexdigest()
    for path in sorted((output/CORE).rglob('*')):
        if path.is_file():
            manifest['files'][str(path.relative_to(output))] = hashlib.sha256(path.read_bytes()).hexdigest()
    (output/'pool-lifetime-source.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('repository', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--rpmh-tail', action='store_true')
    args = parser.parse_args()
    manifest = stage(args.repository.resolve(), args.output.resolve(), args.rpmh_tail)
    print(json.dumps({'base_commit': manifest['base_commit'], 'files': len(manifest['files']),
                      'bookkeeping_only': manifest['bookkeeping_only']}, indent=2))
