#!/usr/bin/env python3
"""Compile the exact production System Info snapshot, only RAM transport.

With an explicit reference, independently walk readSysInfo's prefix layout.
No device calls or firmware load commands; plain reads may still fail on HW.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'xnu/cepheus_touch.c').read_text()
body = source.split('static int\ntouch_sysinfo_snapshot(void)', 1)[1].split(
    'static void\ntouch_report(', 1)[0]
assert body.count('cepheus_bb_xfer(') == 1
assert not any(x in body for x in ('touch_wr(', 'touch_msg(', 'touch_wait(', 'fts_read_cmd('))
assert 'const uint8_t cmd[] = {0xa6, 0x00, 0x00}' in body
if len(sys.argv) == 2:
    lib = Path(sys.argv[1]) / 'drivers/input/touchscreen/xiaomi/fts_521/fts_lib'
    original = (lib / 'ftsCore.c').read_text().split('int readSysInfo(int request)', 1)[1]
    assert 'FTS_CMD_FRAMEBUFFER_R, BITS_16,' in original
    assert 'ADDR_FRAMEBUFFER, data, SYS_INFO_SIZE,' in original
    assert 'if (request == 1)' in original  # not taken by this read-only path
    assert re.search(r'#define\s+SYS_INFO_SIZE\s+208\b', (lib / 'ftsCore.h').read_text())
    core_header = (lib / 'ftsCore.h').read_text()
    assert re.search(r'#define\s+DIE_INFO_SIZE\s+16\b', core_header)
    assert re.search(r'#define\s+EXTERNAL_RELEASE_INFO_SIZE\s+8\b', core_header)
    assert '(EXTERNAL_RELEASE_INFO_SIZE + \\\n\t 8)' in core_header
    # Independently recover offsets by walking actual index operations, not
    # copying a fixture's offsets. Loops consume their source-defined counts.
    prefix = original.split('index += 4;', 1)[1].split('index += 40;', 1)[0]
    index = 4
    offsets = {}
    for line in prefix.splitlines():
        if 'i < DIE_INFO_SIZE' in line or 'i < RELEASE_INFO_SIZE' in line:
            index += 16
        match = re.search(r'u8ToU(?:16|32)\(&data\[index\], &systemInfo\.(\w+)\)', line)
        if match:
            offsets[match[1]] = index
        match = re.search(r'systemInfo\.(\w+) = data\[index\+\+\]', line)
        if match:
            offsets[match[1]] = index
            index += 1
        match = re.search(r'index \+= (\d+);', line)
        if match:
            index += int(match[1])
    expected = {'u16_chip0Id': 10, 'u16_fwVer': 16, 'u16_svnRev': 18,
                'u16_cfgVer': 20, 'u16_cfgProgectId': 22, 'u16_cxVer': 24,
                'u16_cxProjectId': 26, 'u8_cfgAfeVer': 28, 'u8_cxAfeVer': 29,
                'u8_panelCfgAfeVer': 30, 'u8_protocol': 31, 'u32_fwCrc': 64,
                'u32_cfgCrc': 68, 'u16_scrResX': 80, 'u16_scrResY': 82,
                'u8_scrTxLen': 84, 'u8_scrRxLen': 85}
    assert all(offsets.get(k) == v for k, v in expected.items()), offsets

prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "cepheus_fts_sysinfo.h"
#define TOUCH_I2C_ADDR 0x49
static int touch_bb,transport_error,bad_header;static unsigned calls;
static int cepheus_bb_xfer(int *bus,unsigned address,const uint8_t *cmd,unsigned nt,uint8_t *data,unsigned nr)
{
 assert(bus==&touch_bb && address==0x49 && nt==3 && nr==208);
 assert(cmd[0]==0xa6 && !cmd[1] && !cmd[2]);++calls;
 if(transport_error)return transport_error;
 memset(data,0,nr);data[0]=bad_header?0:0xa5;data[1]=1;data[10]=0x36;data[11]=0x39;
 data[80]=0x38;data[81]=4;data[82]=0x24;data[83]=9;
 return 0;
}
'''
suffix = r'''
int main(void)
{
 assert(!touch_sysinfo_snapshot() && calls==1);
 transport_error=ENXIO;assert(touch_sysinfo_snapshot()==ENXIO && calls==2);
 transport_error=ETIMEDOUT;assert(touch_sysinfo_snapshot()==ETIMEDOUT && calls==3);
 transport_error=0;bad_header=1;assert(!touch_sysinfo_snapshot() && calls==4); /* unavailable evidence, no reload */
 puts("Production FTS System Info snapshot: one exact A6/0000 read, bounded/no retry/error propagation PASS (RAM only)");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    c = path / 'sysinfo.c'
    c.write_text(prefix + 'static int\ntouch_sysinfo_snapshot(void)' + body + suffix)
    exe = path / 'sysinfo'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-I', str(root / 'xnu'), str(c), '-o', str(exe)], check=True)
    print(subprocess.check_output([str(exe)], text=True).splitlines()[-1])
