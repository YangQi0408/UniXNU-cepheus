#!/usr/bin/env python3
"""Run one bounded UEFI/XNU SCM-pair diagnostic through fastboot boot only."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
COMMON_SPEC = importlib.util.spec_from_file_location(
    'bounded_device_runner', ROOT / 'scripts/run-rpmh-tail-device.py')
COMMON = importlib.util.module_from_spec(COMMON_SPEC)
COMMON_SPEC.loader.exec_module(COMMON)

EXPECTED_IMAGE_SHA = '415d47275ca5e6f4db13015fd423f6c9ee29547f192895582bc5edc9bb07a30c'
EXPECTED_IMAGE_BYTES = 70688768
EXPECTED_PRODUCT = 'cepheus'
DEFAULT_CAPTURE_SECONDS = 360
FASTBOOT_TIMEOUT_SECONDS = 600

select_device = COMMON.select_device
parse_product = COMMON.parse_product
boot_command = COMMON.boot_command
ensure_port_free = COMMON.ensure_port_free
wait_for_line = COMMON.wait_for_line
validate_capture = COMMON.validate_capture
write_receipt = COMMON.write_receipt


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


def accept_analysis(receipt, analysis_path):
    if not analysis_path.is_file():
        receipt.update(result='FAIL', reason='SCM-pair analyzer produced no result')
        return 1
    try:
        analysis = json.loads(analysis_path.read_text())
    except (OSError, ValueError) as error:
        receipt.update(result='FAIL', reason=f'invalid SCM-pair analysis: {error}')
        return 1
    if not isinstance(analysis, dict) or analysis.get('status') != 'PAIR_COMPLETE':
        receipt.update(result='FAIL', reason='SCM-pair diagnostic incomplete or rejected')
        return 1

    summary = receipt.setdefault('analysis', {})
    summary.update(status=analysis.get('status'), reason=analysis.get('reason'),
                   uefi_pas13=analysis.get('uefi_pas13'),
                   xnu_pas13=analysis.get('xnu_pas13'),
                   scheduled_record_seen=analysis.get('scheduled_record_seen'),
                   logger_delay_verified=analysis.get('logger_delay_verified'),
                   log_origin_authenticated=analysis.get('log_origin_authenticated'),
                   gpu_execution=analysis.get('gpu_execution'),
                   admission_changed=analysis.get('admission_changed'),
                   query_outcome_is_gpu_authorization=False)
    evidence = receipt.get('capture', {}).get('evidence', {})
    if analysis.get('log_sha256') != evidence.get('sha256'):
        receipt.update(result='FAIL', reason='analysis does not match the captured log identity')
        return 1
    if analysis.get('checksum_verified') is not True or \
            analysis.get('log_origin_authenticated') is not False:
        receipt.update(result='FAIL', reason='analysis origin or checksum claims are inconsistent')
        return 1
    for name in ('uefi_pas13', 'xnu_pas13'):
        if not isinstance(analysis.get(name), (bool, type(None))):
            receipt.update(result='FAIL', reason=f'analysis has an invalid {name} outcome')
            return 1
    if summary['gpu_execution'] is not False or summary['admission_changed'] is not False:
        receipt.update(result='FAIL',
                       reason='analysis unexpectedly claims GPU execution or admission mutation')
        return 1
    receipt.update(result='PASS',
                   reason='complete SCM-pair diagnostic; query result does not authorize GPU execution')
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
        'profile': 'scm-pair-device-run',
        'result': 'RUNNING',
        'image': identity,
        'device': {'serial': serial, 'product': product},
        'boot_command': boot_command(serial, identity['path']),
        'capture_seconds': capture_seconds,
        'boot_attempt_count': 0,
        'phone_boot_issued': False,
        'partition_flash_issued': False,
        'reboot_issued': False,
        'gpu_execution': False,
        'admission_changed': False,
        'query_outcome_is_gpu_authorization': False,
    }
    write_receipt(receipt_path, receipt)

    capture = None
    capture_command = [sys.executable, str(ROOT / 'scripts/capture-usb-log.py'),
                       str(capture_dir), '--duration', str(capture_seconds)]
    capture_started = time.monotonic()
    try:
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
                receipt['boot_attempt_count'] = 1
                receipt['phone_boot_issued'] = True
                write_receipt(receipt_path, receipt)
                boot = subprocess.run(boot_command(serial, identity['path']),
                                      capture_output=True, text=True,
                                      timeout=FASTBOOT_TIMEOUT_SECONDS)
                receipt['boot_finished_after_capture_s'] = time.monotonic() - capture_started
                receipt['fastboot'] = {'returncode': boot.returncode,
                                       'stdout': boot.stdout, 'stderr': boot.stderr}
                write_receipt(receipt_path, receipt)
                if boot.returncode:
                    raise ValueError(f'fastboot boot failed with {boot.returncode}')
                try:
                    remaining, _ = capture.communicate(timeout=capture_seconds + 30)
                except subprocess.TimeoutExpired as error:
                    raise ValueError('bounded capture did not finish') from error
                log_stream.write(remaining.encode())
            finally:
                if capture.poll() is None:
                    capture.terminate()
                    capture.wait(timeout=10)

        receipt['capture'] = {'command': capture_command, 'returncode': capture.returncode,
                              'log': str(capture_log)}
        if capture.returncode:
            raise ValueError('capture process failed')
        receipt['capture']['evidence'] = validate_capture(
            capture_dir, receipt['boot_started_after_capture_s'], capture_seconds)

        analysis_command = [sys.executable, str(ROOT / 'scripts/analyze-scm-pair-log.py'),
                            str(capture_dir / 'boot.log'), '--output', str(analysis_path)]
        analyze = subprocess.run(analysis_command, capture_output=True, text=True, timeout=60)
        receipt['analysis'] = {'command': analysis_command, 'returncode': analyze.returncode,
                               'stdout': analyze.stdout, 'stderr': analyze.stderr}
        if analyze.returncode:
            receipt.update(result='FAIL', reason='SCM-pair diagnostic incomplete or rejected')
            write_receipt(receipt_path, receipt)
            return 1
        code = accept_analysis(receipt, analysis_path)
        write_receipt(receipt_path, receipt)
        return code
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        receipt.update(result='FAIL', reason=str(error))
        write_receipt(receipt_path, receipt)
        return 2
    finally:
        if capture is not None and capture.poll() is None:
            capture.terminate()
            try:
                capture.wait(timeout=10)
            except subprocess.TimeoutExpired:
                capture.kill()
                capture.wait(timeout=10)


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
