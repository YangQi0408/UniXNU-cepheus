#!/usr/bin/env python3
"""Exercise the actual touch_fts FIFO loop with RAM reads, never a phone.

Optional kernel reference verifies the Xiaomi continuation rule independently.
No generated substitute driver: compile the extracted production loop verbatim.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'xnu/cepheus_touch.c').read_text()
body = source.split('static int\ntouch_fts(void)', 1)[1].split(
    'static bool\ntouch_smmu_probe(void)', 1)[0]
assert 'st.skip_next = frame.skip_next' in body
assert body.index('if (frame.skip_next)') < body.index('if (e[0] == 0)')
assert 'st.diag_raw < 128' in body
assert 'st.diag_summaries < 30' in source
assert source.count('touch_wr(st.gpio,') == 0  # no new diagnostic GPIO stores
assert 'PE_parse_boot_argn("sweettouchdiag"' in source
if len(sys.argv) == 2:
    original = (Path(sys.argv[1]) / 'drivers/input/touchscreen/xiaomi/fts_521/fts.c').read_text()
    handler = original.split('static irqreturn_t fts_event_handler(', 1)[1].split(
        'static const char *fts_get_config(', 1)[0]
    assert 'static char pre_id[3]' in handler
    assert 'pre_id[0] == EVT_ID_USER_REPORT' in handler
    assert 'pre_id[1] == 0x02 && pre_id[2] == 0x18' in handler
    software = (Path(sys.argv[1]) / 'drivers/input/touchscreen/xiaomi/fts_521/fts_lib/ftsSoftware.h').read_text()
    assert re.search(r'#define\s+EVT_ID_USER_REPORT\s*\\\s*0x53\b', software)

prefix = r'''
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include "cepheus_fts_events.h"
#define FTS_FIFO_EVENT_SIZE 8u
#define FTS_FIFO_DEPTH 32u
#define FTS_CMD_READ_ALL_EVENTS 0x86u
#define TOUCH_X_MAX 1080u
#define TOUCH_Y_MAX 2340u
static struct {
 uint32_t active; int x[10],y[10]; unsigned skip_next;
 int diag; unsigned diag_reads,diag_empty,diag_events,diag_errors;
 unsigned diag_ready,diag_faults,diag_skipped,diag_outside,diag_raw;
} st;
static uint8_t fifo[256];static unsigned cursor,total,reports;
static int read_error;
static int fts_read_cmd(uint8_t cmd,uint8_t *out,unsigned bytes)
{
 assert(cmd==0x86 && bytes && bytes%8==0);
 if(read_error)return read_error;
 assert(cursor+bytes<=total);memcpy(out,fifo+cursor,bytes);cursor+=bytes;return 0;
}
static void touch_report(uint32_t active,const int *x,const int *y)
{st.active=active;memcpy(st.x,x,sizeof(st.x));memcpy(st.y,y,sizeof(st.y));++reports;}
'''
suffix = r'''
static const uint8_t ext[8]={0x53,2,0x18,0,0,0,0,0};
static const uint8_t enter[8]={0x13,0x01,0x34,0x52,0x34,0,0,0};
static const uint8_t payload[8]={0x13,0x11,0xff,0xff,0xff,0,0,0};
static void batch(const uint8_t *data,unsigned bytes)
{memcpy(fifo,data,bytes);cursor=0;total=bytes;assert(!touch_fts());assert(cursor==total);}
int main(void)
{
 batch(enter,8);assert(st.active==1);
 batch(ext,8);assert(st.skip_next==1 && st.active==1);
 batch(payload,8);assert(!st.skip_next && st.active==1); /* across reads */
 batch(payload,8);assert(st.active==3); /* only first payload suppressed */
 uint8_t together[24];memcpy(together,ext,8);together[7]=2;
 memcpy(together+8,payload,8);memcpy(together+16,enter,8);
 st.active=0;batch(together,24);assert(st.active==1 && !st.skip_next);
 uint8_t empty[8]={0};batch(ext,8);batch(empty,8);assert(!st.skip_next);
 batch(payload,8);assert(st.active==3);
 uint8_t invalid[8]={0x13,0xa1,0,0,0,0,0,0};
 batch(ext,8);batch(invalid,8);assert(st.active==3 && !st.skip_next);
 unsigned before=reports;memcpy(fifo,invalid,8);total=8;cursor=0;
 assert(touch_fts()==EIO && reports==before && st.active==3);
 batch(ext,8);before=reports;read_error=ENXIO;
 assert(touch_fts()==ENXIO && st.skip_next==1 && reports==before);
 read_error=0;batch(payload,8);assert(!st.skip_next);
 /* Diagnostic counters/logging must not change parsed outcomes. */
 st.diag=1;st.active=0;batch(together,24);
 assert(st.active==1 && st.diag_reads==1 && st.diag_events==3 && st.diag_skipped==1);
 assert(st.diag_outside==0 && st.diag_raw==3);
 for(unsigned i=0;i<150;++i)batch(enter,8);
 assert(st.diag_raw==128);
 batch(empty,8);assert(st.diag_empty==1);
 puts("Production touch_fts loop: Xiaomi continuation across/between reads, NOEVENT, errors, bounded diagnostics PASS (RAM only)");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    c = path / 'fifo.c'
    c.write_text(prefix + 'static int\ntouch_fts(void)' + body + suffix)
    exe = path / 'fifo'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-I', str(root / 'xnu'), str(c), '-o', str(exe)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
    print(run.stdout.splitlines()[-1])
