#!/usr/bin/env python3
"""Offline tests for the one-shot RPMh exit-tail device runner."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('tail_device', ROOT / 'scripts/run-rpmh-tail-device.py')
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


def refused(callback):
    try:
        callback()
    except ValueError:
        return
    raise AssertionError('invalid input accepted')


def capture_fixture(directory, raw=b'line one\nline two\n', packet_records=None, **changes):
    if packet_records is None:
        packet_records = [{'elapsed_s': 12.0, 'source': ['192.168.7.2', 5140],
                           'bytes': len(raw), 'accepted': True}]
    capture = {
        'source_filter': '192.168.7.2', 'port': 5140,
        'requested_duration_s': 360, 'packets': sum(p['accepted'] for p in packet_records),
        'bytes': sum(p['bytes'] for p in packet_records if p['accepted']),
        'rejected_packets': sum(not p['accepted'] for p in packet_records),
        'first_packet_elapsed_s': min((p['elapsed_s'] for p in packet_records if p['accepted']), default=None),
        'last_packet_elapsed_s': max((p['elapsed_s'] for p in packet_records if p['accepted']), default=None),
        'boot_log_sha256': hashlib.sha256(raw).hexdigest(), 'stop_reason': 'duration',
    }
    capture.update(changes)
    (directory / 'boot.log').write_bytes(raw)
    (directory / 'packets.jsonl').write_text(''.join(json.dumps(p) + '\n' for p in packet_records))
    (directory / 'capture.json').write_text(json.dumps(capture))


def main():
    serial = RUNNER.select_device('abc123\tfastboot\n')
    assert serial == 'abc123'
    refused(lambda: RUNNER.select_device(''))
    refused(lambda: RUNNER.select_device('a\tfastboot\nb\tfastboot\n'))
    refused(lambda: RUNNER.select_device('No devices found\n'))

    assert RUNNER.parse_product('product: cepheus\nFinished. Total time: 0.001s\n') == 'cepheus'
    refused(lambda: RUNNER.parse_product('product: raphael\n'))
    refused(lambda: RUNNER.parse_product('product: cepheus\nproduct: cepheus\n'))

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        analysis = root / 'analysis.json'
        raw = b'captured log\n'
        digest = hashlib.sha256(raw).hexdigest()
        data = {'status': 'TAIL_COMPLETE', 'reason': 'fixture', 'core': {'error': 0},
                'gpu_execution': False, 'admission_changed': False,
                'log_sha256': digest, 'checksum_verified': True,
                'log_origin_authenticated': False}
        analysis.write_text(json.dumps(data))
        receipt = {'analysis': {'status': 'TAIL_COMPLETE'}, 'result': 'RUNNING',
                   'capture': {'evidence': {'sha256': digest}}}
        assert RUNNER.accept_analysis(receipt, analysis) == 0
        assert receipt['result'] == 'PASS'
        for changes, reason in (({'core': {'error': -6}}, 'collector recorded a failure'),
                                ({'gpu_execution': True}, 'unexpectedly claims GPU execution'),
                                ({'admission_changed': True}, 'unexpectedly claims GPU execution')):
            changed = {**data, **changes}
            analysis.write_text(json.dumps(changed))
            receipt = {'analysis': {'status': 'TAIL_COMPLETE'}, 'result': 'RUNNING',
                       'capture': {'evidence': {'sha256': digest}}}
            assert RUNNER.accept_analysis(receipt, analysis) == 1
            assert reason in receipt['reason']
        for changes in ({'log_sha256': '0' * 64}, {'checksum_verified': False},
                        {'log_origin_authenticated': True}):
            analysis.write_text(json.dumps({**data, **changes}))
            receipt = {'analysis': {'status': 'TAIL_COMPLETE'}, 'result': 'RUNNING',
                       'capture': {'evidence': {'sha256': digest}}}
            assert RUNNER.accept_analysis(receipt, analysis) == 1
            assert 'analysis' in receipt['reason']

    command = RUNNER.boot_command('abc123', '/tmp/image.img')
    assert command == ['fastboot', '-s', 'abc123', 'boot', '/tmp/image.img']
    assert 'flash' not in command and 'reboot' not in command

    ready = io.StringIO('READY udp/5140 duration=360s directory=/tmp/test\n')
    assert RUNNER.wait_for_line(ready, 'READY ', 1).startswith('READY ')
    refused(lambda: RUNNER.wait_for_line(io.StringIO(''), 'READY ', 0.01))
    refused(lambda: RUNNER.wait_for_line(io.StringIO('capture failed\n'), 'READY ', 1))

    with tempfile.TemporaryDirectory() as directory:
        capture = Path(directory)
        raw = b'first\nsecond\n'
        packets = [
            {'elapsed_s': 1.0, 'source': ['192.168.31.2', 5140], 'bytes': 9, 'accepted': False},
            {'elapsed_s': 10.0, 'source': ['192.168.7.2', 5140], 'bytes': 6, 'accepted': True},
            {'elapsed_s': 11.0, 'source': ['192.168.7.2', 5140], 'bytes': 7, 'accepted': True},
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
        stale = [dict(p) for p in packets]
        stale[1]['elapsed_s'] = 5.0
        stale[2]['elapsed_s'] = 6.0
        capture_fixture(capture, raw, stale)
        refused(lambda: RUNNER.validate_capture(capture, 9.5, 360))
        wrong_source = [dict(p) for p in packets]
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

    print('RPMh tail device runner: unique device, exact product/image, boot-only command, capture provenance and identity refusal PASS')


if __name__ == '__main__':
    main()
