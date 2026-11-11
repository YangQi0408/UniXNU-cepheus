#!/usr/bin/env python3
"""Offline tests for the one-shot SCM-pair device runner."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
from unittest.mock import MagicMock, patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('pair_device', ROOT / 'scripts/run-scm-pair-device.py')
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


def refused(callback):
    try:
        callback()
    except ValueError:
        return
    raise AssertionError('invalid input accepted')


def capture_fixture(directory, raw=b'pair one\npair two\n', packet_records=None, **changes):
    if packet_records is None:
        packet_records = [{'elapsed_s': 12.0, 'source': ['192.168.7.2', 5140],
                           'bytes': len(raw), 'accepted': True}]
    accepted = [packet for packet in packet_records if packet['accepted']]
    capture = {
        'source_filter': '192.168.7.2', 'port': 5140,
        'requested_duration_s': 360, 'packets': len(accepted),
        'bytes': sum(packet['bytes'] for packet in accepted),
        'rejected_packets': len(packet_records) - len(accepted),
        'first_packet_elapsed_s': min((p['elapsed_s'] for p in accepted), default=None),
        'last_packet_elapsed_s': max((p['elapsed_s'] for p in accepted), default=None),
        'boot_log_sha256': hashlib.sha256(raw).hexdigest(), 'stop_reason': 'duration',
    }
    capture.update(changes)
    (directory / 'boot.log').write_bytes(raw)
    (directory / 'packets.jsonl').write_text(
        ''.join(json.dumps(packet) + '\n' for packet in packet_records))
    (directory / 'capture.json').write_text(json.dumps(capture))


def analysis_fixture(digest, **changes):
    data = {'status': 'PAIR_COMPLETE', 'reason': 'fixture', 'uefi_pas13': False,
            'xnu_pas13': False, 'scheduled_record_seen': True,
            'logger_delay_verified': False, 'gpu_execution': False,
            'admission_changed': False, 'log_sha256': digest,
            'checksum_verified': True, 'log_origin_authenticated': False}
    data.update(changes)
    return data


def test_run_lifecycle():
    for mode in ('complete', 'wrong-product', 'two-devices', 'boot-failed',
                 'capture-failed', 'analysis-failed'):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / 'run'
            commands = []
            capture = MagicMock()
            capture.stdout = io.StringIO('READY udp/5140 duration=360s\n')
            capture.poll.return_value = 0
            capture.returncode = 1 if mode == 'capture-failed' else 0
            capture.communicate.return_value = ('capture complete\n', None)
            digest = hashlib.sha256(b'complete log\n').hexdigest()

            def command_run(command, **kwargs):
                commands.append(command)
                if command[:2] == ['fastboot', 'devices']:
                    devices = 'abc123\tfastboot\n'
                    if mode == 'two-devices':
                        devices += 'other\tfastboot\n'
                    return RUNNER.subprocess.CompletedProcess(command, 0, devices, '')
                if command[3:5] == ['getvar', 'product']:
                    product = 'raphael' if mode == 'wrong-product' else 'cepheus'
                    return RUNNER.subprocess.CompletedProcess(command, 0, '', f'product: {product}\n')
                if command[3:4] == ['boot']:
                    return RUNNER.subprocess.CompletedProcess(
                        command, 1 if mode == 'boot-failed' else 0, '', '')
                if Path(command[1]).name == 'analyze-scm-pair-log.py':
                    (output / 'analysis.json').write_text(json.dumps(analysis_fixture(digest)))
                    return RUNNER.subprocess.CompletedProcess(
                        command, 1 if mode == 'analysis-failed' else 0, '', '')
                raise AssertionError(f'unexpected command: {command}')

            identity = {'path': str(root / 'image.img'), 'bytes': RUNNER.EXPECTED_IMAGE_BYTES,
                        'sha256': RUNNER.EXPECTED_IMAGE_SHA}
            with patch.object(RUNNER, 'image_identity', return_value=identity), \
                    patch.object(RUNNER, 'ensure_port_free'), \
                    patch.object(RUNNER, 'validate_capture', return_value={'sha256': digest}), \
                    patch.object(RUNNER.subprocess, 'run', side_effect=command_run), \
                    patch.object(RUNNER.subprocess, 'Popen', return_value=capture) as launch:
                if mode in ('wrong-product', 'two-devices'):
                    refused(lambda: RUNNER.run(root / 'image.img', output, 360))
                    assert not output.exists()
                    launch.assert_not_called()
                else:
                    code = RUNNER.run(root / 'image.img', output, 360)
                    assert code == (0 if mode == 'complete' else
                                    1 if mode == 'analysis-failed' else 2)
                    receipt = json.loads((output / 'receipt.json').read_text())
                    assert receipt['boot_attempt_count'] == 1
                    assert receipt['phone_boot_issued'] is True
                    assert receipt['partition_flash_issued'] is False
                    assert receipt['reboot_issued'] is False
                    assert receipt['result'] == ('PASS' if mode == 'complete' else 'FAIL')
                    capture.wait.assert_not_called()
                boots = [command for command in commands if command[3:4] == ['boot']]
                assert len(boots) == (0 if mode in ('wrong-product', 'two-devices') else 1)


def main():
    test_run_lifecycle()
    assert RUNNER.EXPECTED_IMAGE_BYTES == 70688768
    assert RUNNER.EXPECTED_IMAGE_SHA == \
        '415d47275ca5e6f4db13015fd423f6c9ee29547f192895582bc5edc9bb07a30c'

    assert RUNNER.select_device('abc123\tfastboot\n') == 'abc123'
    refused(lambda: RUNNER.select_device(''))
    refused(lambda: RUNNER.select_device('a\tfastboot\nb\tfastboot\n'))
    refused(lambda: RUNNER.select_device('No devices found\n'))

    assert RUNNER.parse_product(
        'product: cepheus\nFinished. Total time: 0.001s\n') == 'cepheus'
    refused(lambda: RUNNER.parse_product('product: raphael\n'))
    refused(lambda: RUNNER.parse_product('product: cepheus\nproduct: cepheus\n'))

    command = RUNNER.boot_command('abc123', '/tmp/pair.img')
    assert command == ['fastboot', '-s', 'abc123', 'boot', '/tmp/pair.img']
    assert 'flash' not in command and 'reboot' not in command

    ready = io.StringIO('READY udp/5140 duration=360s directory=/tmp/test\n')
    assert RUNNER.wait_for_line(ready, 'READY ', 1).startswith('READY ')
    refused(lambda: RUNNER.wait_for_line(io.StringIO(''), 'READY ', 0.01))
    refused(lambda: RUNNER.wait_for_line(io.StringIO('capture failed\n'), 'READY ', 1))

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        analysis = root / 'analysis.json'
        raw = b'complete pair log\n'
        digest = hashlib.sha256(raw).hexdigest()
        for uefi in (False, True, None):
            for xnu in (False, True, None):
                analysis.write_text(json.dumps(analysis_fixture(
                    digest, uefi_pas13=uefi, xnu_pas13=xnu)))
                receipt = {'analysis': {'status': 'PAIR_COMPLETE'}, 'result': 'RUNNING',
                           'capture': {'evidence': {'sha256': digest}}}
                assert RUNNER.accept_analysis(receipt, analysis) == 0
                assert receipt['result'] == 'PASS'
                assert receipt['analysis']['uefi_pas13'] is uefi
                assert receipt['analysis']['xnu_pas13'] is xnu
                assert receipt['analysis']['query_outcome_is_gpu_authorization'] is False

        rejected = (
            ({'status': 'INCOMPLETE'}, 'incomplete'),
            ({'status': 'REJECTED'}, 'incomplete'),
            ({'checksum_verified': False}, 'checksum'),
            ({'log_origin_authenticated': True}, 'origin'),
            ({'log_sha256': '0' * 64}, 'identity'),
            ({'gpu_execution': True}, 'GPU execution'),
            ({'admission_changed': True}, 'GPU execution'),
            ({'uefi_pas13': 'false'}, 'uefi_pas13'),
            ({'xnu_pas13': 0}, 'xnu_pas13'),
        )
        for changes, reason in rejected:
            analysis.write_text(json.dumps(analysis_fixture(digest, **changes)))
            receipt = {'analysis': {'status': 'PAIR_COMPLETE'}, 'result': 'RUNNING',
                       'capture': {'evidence': {'sha256': digest}}}
            assert RUNNER.accept_analysis(receipt, analysis) == 1
            assert reason in receipt['reason']

        analysis.unlink()
        receipt = {'result': 'RUNNING'}
        assert RUNNER.accept_analysis(receipt, analysis) == 1
        assert 'no result' in receipt['reason']

    with tempfile.TemporaryDirectory() as directory:
        capture = Path(directory)
        raw = b'first\nsecond\n'
        packets = [
            {'elapsed_s': 1.0, 'source': ['192.168.31.2', 5140], 'bytes': 9,
             'accepted': False},
            {'elapsed_s': 10.0, 'source': ['192.168.7.2', 5140], 'bytes': 6,
             'accepted': True},
            {'elapsed_s': 11.0, 'source': ['192.168.7.2', 5140], 'bytes': 7,
             'accepted': True},
        ]
        capture_fixture(capture, raw, packets)
        evidence = RUNNER.validate_capture(capture, 9.5, 360)
        assert evidence['bytes'] == len(raw) and evidence['packets'] == 2
        assert evidence['first_packet_elapsed_s'] == 10.0

        capture_fixture(capture, b'', [])
        refused(lambda: RUNNER.validate_capture(capture, 9.5, 360))
        capture_fixture(capture, raw, packets, boot_log_sha256='0' * 64)
        refused(lambda: RUNNER.validate_capture(capture, 9.5, 360))
        capture_fixture(capture, raw, packets, packets=1)
        refused(lambda: RUNNER.validate_capture(capture, 9.5, 360))
        capture_fixture(capture, raw, packets, stop_reason='signal')
        refused(lambda: RUNNER.validate_capture(capture, 9.5, 360))
        capture_fixture(capture, raw, packets)
        refused(lambda: RUNNER.validate_capture(capture, 12.0, 360))
        stale = [dict(packet) for packet in packets]
        stale[1]['elapsed_s'] = 5.0
        stale[2]['elapsed_s'] = 6.0
        capture_fixture(capture, raw, stale)
        refused(lambda: RUNNER.validate_capture(capture, 9.5, 360))
        wrong_source = [dict(packet) for packet in packets]
        wrong_source[1]['source'] = ['192.168.7.3', 5140]
        capture_fixture(capture, raw, wrong_source)
        refused(lambda: RUNNER.validate_capture(capture, 9.5, 360))

    with tempfile.TemporaryDirectory() as directory:
        image = Path(directory) / 'image.img'
        image.write_bytes(b'identity-test')
        digest = hashlib.sha256(image.read_bytes()).hexdigest()
        old_sha, old_bytes = RUNNER.EXPECTED_IMAGE_SHA, RUNNER.EXPECTED_IMAGE_BYTES
        try:
            RUNNER.EXPECTED_IMAGE_SHA, RUNNER.EXPECTED_IMAGE_BYTES = digest, image.stat().st_size
            assert RUNNER.image_identity(image)['sha256'] == digest
            image.write_bytes(b'wrong')
            refused(lambda: RUNNER.image_identity(image))
        finally:
            RUNNER.EXPECTED_IMAGE_SHA, RUNNER.EXPECTED_IMAGE_BYTES = old_sha, old_bytes

    print('SCM-pair device runner: exact image/product, boot-only command, capture identity, '
          'all query outcomes and false GPU/admission claims PASS')


if __name__ == '__main__':
    main()
