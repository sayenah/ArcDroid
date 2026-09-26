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
src/82c711.c src/82c711.h
src/82c711_fdc.c src/82c711_fdc.h
src/cmos.c src/cmos.h
src/config.c src/config.h
src/cp15.c src/cp15.h
src/disc.c src/disc.h
src/disc_adf.c src/disc_adf.h
src/ds2401.c src/ds2401.h
src/fpa.c src/fpa.h
src/ioc.c src/ioc.h
src/ioeb.c src/ioeb.h
src/ide.c src/ide.h
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

`src/82c711.c` resets the SuperIO configuration state for each cold machine
creation and connects to ArcDroid's IDE implementation. With no attached disc
the bus continues to return all-one reads, ignore writes, and raise no IRQ.
Slice 10b imports upstream `src/ide.c` and `src/ide.h` from the pinned commit.
The staged `ide.c` blob was verified as
`585c4697e9d9f02cb380827705c15cfff27aca24`. ArcDroid replaces host
`FILE` handling, desktop UI, and unbounded transfers with a 512-byte-sector
backend supplied by the core. It latches full WRITE SECTOR(S) commands before
one store commit, bounds CHS/LBA and count-zero transfers, handles byte and
word data access, controller IRQ/timers, reset, and teardown, and refuses
FORMAT and unsupported commands. `ide.h` replaces upstream file handles and
desktop geometry with the bounded backend and transaction state. The source
is attached only to A3020 install-disc sessions; no other machine gains a
disc. `src/wd1770.c` and `src/82c711_fdc.c` additionally report controller
READ SECTOR commands for the one-shot ArcDoor latch. These changes have
per-file modification notices.
`src/82c711_fdc.c` bounds guest-selected drives and FORMAT data, uses the
controller invalid-command result in every build, and assigns `curdrive` only
for commands with a drive selector (not SPECIFY, CONFIGURE or PERPENDICULAR),
so selectorless result paths remain within the two-drive arrays. The added
`c82c711_fdc_media_change` cancels only the affected active transfer and the
added `c82c711_fdc_shutdown` quiesces the controller during lifecycle teardown.
Terminal error paths clear FIQ; a WRITE DATA request is suppressed when a
synchronous media callback has already entered a terminal result phase; cold
creation fully reinitialises the static controller state. The unchanged HD
ADF loader is now selected by ArcDroid for F-format media; `src/disc.c`
exposes the active poll period to the ArcDroid JFD reader, and `src/disc_adf.c`
counts elapsed guest microseconds for 200 ms index pulses at DD and HD, and
keeps the READ ID sector position per drive when media geometries differ.
Test-core builds also
expose command/read-success counters used to prove the generated guest ROM
executed the New-IO path; those counters are absent from shipping targets.
The three upstream commented-out `dumpregs()` calls are omitted because the
shipping-I/O guard deliberately rejects that desktop debug-file entry point;
their removal changes no compiled behaviour.

`src/keyboard.c` contains ArcDroid's single consumption-acknowledgement hook:
after the existing KART-idle and keyboard-enabled guards, one platform call
applies the next queued input event. Skipped polls never advance the queue and
the upstream packet encoding, handshake, and one-event-per-poll logic remain
unchanged.

`platform/arcdroid_input.c` also clears the engine `keyena` and `mouseena`
globals in `input_init` so each core initialization starts with disabled input
channels.

`src/disc_jfd.c` and `src/disc_jfd.h` are ArcDroid-authored replacements under
GPL-2.0-only, credited to upstream Arculator for the surrounding drive
integration. Upstream commit `7d3d267`'s incomplete, disabled JFD reader was
used as historical reference but was not imported. The replacement consumes
validated immutable descriptors from `core/jfd.c` and performs no file I/O,
allocation, decompression, or parsing in the engine poll path. MAME's JFD
reader was consulted only as an independent format reference; no MAME code was
copied.

The C-11c QA follow-up checks that an effective FileCore map is valid before
setting a terminal-read probe mark or refusing a later write because of one.
When the effective map is unavailable, the JFD reader retains C-11 behavior.

Slice 10a extends ArcDroid's shared `disc_funcs_t.writesector` callback with
the controller's requested size and deleted-data intent. The native JFD
reader now buffers a complete WRITE SECTOR, checks descriptor eligibility,
and calls the ArcDroid overlay publisher before reporting success; read data
comes from the committed record view. `wd1770.c` handles its multiple-sector
write variant and bounds missing-byte failure; `82c711_fdc.c` passes N and
deleted-data intent, refuses early terminal count on JFD, and suppresses DMA after a
synchronous continuation error. FORMAT and WRITE TRACK remain refused. The
engine never owns a host file handle or writes the supplied JFD.
The Slice 10a QA follow-up keeps WD1770 multi-sector RNF detection armed
while the next ID is searched and applies the override guard to continuation;
the 82C711 applies early terminal-count discard only to JFD, preserving raw
ADF zero-fill completion, and rejects out-of-range N on JFD. The native JFD
reader reports whether it is the active drive media for that gate and consumes
the store's precomputed descriptor eligibility when available.
Slice 10a C-11 extends that ArcDroid reader with a scheduled prospective
WRITE SECTOR target for omitted P-DD5 IDs. The reader checks physical C/H,
keeps prospective IDs invisible to reads and address commands until the
overlay's atomic rename, and uses the same effective-sector view after commit.
`82c711_fdc.c` supplies the command cylinder and physical selected side for
that creation check; its original CRLF line endings remain intact. The engine
still performs no host file I/O and does not allocate in its poll path.
The Slice 10a QA low follow-up reports a missing WRITE SECTOR refusal before
delivering the final index pulse to WD1770, because that callback can stop the
reader. This changes only refusal diagnostics; both controller limits remain
unchanged.
C-11c marks only terminal READ SECTOR RNF of an eligible, absent DD slot and
consults the mounted map's probe marks before a prospective write. It checks
the buffered map-sector write for a probed allocation before invoking the
overlay publisher; vetoes use the existing controller data error and leave
store write permission intact. The 82C711 passes requested C/H to the reader
for READ as it already did for WRITE. Its CRLF line endings are preserved.

The pinned upstream `AUTHORS` file is empty. Upstream author and contributor
credits are preserved in the imported `README.md`.

## Deliberate exclusions

- wxWidgets, SDL, desktop platform, resource, generated autotools, and debugger
  console files: replaced by the libretro and ArcDroid platform layers.
- All podules and dynamic loaders: outside Slice 1 and unsuitable for arbitrary
  runtime loading on Android.
- HostFS and ST506 controllers: excluded. IDE is supplied only for Slice 10b
  A3020 install-disc sessions.
- FDI, APD, HFE, SCP, SSD, and DSD readers plus `fdi2raw`: deferred to
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
