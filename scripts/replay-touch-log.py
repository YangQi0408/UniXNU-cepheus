#!/usr/bin/env python3
"""Replay saved RAW FIFO prefixes through production functions in host RAM.

The comparison uses the same parser with an event-ordered reporting model.
Neither mode accesses a phone or establishes the cause of an empty FIFO.
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
RAW = re.compile(
    r'^SWEET TOUCH RAW: n=(\d+) slot=(\d+) remaining=(\d+) skip=([01]) '
    r'bytes=([0-9a-f]{2}(?::[0-9a-f]{2}){7})$', re.M)


def batches(text):
    groups = []
    records = []
    raw_lines = [line for line in text.splitlines() if line.startswith('SWEET TOUCH RAW:')]
    matches = RAW.findall(text)
    if not matches or len(matches) != len(raw_lines):
        raise ValueError('Missing or malformed RAW records')
    for n, slot, remaining, skip, data in matches:
        n, slot, remaining, skip = map(int, (n, slot, remaining, skip))
        if n != len(records) + 1 or n > 128 or remaining > 31:
            raise ValueError('RAW prefix must start at n=1 and be contiguous, at most 128 events')
        record = {'n': n, 'slot': slot, 'remaining': remaining, 'skip': skip,
                  'bytes': list(bytes.fromhex(data.replace(':', ' ')))}
        if slot == 0:
            if groups and len(groups[-1]) != groups[-1][0]['remaining'] + 1:
                raise ValueError('Incomplete batch before the end of the RAW prefix')
            if record['bytes'][7] & 31 != remaining:
                raise ValueError('First event queue depth disagrees with logged remaining')
            groups.append([])
        if not groups or slot != len(groups[-1]):
            raise ValueError('Invalid RAW batch slot or remaining')
        if groups[-1] and remaining != groups[-1][0]['remaining']:
            raise ValueError('Remaining count changed within a logged batch')
        if slot > remaining:
            raise ValueError('RAW slot exceeds the batch size')
        groups[-1].append(record)
        records.append(record)
    omitted = []
    if groups and len(groups[-1]) != groups[-1][0]['remaining'] + 1:
        if len(records) != 128:
            raise ValueError('Incomplete final batch without the RAW limit marker n=128')
        omitted = groups.pop()
    return groups, omitted


def compile_replayer(directory):
    source = (ROOT / 'xnu/cepheus_touch.c').read_text()
    report = source.split('static void\ntouch_report(', 1)[1].split(
        'static int\ntouch_fts(void)', 1)[0]
    fifo = source.split('static int\ntouch_fts(void)', 1)[1].split(
        'static bool\ntouch_smmu_probe(void)', 1)[0]
    prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "cepheus_fts_events.h"
#define TOUCH_MAX 10u
#define TOUCH_X_MAX 1080u
#define TOUCH_Y_MAX 2340u
#define FTS_FIFO_EVENT_SIZE 8u
#define FTS_FIFO_DEPTH 32u
#define FTS_CMD_READ_ALL_EVENTS 0x86u
static struct {
 uint32_t active; int x[10], y[10]; unsigned skip_next;
 int diag; unsigned diag_reads,diag_empty,diag_events,diag_errors;
 unsigned diag_ready,diag_faults,diag_skipped,diag_outside,diag_raw,diag_sent;
} st;
static uint8_t input[256],logged_skip[32]; static unsigned cursor,total,sink_active;
static unsigned down[10],move[10],up[10];
static struct {int id,action,x,y;} events[320];static unsigned event_count;
static void cepheus_cpu_activity(void) {}
static void sweet_hid_touch(int id,int pressed,int x,int y)
{
 assert(id>=0 && id<10 && x>=0 && x<1080 && y>=0 && y<2400);
 unsigned bit=1u<<id;
 int action=pressed?((sink_active&bit)?2:1):0;
 assert(event_count<320);events[event_count].id=id;events[event_count].action=action;
 events[event_count].x=x;events[event_count].y=y;++event_count;
 if(pressed){if(sink_active&bit)++move[id];else ++down[id];sink_active|=bit;}
 else {assert(sink_active&bit);++up[id];sink_active&=~bit;}
}
static int fts_read_cmd(uint8_t cmd,uint8_t *out,unsigned size)
{
 assert(cmd==0x86 && size && size%8==0 && cursor+size<=total);
 memcpy(out,input+cursor,size);cursor+=size;return 0;
}
static int discard_log(const char *format,...) {(void)format;return 0;}
#define printf discard_log
'''
    suffix = r'''
/* Comparison model only: same parser/report, publish after each event. */
static void ordered_model(void)
{
 struct cepheus_fts_state frame={.active=st.active,.skip_next=st.skip_next};
 memcpy(frame.x,st.x,sizeof(frame.x));memcpy(frame.y,st.y,sizeof(frame.y));
 for(unsigned i=0;i<total;i+=8){
  const uint8_t *e=input+i;
  assert(frame.skip_next==logged_skip[i/8]);
  if(!frame.skip_next && !e[0])break;
  assert(!cepheus_fts_event(&frame,e));st.skip_next=frame.skip_next;
  touch_report(frame.active,frame.x,frame.y);
 }
}
static void counts(const unsigned *values)
{fputc('[',stdout);for(unsigned i=0;i<10;++i)fprintf(stdout,"%s%u",i?",":"",values[i]);fputc(']',stdout);}
int main(int argc,char **argv)
{
 assert(argc==2 && (strcmp(argv[1],"ordered")==0 || strcmp(argv[1],"production")==0));
 int ordered=strcmp(argv[1],"ordered")==0;
 unsigned size,batch=0;
 while(scanf("%u",&size)==1){
  assert(size && size<=256 && size%8==0);total=size;cursor=0;
  for(unsigned i=0;i<size;++i){unsigned byte;assert(scanf("%u",&byte)==1 && byte<256);input[i]=(uint8_t)byte;}
  for(unsigned i=0;i<size/8;++i){unsigned skip;assert(scanf("%u",&skip)==1 && skip<2);logged_skip[i]=(uint8_t)skip;}
  memset(down,0,sizeof(down));memset(move,0,sizeof(move));memset(up,0,sizeof(up));
  event_count=0;
  if(ordered)ordered_model();else {assert(!touch_fts());assert(cursor==total);}
  fprintf(stdout,"{\"batch\":%u,\"active\":%u,\"skip_next\":%u,\"down\":",++batch,st.active,st.skip_next);
  counts(down);fputs(",\"move\":",stdout);counts(move);fputs(",\"up\":",stdout);counts(up);
  fputs(",\"events\":[",stdout);
  for(unsigned i=0;i<event_count;++i)fprintf(stdout,"%s[%d,%d,%d,%d]",i?",":"",events[i].id,events[i].action,events[i].x,events[i].y);
  fputs("]}\n",stdout);
 }
 return 0;
}
'''
    c = directory / 'replay.c'
    c.write_text(prefix + 'static void\ntouch_report(' + report +
                 'static int\ntouch_fts(void)' + fifo + suffix)
    executable = directory / 'replay'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-I', str(ROOT / 'xnu'),
                    str(c), '-o', str(executable)], check=True, capture_output=True)
    return executable


def replay(text, executable):
    groups, omitted = batches(text)
    payload = ''.join(str(len(group) * 8) + ' ' + ' '.join(
        str(byte) for event in group for byte in event['bytes']) + ' ' +
        ' '.join(str(event['skip']) for event in group) + '\n' for group in groups)
    modes = {}
    for mode in ('production', 'ordered'):
        run = subprocess.run([str(executable), mode], input=payload,
                             text=True, capture_output=True, check=True)
        modes[mode] = [json.loads(line) for line in run.stdout.splitlines()]
        if len(modes[mode]) != len(groups):
            raise ValueError('Replay output batch count mismatch')
    losses = []
    for group, actual, model in zip(groups, modes['production'], modes['ordered']):
        if (actual['active'], actual['skip_next']) != (model['active'], model['skip_next']):
            raise ValueError('Production and ordered model final parser state diverged')
        lost = {key: sum(model[key]) - sum(actual[key]) for key in ('down', 'move', 'up')}
        if actual['events'] != model['events']:
            losses.append({'first_n': group[0]['n'], 'last_n': group[-1]['n'],
                           'difference_ordered_minus_production': lost,
                           'production_events': actual['events'],
                           'ordered_model_events': model['events']})
    health = [dict((key, int(value, 0)) for key, value in
                   re.findall(r'(\w+)=(0x[0-9a-f]+|\d+)', line))
              for line in text.splitlines() if line.startswith('SWEET TOUCH HEALTH:')]
    return {
        'raw_events_observed': sum(map(len, groups)) + len(omitted),
        'raw_events_replayed': sum(map(len, groups)),
        'complete_batches_replayed': len(groups),
        'final_partial_batch_omitted': [event['n'] for event in omitted],
        'raw_prefix_limit_reached': sum(map(len, groups)) + len(omitted) == 128,
        'hid_sink_counts': {mode: {key: sum(sum(batch[key]) for batch in output)
                                  for key in ('down', 'move', 'up')}
                            for mode, output in modes.items()},
        'hid_event_layout': '[finger, action (0=up, 1=down, 2=move), x, y]',
        'hid_events': {mode: [event for batch in output for event in batch['events']]
                       for mode, output in modes.items()},
        'batch_reporting_differences': losses,
        'health_first': health[0] if health else None,
        'health_last': health[-1] if health else None,
        'initialization_records': [line for line in text.splitlines() if
            re.match(r'SWEET TOUCH (SYSINFO:|\(cepheus\): (probe |active scan |TLMM |powered RESET ))', line)],
        'raw_status_bytes': [':'.join(f'{b:02x}' for b in event['bytes'])
                             for group in groups for event in group if event['bytes'][0] == 0x43],
        'raw_enter_coordinates': [
            {'n': event['n'], 'finger': event['bytes'][1] >> 4,
             'x': ((event['bytes'][3] & 15) << 8) | event['bytes'][2],
             'y': (event['bytes'][4] << 4) | (event['bytes'][3] >> 4)}
            for group in groups for event in group if event['bytes'][0] == 0x13],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, help='New JSON file; refuse to overwrite')
    args = parser.parse_args()
    result = {'scope': 'Host RAM replay of saved RAW prefixes, no phone operations',
              'source_sha256': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                                for name in ('xnu/cepheus_touch.c', 'xnu/cepheus_fts_events.h')},
              'logs': [], 'limitations': [
                  'HID sink counts are host replay, not new hardware or user acceptance',
                  'Ordered reporting is a comparison model, not a changed production driver',
                  'Only complete batches within the bounded RAW prefix are replayed',
                  'No user touch timestamps; line order does not establish scan state',
                  'Lost batch transitions cannot explain absence of new RAW FIFO events']}
    with tempfile.TemporaryDirectory() as temporary:
        executable = compile_replayer(Path(temporary))
        for path in args.log:
            data = path.read_bytes()
            if len(data) > 32 * 1024 * 1024:
                parser.error(f'Log exceeds 32MiB bound: {path}')
            item = replay(data.decode('utf-8', errors='replace'), executable)
            item.update(log=str(path.resolve()), log_bytes=len(data),
                        log_sha256=hashlib.sha256(data).hexdigest())
            result['logs'].append(item)
    serialized = json.dumps(result, indent=2) + '\n'
    if args.output:
        with args.output.open('x') as output:
            output.write(serialized)
        print(f'Saved {len(result["logs"])} host RAM replays to {args.output}')
    else:
        print(serialized, end='')


if __name__ == '__main__':
    main()
