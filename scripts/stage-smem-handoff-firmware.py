#!/usr/bin/env python3
"""Stage pinned DarwinBoot + same-boot SMEM map evidence; never edit siblings."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


PAIR = module('smem_pair_stage', 'stage-scm-pair-firmware.py')
MAP = module('smem_map_stage', 'stage-smem-map-firmware.py')


def integrate(source):
    def replace(old, new, count=1):
        nonlocal source
        if source.count(old) != count:
            raise ValueError('unrecognized SMEM loader anchor: ' + old)
        source = source.replace(old, new)
    replace('#include "CepheusScmPair.h"', '#include "CepheusSmemMapHandoff.h"')
    for old, new in (('CE_SCM_HANDOFF_PROPERTY', 'CE_SMEM_MAP_PROPERTY'),
                     ('CE_SCM_HANDOFF_BYTES', 'CE_SMEM_MAP_BYTES'),
                     ('ceph_scm_pair_profile', 'ceph_smem_map_profile'),
                     ('mCepheusScmAttempted', 'mCepheusSmemAttempted')):
        replace(old, new)
    # Reuse the already-tested chosen/zero/unique-slot parser integration,
    # not its SCM capture or callbacks. No SCM header remains in this package.
    source = source.replace('ScmOffset', 'SmemOffset').replace('ScmCandidate', 'SmemCandidate').replace('ScmProfile', 'SmemProfile')
    replace('  if (SmemProfile == 1) { CepheusScmCapture (); }\n', '')
    replace('  Status = GetMap (&Map, &Capacity, &MapSize, &Key, &Stride);', '''  if (SmemProfile == 1) {
    Status = CepheusSmemCapture ();
    if (Status == EFI_ACCESS_DENIED) { CpuDeadLoop (); }
    if (EFI_ERROR (Status)) { goto Fail; }
  }
  Status = GetMap (&Map, &Capacity, &MapSize, &Key, &Stride);''')
    replace('      if (SmemProfile == 1) { CepheusScmHandoff (TreeAddress + SmemOffset, Attempt); }', '''      if ((SmemProfile == 1) && !CepheusSmemHandoff (TreeAddress, TreeSize, SmemOffset, Attempt)) { CpuDeadLoop (); }''')
    replace('  return gBS->UninstallMultipleProtocolInterfaces', '''  if (mCepheusSmemAttempted) {
    EFI_STATUS Close = CepheusSmemMapClose ();
    if (EFI_ERROR (Close) && Close != EFI_NOT_STARTED) { return Close; }
  }
  return gBS->UninstallMultipleProtocolInterfaces''')
    replace('  if ((Depth > 64) || (Size - *Offset < 8)) { return FALSE; }',
            '  if (!Data || !Offset || *Offset > Size || (Depth > 64) || (Size - *Offset < 8)) { return FALSE; }')
    for pointer, path, size in (('Image', 'Request->KernelPath, SIZE_128MB', 'ImageSize'),
                                ('Tree', 'TreePath, SIZE_16MB', 'TreeSize')):
        anchor = '  Status = ReadFile (Root, ' + path + ', &' + pointer + ', &' + size + ');\n  if (EFI_ERROR (Status)) { goto Fail; }'
        replace(anchor, anchor + '\n  if (!' + pointer + ' || !' + size + ') { Status = EFI_COMPROMISED_DATA; goto Fail; }')
    return source


def stage(repository, uefi, output):
    if output.exists():
        raise ValueError('output exists; preserve prior evidence')
    for relative, expected in ((MAP.PROFILE, MAP.PROFILE_SHA), (MAP.HEADER, MAP.HEADER_SHA)):
        if hashlib.sha256((uefi / relative).read_bytes()).hexdigest() != expected:
            raise ValueError('unrecognized platform bytes: ' + relative)
    PAIR.stage(repository, output)
    driver = output / 'DarwinBootPkg/Driver'
    source = driver / 'DarwinBoot.c'
    source.write_text(integrate(source.read_text()))
    for name in ('CepheusScmPair.h', 'cepheus_scm_handoff.h', 'cepheus_scm_discovery.h'):
        (driver / name).unlink()
    (output / 'scm-pair-source.json').unlink()
    copies = {ROOT / ('firmware/' + name): driver / name
              for name in ('CepheusSmemMapProbe.c', 'CepheusSmemMapProbe.h', 'CepheusSmemMapHandoff.h')}
    copies.update({ROOT / 'xnu/cepheus_smem_map_handoff.h': driver / 'cepheus_smem_map_handoff.h',
                   ROOT / 'firmware/SmemMapProbePkg.dec': output / 'SmemMapProbePkg/SmemMapProbePkg.dec',
                   ROOT / 'firmware/SmemMapPlatform.inf': output / 'SmemMapProbePkg/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.inf',
                   uefi / MAP.PROFILE: output / 'SmemMapProbePkg/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.c',
                   uefi / MAP.HEADER: output / 'SmemMapProbePkg/Include/Library/PlatformMemoryMapLib.h'})
    for original, target in copies.items():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
    probe_sha = hashlib.sha256((driver / 'CepheusSmemMapProbe.c').read_bytes()).hexdigest()
    identities = [('Probe', probe_sha), ('Profile', MAP.PROFILE_SHA), ('Header', MAP.HEADER_SHA)]
    (driver / 'CepheusSmemMapIdentity.h').write_text('/* Build metadata, not running-image authentication. */\n' +
        '\n'.join('STATIC CONST UINT8 mCeSmem' + name + 'Identity[32] = {' +
                  ','.join('0x' + value[i:i+2] for i in range(0, 64, 2)) + '};'
                  for name, value in identities) + '\n')
    inf = driver / 'DarwinBoot.inf'
    text = inf.read_text().replace('  DarwinBoot.c\n', '  DarwinBoot.c\n  CepheusSmemMapProbe.c\n')
    text = text.replace('  UniXNUPkg/UniXNUPkg.dec\n', '  UniXNUPkg/UniXNUPkg.dec\n  SmemMapProbePkg/SmemMapProbePkg.dec\n')
    text = text.replace('  BaseMemoryLib\n', '  BaseMemoryLib\n  DxeServicesTableLib\n  PlatformMemoryMapLib\n')
    text = text.replace('  gEfiFileInfoGuid\n', '  gEfiFileInfoGuid\n  gEfiHobListGuid\n  gEfiEventExitBootServicesGuid\n')
    inf.write_text(text)
    dsc = output / 'DarwinBootPkg/DarwinBoot.dsc'
    text = dsc.read_text().replace('  BaseMemoryLib|',
        '  DxeServicesTableLib|MdePkg/Library/DxeServicesTableLib/DxeServicesTableLib.inf\n'
        '  PlatformMemoryMapLib|SmemMapProbePkg/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.inf\n  BaseMemoryLib|')
    text = text.replace('MdePkg/Library/UefiDebugLibStdErr/UefiDebugLibStdErr.inf',
                        'MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf')
    dsc.write_text(text)
    manifest = {'base_commit': PAIR.BASE, 'aligned_sha256': PAIR.ALIGNED_SHA,
                'scope': 'same-boot mapping diagnostic; no SMEM loads, SCM or GPU execution',
                'files': {}}
    for path in sorted(output.rglob('*')):
        if path.is_file() and '.git' not in path.relative_to(output).parts:
            manifest['files'][str(path.relative_to(output))] = hashlib.sha256(path.read_bytes()).hexdigest()
    (output / 'smem-handoff-source.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', required=True, type=Path)
    parser.add_argument('--uefi', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(stage(args.repository.resolve(), args.uefi.resolve(), args.output.resolve()), indent=2))
