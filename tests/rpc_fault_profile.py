"""Canonical fault ordinals exclude platform-dependent path-prefix probes.
The prefix probes are still injected; every other ordinal and label is pinned.
GPL-2.0-only.
"""
def canonical(rows):
    prefix=[(n,key) for n,key in rows if '/other/stat/' in key]
    kept=[(n,key) for n,key in rows if '/other/stat/' not in key]
    return [(i+1,key) for i,(_,key) in enumerate(kept)],{i+1:n for i,(n,_) in enumerate(kept)},prefix

def checked(rows,fixed):
    count=len(rows)
    assert [n for n,_ in rows]==list(range(1,count+1)),'noncontiguous raw trace'
    normalized,to_raw,prefix=canonical(rows)
    assert len(normalized)==fixed['total'],'canonical trace count drift'
    assert set(key for _,key in prefix)==set(fixed['prefix_classes']),'prefix callback class drift'
    assert len(prefix)<=2048,'path-prefix callback bound'
    return normalized,to_raw,prefix
