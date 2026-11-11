#!/usr/bin/env python3
"""Inspect the linked XNU copied-record receiver, not phone/GPU execution."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

MARKERS = (b'CEPHEUS SMEM MAP: begin version=1 bytes=', b'CEPHEUS SMEM MAP: raw=',
           b'CEPHEUS SMEM MAP: end diagnostic=', b'cepheus-smem-map')
FORBIDDEN = {'smc', 'hvc', 'at', 'dc', 'ic', 'tlbi', 'msr', 'mrs', 'blr', 'br',
             'blraa', 'blrab', 'blraaz', 'blrabz', 'braa', 'brab', 'braaz', 'brabz'}


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def function(assembly, name):
    anchor = '<_' + name + '>:'
    require(assembly.count(anchor) == 1, 'missing or repeated function: ' + name)
    tail = assembly.split(anchor, 1)[1]
    return re.split(r'\n(?:[0-9a-f]+ )?<_[^>]+>:', tail, maxsplit=1)[0]


def instructions(body):
    return re.findall(r'^\s*[0-9a-f]+:\s+([a-z0-9.]+)\s*([^\n]*)', body, re.MULTILINE)


def transfers(body, name, allowed):
    calls = []
    require(instructions(body), 'empty native function: ' + name)
    for opcode, operands in instructions(body):
        require(opcode not in FORBIDDEN, 'unexpected instruction in ' + name + ': ' + opcode)
        if opcode not in {'bl', 'b', 'cbz', 'cbnz', 'tbz', 'tbnz'} and not opcode.startswith('b.'):
            continue
        symbol = re.search(r'<([^>]+)>', operands)
        require(symbol, 'unresolved transfer in ' + name)
        target = symbol.group(1)
        if opcode != 'bl' and (target == name or re.fullmatch(re.escape(name) + r'\+0x[0-9a-f]+', target)):
            continue
        require(opcode in {'bl', 'b'} and target in allowed,
                'unexpected transfer in ' + name + ': ' + target)
        calls.append(target)
    return calls


def objdump_tool():
    tool = os.environ.get('LLVM_OBJDUMP') or shutil.which('llvm-objdump')
    if not tool and sys.platform == 'darwin':
        result = subprocess.run(['xcrun', '--find', 'llvm-objdump'], text=True, capture_output=True)
        if result.returncode == 0:
            tool = result.stdout.strip()
    require(tool, 'llvm-objdump required (CI installs LLVM explicitly)')
    return tool


def inspect(assembly):
    receiver = function(assembly, 'soc_smem_map')
    thread = function(assembly, 'soc_thread')
    require(any(opcode == 'bl' and re.search(r'<_soc_smem_map>', operands)
                for opcode, operands in instructions(thread)), 'diagnostic thread does not call receiver')
    for symbol in ('_SecureDTLookupEntry', '_SecureDTGetProperty', '_printf'):
        require(symbol in receiver, 'missing receiver operation: ' + symbol)
    require('_swait' in receiver or '_clock_interval_to_deadline' in receiver,
            'missing receiver delay')
    allowed = {'_PE_boot_args', '_SecureDTLookupEntry', '_SecureDTGetProperty', '_printf',
               '_memcpy', '_memmove', '_bcopy', '_swait', '_clock_interval_to_deadline',
               '_assert_wait_deadline', '_thread_block', '___stack_chk_fail', '_bcmp', '_memcmp',
               '_ceph_smem_map_profile', '_ceph_smem_map_valid', '_ceph_smem_map_checksum',
               '_ceph_smem_map_cpu_supported', '_ceph_smem_map_diagnostic_pass'}
    calls = transfers(receiver, '_soc_smem_map', allowed)
    require(calls, 'no receiver calls')
    # Pure copied-record helpers may be outlined; inspect them as well when
    # present, so an unchecked helper cannot conceal an extra hardware call.
    checked = []
    pending = [name for name in calls if name.startswith('_ceph_smem_map_')]
    while pending:
        name = pending.pop()
        if name in checked:
            continue
        body = function(assembly, name[1:])
        nested = transfers(body, name, allowed)
        pending.extend(name for name in nested if name.startswith('_ceph_smem_map_'))
        checked.append(name)
    return {'native_receiver_and_thread_verified': True, 'receiver_calls': sorted(set(calls)),
            'outlined_copied_helpers_checked': sorted(checked),
            'scope': 'direct receiver/outlined helper disassembly; external DT/printf implementations are not runtime proof'}


def verify(kernel):
    data = kernel.read_bytes()
    require(data[:8] == bytes.fromhex('cffaedfe0c000001') and len(data) > 2000000, 'not substantial ARM64 XNU')
    require(all(marker in data for marker in MARKERS), 'missing SMEM receiver markers')
    tool = objdump_tool()
    assembly = subprocess.check_output([tool, '-d', '--no-show-raw-insn', str(kernel)], text=True, timeout=120)
    return {'status': 'PASS_LINKED_SMEM_RECEIVER', 'kernel_sha256': hashlib.sha256(data).hexdigest(),
            'kernel_bytes': len(data), **inspect(assembly), 'phone_operations': False,
            'smem_bytes_acquired': False, 'smem_access_authorized': False, 'gpu_execution': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kernel', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    require(not args.output or not args.output.exists(), 'preserve prior receipt')
    result = verify(args.kernel)
    if args.output:
        with args.output.open('x') as stream:
            stream.write(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
