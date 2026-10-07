#!/usr/bin/env python3
"""Independent D-139 calibration arithmetic. GPL-2.0-only.
No core imports, core files, measurements or generated traces are inputs.
Constants and recurrences come from docs/riscpc-timing.md §§2–6.
"""
from fractions import Fraction as F
RATES = (20, 35, 100, 35)

def base(c):
    b = F(1000, c['rate'])
    return max(b, F(125, 2)) if c.get('slow') and c['model'] == 2 else b

def ram(c, addresses, burst=True, buffered=False):
    q = F(125, 4) * (2 if c.get('divided') else 1)
    n, s = (7, 4) if c.get('width') and c.get('space', 'dram') == 'dram' else (5, 2)
    if c.get('byte'): n, s = 5, 5
    result = F(0)
    for i, a in enumerate(addresses):
        seq = i and burst and a == addresses[i-1] + 4 and a % 1024 != 0
        if c['model'] == 2 and a % 16 == 0: seq = False
        if buffered and c['model'] != 2 and i % 8 == 0: seq = False
        result += q * (s if seq else n)
    return result

def rom(c, count):
    cr = c.get('romcr', 64 if c['model'] == 3 else 0)
    q = F(125, 4) * (2 if c.get('divided') else 1)
    chips = 2 if c['model'] == 3 and cr & 64 else 1
    scale = 2 if c['model'] == 3 and not cr & 32 else 1
    burst = (cr >> 3) & 3
    return q * scale * sum(5-burst if burst and i % 4 else 7-min(cr & 7, 5)
                           for i in range(count * chips))

def addresses(c):
    n = c.get('n', 1); at = c.get('offset', 0) + 64
    mode = c.get('mode', 'ia')
    first = at + (4 if mode == 'ib' else -4*n+4 if mode == 'da' else -4*n if mode == 'db' else 0)
    return [first + 4*i for i in range(n)]

