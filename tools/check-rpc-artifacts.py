#!/usr/bin/env python3
"""Bounded fail-closed classifier for public CI staging. GPL-2.0-only."""
import argparse
import hashlib
import gzip
import io
import json
import os
from pathlib import Path
import re
import struct
import zipfile

MAX_BYTES=128*1024*1024
MARKER=re.compile(rb'ARCDROID-ROUTE-V1:gate=(open|closed);catalogue=(production|authored);end')
TEXT={'.txt','.log','.info','.json','.csv','.arcplan','.input','.license','.md','.sha256','.script'}
DATA={'.rom','.adf','.adl','.jfd','.m3u','.seed','.bin','.png','.bmp','.wav','.raw','.cfg','.cmos','.img','.hdf','.hdd'}

def binary(data):
    if data[:4]==b'\x7fELF':return 'shared' if len(data)>18 and struct.unpack_from('<H',data,16)[0]==3 else 'executable'
    if data[:4] in (b'\xcf\xfa\xed\xfe',b'\xce\xfa\xed\xfe'):
        return 'shared' if len(data)>16 and struct.unpack_from('<I',data,12)[0]==6 else 'executable'
    if data[:2]==b'MZ':
        if len(data)<64:raise ValueError('truncated PE')
        at=struct.unpack_from('<I',data,60)[0]
        if at+24>len(data) or data[at:at+4]!=b'PE\0\0':raise ValueError('invalid PE')
        return 'shared' if struct.unpack_from('<H',data,at+22)[0]&0x2000 else 'executable'
    return None

def verify_core(path,manifest,source):
    path=Path(path)
    if path.stat().st_size>MAX_BYTES:raise ValueError('core exceeds byte limit')
    data=path.read_bytes();rows=json.loads(Path(manifest).read_text())['targets']
    target=next((row for row in rows if row['file']==path.name),None)
    if not target or target['gate_open'] or target['authored'] or binary(data)!='shared' or MARKER.findall(data)!=[(b'closed',b'production')]:raise ValueError('package core is not closed production')
    record=json.loads(Path(source).read_text());revision=record.get('revision','')
    if not re.fullmatch('[0-9a-f]{40}',revision):raise ValueError('package needs an exact source revision')
    short=revision[:12].encode()
    if short+b'-dirty\0' in data or not any(value+b'\0' in data for value in (short,revision.encode())):raise ValueError('package core does not identify the exact clean source')
    print('RPC install guard: closed production core with exact clean source')

def staged_files(root):
    pending=[(root,0)];files=[];entries=0
    while pending:
        directory,depth=pending.pop()
        with os.scandir(directory) as listing:
            for entry in listing:
                entries+=1
                if entries>8192:raise ValueError('stage entry limit exceeded')
                if entry.is_symlink():raise ValueError('symlink in artifact stage')
                if entry.is_dir(follow_symlinks=False):
                    if depth>=16:raise ValueError('stage depth limit exceeded')
                    pending.append((Path(entry.path),depth+1))
                elif entry.is_file(follow_symlinks=False):files.append(Path(entry.path))
                else:raise ValueError('unknown stage entry type')
    return sorted(files)

def fixture_zip(data):
    # Supported ZIP nesting: outer fixtures plus one embedded ADFFS package.
    queue=[(data,0)];count=expanded=0
    while queue:
        payload,depth=queue.pop()
        if depth>1:raise ValueError('fixture archive depth exceeded')
        end=payload.rfind(b'PK\x05\x06',max(0,len(payload)-65557))
        if end<0 or end+22>len(payload):raise ValueError('invalid ZIP end record')
        disk,start,local,number,central,offset,comment=struct.unpack_from('<HHHHIIH',payload,end+4)
        if disk or start or local!=number or number>4096 or central==0xffffffff or offset==0xffffffff or end+22+comment!=len(payload):raise ValueError('unsupported or over-limit ZIP layout')
        with zipfile.ZipFile(io.BytesIO(payload)) as archive:
            for entry in archive.infolist():
                count+=1;expanded+=entry.file_size
                if count>4096 or expanded>MAX_BYTES:raise ValueError('archive exceeds bound')
                name=Path(entry.filename)
                if name.is_absolute() or '..' in name.parts or '\\' in entry.filename or ':' in entry.filename or entry.flag_bits&1 or (entry.external_attr>>16)&0o170000==0o120000:raise ValueError('unsafe archive entry')
                if entry.is_dir():continue
                child=archive.read(entry)
                if binary(child) or MARKER.search(child):raise ValueError('binary in fixture archive')
                if child.startswith(b'\x1f\x8b'):
                    with gzip.GzipFile(fileobj=io.BytesIO(child)) as stream:raw=stream.read(MAX_BYTES+1)
                    if len(raw)>MAX_BYTES or not raw.startswith(b'JFD') or MARKER.search(raw):raise ValueError('unsupported compressed archive member')
                if zipfile.is_zipfile(io.BytesIO(child)):queue.append((child,depth+1))
                elif child.startswith((b'7z\xbc\xaf\x27\x1c',b'Rar!',b'BZh',b'\xfd7zXZ',b'\x28\xb5\x2f\xfd')) or child[257:262]==b'ustar':raise ValueError('unsupported archive container')

