# Third-party notices

## Arculator

ArcDroid incorporates an allowlisted snapshot of Arculator by Sarah Walker and
contributors under GPL-2.0-only. See `engine/UPSTREAM.md`, `engine/arculator/AUTHORS`,
and the repository-root `COPYING`.

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

Android uses the NDK platform zlib. Windows uses the MinGW-w64 toolchain's static
zlib. zlib is Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler and is
provided "as-is"; permission is granted to use, alter, and redistribute it
subject to preserving origin and notices. See https://zlib.net/zlib_license.html.
