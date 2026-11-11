#!/usr/bin/env python3
"""Offline tests for the one-shot RPMh exit-tail device runner."""
import hashlib
import importlib.util
import io
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


def main():
    serial = RUNNER.select_device('abc123\tfastboot\n')
    assert serial == 'abc123'
    refused(lambda: RUNNER.select_device(''))
    refused(lambda: RUNNER.select_device('a\tfastboot\nb\tfastboot\n'))
    refused(lambda: RUNNER.select_device('No devices found\n'))

    assert RUNNER.parse_product('product: cepheus\nFinished. Total time: 0.001s\n') == 'cepheus'
    refused(lambda: RUNNER.parse_product('product: raphael\n'))
    refused(lambda: RUNNER.parse_product('product: cepheus\nproduct: cepheus\n'))

    command = RUNNER.boot_command('abc123', '/tmp/image.img')
    assert command == ['fastboot', '-s', 'abc123', 'boot', '/tmp/image.img']
    assert 'flash' not in command and 'reboot' not in command

    ready = io.StringIO('READY udp/5140 duration=360s directory=/tmp/test\n')
    assert RUNNER.wait_for_line(ready, 'READY ', 1).startswith('READY ')
    refused(lambda: RUNNER.wait_for_line(io.StringIO(''), 'READY ', 0.01))
    refused(lambda: RUNNER.wait_for_line(io.StringIO('capture failed\n'), 'READY ', 1))

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

    print('RPMh tail device runner: unique device, exact product/image, boot-only command and identity refusal PASS')


if __name__ == '__main__':
    main()
