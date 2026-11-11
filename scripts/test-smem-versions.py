#!/usr/bin/env python3
"""Synthetic copied-view tests only; never read a phone or firmware memory."""
import importlib.util
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    'smem_versions', Path(__file__).with_name('analyze-smem-versions.py'))
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
BASE = 0x86000000
SIZE = 65536
PART = 16384
PART_SIZE = 16384


def item_fixture():
    blob = bytearray(4096)
    blob[3 * 128:3 * 128 + 17] = b'TZ.XF.5.2-00209\0\0'
    blob[7 * 128:7 * 128 + 9] = b'HYP.TEST\0'
    return blob


def region_fixture(version=12, cached=False):
    blob = bytearray(SIZE)
    struct.pack_into('<I', blob, 64 + 7 * 4, version << 16)
    struct.pack_into('<IIII', blob, 192, 1, 32768, SIZE - 32768, 0)
    if version == 11:
        struct.pack_into('<IIII', blob, 208 + 469 * 16, 1, PART, 4096, 0)
        blob[PART:PART + 4096] = item_fixture()
        return blob
    footer = SIZE - 4096
    struct.pack_into('<4sII', blob, footer, b'$TOC', 1, 1)
    struct.pack_into('<IIIHHI', blob, footer + 32, PART, PART_SIZE, 0,
                     0xfffe, 0xfffe, 64)
    struct.pack_into('<4sIIIH', blob, footer + 80, b'SIII', SIZE, BASE, 0, 512)
    header = PART + PART_SIZE - 64 if cached else PART + 32
    data = header - 4096 if cached else header + 16
    uncached = 32 if cached else 32 + 16 + 4096
    cached_free = PART_SIZE - 64 - 4096 if cached else PART_SIZE
    struct.pack_into('<4sHHIII', blob, PART, b'$PRT', 0xfffe, 0xfffe,
                     PART_SIZE, uncached, cached_free)
    struct.pack_into('<HHIHHI', blob, header, 0xa5a5, 469, 4096, 0, 0, 0)
    blob[data:data + 4096] = item_fixture()
    return blob


