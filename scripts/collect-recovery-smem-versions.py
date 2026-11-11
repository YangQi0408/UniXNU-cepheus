#!/usr/bin/env python3
"""Read version metadata from already-running Cepheus recovery; never boot/write."""
import argparse
import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess

SPEC = importlib.util.spec_from_file_location(
    'smem_versions', Path(__file__).with_name('analyze-smem-versions.py'))
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)
SERIAL = '67eea163'
PATHS = ('/sys/devices/soc0/images', '/sys/devices/system/soc/soc0/images')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def collect(output, images_path):
    require(images_path in PATHS, 'images path is not allowlisted')
    output = Path(output).resolve()
    require(not output.exists(), 'output exists; preserve previous evidence')
    output.mkdir(parents=True)
    receipt = {'status': 'IN_PROGRESS', 'serial': SERIAL, 'images_path': images_path,
               'time_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'commands': [], 'files': {}, 'boot_issued': False, 'reboot_issued': False,
               'partition_write_issued': False, 'sysfs_write_issued': False,
               'smc_issued': False, 'gpu_execution': False,
               'running_secure_identity_authenticated': False,
               'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               'analyzer_sha256': hashlib.sha256(Path(ANALYZER.__file__).read_bytes()).hexdigest(),
               'limits': ['Recovery may independently write mounted persistent filesystems.',
                          'Device/kernel reported metadata; origin is not authenticated.',
                          'Stable duplicate reads in one boot do not prove coherent raw SMEM or secure bytes.',
                          'No raw SMEM access, PAS query/authentication, resource ownership or GPU proof.']}

    def save():
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')

    def invoke(arguments, filename, cap=16384):
        command = ['adb', '-s', SERIAL, *arguments]
        receipt['commands'].append({'argv': command, 'file': filename})
        save()
        stdout, stderr = output / filename, output / (filename + '.stderr')
        code = None
        try:
            with stdout.open('xb') as stream, stderr.open('xb') as errors:
                run = subprocess.run(command, stdout=stream, stderr=errors, timeout=20)
                code = run.returncode
        finally:
            if stdout.exists() and stderr.exists():
                def digest(path):
                    with path.open('rb') as stream:
                        return hashlib.file_digest(stream, 'sha256').hexdigest()
                receipt['files'][filename] = {'bytes': stdout.stat().st_size, 'exit_code': code,
                    'sha256': digest(stdout), 'stderr_sha256': digest(stderr)}
                save()
        with stdout.open('rb') as stream:
            raw = stream.read(cap + 1)
        require(code == 0 and len(raw) <= cap,
                filename + ': command failed or output exceeded cap')
        return raw

    def read(path, filename, cap=16384):
        return invoke(['exec-out', 'cat', path], filename, cap)

    try:
        require(invoke(['get-state'], 'state.txt', 128).strip() == b'recovery',
                'device not already in recovery; no boot or reboot will be issued')
        require(invoke(['shell', 'getprop', 'ro.product.device'], 'product.txt', 128).strip()
                == b'cepheus', 'product mismatch')
        require(invoke(['shell', 'getprop', 'ro.serialno'], 'serial.txt', 128).strip()
                == SERIAL.encode(), 'serial mismatch')
        require(b'uid=0(root)' in invoke(['shell', 'id'], 'uid.txt', 2048), 'recovery root missing')
        require(invoke(['shell', 'getprop', 'ro.twrp.version'], 'recovery-version.txt', 256).strip(),
                'recovery version unavailable')
        invoke(['shell', 'uname', '-a'], 'kernel.txt', 2048)
        before = read('/proc/sys/kernel/random/boot_id', 'boot-id-before.txt', 128).strip()
        require(re.fullmatch(rb'[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}', before),
                'invalid boot id')
        read('/proc/mounts', 'mounts-before.txt')
        first = read(images_path, 'images-first.txt', 4096)
        second = read(images_path, 'images-second.txt', 4096)
        after = read('/proc/sys/kernel/random/boot_id', 'boot-id-after.txt', 128).strip()
        read('/proc/mounts', 'mounts-after.txt')
        require(before == after and first == second, 'boot identity or version metadata changed')
        parsed = ANALYZER.analyze_sysfs(first)
        with (output / 'analysis.json').open('x') as stream:
            json.dump(parsed, stream, indent=2)
            stream.write('\n')
        receipt['files']['analysis.json'] = {
            'sha256': hashlib.sha256((output / 'analysis.json').read_bytes()).hexdigest()}
        receipt.update(status='PASS_ACQUISITION_METADATA_ONLY', same_boot_verified=True,
                       duplicate_read_identical=True, label_candidates=parsed['label_candidates'])
        save()
        print('PASS_ACQUISITION_METADATA_ONLY: stable rendered versions; no secure identity or GPU proof')
        return 0
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        receipt.update(status='FAIL', reason=str(error))
        save()
        print('SMEM version collection FAIL; partial host evidence preserved:', error)
        return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--images-path', choices=PATHS, required=True)
    args = parser.parse_args()
    raise SystemExit(collect(args.output, args.images_path))


if __name__ == '__main__':
    main()
