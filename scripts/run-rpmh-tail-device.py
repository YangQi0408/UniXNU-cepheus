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


def validate_capture(directory, boot_started_after_capture_s, requested_seconds):
    directory = directory.resolve()
    capture_path = directory / 'capture.json'
    boot_path = directory / 'boot.log'
    packets_path = directory / 'packets.jsonl'
    for path in (capture_path, boot_path, packets_path):
        if not path.is_file():
            raise ValueError(f'missing capture evidence: {path.name}')
    try:
        capture = json.loads(capture_path.read_text())
    except (OSError, ValueError) as error:
        raise ValueError('invalid capture receipt') from error
    required = {'source_filter', 'port', 'requested_duration_s', 'packets', 'bytes',
                'rejected_packets', 'first_packet_elapsed_s', 'last_packet_elapsed_s',
                'boot_log_sha256', 'stop_reason'}
    if not isinstance(capture, dict) or not required <= capture.keys():
        raise ValueError('capture receipt is missing required fields')
    if capture['source_filter'] != '192.168.7.2' or capture['port'] != 5140:
        raise ValueError('capture source identity mismatch')
    if capture['stop_reason'] != 'duration' or capture['requested_duration_s'] != requested_seconds:
        raise ValueError('capture did not complete its bounded duration')
    raw = boot_path.read_bytes()
    if not raw or capture['bytes'] != len(raw):
        raise ValueError('capture byte count mismatch')
    if capture['boot_log_sha256'] != hashlib.sha256(raw).hexdigest():
        raise ValueError('capture log hash mismatch')
    packets = []
    try:
        for line in packets_path.read_text().splitlines():
            packet = json.loads(line)
            if not isinstance(packet, dict) or not {'bytes', 'elapsed_s', 'accepted', 'source'} <= packet.keys():
                raise ValueError('malformed packet record')
            packets.append(packet)
    except (OSError, ValueError) as error:
        raise ValueError('invalid packet timing evidence') from error
    accepted = [packet for packet in packets if packet['accepted'] is True]
    if len(accepted) != capture['packets'] or not accepted:
        raise ValueError('accepted packet count mismatch')
    if sum(packet['bytes'] for packet in accepted) != len(raw):
        raise ValueError('accepted packet bytes mismatch')
    if any(not isinstance(packet['bytes'], int) or packet['bytes'] <= 0 or
           not isinstance(packet['elapsed_s'], (int, float)) or packet['elapsed_s'] < 0 or
           not isinstance(packet['source'], list) or len(packet['source']) < 1 or
           packet['source'][0] != '192.168.7.2' for packet in accepted):
        raise ValueError('invalid accepted packet record')
    first = capture['first_packet_elapsed_s']
    last = capture['last_packet_elapsed_s']
    if not isinstance(first, (int, float)) or not isinstance(last, (int, float)) or first > last:
        raise ValueError('invalid capture timing')
    if first != min(packet['elapsed_s'] for packet in accepted) or \
            last != max(packet['elapsed_s'] for packet in accepted):
        raise ValueError('capture timing summary mismatch')
    # Packets already arriving before the boot command are not evidence from
    # this temporary boot. Allow only sub-second process-start scheduling skew.
    if first < boot_started_after_capture_s - 1.0:
        raise ValueError('capture contains packets from before the boot command')
    return {'directory': str(directory), 'bytes': len(raw), 'sha256': capture['boot_log_sha256'],
            'packets': len(accepted), 'first_packet_elapsed_s': first,
            'last_packet_elapsed_s': last, 'rejected_packets': capture['rejected_packets'],
            'boot_started_after_capture_s': boot_started_after_capture_s}


def write_receipt(path, receipt):
    path.write_text(json.dumps(receipt, indent=2) + '\n')


def accept_analysis(receipt, analysis_path):
    if not analysis_path.is_file():
        receipt.update(result='FAIL', reason='RPMh analyzer produced no result')
        return 1
    try:
        analysis = json.loads(analysis_path.read_text())
    except (OSError, ValueError) as error:
        receipt.update(result='FAIL', reason=f'invalid RPMh analysis: {error}')
        return 1
    if not isinstance(analysis, dict) or analysis.get('status') != 'TAIL_COMPLETE':
        receipt.update(result='FAIL', reason='RPMh tail record incomplete or rejected')
        return 1
    summary = receipt.setdefault('analysis', {})
    summary.update(status=analysis.get('status'), reason=analysis.get('reason'),
                   core_error=analysis.get('core', {}).get('error'),
                   gpu_execution=analysis.get('gpu_execution'),
                   admission_changed=analysis.get('admission_changed'))
    if summary['core_error'] != 0:
        receipt.update(result='FAIL', reason='RPMh collector recorded a failure; no request snapshot')
        return 1
    evidence = receipt.get('capture', {}).get('evidence', {})
    if analysis.get('log_sha256') != evidence.get('sha256'):
        receipt.update(result='FAIL', reason='analysis does not match the captured log identity')
        return 1
    if analysis.get('checksum_verified') is not True or analysis.get('log_origin_authenticated') is not False:
        receipt.update(result='FAIL', reason='analysis origin or checksum claims are inconsistent')
        return 1
    if summary['gpu_execution'] is not False or summary['admission_changed'] is not False:
        receipt.update(result='FAIL', reason='analysis unexpectedly claims GPU execution or admission mutation')
        return 1
    receipt.update(result='PASS', reason='complete RPMh tail diagnostic; not ownership or GPU execution')
    return 0


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
    capture_started = time.monotonic()
    with capture_log.open('wb') as log_stream:
        capture = subprocess.Popen(capture_command, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, bufsize=1)
        try:
            ready_line = wait_for_line(capture.stdout, 'READY ', 10)
            log_stream.write((ready_line + '\n').encode())
            receipt['capture_started'] = True
            receipt['capture_ready_after_launch_s'] = time.monotonic() - capture_started
            write_receipt(receipt_path, receipt)
            boot_started = time.monotonic()
            receipt['boot_started_after_capture_s'] = boot_started - capture_started
            write_receipt(receipt_path, receipt)
            boot = subprocess.run(boot_command(serial, identity['path']), capture_output=True,
                                  text=True, timeout=FASTBOOT_TIMEOUT_SECONDS)
            receipt['phone_boot_issued'] = True
            receipt['boot_finished_after_capture_s'] = time.monotonic() - capture_started
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

    try:
        receipt['capture']['evidence'] = validate_capture(
            capture_dir, receipt['boot_started_after_capture_s'], capture_seconds)
    except ValueError as error:
        receipt.update(result='FAIL', reason=str(error))
        write_receipt(receipt_path, receipt)
        return 1

    analysis_command = [sys.executable, str(ROOT / 'scripts/analyze-rpmh-tail-log.py'),
                        str(capture_dir / 'boot.log'), '--output', str(analysis_path)]
    analyze = subprocess.run(analysis_command, capture_output=True, text=True, timeout=60)
    receipt['analysis'] = {'command': analysis_command, 'returncode': analyze.returncode,
                           'stdout': analyze.stdout, 'stderr': analyze.stderr}
    if analyze.returncode:
        receipt.update(result='FAIL', reason='RPMh tail record incomplete or rejected')
        write_receipt(receipt_path, receipt)
        return 1
    code = accept_analysis(receipt, analysis_path)
    write_receipt(receipt_path, receipt)
    return code


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
