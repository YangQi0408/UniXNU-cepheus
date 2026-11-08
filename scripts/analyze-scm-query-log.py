#!/usr/bin/env python3
"""Verify saved QUERY7 transcripts against the production RAM-only engine.

No native SMC callback is linked. A complete transcript can report supported=0;
neither outcome establishes PAS authentication, UI acceptance or GPU execution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REGS = re.compile(r'CEPHEUS SCM INFO REGS: (in|out)=((?:0x[0-9a-f]+|0)(?::(?:0x[0-9a-f]+|0)){7})')
SUMMARY = re.compile(r'CEPHEUS SCM INFO: e=(-?\d+) convention=(\d+) available=(0x[0-9a-f]+|0) supported=(\d+) calls=(\d+) transport=(0x[0-9a-f]+|0) secure=(0x[0-9a-f]+|0); no PAS mutations')
QUERY7 = re.compile(r'CEPHEUS SCM QUERY7: confirmed=([01]) \(0 means false/error/not-reached; see REGS\); normal64 calls=7; false never overridden; no PAS mutations')
END = re.compile(r'CEPHEUS SCM INFO: end error=(-?\d+) calls=(\d+) supported=(\d+); no power or GPU startup')
BEGIN = 'CEPHEUS SCM INFO: begin query_limit=16; secure return time unbounded; no PAS mutations'


def compile_replayer(directory):
    source = directory / 'replay.c'
    source.write_text(r'''
#include <inttypes.h>
#include <stdio.h>
#include "cepheus_scm_discovery.h"
static uint64_t inputs[16][8], outputs[16][8];
static unsigned count, position, mismatch;
static int lease(void *unused) {(void)unused; return 0;}
static int invoke(void *unused, struct ceph_scm_regs *regs)
{
    (void)unused;
    if (position >= count) { mismatch = 1; return -1; }
    for (unsigned i = 0; i < 8; ++i)
        if (regs->x[i] != inputs[position][i]) { mismatch = 1; return -1; }
    for (unsigned i = 0; i < 8; ++i) regs->x[i] = outputs[position][i];
    ++position;
    return 0;
}
int main(void)
{
    if (scanf("%u", &count) != 1 || !count || count > 16) return 2;
    for (unsigned n = 0; n < count; ++n) {
        for (unsigned i = 0; i < 8; ++i)
            if (scanf("%" SCNx64, &inputs[n][i]) != 1) return 2;
        for (unsigned i = 0; i < 8; ++i)
            if (scanf("%" SCNx64, &outputs[n][i]) != 1) return 2;
    }
    struct ceph_scm_discovery state = {0};
    struct ceph_scm_discovery_ops ops = {NULL, lease, invoke};
    int error = ceph_scm_discover_pas13(&state, &ops);
    if (mismatch || position != count) return 3;
    printf("%d %u %u %u %u %" PRIu64 " %" PRIu64 "\n", error,
        (unsigned)state.convention, state.available_mask, state.supported,
        state.calls, state.transport, state.secure);
    return 0;
}
''')
    executable = directory / 'replay'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-I', str(ROOT / 'xnu'),
                    str(source), '-o', str(executable)], check=True, capture_output=True)
    return executable


def analyze(text, executable):
    records = [(n, line) for n, line in enumerate(text.splitlines(), 1)
               if line.startswith('CEPHEUS SCM')]
    result = {'status': 'INCOMPLETE', 'query7_confirmed': None,
              'pas13_supported': None, 'records': records,
              'scope': 'Saved transcript checked against production engine in host RAM; no SMC',
              'limitations': ['Log origin is not authenticated',
                              'No UI acceptance, PAS authentication or GPU execution proof',
                              'Missing UDP records cannot establish a secure call did not return']}
    if len(records) > 40:
        return {**result, 'status': 'REJECTED', 'reason': 'Multiple or oversized query transcripts'}
    if not records or records[0][1] != BEGIN:
        return {**result, 'reason': 'Missing fixed begin record'}
    summaries = [(i, SUMMARY.fullmatch(line)) for i, (_, line) in enumerate(records)
                 if SUMMARY.fullmatch(line)]
    confirmations = [(i, QUERY7.fullmatch(line)) for i, (_, line) in enumerate(records)
                     if QUERY7.fullmatch(line)]
    ends = [(i, END.fullmatch(line)) for i, (_, line) in enumerate(records) if END.fullmatch(line)]
    if any(len(items) > 1 for items in (summaries, confirmations, ends)) or sum(line == BEGIN for _, line in records) != 1:
        return {**result, 'status': 'REJECTED', 'reason': 'Duplicate one-shot records'}
    if not summaries or not confirmations or not ends:
        return {**result, 'reason': 'Missing summary/QUERY7/end; legacy six-call logs are not QUERY7 evidence'}
    summary_index, summary = summaries[0]
    if (confirmations[0][0], ends[0][0], len(records)) != (summary_index + 1, summary_index + 2, summary_index + 3):
        return {**result, 'status': 'REJECTED', 'reason': 'Summary/QUERY7/end ordering or extra records'}
    pairs = []
    pending = None
    for _, line in records[1:summary_index]:
        match = REGS.fullmatch(line)
        if not match:
            return {**result, 'status': 'REJECTED', 'reason': 'Malformed or unexpected register record'}
        values = [int(value, 16) for value in match[2].split(':')]
        if any(value >= 1 << 64 for value in values):
            return {**result, 'status': 'REJECTED', 'reason': 'Register exceeds64bits'}
        if match[1] == 'in':
            if pending is not None:
                return {**result, 'reason': 'Input without preceding return'}
            pending = values
        else:
            if pending is None:
                return {**result, 'reason': 'Return without preceding input'}
            pairs.append((pending, values))
            pending = None
    if pending is not None or not pairs:
        return {**result, 'reason': 'Missing paired call records'}
    claimed = [int(value, 16 if index in (2, 5, 6) else 10)
               for index, value in enumerate(summary.groups())]
    if len(pairs) != claimed[4]:
        return {**result, 'reason': 'Summary call count differs from paired records'}
    if len(pairs) > 16:
        return {**result, 'status': 'REJECTED', 'reason': 'Query budget exceeded'}
    payload = str(len(pairs)) + '\n' + '\n'.join(
        ' '.join(f'{value:x}' for regs in pair for value in regs) for pair in pairs) + '\n'
    run = subprocess.run([str(executable)], input=payload, capture_output=True, text=True, timeout=10)
    if run.returncode != 0:
        return {**result, 'status': 'REJECTED', 'reason': 'Transcript differs from production query/resume sequence'}
    expected = list(map(int, run.stdout.split()))
    if expected != claimed or list(map(int, ends[0][1].groups())) != [expected[0], expected[4], expected[3]]:
        return {**result, 'status': 'REJECTED', 'reason': 'Summary/end contradict production result'}
    confirmed = int(confirmations[0][1][1])
    if confirmed != bool(expected[2] & 0x10):
        return {**result, 'status': 'REJECTED', 'reason': 'QUERY7 flag contradicts verified availability mask'}
    # Only a paired, completed PAS13 query can establish its boolean result.
    # Error/unreached remain unknown even when the engine's supported field is0.
    pas_result = None
    for index, (incoming, outgoing) in enumerate(pairs):
        if incoming[0] in (0x42000207, 0x02000207) and incoming[1:3] == [1, 13]:
            while (outgoing[0] & 0xffffffff) == 1 and index + 1 < len(pairs):
                index += 1
                outgoing = pairs[index][1]
            if (outgoing[0] & 0xffffffff) == 0 and outgoing[1] in (0, 1):
                pas_result = bool(outgoing[1])
    return {**result, 'status': 'TRANSCRIPT_COMPLETE', 'calls': expected[4],
            'convention': expected[1], 'available_mask': expected[2], 'engine_error': expected[0],
            'query7_confirmed': bool(confirmed), 'pas13_supported': pas_result,
            'reason': 'Complete evidence of query outcome only; false/error never overrides admission'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('keep existing evidence; output already exists')
    if args.log.stat().st_size > 32 * 1024 * 1024:
        parser.error('log exceeds32MiB bound')
    raw = args.log.read_bytes()
    with tempfile.TemporaryDirectory() as temporary:
        result = analyze(raw.decode('utf-8', errors='replace'), compile_replayer(Path(temporary)))
    result.update(log_sha256=hashlib.sha256(raw).hexdigest(), log_bytes=len(raw),
                  source_sha256=hashlib.sha256((ROOT / 'xnu/cepheus_scm_discovery.h').read_bytes()).hexdigest())
    with args.output.open('x') as output:
        json.dump(result, output, indent=2)
        output.write('\n')
    print(result['status'] + ': ' + result['reason'])
    return {'TRANSCRIPT_COMPLETE': 0, 'INCOMPLETE': 2, 'REJECTED': 1}[result['status']]


if __name__ == '__main__':
    raise SystemExit(main())
