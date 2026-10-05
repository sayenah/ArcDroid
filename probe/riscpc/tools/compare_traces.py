#!/usr/bin/env python3
"""Compare complete authored mechanism traces; normalize CRLF only."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def check(path):
    raw=Path(path).read_bytes();text=raw.replace(b'\r\n',b'\n')
    if b'\r' in text:
        raise ValueError('unexpected CR in trace')
    final=re.search(rb'checks=([0-9]+) failures=0\n?$',text)
    if not final or int(final[1])<=0 or b'FAIL' in text or b'fatal' in text:
        raise ValueError('missing/failed mechanism completion')
    if [int(x) for x in re.findall(rb'^model=([0-9]+)$',text,re.M)] != [0,1,2,3]:
        raise ValueError('incomplete model matrix')
    return text,dict(raw_sha256=hashlib.sha256(raw).hexdigest(), normalized_sha256=hashlib.sha256(text).hexdigest(),checks=int(final[1]))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('reference',type=Path);p.add_argument('actual',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
    try:
        x,rx=check(a.reference);y,ry=check(a.actual)
        result=dict(status='PASS' if x==y else 'FAIL',reference=rx,actual=ry,normalization='CRLF to LF only')
        if x!=y:
            result['first_different_line']=next((i+1 for i,(u,v) in enumerate(zip(x.splitlines(),y.splitlines())) if u!=v),min(len(x.splitlines()),len(y.splitlines()))+1)
        print(json.dumps(result,indent=2))
        if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
        return 0 if x==y else 1
    except (OSError,ValueError) as e:
        print('MISSING:',e);return 2

if __name__=='__main__':raise SystemExit(main())
