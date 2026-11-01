#!/usr/bin/env python3
"""Summarize SWEET PERF windows. Rates are software calls, NOT panel FPS."""
import argparse
from pathlib import Path
import re

FIELDS = ('ms', 'submitted', 'presented', 'overwritten', 'copy_us',
          'max_us', 'bytes', 'missed', 'off')
PATTERN = re.compile(r'SWEET PERF: ' + r'\s+'.join(f'{f}=(\\d+)' for f in FIELDS))


def windows(text):
    return [dict(zip(FIELDS, map(int, match.groups()))) for match in PATTERN.finditer(text)]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    a = p.parse_args()
    records = windows(a.log.read_text(errors='replace'))
    if not records: p.error('no complete SWEET PERF windows yet')
    print('window  seconds  submit/s  present/s  avg_present_ms  max_ms  MiB_copied  missed  off')
    for i, r in enumerate(records, 1):
        seconds = r['ms'] / 1000
        if seconds <= 0: continue
        average = r['copy_us'] / 1000 / r['presented'] if r['presented'] else 0
        print(f"{i:6}  {seconds:7.3f}  {r['submitted']/seconds:8.2f}  "
              f"{r['presented']/seconds:9.2f}  {average:14.3f}  "
              f"{r['max_us']/1000:6.3f}  {r['bytes']/2**20:10.1f}  {r['missed']:6}  {r['off']}")
    print('Static-screen low submission rate is normal. Present timing excludes userspace rendering and off-screen fade.')


if __name__ == '__main__':
    main()