def duration(c):
    b = base(c); k = c.get('k', 1); n = c.get('n', 1)
    kind = c['kind']; a = addresses(c)
    if kind in ('alu', 'branch', 'disable'): return k*b
    if kind=='romfallthrough':return 2*b+rom(c,2)
    if kind in ('romnext','romdetour'):
        return (2 if kind=='romnext' else 3)*b+2*rom(c,1)
    if kind == 'romcastout':
        initial=F(125,4)*(7-min(c['romcr']&7,5))
        return 34*b+rom(c,8)+32*ram(c,list(range(0,32,4)))+4*initial
    if kind == 'rom': return k*b + rom(c, k)
    if kind == 'rombranch':return k*(b+rom(c,1))
    if kind == 'romhot': return k*b
    if kind == 'io': return k*(2*b+F(125)*(2 if c.get('divided') else 1))
    if kind=='abortfetch':return b
    if kind in ('abortload','abortstore'):
        return b+ram(c,[0xff8,0xffc],c['model']!=2 or c.get('wb',False),c.get('wb',False))
    if kind == 'swap':
        if not c.get('cache') or not c.get('bufferable', True):
            return b + ram(c, [64,64], False)
        return b if c.get('hot_data') else b + ram(dict(c,byte=False), list(range(64,96,4)))
    if kind in ('load', 'ldm'):
        if c.get('hot_data'): return b
        if c.get('cache'):
            length = 32 if c['model'] == 2 else 16
            lines = sorted(set(v // length for v in a))
            return b + sum(ram(dict(c,byte=False), list(range(t*length, (t+1)*length, 4))) for t in lines)
        return b + ram(c, a, c['model'] != 2)
    if kind in ('store', 'stm'):
        if c['model'] == 2 and c.get('hot_data'): return b
        if c.get('wb') and c.get('bufferable', True): return b+ram(c, a, True, True)
        return b+ram(c, a, c['model'] != 2)
    if kind in ('stream', 'random', 'streamwrite', 'randomwrite', 'clear'):
        ins = 4 if kind.startswith('random') else 3
        if kind == 'clear':
            if c.get('wb'): return 2*b + k*ram(c, list(range(0,32,4)), True, True)
            return 3*k*b + k*ram(c, list(range(0,32,4)), c['model'] != 2)
        if kind.endswith('write') and c.get('wb'):
            n=ram(c,[0],False)
            return max(ins*k*b,(ins*(k-1)+1)*b+n) if ins*b>=n else b+k*n
        if kind.endswith('write') or not c.get('cache'): return ins*k*b+k*ram(c, [0], False)
        if kind == 'stream':
            length = 32 if c['model'] == 2 else 16
            fills = (4*k+length-1)//length
            extra = 1 if c['model'] in (0,3) and k == 1024 else 0
            return ins*k*b + (fills+extra)*ram(c, list(range(0,length,4)))
        length = 32 if c['model'] == 2 else 16
        extra = (4,2,0,4)[c['model']] if k == 1024 else 0
        return ins*k*b+(k+extra)*ram(c, list(range(0,length,4)))
    raise ValueError(kind)

def cases():
    for model, rate in enumerate(RATES):
        d = dict(model=model,rate=rate)
        for kind in ('alu','branch'):
            yield dict(d,kind=kind,k=1000000,name=kind)
        for space in ('dram','vram') if model != 3 else ('dram',):
            for kind in ('ldm','stm'):
                for n in range(1,17):
                    for mode in ('ia','ib','da','db'):
                        for offset in (0,4,8,12):
                            yield dict(d,kind=kind,n=n,mode=mode,offset=offset,space=space,
                                       name=f'{space}-{kind}-{n}-{mode}-{offset}')
            for kind in ('load','store'):
                for byte in (False,True):
                    yield dict(d,kind=kind,byte=byte,space=space,name=f'{space}-{kind}-byte{int(byte)}')
        for n in range(1,17):
            yield dict(d,kind='ldm',n=n,cache=True,name=f'cold-ldm-{n}')
            yield dict(d,kind='stm',n=n,wb=True,name=f'buffered-stm-{n}')
        for space in ('dram','vram') if model!=3 else ('dram',):
            for kind in ('stream','random','streamwrite','randomwrite'):
                for cache in (False,True):
                    for wb in (False,True) if kind.endswith('write') else (False,):
                        yield dict(d,kind=kind,k=1024,cache=cache,wb=wb,space=space,
                                   name=f'{space}-{kind}-cache{int(cache)}w{int(wb)}')
        for kind in ('load','store'):
            for wb in (False,True):
                yield dict(d,kind=kind,cache=True,hot_data=True,wb=wb,name=f'hot-data-{kind}-w{int(wb)}')
        for cache in (False,True):
            for wb in (False,True):
                for bufferable in (False,True):
                    yield dict(d,kind='store',cache=cache,wb=wb,bufferable=bufferable,
                               name=f'write-c{int(cache)}w{int(wb)}b{int(bufferable)}')
        for wb in (False,True):
            yield dict(d,kind='clear',k=1024,wb=wb,name=f'clear-w{int(wb)}')
        yield dict(d,kind='disable',k=1024,name='qualified-disable-retention')
        yield dict(d,kind='io',k=1024,name='prog-register')
        yield dict(d,kind='romhot',k=1024,name='rom-hot')
        for cr in (8,16,24):
            for kind in ('romnext','romdetour','romfallthrough'):
                yield dict(d,kind=kind,romcr=cr|(32 if model==3 else 0),
                           name=f'{kind}-{cr:02x}')
        for cr in ((0,8,16,24) if model != 3 else (64,72,80,88,32,61)):
            yield dict(d,kind='rom',k=32,romcr=cr,name=f'rom-cold-{cr:02x}')
            yield dict(d,kind='rombranch',k=32,romcr=cr,name=f'rom-taken-{cr:02x}')
        yield dict(d,kind='abortfetch',name='denied-prefetch')
        yield dict(d,kind='abortload',n=8,name='partial-ldm-abort')
        for wb in (False,True):
            yield dict(d,kind='abortstore',n=8,wb=wb,name=f'partial-stm-abort-w{int(wb)}')
        if model == 2:
            for cr in (0,24,5,29):
                for wb in (False,True):
                    yield dict(d,kind='romcastout',romcr=cr,wb=wb,
                               name=f'rom-castout-{cr:02x}-w{int(wb)}')
            yield dict(d,kind='branch',k=1024,slow=True,name='reset-slow-hot')
            for byte in (False,True):
                for cache,hot,bufferable in ((True,True,True),(True,False,True),(False,False,False),(True,True,False)):
                    for wb in (False,True):
                        yield dict(d,kind='swap',byte=byte,cache=cache,hot_data=hot,bufferable=bufferable,wb=wb,
                                   name=f'swap-byte{int(byte)}-c{int(cache)}h{int(hot)}b{int(bufferable)}w{int(wb)}')
        if model == 3:
            for kind in ('load','store','ldm','stm'):
                for byte in (False,True) if kind in ('load','store') else (False,):
                    for wb in (False,True) if kind in ('store','stm') else (False,):
                        for cache in (False,True) if kind in ('load','ldm') else (False,):
                            yield dict(d,kind=kind,n=8 if kind in ('ldm','stm') else 1,width=True,
                                       byte=byte,wb=wb,cache=cache,name=f'width16-{kind}-{int(byte)}{int(wb)}{int(cache)}')
    for rate in range(1,101):
        yield dict(model=2,rate=rate,kind='branch',k=10000,name=f'exact-rate-{rate}')
    for rate in (60,99):
        yield dict(model=2,rate=rate,kind='random',k=60000000,cache=False,name=f'long-exact-stalls-{rate}')
