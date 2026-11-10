#!/usr/bin/env python3
"""Reconstruct and independently validate the exit-tail diagnostic log, not ownership."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

BYTES=1128
PREFIX='CEPHEUS RPMH TAIL: '
HEX=r'(0x[0-9a-f]+|0)'
BEGIN=re.compile(PREFIX+r'begin flags='+HEX+r' bind='+HEX+r' copy='+HEX+r' ebs=(\d+) checksum='+HEX)
RAW=re.compile(PREFIX+r'raw=(\d+) words=([0-9a-f]{16}(?::[0-9a-f]{16}){7})')
CORE=re.compile(PREFIX+r'core error=(-?\d+) boundaries='+HEX+r' reads=(\d+) identity='+HEX+
                r' resources=(\d+) serial='+HEX+r' ticks='+HEX+'/'+HEX)
SCHEDULED=PREFIX+'scheduled delay_s=90'
END=PREFIX+'end; diagnostic only, no ACK/ownership or GPU execution'


def require(condition,reason):
    if not condition:raise ValueError(reason)


def checksum(data,offset):
    value=14695981039346656037
    for i,byte in enumerate(data):
        value=((value^(0 if offset<=i<offset+8 else byte))*1099511628211)&((1<<64)-1)
    return value


def decode(data):
    require(len(data)==BYTES,'Envelope length')
    magic,version,size,flags,attempts,bind,copy,check=struct.unpack_from('<QIIIIQQQ',data)
    require((magic,version,size)==(0x31464f4854504852,1,BYTES),'Envelope identity')
    require(not flags&~15 and flags&9==9 and 1<=attempts<=3,'Envelope flags/attempts')
    require(check==checksum(data,40),'Envelope checksum')
    require((bind==0)==bool(flags&2),'Bind status/flag contradiction')
    result=dict(flags=flags,bind_status=bind,copy_status=copy,ebs_attempts=attempts,checksum=check)
    tail=data[48:]
    if not flags&4:
        require(copy!=0 and tail==bytes(1080),'Failed copy must preserve error and zero record')
        return result
    require(flags&2 and copy==0,'Copy status/flag contradiction')
    magic,version,size,complete,error,boundaries,reads,serial,begin,end,check=struct.unpack_from('<QIIIiIIQQQQ',tail)
    require((magic,version,size,complete)==(0x314c494154485052,1,1080,1),'Core record identity')
    require(-7<=error<=0 and not boundaries&~15 and reads<=65536,'Core bounds')
    require(check==checksum(tail,56),'Core checksum')
    snap=tail[64:]
    (magic,version,size,complete,s_error,identity,count,s_attempts,done,phase,reserved,
     el,mpidr,s_begin,s_end,s_check)=struct.unpack_from('<QIIIiIIIIIIQQQQQ',snap)
    require((magic,version,size,complete)==(0x3150414e53485052,1,1016,1),'Requests identity')
    require(s_error==error and not identity&~7 and count<=512,'Requests bounds')
    require(s_attempts==attempts and done==1 and phase==2 and reserved==0 and el==4,'Requests context')
    require((s_begin,s_end)==(begin,end) and s_check==checksum(snap,80),'Requests timing/checksum')
    resources=[]
    if error:
        require(snap[88:]==bytes(928),'Failed collection must zero resource payload')
    else:
        require(boundaries==15 and serial!=0 and reads!=0 and end>=begin and identity==7 and count!=0,
                'Successful collection lacks required local evidence')
        for r in range(2):
            offset=88+r*464
            address,n,pdc,reserved=struct.unpack_from('<IIII',snap,offset)
            require(address>>16==3 and address&3==0 and 1<=n<=8 and pdc<=1 and reserved==0,'Resource header')
            drivers=[]
            for i in range(8):
                raw=snap[offset+16+i*56:offset+16+(i+1)*56]
                if i>=n:
                    require(raw==bytes(56),'Unused driver must be zero');continue
                ident,state,priority,shadow,valid,*votes=struct.unpack('<14I',raw)
                require(state<=1 and priority<=1 and valid<=1,'Driver bounds')
                require(all(ident!=d['id'] for d in drivers),'Duplicate driver')
                local=[]
                for k in range(3):
                    datum,v,dirty=votes[k*3:k*3+3]
                    require(v<=1 and dirty<=4,'Vote bounds')
                    local.append(dict(data=datum,valid=v,dirty=dirty))
                drivers.append(dict(id=ident,state=state,priority=priority,shadow_data=shadow,
                                    shadow_valid=valid,local=local))
            resources.append(dict(address=address,count=n,pdc_present=pdc,drivers=drivers))
        require(resources[0]['address']!=resources[1]['address'],'Duplicate resource')
    result['core']=dict(error=error,boundaries=boundaries,reads=reads,allocation_serial=serial,
                        begin_ticks=begin,end_ticks=end,identity_mask=identity,resource_count=count,
                        mpidr=mpidr,resources=resources)
    return result


def analyze(text):
    lines=[line for line in text.splitlines() if line.startswith(PREFIX)]
    result=dict(status='INCOMPLETE',gpu_execution=False,admission_changed=False,
                log_origin_authenticated=False,logger_delay_verified=False,
                scope='Software requests at a local exit boundary; not hardware ACK or exclusive ownership')
    def reject(reason,status='REJECTED'):return {**result,'status':status,'reason':reason}
    if not lines:return reject('No tail records','INCOMPLETE')
    if len(lines)>22 or lines.count(SCHEDULED)>1:return reject('Oversized or duplicate diagnostic')
    result['scheduled_record_seen']=lines[0]==SCHEDULED
    if result['scheduled_record_seen']:lines.pop(0)
    if not lines or not BEGIN.fullmatch(lines[0]):return reject('Missing or refused envelope','INCOMPLETE')
    header=[int(v,0) for v in BEGIN.fullmatch(lines[0]).groups()]
    if lines[-1]!=END:return reject('Missing end marker','INCOMPLETE')
    chunks=[];index=1
    while index<len(lines)-1 and RAW.fullmatch(lines[index]):
        match=RAW.fullmatch(lines[index])
        if int(match[1])!=64*len(chunks):return reject('Duplicate, missing or reordered raw chunk')
        chunks.append(struct.pack('<8Q',*(int(v,16) for v in match[2].split(':'))));index+=1
    if len(chunks)!=18:return reject('Incomplete raw envelope','INCOMPLETE')
    raw=b''.join(chunks)
    if raw[BYTES:]!=bytes(24):return reject('Nonzero final chunk padding')
    raw=raw[:BYTES]
    try:decoded=decode(raw)
    except (ValueError,struct.error) as e:return reject(str(e))
    if header!=[decoded[n] for n in ('flags','bind_status','copy_status','ebs_attempts','checksum')]:
        return reject('Envelope summary contradicts raw bytes')
    if 'core' in decoded:
        summary=CORE.fullmatch(lines[index]) if index<len(lines)-1 else None
        if not summary:return reject('Missing core summary','INCOMPLETE')
        values=[int(v,0) for v in summary.groups()]
        if values!=[decoded['core'][n] for n in ('error','boundaries','reads','identity_mask','resource_count',
                                                'allocation_serial','begin_ticks','end_ticks')]:
            return reject('Core summary contradicts raw bytes')
        index+=1
    if index!=len(lines)-1:return reject('Unexpected or duplicated tail record')
    return {**result,**decoded,'status':'TAIL_COMPLETE','reason':'Complete envelope and nested checksums verified',
            'raw_sha256':hashlib.sha256(raw).hexdigest(),'checksum_verified':True}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('log',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error('output exists')
    if a.log.stat().st_size>32*1024*1024:p.error('log exceeds 32MiB')
    raw=a.log.read_bytes();result=analyze(raw.decode(errors='replace'))
    result['log_sha256']=hashlib.sha256(raw).hexdigest()
    result['analyzer_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(result['status']+': '+result['reason'])
    return {'TAIL_COMPLETE':0,'REJECTED':1,'INCOMPLETE':2}[result['status']]


if __name__=='__main__':raise SystemExit(main())
