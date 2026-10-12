#!/usr/bin/env python3
"""Embed only ArcDroid-authored boot text. GPL-2.0-only."""
from pathlib import Path
import sys
root, output = map(Path, sys.argv[1:])
assets = [('Multisync', 'Multisync', 0xfed), ('BootEnv', 'BootEnv', 0xfff),
          ('BootRes', 'BootRes', 0xfff), ('BootTasks', 'BootTasks', 0xfff),
          ('ArcTasks', '!Boot.ArcTasks', 0xfea), ('Run', '!Boot.!Run', 0xfeb),
          ('Boot', '!Boot.!Boot', 0xfeb)]
text = ['/* Generated from ArcDroid GPL-2.0-only text; no package data. */',
        '#include "rpc_boot_assets.h"']
sizes = []
for index, (name, path, kind) in enumerate(assets):
    data = (root / name).read_bytes()
    if b'\r' in data or b'\0' in data:
        raise ValueError('authored source must be LF text')
    if name != 'Multisync':
        data = data.replace(b'\n', b'\r')
    if len(data) > 16384:
        raise ValueError('authored asset bound')
    sizes.append(len(data))
    text.append(f'static const uint8_t asset{index}[] = {{' + ','.join(map(str, data)) + '};')
text.append('const ArcBootAsset arc_boot_assets[ARC_BOOT_ASSET_COUNT] = {')
for i, (_, path, kind) in enumerate(assets):
    text.append(f'{{"{path}",asset{i},{sizes[i]},0x{kind:x}}},')
text.append('};')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text('\n'.join(text) + '\n')
