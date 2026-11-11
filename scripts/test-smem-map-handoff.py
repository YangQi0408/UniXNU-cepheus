#!/usr/bin/env python3
"""Actual firmware adapter -> copied ADT -> XNU log -> independent host decode."""
import argparse
import ctypes
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


STAGE = module('smem_handoff_stage', 'stage-smem-handoff-firmware.py')
LOG = module('smem_handoff_log', 'analyze-smem-handoff-log.py')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--uefi', required=True, type=Path)
    parser.add_argument('--repository', required=True, type=Path)
    args = parser.parse_args()
    uefi = args.uefi.resolve()
    include = uefi / 'MU_BASECORE/MdePkg/Include'
    with tempfile.TemporaryDirectory(prefix='smem-handoff-test-') as directory:
        work = Path(directory)
        STAGE.stage(args.repository.resolve(), uefi, work / 'source')
        driver = work / 'source/DarwinBootPkg/Driver'
        boot = (driver / 'DarwinBoot.c').read_text()
        # Keep the actual loop source intact; only its surrounding boot inputs
        # and terminal firmware transfer are supplied by the host fixture.
        ebs_loop = '  for (Attempt = 0; Attempt < 3; Attempt++) {\n    MapSize' + boot.split(
            '  for (Attempt = 0; Attempt < 3; Attempt++) {\n    MapSize', 1)[1].split('\nFail:', 1)[0]
        (work / 'smem-ebs-loop.c').write_text('''
STATIC VOID ActualEbsLoop (VOID) {
 UINTN Attempt, MapSize, Capacity=64, Key=0, Stride=0, SmemOffset=128, TreeSize=sizeof(Tree);
 UINT32 Version=0; EFI_MEMORY_DESCRIPTOR *Map=NULL; EFI_STATUS Status;
 EFI_HANDLE mImage=(VOID *)1; int SmemProfile=1;
 UINT64 TreeAddress=(UINTN)Tree, ArgsAddress=0, Address=0, WindowSize=0, Video[6]={0};
 struct { UINT64 Base, Entry; } Kernel={0};
''' + ebs_loop.replace('  return EFI_ABORTED;', '  return;') + '\nFail:\n CpuDeadLoop ();\n}\n')
        text = (driver / 'CepheusSmemMapProbe.c').read_text()
        start = text.index('STATIC CE_SMEM_CPU_MAP\nCeSmemCpuMap (VOID)')
        end = text.index('STATIC BOOLEAN\nCeSmemCpuValid', start)
        (work / 'map-probe-host.c').write_text(text[:start] +
            'extern CE_SMEM_CPU_MAP CeSmemCpuMap (VOID);\nextern UINT64 CeSmemPar (UINT64 Address);\n' + text[end:])
        (work / 'map-probe-host-tests.c').write_text((ROOT / 'firmware/test_smem_map_probe.c').read_text().replace('int main (void)', 'int probe_fixture_main (void)'))
        binary = work / 'adapter'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fshort-wchar', '-fno-builtin',
            '-O1', '-fsanitize=address,undefined', '-I', str(driver), '-I', str(include),
            '-I', str(include / 'X64'), '-I', str(driver.parents[1] / 'SmemMapProbePkg/Include'),
            '-I', str(work), str(ROOT / 'firmware/test_smem_map_handoff.c'), '-o', str(binary)], check=True)
        raw = subprocess.check_output([str(binary)])
        require(len(raw) == LOG.BYTES and LOG.decode(raw)['diagnostic_pass'], 'producer decode')
        source = (ROOT / 'xnu/cepheus_soc.c').read_text()
        helper = 'static bool soc_smem_map(void)' + source.split('static bool soc_smem_map(void)', 1)[1].split('static bool soc_scm_pair', 1)[0]
        require(source.index('if (soc_smem_map())') < source.index('if (soc_rpmh_tail())'), 'isolated path priority')
        prefix = r'''
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cepheus_smem_map_handoff.h"
#define CHECK(c) do {if (!(c)) {fprintf(stderr,"failed %u\n",__LINE__);exit(1);}} while(0)
typedef void *DTEntry;
#define kSuccess 0
static char *line;
static unsigned waits,lookups,bytes,missing;
static unsigned char raw[CE_SMEM_MAP_BYTES+1];
static char *PE_boot_args(void) {return line;}
static void swait(unsigned ms) {CHECK(ms==90000);++waits;}
static int SecureDTLookupEntry(DTEntry parent,const char *path,DTEntry *entry)
{CHECK(!parent && !strcmp(path,"/chosen"));++lookups;*entry=(void *)1;return missing?1:kSuccess;}
static int SecureDTGetProperty(DTEntry entry,const char *name,const void **out,unsigned *size)
{CHECK(entry==(void *)1 && !strcmp(name,CE_SMEM_MAP_PROPERTY));*out=raw+1;*size=bytes;return kSuccess;}
'''
        suffix = r'''
int main(int argc,char **argv) {
 CHECK(argc==2);FILE *f=fopen(argv[1],"rb");CHECK(f);
 CHECK(fread(raw+1,1,CE_SMEM_MAP_BYTES,f)==CE_SMEM_MAP_BYTES);CHECK(!fclose(f));bytes=CE_SMEM_MAP_BYTES;
 line="sweetusb=2";CHECK(!soc_smem_map() && !waits && !lookups);
 const char *bad[]={"cephsmemmap=0","cephsmemmap=01","cephsmemmap=1x","cephsmemmap-extra=1",
 "cephsmemmap=1 cephsoc=0","cephscmpair=1 cephsmemmap=1","cephsmemmap=1 cephsmemmap=1"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
  line=(char *)bad[i];CHECK(soc_smem_map() && !waits && !lookups);
 }
 line="sweetusb=2 cephsmemmap=1";missing=1;CHECK(soc_smem_map());missing=0;
 bytes--;CHECK(soc_smem_map());bytes++;
 raw[1]^=1;CHECK(soc_smem_map());raw[1]^=1;
 CHECK(soc_smem_map());CHECK(waits==4);
 return 0;
}
'''
        src = work / 'hook.c'
        src.write_text(prefix + helper + suffix)
        hook = work / 'hook'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
            '-I', str(ROOT / 'xnu'), str(src), '-o', str(hook)], check=True)
        record = work / 'record.bin'
        record.write_bytes(raw)
        output = subprocess.check_output([str(hook), str(record)])
        # Earlier expected refusal tests are excluded from the single transcript.
        output = output[output.rindex(b'CEPHEUS SMEM MAP: scheduled delay_s=90\n'):]
        decoded = LOG.analyze(output)
        require(decoded['raw_sha256'] == LOG.decode(raw)['raw_sha256'] and decoded['diagnostic_pass'], 'XNU round trip')
        lines = output.splitlines(keepends=True)
        for index in range(1, len(lines)):
            try:
                LOG.analyze(b''.join(lines[:index]))
            except ValueError:
                continue
            raise ValueError('accepted truncation ' + str(index))
        for bad in (output + output, output[:-1], output.replace(b'raw=64 ', b'raw=0 '),
                    output.replace(b'end diagnostic=1;', b'end diagnostic=0;'), output.replace(b'CEPHEUS SMEM MAP: begin', b'foreign CEPHEUS SMEM MAP: begin')):
            try:
                LOG.analyze(bad)
            except ValueError:
                continue
            raise ValueError('accepted malformed transcript')
        # Compare independent host and actual shared-C semantics with recalculated
        # checksums, so checksummed corruption cannot hide behind the checksum.
        wrapper = work / 'valid.c'
        wrapper.write_text('#include <string.h>\n#include "cepheus_smem_map_handoff.h"\n'
            'int valid(const void *p) {struct ceph_smem_map_handoff h;memcpy(&h,p,sizeof(h));return ceph_smem_map_valid(&h);}\n'
            'int diagnostic_pass(const void *p) {struct ceph_smem_map_handoff h;memcpy(&h,p,sizeof(h));return ceph_smem_map_diagnostic_pass(&h);}\n')
        library = work / 'valid.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=undefined', '-I', str(ROOT / 'xnu'), str(wrapper), '-o', str(library)], check=True)
        lib = ctypes.CDLL(str(library))
        lib.valid.argtypes = lib.diagnostic_pass.argtypes = [ctypes.c_char_p]
        for offset in range(0, LOG.BYTES, 4):
            damaged = bytearray(raw)
            damaged[offset] ^= 0xff
            struct.pack_into('<Q', damaged, 88, LOG.checksum(damaged))
            c_valid, c_pass = lib.valid(bytes(damaged)) == 0, bool(lib.diagnostic_pass(bytes(damaged)))
            try:
                p_pass = LOG.decode(bytes(damaged))['diagnostic_pass']
            except ValueError:
                require(not c_valid or not c_pass, 'host rejects C successful mutation')
                continue
            require(c_valid and c_pass == p_pass, 'C/host mismatch at ' + str(offset))
        failed = bytearray(raw)
        struct.pack_into('<Q', failed, 64, (1 << 63) | 17)
        struct.pack_into('<Q', failed, 80, (1 << 63) | 17)
        struct.pack_into('<I', failed, 124, 5)
        struct.pack_into('<Q', failed, 480 + 4 * 8, struct.unpack_from('<Q', failed, 480 + 4 * 8)[0] | 1)
        failed[480 + 5 * 8:] = bytes(LOG.BYTES - 480 - 5 * 8)
        struct.pack_into('<Q', failed, 88, LOG.checksum(failed))
        record.write_bytes(failed)
        failed_log = subprocess.check_output([str(hook), str(record)])
        failed_log = failed_log[failed_log.rindex(b'CEPHEUS SMEM MAP: scheduled delay_s=90\n'):]
        failed_result = LOG.analyze(failed_log)
        require(not failed_result['diagnostic_pass'] and failed_result['diagnostic']['end']['pages'] == 5,
                'partial evidence lost/reclassified as pass')
        cli_log = work / 'log.txt'
        cli_log.write_bytes(output)
        result = work / 'log.json'
        subprocess.run(['python3', str(ROOT / 'scripts/analyze-smem-handoff-log.py'), str(cli_log), '--output', str(result)], check=True)
        require(subprocess.run(['python3', str(ROOT / 'scripts/analyze-smem-handoff-log.py'), str(cli_log), '--output', str(result)], capture_output=True).returncode != 0, 'CLI overwrites evidence')
        cli_log.write_bytes(output[:-1])
        rejected = work / 'bad.json'
        require(subprocess.run(['python3', str(ROOT / 'scripts/analyze-smem-handoff-log.py'), str(cli_log), '--output', str(rejected)],
                               capture_output=True).returncode != 0 and not rejected.exists(), 'CLI publishes malformed evidence')
        print('SMEM handoff producer/XNU/host round-trip, actual EBS loop 1..3 retries/error stops, 512 partial faults, bounds, 1144 mutations and transcript rejection PASS; RAM only')


if __name__ == '__main__':
    main()
