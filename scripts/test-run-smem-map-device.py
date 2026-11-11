#!/usr/bin/env python3
"""Mock-only lifecycle and copied-record tests; never invoke a device tool."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('smem_device_test', ROOT / 'scripts/run-smem-map-device.py')
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


def transcript(passed=True, *, wrong_metadata=False):
    # Synthetic copied record based on the portable C contract fixture.
    data = bytearray(RUNNER.LOG.BYTES)
    struct.pack_into('<Q4I', data, 0, 0x3150414d4d454d53, 1, len(data), 1, 1)
    struct.pack_into('<12Q', data, 24, 1, 2, 0x90000000, 8192,
                     0 if passed else 1, 0, 0, 0, 0, 0x86000000, 0x200000, 128)
    struct.pack_into('<8I', data, 120, 3, 512 if passed else 0, 1, 1, 1, 1, 1, 0)
    for offset, name in zip((152, 184, 216), ('probe_source', 'profile_source', 'profile_header')):
        data[offset:offset + 32] = bytes.fromhex(RUNNER.EXPECTED_BUILD_METADATA[name])
    if wrong_metadata:
        data[152] ^= 1
    cpu = (4, 0x80000000, 5, 32, 0x90000000, 0, 0xff4400, 5)
    for offset in (248, 312):
        struct.pack_into('<8Q', data, offset, *cpu)
    struct.pack_into('<HHI', data, 376, 3, 48, 0)
    struct.pack_into('<IIQQ', data, 400, 5, 0x800, 0x86000000, 0x200000)
    struct.pack_into('<6Q2I', data, 424, 0x86000000, 0x200000, 0, 0, 0, 0, 1, 0)
    if passed:
        struct.pack_into('<512Q', data, 480,
                         *(0x4400000086000000 + page * 4096 for page in range(512)))
    checksum = RUNNER.LOG.checksum(data)
    struct.pack_into('<Q', data, 88, checksum)
    padded = data + bytes(32)
    lines = ['CEPHEUS SMEM MAP: scheduled delay_s=90',
             f'CEPHEUS SMEM MAP: begin version=1 bytes=4576 checksum={checksum:016x}']
    for offset in range(0, len(data), 64):
        words = ':'.join(f'{word:016x}' for word in struct.unpack_from('<8Q', padded, offset))
        lines.append(f'CEPHEUS SMEM MAP: raw={offset} words={words}')
    lines.append(f'CEPHEUS SMEM MAP: end diagnostic={int(passed)}; no SMEM reads, SCM or GPU execution')
    return ('\n'.join(lines) + '\n').encode()


def evidence(directory, raw, duration=300):
    directory.mkdir()
    digest = hashlib.sha256(raw).hexdigest()
    (directory / 'boot.log').write_bytes(raw)
    (directory / 'packets.jsonl').write_text(json.dumps({
        'elapsed_s': 120.0, 'source': ['192.168.7.2', 5140],
        'bytes': len(raw), 'accepted': True}) + '\n')
    (directory / 'capture.json').write_text(json.dumps({
        'source_filter': '192.168.7.2', 'port': 5140, 'requested_duration_s': duration,
        'packets': 1, 'bytes': len(raw), 'rejected_packets': 0,
        'first_packet_elapsed_s': 120.0, 'last_packet_elapsed_s': 120.0,
        'boot_log_sha256': digest, 'stop_reason': 'duration'}))
    return {'directory': str(directory), 'sha256': digest}


class DeviceRunner(unittest.TestCase):
    def test_complete_and_failed_mapping_are_distinct(self):
        for passed in (True, False):
            with self.subTest(passed=passed), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                raw = transcript(passed)
                capture = evidence(root / 'capture', raw)
                analysis = root / 'analysis.json'
                analysis.write_text(json.dumps(RUNNER.LOG.analyze(raw)))
                receipt = {'capture': {'evidence': capture}}
                self.assertEqual(RUNNER.accept_analysis(receipt, analysis), 0)
                self.assertEqual(receipt['result'], 'PASS')
                self.assertIs(receipt['evidence_complete'], True)
                self.assertIs(receipt['mapping_diagnostic_pass'], passed)
                self.assertEqual((root / 'capture/boot.log').read_bytes(), raw)

    def test_rejected_analysis_preserves_log(self):
        raw = transcript()
        for mode in ('missing', 'invalid-json', 'truncated', 'log-hash', 'forged-summary',
                     'wrong-metadata', 'oversized',
                     'gpu_execution', *RUNNER.RECEIPT_SCOPE, 'logger_delay_verified'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                saved_raw = transcript(wrong_metadata=True) if mode == 'wrong-metadata' else raw
                if mode == 'oversized':
                    saved_raw = raw + b'x' * (2 * 1024 * 1024)
                capture = evidence(root / 'capture', saved_raw)
                analysis = root / 'analysis.json'
                result = RUNNER.LOG.analyze(saved_raw if mode != 'oversized' else raw)
                if mode == 'log-hash':
                    capture['sha256'] = '0' * 64
                elif mode == 'forged-summary':
                    result['diagnostic_pass'] = False
                elif mode == 'truncated':
                    (root / 'capture/boot.log').write_bytes(raw[:-1])
                    capture['sha256'] = hashlib.sha256(raw[:-1]).hexdigest()
                elif mode in RUNNER.RECEIPT_SCOPE or mode in ('gpu_execution', 'logger_delay_verified'):
                    result[mode] = True
                if mode != 'missing':
                    analysis.write_text('not-json' if mode == 'invalid-json' else json.dumps(result))
                saved = (root / 'capture/boot.log').read_bytes()
                receipt = {'capture': {'evidence': capture}}
                self.assertEqual(RUNNER.accept_analysis(receipt, analysis), 1)
                self.assertEqual(receipt['result'], 'FAIL')
                self.assertIsNone(receipt['mapping_diagnostic_pass'])
                self.assertIs(receipt['evidence_complete'], False)
                self.assertEqual((root / 'capture/boot.log').read_bytes(), saved)

    def test_image_and_duration_refusal_precede_device_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / 'image.img'
            image.write_bytes(b'fixture-image')
            digest = hashlib.sha256(image.read_bytes()).hexdigest()
            with patch.object(RUNNER, 'EXPECTED_IMAGE_SHA', digest), \
                    patch.object(RUNNER, 'EXPECTED_IMAGE_BYTES', image.stat().st_size):
                self.assertEqual(RUNNER.image_identity(image)['sha256'], digest)
                image.write_bytes(b'wrong-image')
                with patch.object(RUNNER.COMMON.subprocess, 'run') as command:
                    with self.assertRaises(ValueError):
                        RUNNER.run(image, root / 'output', 300)
                    command.assert_not_called()
            with patch.object(RUNNER.COMMON, 'run') as run:
                for duration in (299, 601, True, 300.0):
                    with self.assertRaises(ValueError):
                        RUNNER.run(image, root / 'output', duration)
                run.assert_not_called()

    def test_import_and_cli_help_do_not_invoke_device_tools(self):
        with patch.object(subprocess, 'run') as command, \
                patch.object(subprocess, 'Popen') as launch:
            spec = importlib.util.spec_from_file_location('smem_no_device_import', SPEC.origin)
            imported = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(imported)
            with patch.object(sys, 'argv', [SPEC.origin, '--help']), \
                    patch.object(sys, 'stdout', io.StringIO()), \
                    self.assertRaises(SystemExit) as ended:
                imported.main()
            self.assertEqual(ended.exception.code, 0)
            with tempfile.TemporaryDirectory() as directory, \
                    patch.object(sys, 'argv', [SPEC.origin, str(Path(directory) / 'missing.img'), directory]), \
                    patch.object(sys, 'stderr', io.StringIO()) as errors:
                self.assertEqual(imported.main(), 2)
                self.assertIn('REFUSED: missing mapping image', errors.getvalue())
            command.assert_not_called()
            launch.assert_not_called()

    def test_existing_output_and_busy_port_refuse_before_device_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            identity = {'path': str(root / 'image.img')}
            existing = root / 'existing'
            existing.mkdir()
            with patch.object(RUNNER.Diagnostic, 'image_identity', return_value=identity), \
                    patch.object(RUNNER.COMMON, 'ensure_port_free', side_effect=ValueError('busy port')) as port, \
                    patch.object(subprocess, 'run') as command, \
                    patch.object(subprocess, 'Popen') as launch:
                with self.assertRaisesRegex(ValueError, 'output already exists'):
                    RUNNER.run(root / 'image.img', existing, 300)
                port.assert_not_called()
                with self.assertRaisesRegex(ValueError, 'busy port'):
                    RUNNER.run(root / 'image.img', root / 'new', 300)
                self.assertFalse((root / 'new').exists())
                command.assert_not_called()
                launch.assert_not_called()

    def test_mocked_one_boot_lifecycle_and_cleanup(self):
        modes = ('complete', 'diagnostic-failed', 'wrong-product', 'two-devices',
                 'boot-failed', 'boot-timeout', 'not-ready', 'capture-failed',
                 'capture-timeout', 'analysis-failed', 'analysis-timeout', 'image-changed',
                 'terminate-timeout')
        for mode in modes:
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                output = root / 'run'
                commands = []
                capture = MagicMock()
                capture.stdout = io.StringIO('capture failed\n' if mode == 'not-ready' else 'READY udp/5140\n')
                capture.poll.return_value = None
                capture.returncode = 1 if mode == 'capture-failed' else 0
                raw = transcript(mode != 'diagnostic-failed')

                def communicate(**kwargs):
                    if mode in ('capture-timeout', 'terminate-timeout'):
                        raise subprocess.TimeoutExpired('capture', kwargs['timeout'])
                    evidence(output / 'capture', raw)
                    capture.poll.return_value = capture.returncode
                    return 'capture complete\n', None

                def wait(**kwargs):
                    if mode == 'terminate-timeout' and not capture.kill.called:
                        raise subprocess.TimeoutExpired('capture', kwargs['timeout'])
                    capture.poll.return_value = capture.returncode
                    return capture.returncode

                def command_run(command, **kwargs):
                    commands.append(command)
                    if command[:2] == ['fastboot', 'devices']:
                        devices = 'abc123\tfastboot\n'
                        if mode == 'two-devices':
                            devices += 'other\tfastboot\n'
                        return subprocess.CompletedProcess(command, 0, devices, '')
                    if command[3:5] == ['getvar', 'product']:
                        product = 'raphael' if mode == 'wrong-product' else 'cepheus'
                        return subprocess.CompletedProcess(command, 0, '', f'product: {product}\n')
                    if command[3:4] == ['boot']:
                        if mode == 'boot-timeout':
                            raise subprocess.TimeoutExpired(command, kwargs['timeout'])
                        return subprocess.CompletedProcess(command, int(mode == 'boot-failed'), '', '')
                    if Path(command[1]).name == RUNNER.ANALYZER:
                        if mode == 'analysis-timeout':
                            raise subprocess.TimeoutExpired(command, kwargs['timeout'])
                        (output / 'analysis.json').write_text(json.dumps(RUNNER.LOG.analyze(raw)))
                        return subprocess.CompletedProcess(command, int(mode == 'analysis-failed'), '', '')
                    raise AssertionError(f'unexpected command: {command}')

                capture.communicate.side_effect = communicate
                capture.wait.side_effect = wait
                identity = {'path': str(root / 'image.img'), 'bytes': RUNNER.EXPECTED_IMAGE_BYTES,
                            'sha256': RUNNER.EXPECTED_IMAGE_SHA}
                changed = dict(identity, sha256='0' * 64)
                with patch.object(RUNNER.Diagnostic, 'image_identity', side_effect=[identity, changed if mode == 'image-changed' else identity]), \
                        patch.object(RUNNER.COMMON, 'ensure_port_free'), \
                        patch.object(RUNNER.COMMON.subprocess, 'run', side_effect=command_run), \
                        patch.object(RUNNER.COMMON.subprocess, 'Popen', return_value=capture) as launch:
                    if mode in ('wrong-product', 'two-devices'):
                        with self.assertRaises(ValueError):
                            RUNNER.run(root / 'image.img', output, 300)
                        self.assertFalse(output.exists())
                        launch.assert_not_called()
                    else:
                        code = RUNNER.run(root / 'image.img', output, 300)
                        expected = 0 if mode in ('complete', 'diagnostic-failed') else 1 if mode == 'analysis-failed' else 2
                        self.assertEqual(code, expected)
                        receipt = json.loads((output / 'receipt.json').read_text())
                        attempts = 0 if mode in ('not-ready', 'image-changed') else 1
                        self.assertEqual(receipt['boot_attempt_count'], attempts)
                        self.assertIs(receipt['phone_boot_issued'], bool(attempts))
                        self.assertEqual(receipt['profile'], RUNNER.PROFILE)
                        for name in (*RUNNER.RECEIPT_SCOPE, 'partition_flash_issued', 'reboot_issued', 'gpu_execution'):
                            self.assertIs(receipt[name], False)
                        self.assertEqual(receipt['result'], 'PASS' if not expected else 'FAIL')
                        self.assertIsNotNone(capture.poll())
                        if mode in ('boot-failed', 'boot-timeout', 'not-ready', 'capture-timeout', 'image-changed'):
                            capture.terminate.assert_called_once()
                            capture.wait.assert_called_once()
                        if mode == 'terminate-timeout':
                            capture.terminate.assert_called_once()
                            capture.kill.assert_called_once()
                            self.assertEqual(capture.wait.call_count, 2)
                        if expected:
                            self.assertIsNone(receipt['mapping_diagnostic_pass'])
                            self.assertIs(receipt['evidence_complete'], False)
                        if mode in ('complete', 'diagnostic-failed', 'analysis-failed'):
                            self.assertEqual((output / 'capture/boot.log').read_bytes(), raw)
                boots = [command for command in commands if command[3:4] == ['boot']]
                self.assertEqual(len(boots), 0 if mode in ('wrong-product', 'two-devices', 'not-ready', 'image-changed') else 1)
                self.assertTrue(all(command[3:4] in (['getvar'], ['boot']) for command in commands if command[0] == 'fastboot' and command[1] == '-s'))


if __name__ == '__main__':
    unittest.main()
