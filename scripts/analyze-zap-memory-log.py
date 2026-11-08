#!/usr/bin/env python3
"""Validate one saved ZAP CPU preflight transcript. Never accesses a device."""
import argparse
import hashlib
import json
from pathlib import Path
import re

N=r'(?:0x[0-9a-fA-F]+|[0-9]+)'
TRANSLATION=rf'CEPHEUS ZAP MEM: translated pa=({N}) bytes=({N}) attr=({N}) first_par=({N}) pages=({N})'
RESTORE=rf'CEPHEUS ZAP MEM: restored pa=({N}) bytes=({N}) attr=0xff'
EXPECTED=[r'CEPHEUS ZAP TEST: scheduled delay_s=90',r'CEPHEUS ZAP TEST: begin version=1 cpu_only=1',
          TRANSLATION,TRANSLATION,
          r'CEPHEUS ZAP TEST: pattern=PASS mdt_bytes=16384 image_bytes=1048576 logical_bytes=4096',
          RESTORE,r'CEPHEUS ZAP TEST: end result=0 retained_pages=0 cpu_only=1']

def analyze(text):
    lines=[];errors=[]
    for line in text.splitlines():
        if 'CEPHEUS ZAP ' in line:lines.append(line[line.index('CEPHEUS ZAP '):].strip())
        if any(marker in line for marker in ('CEPHEUS SCM INFO: begin','CEPHEUS OSM:',
                                            'CEPHEUS GPU GX:','CEPHEUS ARC:')):
            errors.append('conflicting probe output')
    if len(lines)!=len(EXPECTED):errors.append('missing, repeated or unexpected ZAP records')
    matches=[re.fullmatch(pattern,line) for pattern,line in zip(EXPECTED,lines)]
    if len(matches)!=len(EXPECTED) or not all(matches):errors.append('phase order, result or record format mismatch')
    translations=[]
    if not errors:
        for index,(size,attr) in enumerate(((16384,0),(1048576,0x44)),2):
            pa,capacity,actual_attr,par,pages=(int(x,0) for x in matches[index].groups())
            translations.append(dict(pa=pa,bytes=capacity,attr=actual_attr,first_par=par,pages=pages))
            if (capacity!=size or pages!=size//4096 or actual_attr!=attr or
                    not pa or pa+size>(1<<40) or pa&(size-1) or par&1 or
                    par>>56!=attr or par&0x0000fffffffff000!=pa):
                errors.append('translation geometry, address or attributes mismatch')
        mdt,image=translations
        if mdt['pa']<image['pa']+image['bytes'] and image['pa']<mdt['pa']+mdt['bytes']:
            errors.append('metadata/image physical ranges overlap')
        restored_pa,restored_bytes=(int(x,0) for x in matches[5].groups())
        if (restored_pa,restored_bytes)!=(mdt['pa'],mdt['bytes']):errors.append('restoration is not for this metadata allocation')
    return dict(status='PASS' if not errors else 'NOT_PASS',errors=errors,records=len(lines),translations=translations,
                scope='CPU memory transcript only; not secure reachability, PAS authentication or GPU execution')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();data=args.log.read_bytes()
    result=analyze(data.decode(errors='replace'))
    result.update(log=str(args.log),log_sha256=hashlib.sha256(data).hexdigest())
    with args.output.open('x') as file:json.dump(result,file,indent=2);file.write('\n')
    print(json.dumps(result,indent=2))
    return 0 if result['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
