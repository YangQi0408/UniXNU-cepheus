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


def stage(repository, output):
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
    args = parser.parse_args()
    print(json.dumps(stage(args.repository.resolve(), args.output.resolve()), indent=2))
