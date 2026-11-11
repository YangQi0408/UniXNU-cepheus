#!/usr/bin/env python3
"""Mock the fixed recovery collector; these tests never invoke adb."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'smem_collect', Path(__file__).with_name('collect-recovery-smem-versions.py'))
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
IMAGES = b'3:\n\tCRM:\t\tTZ.XF.5.2-00209\n\tVariant:\tYAQAANAAA\n\tVersion:\tCRM\n'
BOOT = b'11223344-1122-3344-5566-112233445566\n'


class CollectorTests(unittest.TestCase):
    def test_lifecycle_failures_and_allowlist(self):
        for mode in ('complete', 'state', 'product', 'serial', 'root', 'version',
                     'changed', 'boot-changed', 'malformed', 'timeout', 'command-error', 'oversized'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'capture'
                commands = []
                counts = {'images': 0, 'boot_id': 0}

                def run(command, **kwargs):
                    self.assertEqual(command[:3], ['adb', '-s', MODULE.SERIAL])
                    self.assertEqual(kwargs['timeout'], 20)
                    arguments = command[3:]
                    commands.append(arguments)
                    code = 0
                    if arguments == ['get-state']:
                        raw = b'device\n' if mode == 'state' else b'recovery\n'
                    elif arguments == ['shell', 'getprop', 'ro.product.device']:
                        raw = b'raphael\n' if mode == 'product' else b'cepheus\n'
                    elif arguments == ['shell', 'getprop', 'ro.serialno']:
                        raw = b'wrong\n' if mode == 'serial' else MODULE.SERIAL.encode() + b'\n'
                    elif arguments == ['shell', 'id']:
                        raw = b'uid=2000(shell)\n' if mode == 'root' else b'uid=0(root)\n'
                    elif arguments == ['shell', 'getprop', 'ro.twrp.version']:
                        raw = b'' if mode == 'version' else b'3.7.1\n'
                    elif arguments == ['shell', 'uname', '-a']:
                        raw = b'Linux test aarch64\n'
                    elif arguments == ['exec-out', 'cat', '/proc/sys/kernel/random/boot_id']:
                        counts['boot_id'] += 1
                        raw = BOOT.replace(b'11223344', b'00000000', 1) if mode == 'boot-changed' \
                            and counts['boot_id'] == 2 else BOOT
                    elif arguments == ['exec-out', 'cat', '/proc/mounts']:
                        raw = b'/dev/block/sda19 /metadata ext4 rw 0 0\n'
                    elif arguments == ['exec-out', 'cat', MODULE.PATHS[0]]:
                        counts['images'] += 1
                        if mode == 'timeout':
                            kwargs['stdout'].write(b'partial metadata')
                            raise subprocess.TimeoutExpired(command, 20)
                        code = 1 if mode == 'command-error' else 0
                        raw = b'Unavailable\n' if mode == 'malformed' else IMAGES
                        if mode == 'changed' and counts['images'] == 2:
                            raw = raw.replace(b'00209', b'00210')
                        if mode == 'oversized':
                            raw = b'x' * 4097
                    else:
                        raise AssertionError('non-allowlisted command: ' + repr(arguments))
                    kwargs['stdout'].write(raw)
                    return subprocess.CompletedProcess(command, code)

                with patch.object(MODULE.subprocess, 'run', side_effect=run):
                    status = MODULE.collect(output, MODULE.PATHS[0])
                self.assertEqual(status, 0 if mode == 'complete' else 1)
                receipt = json.loads((output / 'receipt.json').read_bytes())
                self.assertEqual(receipt['status'], 'PASS_ACQUISITION_METADATA_ONLY'
                                 if mode == 'complete' else 'FAIL')
                self.assertFalse(receipt['boot_issued'])
                self.assertFalse(receipt['reboot_issued'])
                self.assertFalse(receipt['partition_write_issued'])
                self.assertFalse(receipt['sysfs_write_issued'])
                self.assertFalse(receipt['smc_issued'])
                self.assertFalse(receipt['running_secure_identity_authenticated'])
                self.assertFalse(receipt['gpu_execution'])
                self.assertEqual(counts['images'], 2 if mode in
                                 ('complete', 'changed', 'boot-changed', 'malformed') else
                                 1 if mode in ('timeout', 'command-error', 'oversized') else 0)
                if mode == 'complete':
                    self.assertTrue(receipt['same_boot_verified'])
                    self.assertEqual((output / 'images-first.txt').read_bytes(), IMAGES)
                else:
                    self.assertFalse((output / 'analysis.json').exists())
                    if mode == 'timeout':
                        self.assertEqual((output / 'images-first.txt').read_bytes(), b'partial metadata')
                        self.assertIsNone(receipt['files']['images-first.txt']['exit_code'])

    def test_bad_path_or_existing_output_no_device_call(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(MODULE.subprocess, 'run') as run:
            with self.assertRaises(ValueError):
                MODULE.collect(Path(directory) / 'new', '/dev/mem')
            with self.assertRaises(ValueError):
                MODULE.collect(Path(directory), MODULE.PATHS[0])
            run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
