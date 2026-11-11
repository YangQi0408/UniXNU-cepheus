#!/usr/bin/env python3
"""Run one bounded RPMh exit-tail diagnostic through fastboot boot only."""
import argparse
import hashlib
import io
import json
import re
from pathlib import Path
import select
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_IMAGE_SHA = 'aa519aeaf36d3c87b0b516695b814aefbf1d44655a94bc926606eb9de8d1616b'
EXPECTED_IMAGE_BYTES = 70696960
EXPECTED_PRODUCT = 'cepheus'
DEFAULT_CAPTURE_SECONDS = 360
FASTBOOT_TIMEOUT_SECONDS = 600


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def image_identity(path):
    path = path.resolve()
    if not path.is_file():
        raise ValueError(f'missing image: {path}')
    size = path.stat().st_size
    digest = sha256(path)
    if size != EXPECTED_IMAGE_BYTES or digest != EXPECTED_IMAGE_SHA:
        raise ValueError(f'image identity mismatch: {size} bytes sha256={digest}')
    return {'path': str(path), 'bytes': size, 'sha256': digest}


def select_device(output):
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    if len(lines) != 1:
        raise ValueError(f'expected exactly one fastboot device, found {len(lines)}')
    fields = lines[0].split()
    if not fields or fields[0].lower() in ('no', 'error'):
        raise ValueError(f'invalid fastboot device line: {lines[0]}')
    return fields[0]


def parse_product(output):
    matches = re.findall(r'^product:\s*(\S+)\s*$', output, re.MULTILINE)
    if len(matches) != 1:
        raise ValueError(f'expected one product value, found {len(matches)}')
    if matches[0] != EXPECTED_PRODUCT:
        raise ValueError(f'unexpected product {matches[0]!r}')
    return matches[0]


def boot_command(serial, image):
    return ['fastboot', '-s', serial, 'boot', str(image)]


def ensure_port_free():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        try:
            probe.bind(('0.0.0.0', 5140))
        except OSError as error:
            raise ValueError('UDP port 5140 is already in use') from error


def wait_for_line(stream, prefix, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            ready, _, _ = select.select([stream], [], [], min(0.5, deadline - time.monotonic()))
            if not ready:
                continue
            line = stream.readline()
        except (OSError, ValueError, io.UnsupportedOperation):
            line = stream.readline()
        if not line:
            raise ValueError(f'capture exited before printing {prefix!r}')
        line = line.rstrip()
        print(line, flush=True)
        if line.startswith(prefix):
            return line
    raise ValueError(f'timed out waiting for {prefix!r}')


def write_receipt(path, receipt):
    path.write_text(json.dumps(receipt, indent=2) + '\n')


def run(image, output, capture_seconds):
    identity = image_identity(image)
    if output.exists():
        raise ValueError(f'output already exists: {output}')
    ensure_port_free()

    devices = subprocess.run(['fastboot', 'devices', '-l'], check=True,
                             capture_output=True, text=True, timeout=30)
    serial = select_device(devices.stdout)
    product_run = subprocess.run(['fastboot', '-s', serial, 'getvar', 'product'],
                                 check=True, capture_output=True, text=True, timeout=30)
    product = parse_product(product_run.stdout + '\n' + product_run.stderr)
    output.mkdir(parents=True)
    receipt_path = output / 'receipt.json'
    capture_dir = output / 'capture'
    capture_log = output / 'capture.log'
    analysis_path = output / 'analysis.json'
    receipt = {
        'profile': 'rpmh-exit-tail-device-run',
        'result': 'RUNNING',
        'image': identity,
        'device': {'serial': serial, 'product': product},
        'boot_command': boot_command(serial, identity['path']),
        'capture_seconds': capture_seconds,
        'phone_boot_issued': False,
        'partition_flash_issued': False,
        'reboot_issued': False,
        'gpu_execution': False,
    }
    write_receipt(receipt_path, receipt)

    capture_command = [sys.executable, str(ROOT / 'scripts/capture-usb-log.py'),
                       str(capture_dir), '--duration', str(capture_seconds)]
    with capture_log.open('wb') as log_stream:
        capture = subprocess.Popen(capture_command, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, bufsize=1)
        try:
            ready_line = wait_for_line(capture.stdout, 'READY ', 10)
            log_stream.write((ready_line + '\n').encode())
            receipt['capture_started'] = True
            write_receipt(receipt_path, receipt)
            boot = subprocess.run(boot_command(serial, identity['path']), capture_output=True,
                                  text=True, timeout=FASTBOOT_TIMEOUT_SECONDS)
            receipt['phone_boot_issued'] = True
            receipt['fastboot'] = {'returncode': boot.returncode,
                                   'stdout': boot.stdout, 'stderr': boot.stderr}
            write_receipt(receipt_path, receipt)
            if boot.returncode:
                capture.terminate()
                raise ValueError(f'fastboot boot failed with {boot.returncode}')
            try:
                remaining, _ = capture.communicate(timeout=capture_seconds + 30)
            except subprocess.TimeoutExpired as error:
                capture.terminate()
                raise ValueError('bounded capture did not finish') from error
            log_stream.write(remaining.encode())
        finally:
            if capture.poll() is None:
                capture.terminate()
                capture.wait(timeout=10)

    receipt['capture'] = {'command': capture_command, 'returncode': capture.returncode,
                          'log': str(capture_log)}
    if capture.returncode:
        receipt.update(result='FAIL', reason='capture process failed')
        write_receipt(receipt_path, receipt)
        return 1

    analysis_command = [sys.executable, str(ROOT / 'scripts/analyze-rpmh-tail-log.py'),
                        str(capture_dir / 'boot.log'), '--output', str(analysis_path)]
    analyze = subprocess.run(analysis_command, capture_output=True, text=True, timeout=60)
    receipt['analysis'] = {'command': analysis_command, 'returncode': analyze.returncode,
                           'stdout': analyze.stdout, 'stderr': analyze.stderr}
    if analysis_path.is_file():
        analysis = json.loads(analysis_path.read_text())
        receipt['analysis']['status'] = analysis.get('status')
        receipt['analysis']['reason'] = analysis.get('reason')
    if analyze.returncode:
        receipt.update(result='FAIL', reason='RPMh tail record incomplete or rejected')
        write_receipt(receipt_path, receipt)
        return 1

    receipt.update(result='PASS', reason='complete RPMh tail diagnostic; not ownership or GPU execution')
    write_receipt(receipt_path, receipt)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--capture-seconds', type=int, default=DEFAULT_CAPTURE_SECONDS)
    args = parser.parse_args()
    if args.capture_seconds < 300:
        parser.error('capture must cover the scheduled 90-second diagnostic and USB tail')
    try:
        return run(args.image, args.output.resolve(), args.capture_seconds)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'REFUSED: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
