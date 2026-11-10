#!/usr/bin/env python3
"""Stage a pinned DarwinBoot with the opt-in SCM transcript, never edit source trees."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
BASE = '85332b317ee80898fc78939ea64e6ca52d3837a3'
ALIGNED_SHA = 'a18db6f543c49f2f67be9a8a541981ddcf65324e74783e182f5ab7c08a47a593'


def integrate(source):
    def replace(old, new, count=1):
        nonlocal source
        if source.count(old) != count:
            raise ValueError('unexpected DarwinBoot anchor: ' + old)
        source = source.replace(old, new)
    replace('STATIC UINT32 U32', '#include "CepheusScmPair.h"\n\nSTATIC UINT32 U32')
    replace('BOOLEAN *KeepPacked)', 'BOOLEAN *KeepPacked, UINTN *ScmOffset)')
    replace('  UINTN  MemoryCandidate;', '  UINTN  MemoryCandidate;\n  UINTN  ScmCandidate;')
    replace('  MemoryCandidate = MAX_UINTN;', '  MemoryCandidate = MAX_UINTN;\n  ScmCandidate = MAX_UINTN;')
    replace('    *Offset += Length;', '''    if (AsciiStrCmp ((CONST CHAR8 *)(Data + *Offset - 36), CE_SCM_HANDOFF_PROPERTY) == 0) {
      if ((ScmCandidate != MAX_UINTN) || (Length != CE_SCM_HANDOFF_BYTES)) { return FALSE; }
      for (UINTN Byte = 0; Byte < Length; Byte++) { if (Data[*Offset + Byte] != 0) { return FALSE; } }
      ScmCandidate = *Offset;
    }
    *Offset += Length;''')
    replace('  if (Children > (Size - *Offset) / 8)', '''  if (ScmCandidate != MAX_UINTN) {
    if ((Path != 1) || (*ScmOffset != MAX_UINTN)) { return FALSE; }
    *ScmOffset = ScmCandidate;
  }
  if (Children > (Size - *Offset) / 8)''')
    replace('WindowSize, KeepPacked))', 'WindowSize, KeepPacked, ScmOffset))')
    replace('  UINT64                          Video[6];', '  UINT64                          Video[6];\n  UINTN                           ScmOffset;\n  int                             ScmProfile;')
    replace('  Root = NULL;', '''  ScmProfile = ceph_scm_pair_profile (CommandLine);
  if (ScmProfile < 0) { return EFI_INVALID_PARAMETER; }
  if ((ScmProfile == 1) && mCepheusScmAttempted) { return EFI_ALREADY_STARTED; }
  Root = NULL;''')
    replace('  Offset = 0;\n  RamdiskOffset', '  Offset = 0;\n  ScmOffset = MAX_UINTN;\n  RamdiskOffset')
    replace('&WindowSize, &KeepPacked)', '&WindowSize, &KeepPacked, &ScmOffset)')
    replace('  if (KeepPacked &&', '''  if ((ScmProfile == 1) != (ScmOffset != MAX_UINTN)) { Status = EFI_COMPROMISED_DATA; goto Fail; }
  if (KeepPacked &&''')
    replace('  for (Attempt = 0; Attempt < 3; Attempt++) {\n    MapSize', '''  if (ScmProfile == 1) { CepheusScmCapture (); }
  for (Attempt = 0; Attempt < 3; Attempt++) {
    MapSize''')
    replace('      DarwinEnter (ArgsAddress', '''      if (ScmProfile == 1) { CepheusScmHandoff (TreeAddress + ScmOffset, Attempt); }
      DarwinEnter (ArgsAddress''')
    return source


def integrate_snapshot(source, rpmh_tail=False):
    def replace(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise ValueError('unexpected RPMh snapshot anchor: ' + old)
        source = source.replace(old, new)
    header = 'CepheusRpmhTailHandoff.h' if rpmh_tail else 'CepheusRpmhSnapshot.h'
    property_name = 'CE_RPMH_TAIL_HANDOFF_PROPERTY' if rpmh_tail else 'CE_RPMH_SNAPSHOT_PROPERTY'
    slot_bytes = 'CE_RPMH_TAIL_HANDOFF_BYTES' if rpmh_tail else 'CE_RPMH_SNAPSHOT_BYTES'
    profile = 'ceph_rpmh_tail_profile' if rpmh_tail else 'ceph_rpmh_snapshot_profile'
    attempted = 'mCepheusRpmhTailAttempted' if rpmh_tail else 'mCepheusRpmhAttempted'
    prepare = 'CepheusRpmhTailPrepare' if rpmh_tail else 'CepheusRpmhPrepare'
    handoff = 'CepheusRpmhTailHandoff' if rpmh_tail else 'CepheusRpmhHandoff'
    replace('#include "CepheusScmPair.h"', '#include "CepheusScmPair.h"\n#include "'+header+'"')
    replace('UINTN *ScmOffset)', 'UINTN *ScmOffset, UINTN *RpmhOffset)')
    replace('  UINTN  ScmCandidate;', '  UINTN  ScmCandidate;\n  UINTN  RpmhCandidate;')
    replace('  ScmCandidate = MAX_UINTN;', '  ScmCandidate = MAX_UINTN;\n  RpmhCandidate = MAX_UINTN;')
    replace('    *Offset += Length;', '''    if (AsciiStrCmp ((CONST CHAR8 *)(Data + *Offset - 36), '''+property_name+''') == 0) {
      if ((RpmhCandidate != MAX_UINTN) || (Length != '''+slot_bytes+''')) { return FALSE; }
      for (UINTN Byte = 0; Byte < Length; Byte++) { if (Data[*Offset + Byte] != 0) { return FALSE; } }
      RpmhCandidate = *Offset;
    }
    *Offset += Length;''')
    replace('  if (Children > (Size - *Offset) / 8)', '''  if (RpmhCandidate != MAX_UINTN) {
    if ((Path != 1) || (*RpmhOffset != MAX_UINTN)) { return FALSE; }
    *RpmhOffset = RpmhCandidate;
  }
  if (Children > (Size - *Offset) / 8)''')
    replace('WindowSize, KeepPacked, ScmOffset))', 'WindowSize, KeepPacked, ScmOffset, RpmhOffset))')
    replace('  int                             ScmProfile;', '  int                             ScmProfile;\n  int                             RpmhProfile;\n  UINTN                           RpmhOffset;')
    replace('  ScmProfile = ceph_scm_pair_profile (CommandLine);', '''  RpmhProfile = '''+profile+''' (CommandLine);
  if (RpmhProfile < 0) { return EFI_INVALID_PARAMETER; }
  if ((RpmhProfile == 1) && '''+attempted+''') { return EFI_ALREADY_STARTED; }
  ScmProfile = ceph_scm_pair_profile (CommandLine);''')
    replace('  ScmOffset = MAX_UINTN;', '  ScmOffset = MAX_UINTN;\n  RpmhOffset = MAX_UINTN;')
    replace('&WindowSize, &KeepPacked, &ScmOffset)', '&WindowSize, &KeepPacked, &ScmOffset, &RpmhOffset)')
    replace('  if (KeepPacked &&', '''  if ((RpmhProfile == 1) != (RpmhOffset != MAX_UINTN)) { Status = EFI_COMPROMISED_DATA; goto Fail; }
  if (KeepPacked &&''')
    replace('  Status = GetMap (&Map, &Capacity, &MapSize, &Key, &Stride);', '''  if (RpmhProfile == 1) { '''+prepare+''' (); }
  Status = GetMap (&Map, &Capacity, &MapSize, &Key, &Stride);''')
    if not rpmh_tail:
        replace('  if (ScmProfile == 1) { CepheusScmCapture (); }', '''  if (RpmhProfile == 1) { CepheusRpmhBind (Map, MapSize, Stride); }
  if (ScmProfile == 1) { CepheusScmCapture (); }''')
        replace('    Status = gBS->ExitBootServices (mImage, Key);', '''    if (RpmhProfile == 1) { CepheusRpmhCapture (Map, MapSize, Stride); }
    Status = gBS->ExitBootServices (mImage, Key);''')
    replace('      DarwinEnter (ArgsAddress', '''      if (RpmhProfile == 1) { '''+handoff+''' (TreeAddress + RpmhOffset, Attempt); }
      DarwinEnter (ArgsAddress''')
    if rpmh_tail:
        replace('  if ((Depth > 64) || (Size - *Offset < 8)) { return FALSE; }',
                '  if (!Data || !Offset || *Offset > Size || (Depth > 64) || (Size - *Offset < 8)) { return FALSE; }')
        # ReadFile can return success without data if GetInfo unexpectedly
        # succeeds on its zero-capacity query. Reject that before parsing/copy.
        for pointer, size in (('Image', 'ImageSize'), ('Tree', 'TreeSize')):
            anchor = ('  Status = ReadFile (Root, '+('Request->KernelPath, SIZE_128MB' if pointer == 'Image'
                       else 'TreePath, SIZE_16MB')+', &'+pointer+', &'+size+');\n'
                      '  if (EFI_ERROR (Status)) { goto Fail; }')
            replace(anchor, anchor+'\n  if (!'+pointer+' || !'+size+') { Status = EFI_COMPROMISED_DATA; goto Fail; }')
    return source


def stage(repository, output, rpmh_snapshot=False, rpmh_tail=False):
    if rpmh_snapshot and rpmh_tail:
        raise ValueError('RPMh profiles must be isolated')
    if output.exists():
        raise ValueError('output already exists')
    archive = subprocess.check_output(['git', '-C', str(repository), 'archive', BASE])
    output.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(archive)) as files:
        files.extractall(output, filter='data')
    subprocess.run(['git', 'init', '-q', str(output)], check=True)
    subprocess.run(['git', 'apply', str(ROOT/'patches/darwinboot-cepheus.patch')], cwd=output, check=True)
    driver = output/'DarwinBootPkg/Driver'
    source = driver/'DarwinBoot.c'
    if hashlib.sha256(source.read_bytes()).hexdigest() != ALIGNED_SHA:
        raise ValueError('aligned baseline differs')
    source.write_text(integrate(source.read_text()))
    for file in ('cepheus_scm_discovery.h', 'cepheus_scm_handoff.h'):
        shutil.copyfile(ROOT/'xnu'/file, driver/file)
    shutil.copyfile(ROOT/'firmware/CepheusScmPair.h', driver/'CepheusScmPair.h')
    if rpmh_snapshot:
        source.write_text(integrate_snapshot(source.read_text()))
        for file in ('CepheusRpmhSnapshot.h', 'cepheus_rpmh_snapshot.h',
                     'cepheus_rpmh_snapshot_reader.h', 'cepheus_rpmh_pool.h'):
            shutil.copyfile(ROOT/'firmware'/file, driver/file)
        subprocess.run(['python3', str(ROOT/'scripts/generate-rpmh-snapshot-identity.py'),
                        str(ROOT.parent), str(driver/'CepheusRpmhSnapshotIdentity.h')], check=True)
    if rpmh_tail:
        source.write_text(integrate_snapshot(source.read_text(), rpmh_tail=True))
        for file in ('CepheusRpmhTailHandoff.h', 'CepheusRpmhTailProtocol.h',
                     'cepheus_rpmh_tail_handoff.h', 'cepheus_rpmh_tail.h', 'cepheus_rpmh_snapshot.h'):
            shutil.copyfile(ROOT/'firmware'/file, driver/file)
    manifest = {'base_commit': BASE, 'aligned_sha256': ALIGNED_SHA, 'files': {}}
    for file in sorted(driver.iterdir()):
        if file.is_file():
            manifest['files'][file.name] = hashlib.sha256(file.read_bytes()).hexdigest()
    (output/'scm-pair-source.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('repository', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--rpmh-snapshot', action='store_true')
    parser.add_argument('--rpmh-tail', action='store_true')
    args = parser.parse_args()
    print(json.dumps(stage(args.repository.resolve(), args.output.resolve(), args.rpmh_snapshot, args.rpmh_tail), indent=2))
