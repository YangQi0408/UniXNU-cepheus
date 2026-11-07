#!/usr/bin/env python3
"""Collect fixed Cepheus TZ bytes and RAM logs from an already-running recovery.

No boot/reboot/flash, filesystem mount, block-write, SMC or GPU control command.
The recovery itself may already have RW mounts: record them, never hide them.
Output must be a new host directory; failures retain partial evidence as FAIL.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess

SERIAL = '67eea163'
TZ = '/dev/block/sde37'
SIZE = 4 * 1024 * 1024


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists():
        parser.error('output already exists; preserve earlier evidence')
    out.mkdir(parents=True)
    receipt = {'time_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'scope': 'Fixed TZ read and RAM-log collection from user-started recovery only',
               'serial': SERIAL, 'tz_path': TZ, 'expected_tz_bytes': SIZE,
               'result': 'IN_PROGRESS', 'commands': [], 'files': {},
               'agent_boot_reboot_flash_mount_write_smc': False,
               'limits': ['Recovery may independently write already-mounted cache/metadata',
                          'Recovery logs are not an original Android GPU/ZAP success trace',
                          'Partition identity does not prove active secure-runtime image identity']}

    def save():
        (out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')

    def invoke(command, filename=None, timeout=60):
        receipt['commands'].append(command)
        save()
        if filename:
            with (out / filename).open('xb') as stream:
                run = subprocess.run(command, stdout=stream, stderr=subprocess.PIPE, timeout=timeout)
            path = out / filename
            receipt['files'][filename] = {'bytes': path.stat().st_size,
                'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'exit_code': run.returncode}
            save()
            if run.returncode:
                raise RuntimeError(f'{filename}: exit={run.returncode}; {run.stderr.decode(errors="replace")[:500]}')
            return path.read_bytes()
        run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
        if run.returncode:
            raise RuntimeError(f'probe failed: exit={run.returncode}; {run.stderr.decode(errors="replace")[:500]}')
        return run.stdout

    def shell(command, filename=None):
        return invoke(['adb', '-s', SERIAL, 'shell', command], filename)

    def remote_hash():
        text = shell('sha256sum ' + TZ).decode().strip()
        match = re.fullmatch(r'([0-9a-f]{64})\s+/dev/block/sde37', text)
        if not match:
            raise RuntimeError('unexpected remote TZ hash output')
        return match[1]

    try:
        state = invoke(['adb', '-s', SERIAL, 'get-state']).decode().strip()
        if state != 'recovery':
            raise RuntimeError('device not in recovery: ' + state)
        identity = shell('id; getprop ro.product.device; getprop ro.product.model; '
                         'getprop ro.serialno; getprop ro.twrp.version; uname -a', 'identity.txt').decode()
        if 'uid=0(root)' not in identity or 'cepheus' not in identity.splitlines() or SERIAL not in identity.splitlines():
            raise RuntimeError('root/product/serial identity mismatch')
        if shell('readlink -f /dev/block/by-name/tz').decode().strip() != TZ:
            raise RuntimeError('TZ canonical partition mismatch')
        sysfs = shell('for p in /sys/class/block/sde37/size /sys/class/block/sde37/start '
                      '/sys/class/block/sde37/ro /sys/class/block/sde37/uevent '
                      '/sys/class/block/sde/queue/logical_block_size; do '
                      'printf "%s\\n" "$p"; awk "{print}" "$p"; done', 'tz-sysfs.txt').decode()
        if ('/sys/class/block/sde37/size\n8192\n' not in sysfs or
                'PARTNAME=tz' not in sysfs or 'PARTN=37' not in sysfs):
            raise RuntimeError('TZ sysfs name/size mismatch')
        before = shell('awk "{print}" /proc/mounts', 'mounts-before.txt').decode()
        receipt['persistent_rw_mounts_before'] = [line for line in before.splitlines()
            if line.startswith('/dev/block/') and 'rw' in line.split()[3].split(',')]
        if any(line.startswith(TZ + ' ') for line in before.splitlines()):
            raise RuntimeError('TZ is mounted; refuse acquisition')
        receipt['remote_tz_sha256_before'] = remote_hash()
        blob = invoke(['adb', '-s', SERIAL, 'exec-out', 'dd', 'if=' + TZ,
                       'bs=4096', 'count=1024', 'status=none'], 'tz-partition.img', timeout=120)
        if len(blob) != SIZE:
            raise RuntimeError('short or oversized TZ acquisition')
        receipt['remote_tz_sha256_after'] = remote_hash()
        digest = hashlib.sha256(blob).hexdigest()
        if not digest == receipt['remote_tz_sha256_before'] == receipt['remote_tz_sha256_after']:
            raise RuntimeError('host/remote before/after TZ hash mismatch')
        invoke(['adb', '-s', SERIAL, 'exec-out', 'dmesg'], 'recovery-dmesg.log')
        invoke(['adb', '-s', SERIAL, 'exec-out', 'dd', 'if=/tmp/recovery.log',
                'bs=4096', 'count=256', 'status=none'], 'recovery.log')
        shell('awk "{print}" /proc/mounts', 'mounts-after.txt')
        shell('ls -l /sys/fs/pstore', 'pstore-list.txt')
        receipt['result'] = 'PASS_ACQUISITION_ONLY'
        save()
        print('Recovery TZ acquisition PASS: exact 4 MiB, remote-before/host/remote-after SHA256 match')
        print('TZ SHA256', digest)
        print('Receipt', out / 'result.json')
        return 0
    except Exception as error:
        receipt['result'] = 'FAIL'
        receipt['error'] = str(error)
        save()
        raise


if __name__ == '__main__':
    raise SystemExit(main())
