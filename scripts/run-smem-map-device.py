#!/usr/bin/env python3
"""One explicitly invoked mapping diagnostic; no SMEM content or GPU execution."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


COMMON = module('smem_mapping_device_common', 'run-scm-pair-device.py')
LOG = module('smem_mapping_device_log', 'analyze-smem-handoff-log.py')
EXPECTED_IMAGE_SHA = '005b64524421ab90fa6d2b991d2e8cd298d19aee48e64b0d0c08cc4a1d2e2091'
EXPECTED_IMAGE_BYTES = 70684672
# Metadata embedded in the accepted candidate, not runtime authentication.
EXPECTED_BUILD_METADATA = {
    'probe_source': '4fa28bc8507c948fe144ead698de5ff72714333627a8ef1f6072c368056b9798',
    'profile_source': '95b8889c52eb2544d675a8ae2902a85ac6ebe2c178c5302bdeeac69571e7e7ff',
    'profile_header': 'ec8e6712ff80c3f7d207d8a62df0941ac6d938e48fd1cf6cd727a74903d09c59',
}
PROFILE = 'smem-map-device-run'
ANALYZER = 'analyze-smem-handoff-log.py'
DEFAULT_CAPTURE_SECONDS = 300
RECEIPT_SCOPE = {
    'smem_bytes_acquired': False,
    'smem_access_authorized': False,
    'gpu_admission_changed': False,
    'running_secure_identity_authenticated': False,
    'log_origin_authenticated': False,
    'same_boot_origin_authenticated': False,
}


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def image_identity(image):
    image = image.resolve()
    require(image.is_file(), 'missing mapping image')
    size = image.stat().st_size
    digest = COMMON.sha256(image)
    require(size == EXPECTED_IMAGE_BYTES and digest == EXPECTED_IMAGE_SHA,
            'mapping image identity mismatch')
    return {'path': str(image), 'bytes': size, 'sha256': digest}


def accept_analysis(receipt, analysis_path):
    try:
        evidence = receipt['capture']['evidence']
        raw_path = Path(evidence['directory']) / 'boot.log'
        with raw_path.open('rb') as stream:
            raw = stream.read(2 * 1024 * 1024 + 1)
        require(hashlib.sha256(raw).hexdigest() == evidence['sha256'],
                'mapping analysis log identity mismatch')
        analysis = json.loads(analysis_path.read_text())
        require(analysis == LOG.analyze(raw), 'mapping analysis differs from independent reconstruction')
        require(analysis.get('status') == 'HANDOFF_COMPLETE', 'mapping handoff is incomplete')
        require(analysis.get('build_metadata') == EXPECTED_BUILD_METADATA,
                'mapping build metadata does not match the accepted candidate')
        passed = analysis.get('diagnostic_pass')
        require(type(passed) is bool, 'invalid mapping diagnostic outcome')
        require(analysis.get('diagnostic', {}).get('status') ==
                ('MAPPING_DIAGNOSTIC_PASS' if passed else 'DIAGNOSTIC_FAILED'),
                'mapping diagnostic summary contradiction')
        for name in (*RECEIPT_SCOPE, 'logger_delay_verified', 'gpu_execution'):
            require(analysis.get(name) is False, 'unexpected mapping claim: ' + name)
        require(analysis.get('log_sha256') == evidence['sha256'], 'mapping analyzer log identity mismatch')
    except (OSError, ValueError, KeyError, TypeError) as error:
        receipt.update(result='FAIL', reason=str(error), mapping_diagnostic_pass=None,
                       evidence_complete=False)
        return 1
    receipt.setdefault('analysis', {}).update(
        status=analysis['status'], diagnostic_pass=passed,
        diagnostic_status=analysis['diagnostic']['status'],
        log_sha256=analysis['log_sha256'], raw_sha256=analysis['raw_sha256'],
        scheduled_record_seen=analysis['scheduled_record_seen'],
        build_metadata=analysis['build_metadata'], build_metadata_matches_candidate=True,
        checksum_verified=True, gpu_execution=False, gpu_admission_changed=False)
    receipt.update(result='PASS', mapping_diagnostic_pass=passed, evidence_complete=True,
                   reason='complete mapping evidence; diagnostic outcome is not SMEM or GPU authorization')
    return 0


class Diagnostic:
    PROFILE = PROFILE
    ANALYZER = ANALYZER
    RECEIPT_SCOPE = {**RECEIPT_SCOPE, 'mapping_diagnostic_pass': None,
                     'evidence_complete': False}
    image_identity = staticmethod(image_identity)
    accept_analysis = staticmethod(accept_analysis)


def run(image, output, capture_seconds):
    require(type(capture_seconds) is int and 300 <= capture_seconds <= 600,
            'mapping capture duration must be 300..600 seconds')
    return COMMON.run(image, output, capture_seconds, diagnostic=Diagnostic)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--capture-seconds', type=int, default=DEFAULT_CAPTURE_SECONDS)
    args = parser.parse_args()
    try:
        return run(args.image, args.output.resolve(), args.capture_seconds)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'REFUSED: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