class SmemVersionsTests(unittest.TestCase):
    def reject(self, blob, message=None, base=BASE):
        with self.assertRaisesRegex(ValueError, message or '.'):
            MODULE.analyze(blob, 'region', base)

    def test_item_labels_not_identity(self):
        report = MODULE.analyze(item_fixture(), 'item')
        self.assertEqual([x['kind'] for x in report['label_candidates']], ['TZ', 'HYP'])
        self.assertEqual(len(report['records']), 32)
        self.assertFalse(report['running_secure_identity_authenticated'])
        self.assertFalse(report['gpu_admission_changed'])

    def test_exact_item_length(self):
        for blob in (bytes(4095), bytes(4097)):
            with self.assertRaises(ValueError):
                MODULE.analyze(blob, 'item')

    def test_all_supported_heaps(self):
        for version, cached in ((11, False), (12, False), (12, True)):
            blob = region_fixture(version, cached)
            before = bytes(blob)
            with self.subTest(version=version, cached=cached):
                report = MODULE.analyze(blob, 'region', BASE)
                self.assertEqual(report['item469_sha256'], MODULE.analyze(item_fixture(), 'item')['input_sha256'])
                self.assertEqual(bytes(blob), before)

    def test_empty_and_nonascii_fields_preserved(self):
        blob = bytearray(4096)
        blob[75] = 0xff
        blob[128:203] = b'A' * 75
        report = MODULE.parse_item(blob)
        self.assertFalse(report['records'][0]['all_zero'])
        self.assertFalse(report['records'][0]['sysfs_active'])
        self.assertFalse(report['records'][0]['fields']['variant']['printable_ascii'])
        self.assertFalse(report['records'][1]['fields']['crm']['nul_terminated'])
        self.assertEqual(report['records'][1]['fields']['crm']['text'], 'A' * 75)

    def test_duplicate_labels_not_selected(self):
        blob = item_fixture()
        blob[128:256] = blob[384:512]
        self.assertEqual(len(MODULE.parse_item(blob)['label_candidates']), 3)

    def test_base_required_and_not_guessed(self):
        with self.assertRaises(ValueError):
            MODULE.analyze(region_fixture(), 'region')
        self.reject(region_fixture(), base=BASE + 4096)
        for base in (-1, 1 << 64, True):
            self.reject(region_fixture(), base=base)

    def test_initialized_version_header(self):
        for offset, value in ((192, 0), (204, 1), (92, 13 << 16), (196, 0xffffffff), (200, SIZE)):
            blob = region_fixture()
            struct.pack_into('<I', blob, offset, value)
            self.reject(blob)

    def test_footer_extent_version_info(self):
        for offset, value in ((SIZE - 4096, 0), (SIZE - 4092, 2),
                              (SIZE - 4088, 0xffffffff), (SIZE - 4016, 0),
                              (SIZE - 4012, 1), (SIZE - 4004, 1)):
            blob = region_fixture()
            struct.pack_into('<I', blob, offset, value)
            self.reject(blob)

    def test_partition_boundaries(self):
        for offset, fmt, value in ((SIZE - 4064, '<I', SIZE),
                                   (SIZE - 4060, '<I', 0xffffffff),
                                   (PART, '<I', 0), (PART + 4, '<H', 0),
                                   (PART + 8, '<I', 8192),
                                   (PART + 12, '<I', PART_SIZE + 1),
                                   (PART + 16, '<I', 16)):
            blob = region_fixture()
            struct.pack_into(fmt, blob, offset, value)
            self.reject(blob)

    def test_duplicate_or_overlapping_partitions(self):
        for same_hosts in (True, False):
            blob = region_fixture()
            footer = SIZE - 4096
            struct.pack_into('<I', blob, footer + 8, 2)
            blob[footer + 80:footer + 128] = blob[footer + 32:footer + 80]
            if not same_hosts:
                struct.pack_into('<I', blob, footer + 80, PART + 8192)
                struct.pack_into('<HH', blob, footer + 92, 0, 1)
                struct.pack_into('<4sHHIII', blob, PART + 8192, b'$PRT', 0, 1,
                                 PART_SIZE, 32, PART_SIZE)
            struct.pack_into('<4sIIIH', blob, footer + 128, b'SIII', SIZE, BASE, 0, 512)
            self.reject(blob, 'duplicate' if same_hosts else 'overlapping')

    def test_private_item_bounds_and_padding(self):
        for offset, fmt, value in ((PART + 32, '<H', 0),
                                   (PART + 34, '<H', 512),
                                   (PART + 36, '<I', 0xffffffff),
                                   (PART + 40, '<H', 4096),
                                   (PART + 42, '<H', 0xffff),
                                   (PART + 44, '<I', 1)):
            blob = region_fixture()
            struct.pack_into(fmt, blob, offset, value)
            self.reject(blob)

    def test_missing_duplicate_wrong_size_item(self):
        blob = region_fixture()
        struct.pack_into('<H', blob, PART + 34, 468)
        self.reject(blob, 'missing')
        blob = region_fixture()
        struct.pack_into('<I', blob, PART + 12, 32 + 2 * (16 + 4096))
        blob[PART + 4144:PART + 8256] = blob[PART + 32:PART + 4144]
        self.reject(blob, 'duplicate')
        blob = region_fixture()
        struct.pack_into('<H', blob, PART + 40, 8)
        self.reject(blob, '4096')

    def test_global_toc_rejections(self):
        for offset, value in ((208 + ITEM_OFFSET, 0), (212 + ITEM_OFFSET, 0xfffffff8),
                              (216 + ITEM_OFFSET, 0xfffffff8), (220 + ITEM_OFFSET, 0x12340000)):
            blob = region_fixture(11)
            struct.pack_into('<I', blob, offset, value)
            self.reject(blob)
        blob = region_fixture(11)
        struct.pack_into('<IIII', blob, 208 + 470 * 16, 1, PART, 4096, 0)
        self.reject(blob, 'overlapping')

    def test_cached_rejections(self):
        for offset, value in ((SIZE - 4048, 16), (SIZE - 4048, 48),
                              (PART + 16, PART_SIZE - 10)):
            blob = region_fixture(cached=True)
            struct.pack_into('<I', blob, offset, value)
            self.reject(blob)
        blob = region_fixture(cached=True)
        struct.pack_into('<I', blob, PART + PART_SIZE - 64 + 4, PART_SIZE)
        self.reject(blob)

    def test_two_cached_items_and_duplicate(self):
        blob = region_fixture(cached=True)
        first_header = PART + PART_SIZE - 64
        second_header = first_header - 4096 - 64
        struct.pack_into('<HHIHHI', blob, second_header, 0xa5a5, 468, 64, 0, 0, 0)
        struct.pack_into('<I', blob, PART + 16, second_header - 64 - PART)
        self.assertEqual(MODULE.analyze(blob, 'region', BASE)['mapping']['global_allocations_scanned'], 2)
        struct.pack_into('<H', blob, second_header + 2, 469)
        self.reject(blob, 'duplicate')

    def test_global_and_uncached_padding(self):
        blob = region_fixture(11)
        struct.pack_into('<I', blob, 220 + ITEM_OFFSET, BASE)
        self.assertEqual(MODULE.analyze(blob, 'region', BASE)['mapping']['smem_version'], 11)
        blob = region_fixture()
        struct.pack_into('<I', blob, PART + 12, 32 + 16 + 8 + 4104)
        struct.pack_into('<I', blob, PART + 36, 4104)
        struct.pack_into('<HH', blob, PART + 40, 8, 8)
        blob[PART + 56:PART + 56 + 4096] = item_fixture()
        self.assertEqual(MODULE.analyze(blob, 'region', BASE)['mapping']['item469']['size'], 4096)

    def test_v12_partition_does_not_require_legacy_toc_extent(self):
        blob = region_fixture()
        low = 4096
        blob[low:low + PART_SIZE] = blob[PART:PART + PART_SIZE]
        struct.pack_into('<I', blob, SIZE - 4064, low)
        self.assertEqual(MODULE.analyze(blob, 'region', BASE)['mapping']['item469']['offset'], low + 48)

    def test_sysfs_format_and_labels(self):
        raw = b'3:\n\tCRM:\t\tTZ.XF.5.2-00209\n\tVariant:\tYAQAANAAA\n\tVersion:\tCRM\n'
        result = MODULE.analyze_sysfs(raw)
        self.assertEqual(result['records'][0]['index'], 3)
        self.assertEqual(result['label_candidates'][0]['kind'], 'TZ')
        self.assertFalse(result['running_secure_identity_authenticated'])
        self.assertFalse(result['gpu_admission_changed'])
        for bad in (b'', b'Unavailable\n', b'Unknown\n', raw[:-1], raw + b'4:\n',
                    raw + raw, raw.replace(b'3:', b'32:'), raw.replace(b'CRM', b'BAD', 1),
                    raw.replace(b'CRM\n', b'\xff\n'), raw + b' ' * 4096,
                    raw.replace(b'YAQAANAAA', b'x' * 21), raw.replace(b'YAQAANAAA', b'\0'),
                    raw.replace(b'TZ.XF.5.2-00209', b'')):
            with self.subTest(bad=bad[:40]), self.assertRaises(ValueError):
                MODULE.analyze_sysfs(bad)

    def test_deterministic_mutation_campaign(self):
        rng = random.Random(469)
        source = region_fixture()
        for _ in range(256):
            blob = bytearray(source)
            offset = rng.randrange(len(blob))
            blob[offset] ^= 1 << rng.randrange(8)
            try:
                result = MODULE.analyze(blob, 'region', BASE)
                self.assertFalse(result['running_secure_identity_authenticated'])
            except ValueError:
                pass

    def test_cli_output_and_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            input_ = path / 'item.bin'
            output = path / 'receipt.json'
            input_.write_bytes(item_fixture())
            command = [sys.executable, str(Path(MODULE.__file__)), str(input_),
                       '--format', 'item', '--output', str(output)]
            run = subprocess.run(command, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            before = output.read_bytes()
            self.assertEqual(json.loads(before)['status'], 'PARSED_METADATA_ONLY')
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(output.read_bytes(), before)

    def test_cli_region_sysfs_and_failures(self):
        raw = b'3:\n\tCRM:\t\tTZ.XF.5.2-00209\n\tVariant:\tYAQAANAAA\n\tVersion:\tCRM\n'
        for name, blob, options, expected in (
            ('region', region_fixture(), ['--format', 'region', '--physical-base', hex(BASE)], 0),
            ('sysfs', raw, ['--format', 'sysfs'], 0),
            ('short', bytes(4095), ['--format', 'item'], 1),
            ('missing-base', region_fixture(), ['--format', 'region'], 1),
            ('truncated-sysfs', raw[:-1], ['--format', 'sysfs'], 1),
        ):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                input_ = path / 'input.bin'
                output = path / 'result.json'
                input_.write_bytes(blob)
                run = subprocess.run([sys.executable, str(Path(MODULE.__file__)), str(input_),
                                      *options, '--output', str(output)], capture_output=True)
                self.assertEqual(run.returncode, expected, run.stderr)
                self.assertEqual(output.exists(), expected == 0)


ITEM_OFFSET = 469 * 16

if __name__ == '__main__':
    unittest.main()
