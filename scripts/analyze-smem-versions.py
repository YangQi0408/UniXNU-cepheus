#!/usr/bin/env python3
"""Parse saved SMEM item469 or a copied region; metadata only, no device access."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

ITEM = 469
TABLE_BYTES = 4096
MAX_REGION = 16 * 1024 * 1024
HEADER_BYTES = 208
TOC_BYTES = HEADER_BYTES + 512 * 16
FOOTER_BYTES = 4096


def require(condition, message):
    if not condition:
        raise ValueError(message)


def bounded(blob, offset, size, start=0, end=None):
    end = len(blob) if end is None else end
    require(0 <= start <= offset <= end <= len(blob)
            and 0 <= size <= end - offset, 'range outside copied view')
    return blob[offset:offset + size]


def unpack(blob, fmt, offset, start=0, end=None):
    return struct.unpack(fmt, bounded(blob, offset, struct.calcsize(fmt), start, end))


def nonoverlapping(ranges):
    ordered = sorted(ranges)
    require(all(left[1] <= right[0] for left, right in zip(ordered, ordered[1:])),
            'overlapping allocated ranges')


def extract_region(blob, physical_base):
    require(type(physical_base) is int and 0 <= physical_base < 1 << 64,
            'invalid caller-supplied physical base')
    require(HEADER_BYTES + FOOTER_BYTES <= len(blob) <= MAX_REGION
            and physical_base + len(blob) <= 1 << 64, 'invalid region size')
    version = unpack(blob, '<I', 64 + 7 * 4)[0] >> 16
    initialized, free, available, reserved = unpack(blob, '<IIII', 192)
    require(initialized == 1 and reserved == 0 and free <= len(blob)
            and available <= len(blob) - free, 'invalid initialized SMEM header')
    require(version in (11, 12), 'unsupported SMEM version')
    heap_start = TOC_BYTES if version == 11 else HEADER_BYTES
    require(heap_start <= len(blob), 'SMEM header extent exceeds region')
    footer = len(blob) - FOOTER_BYTES
    has_ptable = bounded(blob, footer, 4) == b'$TOC'
    partitions, pairs = [], set()
    limit = footer if has_ptable else len(blob)
    count = 512
    if has_ptable:
        table_version, entries = unpack(blob, '<II', footer + 4)
        require(table_version == 1 and entries <= (FOOTER_BYTES - 32 - 18) // 48,
                'invalid partition table extent/version')
        for index in range(entries):
            offset, size, flags, host0, host1, cacheline = unpack(
                blob, '<IIIHHI', footer + 32 + index * 48)
            if offset == size == 0:
                continue
            require(offset >= heap_start and size >= 32 and offset % 8 == 0,
                    'invalid partition offset/size')
            bounded(blob, offset, size, heap_start, footer)
            pair = tuple(sorted((host0, host1)))
            require(pair not in pairs, 'duplicate partition host pair')
            pairs.add(pair)
            magic, actual0, actual1, actual_size, uncached, cached = unpack(
                blob, '<4sHHIII', offset)
            require(magic == b'$PRT' and (actual0, actual1) == (host0, host1)
                    and actual_size == size and 32 <= uncached <= cached <= size,
                    'invalid partition header/free boundaries')
            partitions.append({'offset': offset, 'size': size, 'flags': flags,
                               'hosts': [host0, host1], 'cacheline': cacheline,
                               'uncached': uncached, 'cached': cached})
        nonoverlapping([(p['offset'], p['offset'] + p['size']) for p in partitions])
        info_offset = footer + 32 + entries * 48
        if version == 12:
            magic, size, base, reserved, count = unpack(blob, '<4sIIIH', info_offset)
            require(magic == b'SIII' and size == len(blob) and base == physical_base
                    and reserved == 0 and count > ITEM, 'invalid v12 item-count info')
    require(version != 12 or has_ptable, 'v12 partition table missing')

    allocations = []
    if version == 11:
        require(free >= TOC_BYTES and free <= limit, 'invalid global free offset')
        for item in range(512):
            allocated, offset, size, aux = unpack(blob, '<IIII', HEADER_BYTES + item * 16)
            if not allocated:
                continue
            require(allocated == 1 and size > 0, 'invalid global allocation')
            # External auxiliary regions are not in this copied single-region view.
            require((aux & 0xfffffffc) in (0, physical_base), 'external auxiliary region unsupported')
            if item < 8:
                bounded(blob, offset, size, 0, TOC_BYTES)
            else:
                require(offset % 8 == 0 and size % 8 == 0, 'unaligned global allocation')
                bounded(blob, offset, size, TOC_BYTES, free)
            allocations.append({'item': item, 'offset': offset, 'size': size,
                                'heap': 'global-toc'})
        nonoverlapping([(a['offset'], a['offset'] + a['size']) for a in allocations])
        nonoverlapping([(a['offset'], a['offset'] + a['size']) for a in allocations
                        if a['item'] >= 8] +
                       [(p['offset'], p['offset'] + p['size']) for p in partitions])
    else:
        globals_ = [p for p in partitions if p['hosts'] == [0xfffe, 0xfffe]]
        require(len(globals_) == 1, 'missing/ambiguous global partition')
        partition = globals_[0]
        start, size = partition['offset'], partition['size']
        uncached_end = start + partition['uncached']
        cached_end = start + partition['cached']
        end = start + size
        seen = set()

        def allocation(header, lower, upper, cached=False):
            canary, item, length, padding, header_padding, reserved = unpack(
                blob, '<HHIHHI', header, lower, upper)
            require(canary == 0xa5a5 and item < count and item not in seen
                    and length > padding and reserved == 0, 'invalid/duplicate private item')
            seen.add(item)
            if cached:
                require(header_padding == 0, 'cached header padding unsupported')
                data = header - length
                bounded(blob, data, length, lower, header)
            else:
                data = header + 16 + header_padding
                bounded(blob, data, length, lower, upper)
            allocations.append({'item': item, 'offset': data, 'size': length - padding,
                                'allocated_bytes': length, 'header_offset': header,
                                'heap': 'cached' if cached else 'uncached'})
            return length, header_padding

        cursor = start + 32
        while cursor < uncached_end:
            length, padding = allocation(cursor, start + 32, uncached_end)
            cursor += 16 + padding + length
        require(cursor == uncached_end, 'uncached allocation boundary mismatch')
        if cached_end != end:
            line = partition['cacheline']
            # Kernel uses aligned32 for the first header, UEFI aligned16. At
            # cacheline>=32 these agree; do not choose an ambiguous smaller layout.
            require(32 <= line <= 4096 and (line & (line - 1)) == 0 and size % line == 0,
                    'unsupported/ambiguous cached alignment')
            cursor = end - line
            while cursor > cached_end:
                length, _ = allocation(cursor, cached_end, end, cached=True)
                cursor -= length + line
            require(cursor + line == cached_end, 'cached allocation boundary mismatch')
        nonoverlapping([(a['offset'], a['offset'] + a['size']) for a in allocations])
    selected = [a for a in allocations if a['item'] == ITEM]
    require(len(selected) == 1, 'item469 missing/ambiguous')
    selected = selected[0]
    require(selected['size'] == TABLE_BYTES, 'item469 must contain exactly4096 bytes')
    return bounded(blob, selected['offset'], TABLE_BYTES), {
        'smem_version': version, 'physical_base_supplied': hex(physical_base),
        'item_count': count, 'partitions': partitions, 'item469': selected,
        'global_allocations_scanned': len(allocations),
    }


def parse_item(blob):
    require(len(blob) == TABLE_BYTES, 'version table must contain exactly4096 bytes')
    records, candidates = [], []
    for index in range(32):
        raw = blob[index * 128:(index + 1) * 128]
        fields = {}
        for name, offset, size in (('crm', 0, 75), ('variant', 75, 20), ('oem', 95, 33)):
            value = raw[offset:offset + size]
            text = value.split(b'\0', 1)[0]
            fields[name] = {'text': text.decode('ascii', errors='backslashreplace'),
                            'raw_hex': value.hex(), 'nul_terminated': b'\0' in value,
                            'printable_ascii': all(32 <= byte <= 126 for byte in text)}
        candidate = None
        if fields['crm']['printable_ascii']:
            match = re.match(r'^(TZ|HYP)\.[A-Za-z0-9_.-]+(?:$|[ :])', fields['crm']['text'])
            if match:
                candidate = match[1]
                candidates.append({'index': index, 'kind': candidate,
                                   'crm': fields['crm']['text']})
        records.append({'index': index, 'offset': index * 128,
                        'sha256': hashlib.sha256(raw).hexdigest(),
                        'all_zero': not any(raw), 'sysfs_active': raw[0] != 0,
                        'fields': fields, 'label_candidate': candidate})
    return {'status': 'PARSED_METADATA_ONLY', 'records': records,
            'label_candidates': candidates,
            'running_secure_identity_authenticated': False,
            'gpu_admission_changed': False, 'device_operations': False,
            'limits': ['Copied metadata, not authenticated running secure image bytes.',
                       'TZ/HYP indices are not assumed; label matches are candidates only.',
                       'Copy coherence, live mapping/cache state and metadata writers are unknown.',
                       'No PAS support/authentication, owner, secure memory or GPU execution proof.']}


def analyze(blob, format_, physical_base=None):
    blob = bytes(blob)
    mapping = None
    if format_ == 'region':
        require(physical_base is not None, 'region requires explicit physical base')
        item, mapping = extract_region(blob, physical_base)
    elif format_ == 'item':
        require(physical_base is None, 'item format has no physical base')
        item = blob
    else:
        raise ValueError('unsupported input format')
    result = parse_item(item)
    result.update(input_format=format_, input_bytes=len(blob),
                  input_sha256=hashlib.sha256(blob).hexdigest(),
                  item469_sha256=hashlib.sha256(item).hexdigest(), mapping=mapping)
    return result


def analyze_sysfs(blob):
    """Accept only the complete unambiguous msm_get_images rendering grammar."""
    require(0 < len(blob) < 4096, 'sysfs view empty or possibly PAGE_SIZE-truncated')
    try:
        text = blob.decode('ascii')
    except UnicodeDecodeError as error:
        raise ValueError('non-ASCII sysfs rendering; retain raw evidence') from error
    require(text not in ('Unavailable\n', 'Unknown\n'), 'version metadata unavailable')
    pattern = re.compile(r'(\d+):\n\tCRM:\t\t([^\n\r]*)\n'
                         r'\tVariant:\t([^\n\r]*)\n\tVersion:\t([^\n\r]*)\n')
    records, candidates, cursor, previous = [], [], 0, -1
    for match in pattern.finditer(text):
        require(match.start() == cursor, 'malformed/partial sysfs record')
        index = int(match[1])
        require(previous < index < 32, 'reordered/duplicate/out-of-range sysfs index')
        fields = dict(zip(('crm', 'variant', 'oem'), match.groups()[1:]))
        require(fields['crm'] and all(len(fields[name]) <= size for name, size
                in (('crm', 75), ('variant', 20), ('oem', 33)))
                and all(all(32 <= ord(c) <= 126 for c in value) for value in fields.values()),
                'invalid sysfs field length/characters')
        candidate = re.match(r'^(TZ|HYP)\.[A-Za-z0-9_.-]+(?:$|[ :])', fields['crm'])
        records.append({'index': index, 'fields': fields,
                        'label_candidate': candidate[1] if candidate else None})
        if candidate:
            candidates.append({'index': index, 'kind': candidate[1], 'crm': fields['crm']})
        cursor, previous = match.end(), index
    require(records and cursor == len(text), 'malformed/partial sysfs rendering')
    return {'status': 'PARSED_SYSFS_METADATA_ONLY', 'input_bytes': len(blob),
            'input_sha256': hashlib.sha256(blob).hexdigest(), 'records': records,
            'label_candidates': candidates, 'running_secure_identity_authenticated': False,
            'gpu_admission_changed': False, 'device_operations': False,
            'limits': ['Sysfs rendering only; absent rows are not a raw all-zero record proof.',
                       'Grammar and size checks cannot detect truncation exactly between complete records.',
                       'Raw item bytes, NUL padding and allocation size are not recovered.',
                       'Matching TZ/HYP labels are not authenticated running image identity.',
                       'No PAS support/authentication, owner, secure memory or GPU execution proof.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--format', choices=('item', 'region', 'sysfs'), required=True)
    parser.add_argument('--physical-base', type=lambda value: int(value, 0))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists(), 'refuse output overwrite')
    with args.input.open('rb') as stream:
        blob = stream.read(MAX_REGION + 1)
    if args.format == 'sysfs':
        require(args.physical_base is None, 'sysfs format has no physical base')
        result = analyze_sysfs(blob)
    else:
        result = analyze(blob, args.format, args.physical_base)
    result['analyzer_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    with args.output.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(f"{result['status']}: {len(result['records'])} records; no running secure identity or GPU proof")


if __name__ == '__main__':
    main()
