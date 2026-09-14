# Arculator upstream snapshot

- Project: Arculator
- Source: https://github.com/sarah-walker-pcem/arculator
- Commit: `7d3d2679589bba759197e3a9288a12749c797381`
- Commit date: 2026-08-10
- Imported: 2026-09-13
- License: GPL-2.0-only; the upstream `COPYING` is preserved at the repository root.

## Allowlist

The snapshot contains `AUTHORS`, `README.md`, the accepted binary assets listed
below, and these machine-core sources:

```text
src/arc.h
src/arm.c src/arm.h
src/cmos.c src/cmos.h
src/config.c src/config.h
src/cp15.c src/cp15.h
src/disc.c src/disc.h
src/disc_adf.c src/disc_adf.h
src/ds2401.c src/ds2401.h
src/fpa.c src/fpa.h
src/ioc.c src/ioc.h
src/ioeb.c src/ioeb.h
src/joystick.c src/joystick.h
src/keyboard.c src/keyboard.h src/keytable.h
src/lc.c src/lc.h
src/main.c
src/mem.c src/mem.h
src/memc.c src/memc.h
src/plat_input.h src/plat_joystick.h src/plat_sound.h src/plat_video.h
src/printer.c src/printer.h
src/romload.c src/romload.h
src/sound.c src/sound.h
src/timer.c src/timer.h
src/vidc.c src/vidc.h src/video.h
src/wd1770.c src/wd1770.h
```

This is an allowlisted copy, not a subtree. ArcDroid modifications carry an
explicit notice in each modified upstream file.

The pinned upstream `AUTHORS` file is empty. Upstream author and contributor
credits are preserved in the imported `README.md`.

## Deliberate exclusions

- wxWidgets, SDL, desktop platform, resource, generated autotools, and debugger
  console files: replaced by the libretro and ArcDroid platform layers.
- All podules and dynamic loaders: outside Slice 1 and unsuitable for arbitrary
  runtime loading on Android.
- HostFS, ST506, and IDE: outside Slice 1.
- FDI, APD, HFE, SCP, JFD, SSD, and DSD readers plus `fdi2raw`: deferred to
  separately hardened media slices.
- Drive-noise WAV files: outside Slice 1 and not redistributable evidence.
- All upstream `.dll` and `.so` files: binaries are built from corresponding
  source by ArcDroid.
- ROM-set directories and firmware: users provide classic Arthur/RISC OS images;
  no copyrighted firmware is imported.

## Founder-accepted binaries without source

Decision D-018 expressly accepts only these upstream-distributed assets. Hashes
are SHA-256 over the exact files at the pinned commit.

| Path | Size | SHA-256 |
|---|---:|---|
| `arculator/roms/arcrom_ext` | 65,536 | `c181c7fbbd0f0038f6adf2976a9cd03cb4ad58be3bd32074719fd516d1ddae98` |
| `arculator/cmos/arthur/cmos.bin` | 256 | `7dd1775b3a54da74abd2e74a1f5fedf573361c33c132030792ae51f554938219` |
| `arculator/cmos/riscos2/cmos.bin` | 256 | `7d1b2c6ac95cd5ea1020f6397a20f4cb3b2d8f896f0266a6ce2a9acd52cf4002` |
| `arculator/cmos/riscos3_new/cmos.bin` | 256 | `855a7a33f559d03e4f66010ee493307a75a6e2c80729df3f98cb08f95dd862e9` |
| `arculator/cmos/riscos3_old/cmos.bin` | 256 | `d809253f16f4e865a4e966133032dc430ee0a4b0df2a71caf7c0ce7a075e81c8` |

The support ROM is retained for future HostFS work but is not wired into Slice 1.