def classify(root,manifest):
    root=Path(root);targets={row['file']:row for row in json.loads(Path(manifest).read_text())['targets']}
    if not (root/'COPYING').is_file() or not (root/'THIRD_PARTY_NOTICES.md').is_file() or not (root/'SOURCE.json').is_file():raise ValueError('distribution lacks licence, notices or corresponding-source record')
    source=json.loads((root/'SOURCE.json').read_text())
    if not re.fullmatch('[0-9a-f]{40}',source.get('revision','')) or (source.get('url')!='https://github.com/sayenah/ArcDroid/tree/'+source['revision'] or source.get('archive')!='https://github.com/sayenah/ArcDroid/archive/'+source['revision']+'.zip'):raise ValueError('invalid exact-source record')
    results=[];total=0
    for path in staged_files(root):
        if path.name=='CLASSIFIED.json':continue
        if path.stat().st_size>MAX_BYTES:raise ValueError('artifact exceeds byte limit')
        data=path.read_bytes();total+=len(data)
        if total>1024*1024*1024:raise ValueError('stage exceeds byte limit')
        form=binary(data);descriptor=MARKER.findall(data)
        if form:
            target=targets.get(path.name)
            if not target:raise ValueError('binary is not in configured target manifest: '+path.name)
            if target['gate_open'] or target['authored']:raise ValueError('ordinary-route-capable target cannot be distributed: '+path.name)
            if form=='shared' and len(descriptor)!=1:raise ValueError('core lacks unique build descriptor: '+path.name)
            if descriptor:
                short=source['revision'][:12].encode()
                if short+b'-dirty\0' in data or not any(value+b'\0' in data for value in (short,source['revision'].encode())):raise ValueError('binary does not identify the exact clean corresponding source')
                if len(descriptor)!=1 or descriptor[0]!=(b'closed',b'production'):raise ValueError('ordinary-route-capable or substituted core cannot be distributed: '+path.name)
                if target['gate_open'] or target['authored']:raise ValueError('manifest/descriptor contradiction')
        elif zipfile.is_zipfile(io.BytesIO(data)):
            fixture_zip(data)
        elif data.startswith(b'\x1f\x8b'):
            if path.suffix.lower()!='.jfd':raise ValueError('gzip is supported only for JFD fixtures')
            with gzip.GzipFile(fileobj=io.BytesIO(data)) as stream:payload=stream.read(MAX_BYTES+1)
            if len(payload)>MAX_BYTES or binary(payload) or MARKER.search(payload) or not payload.startswith(b'JFD'):raise ValueError('unsupported compressed JFD payload')
        elif path.suffix in ('.0','.1') and re.fullmatch(r'[0-9a-f]{40}-m[0-3]-[0-9a-f]{64}\.[01]',path.name):
            if not data.startswith(b'ARCRPCM1') or len(data)>4096 or descriptor:raise ValueError('invalid authored CMOS record')
        elif descriptor:
            raise ValueError('core descriptor in an unsupported container')
        elif data.startswith((b'7z\xbc\xaf\x27\x1c', b'Rar!', b'BZh', b'\xfd7zXZ', b'\x28\xb5\x2f\xfd')) or data[257:262]==b'ustar':
            raise ValueError('unsupported archive container')
        elif path.name not in ('COPYING','SOURCE.json') and path.suffix.lower() not in TEXT|DATA:
            raise ValueError('unknown artifact format: '+path.name)
        results.append({'file':path.relative_to(root).as_posix(),'sha256':hashlib.sha256(data).hexdigest(),'target':targets.get(path.name) if form else None})
    if len(results)>8192:raise ValueError('stage entry limit exceeded')
    (root/'CLASSIFIED.json').write_text(json.dumps({'schema':1,'source':source,'files':results},sort_keys=True,indent=2)+'\n')
    print(f'RPC artifact classifier: {len(results)} allowlisted files; no ordinary-route-capable core')

if __name__=='__main__':
    p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group(required=True);g.add_argument('--stage');g.add_argument('--verify-core');p.add_argument('--source');p.add_argument('--manifest',required=True);a=p.parse_args()
    if a.verify_core:
        if not a.source:p.error('--verify-core requires --source')
        verify_core(a.verify_core,a.manifest,a.source)
    else:classify(a.stage,a.manifest)
