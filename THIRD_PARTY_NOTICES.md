<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Third-party notices

## Arculator

ArcDroid incorporates an allowlisted snapshot of Arculator by Sarah Walker and
contributors under GPL-2.0-only. See `engine/UPSTREAM.md`, `engine/arculator/AUTHORS`,
and the repository-root `COPYING`.
Slice 10b adds the pinned upstream `src/ide.c` and `src/ide.h` under the same
license. ArcDroid replaces their desktop file access with an in-memory bounded
block backend and transaction handling; modification notices are in both files.
The unused upstream `arcrom_ext` HostFS binary is excluded (D-197). Only the
four accepted CMOS default data images remain; their provenance is recorded
in `engine/UPSTREAM.md`.

## RPCEmu

ArcDroid contains the D-137 allowlisted source snapshot of RPCEmu 0.9.5,
Copyright (C) 2005–2010 Sarah Walker, under GPL version 2 or any later version.
Its licence remains in `engine/rpcemu/COPYING`; original notices remain in
every imported C/header file. ArcDroid changes are marked in their files.
The exact source hashes, pristine commit and patch lineage are recorded in
`engine/rpcemu/manifest.json` and `engine/rpcemu/UPSTREAM.md`.
The owned `engine/rpc/rpc_audio.c` adapts the quadword/channel layout reviewed
from RPCEmu's licensed `src/sound.c`; its full original GPL notice and Sarah
Walker attribution remain there. No upstream host sound/thread code is used.
No RPCEmu guest data, header-less disc/HostFS source, GUI, network or binary
assets are imported. ArcDroid additions are GPL-2.0-only.
The imported CMOS implementation also preserves its attribution to Softgun
by Jochen Karrer, used upstream under GPLv2.

## libretro API header

`libretro/include/libretro.h` is from libretro-common commit
`5472d4dc876c3d990805989025a9485271295662`. Its per-file notice states that it
is released under the MIT license:

> Copyright (C) 2010-2026 The RetroArch team
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the conditions in the header.

The complete MIT notice remains at the top of the vendored file.

## zlib

ArcDroid vendors the in-memory inflate subset of zlib 1.3.2, Copyright (C)
1995-2024 Jean-loup Gailly and Mark Adler, from tag `v1.3.2`:

- tag commit: `da607da739fa6047df13e66a2af6b8bec7c2a498`
- upstream tree: `79b5a06f88838dd54f90f821e8650254abfedb7e`
- GitHub tag tarball SHA-256:
  `b99a0b86c0ba9360ec7e78c4f1e43b1cbdf1e6936c8fa0f6835c0cd694a495a1`
- compile definition: `Z_SOLO` (prevents inclusion of `gzguts.h` and requires
  ArcDroid's in-memory allocator callbacks)

The exact source allowlist is:

```text
adler32.c
crc32.c crc32.h
inffast.c inffast.h
inffixed.h
inflate.c inflate.h
inftrees.c inftrees.h
zconf.h zlib.h
zutil.c zutil.h
LICENSE
```

No `gz*`, deflate, file wrapper, or platform zlib is compiled or linked. The
unmodified upstream licence is preserved at `third_party/zlib/LICENSE`.
