#!/usr/bin/env python3
"""Rehash an explicit private AC-9 artifact inventory, without directory scans."""
import argparse
import json
from pathlib import Path
from evidence import digest

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('inventory',type=Path);a=p.parse_args()
    try:entries=json.loads(a.inventory.read_text())['artifacts']
    except (OSError,ValueError,KeyError) as e:print('MISSING inventory:',e);return 2
    statuses=[]
    for entry in entries:
        try:
            result='PASS' if digest(a.inventory.parent/entry['file'])==entry['sha256'] else 'FAIL'
        except (OSError,KeyError):result='MISSING'
        statuses.append(result);print(result,entry.get('role','artifact'))
    if not entries:print('MISSING empty inventory');return 2
    return 1 if 'FAIL' in statuses else 2 if 'MISSING' in statuses else 0
if __name__=='__main__':raise SystemExit(main())
