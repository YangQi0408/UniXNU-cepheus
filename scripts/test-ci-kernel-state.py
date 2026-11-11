#!/usr/bin/env python3
"""CI state fixtures only; required build failures or skips cannot be admitted."""
import importlib.util
from pathlib import Path

spec=importlib.util.spec_from_file_location('ci_state',Path(__file__).with_name('verify-ci-scm-pair.py'))
ci=importlib.util.module_from_spec(spec);spec.loader.exec_module(ci)


def fixture():
    names={9:'Build XNU',10:'Fail if required patches or cepheus source were skipped',
           11:'Run actions/upload-artifact@v4',12:'Retain unaccepted kernel and disassembly after verification failure',
           13:'Run actions/upload-artifact@v4'}
    return {'headSha':'fixture','status':'completed','conclusion':'success',
            'jobs':[{'name':'kernel','status':'completed','conclusion':'success','steps':[
                {'number':n,'name':name,'status':'completed','conclusion':'skipped' if n in (12,13) else 'success'}
                for n,name in names.items()]}]}


state=fixture()
if ci.validate_ci_state(state,'fixture')!=[12,13]:raise ValueError('expected skips lost')
legacy=fixture();legacy['jobs'][0]['steps']=legacy['jobs'][0]['steps'][:3]
ci.validate_ci_state(legacy,'fixture')
bad=[]
for number in (9,10,11):
    changed=fixture();changed['jobs'][0]['steps'][number-9]['conclusion']='skipped';bad.append(changed)
for key,value in (('headSha','foreign'),('status','in_progress'),('conclusion','failure'),('jobs',[])):
    changed=fixture();changed[key]=value;bad.append(changed)
changed=fixture();changed['jobs'][0]['steps'][3]['name']='unknown';bad.append(changed)
changed=fixture();changed['jobs'][0]['steps'][4]['conclusion']='success';bad.append(changed)
changed=fixture();changed['jobs'][0]['steps'][0]['conclusion']='failure';bad.append(changed)
changed=fixture();changed['jobs'][0]['steps']=changed['jobs'][0]['steps'][1:];bad.append(changed)
for changed in bad:
    try:ci.validate_ci_state(changed,'fixture')
    except ValueError:continue
    raise ValueError('unsafe CI state admitted')
print(f'CI kernel state: expected failure-only skips and {len(bad)} rejection cases PASS (fixtures only)')
