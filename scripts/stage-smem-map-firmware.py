#!/usr/bin/env python3
"""Stage a standalone, opt-in mapping diagnostic; no sibling edits or devices."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
PROFILE = 'Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.c'
PROFILE_SHA = '95b8889c52eb2544d675a8ae2902a85ac6ebe2c178c5302bdeeac69571e7e7ff'
HEADER = 'Platforms/AndromedaPkg/Include/Library/PlatformMemoryMapLib.h'
HEADER_SHA = 'ec8e6712ff80c3f7d207d8a62df0941ac6d938e48fd1cf6cd727a74903d09c59'


def stage(uefi, output):
    if output.exists():
        raise ValueError('output exists; preserve earlier evidence')
    for path, expected in ((PROFILE, PROFILE_SHA), (HEADER, HEADER_SHA)):
        if hashlib.sha256((uefi / path).read_bytes()).hexdigest() != expected:
            raise ValueError('unrecognized platform input: ' + path)
    package = output / 'SmemMapProbePkg'
    copies = {ROOT / ('firmware/' + name): package / 'Driver' / name
              for name in ('CepheusSmemMapProbe.c', 'CepheusSmemMapProbe.h',
                           'CepheusSmemMapDriver.c', 'SmemMapProbe.inf')}
    copies.update({ROOT / 'firmware/SmemMapProbe.dsc': package / 'SmemMapProbe.dsc',
                   ROOT / 'firmware/SmemMapProbePkg.dec': package / 'SmemMapProbePkg.dec',
                   ROOT / 'firmware/SmemMapPlatform.inf': package / 'Library/PlatformMemoryMapLib/PlatformMemoryMapLib.inf',
                   uefi / PROFILE: package / 'Library/PlatformMemoryMapLib/PlatformMemoryMapLib.c',
                   uefi / HEADER: package / 'Include/Library/PlatformMemoryMapLib.h'})
    manifest = {'scope': 'standalone explicit opt-in mapping diagnostic; no SMEM load or GPU execution',
                'profile_source_sha256': PROFILE_SHA, 'profile_header_sha256': HEADER_SHA, 'files': {}}
    for source, target in copies.items():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        manifest['files'][str(target.relative_to(output))] = hashlib.sha256(target.read_bytes()).hexdigest()
    (output / 'smem-map-source.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--uefi', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(stage(args.uefi.resolve(), args.output.resolve()), indent=2))
