# ArcDroid

ArcDroid is a RetroArch/libretro core for playing Acorn Archimedes games on Windows and Android. Its aim is **select game → play**: ArcDroid inspects the game media, chooses a suitable machine and operating system version, mounts the discs and handles the boot, so the player never has to set up an Archimedes by hand.

**Status:** in development. There is no release yet. Archimedes (Arculator-based) titles are the current focus; RiscPC support is being built and is not offered to players until it runs at near-hardware speed.

## What you need to supply

ArcDroid ships **no** firmware, operating-system files or games. You provide them from sources you are entitled to use, and place them in RetroArch's system (BIOS) folder:

- **Archimedes:** an Arthur or RISC OS 2/3 ROM image (RISC OS 3.11 recommended). The core's `.info` file lists the versions it recognises.
- **RiscPC (when available):** a RISC OS 3.5–3.71 ROM image, and the RISC OS boot sequence files (HardDisc4 and `!System`) from RISC OS Open.
- **ADFFS (optional):** for titles that need it, your own copy of ADFFS. It is never bundled.

ArcDroid identifies firmware by checksum and never downloads, bundles or logs it.

## Supported content

ADF and ADL disc images, JFD images, `.m3u` playlists for multi-disc games, and `.zip` archives containing these.

## Licence

ArcDroid is free software under the **GNU General Public License version 2 only** (GPL-2.0-only); see `COPYING`. It incorporates third-party code under compatible licences, listed with their notices in `THIRD_PARTY_NOTICES.md`:

- **Arculator** by Sarah Walker and contributors (GPL-2.0) — the Archimedes emulation engine;
- **RPCEmu 0.9.5** by Sarah Walker (GPL-2.0-or-later, used here as part of a GPL-2.0-only whole) — the RiscPC engine;
- **zlib 1.3.2** inflate (zlib licence);
- **libretro.h** from libretro-common (MIT).

## Relationship to Arculator and RPCEmu

ArcDroid is an **independent project**. It is not Arculator or RPCEmu, is not endorsed by their authors, and its changes are not contributions to those projects. ArcDroid's modifications to their source are marked in each changed file and recorded in `engine/UPSTREAM.md` and `engine/rpcemu/UPSTREAM.md`. Much of ArcDroid's own code, and its changes to the imported engines, were written with AI assistance under human direction. Please report problems with ArcDroid here, not to the upstream projects.

## Building

ArcDroid builds with CMake and Ninja. Host build and tests:

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Windows (MinGW-w64) and Android (NDK, arm64-v8a) builds are covered by the CI workflow in `.github/workflows/ci.yml`.

## Security

Please report security issues privately through GitHub's "Report a vulnerability" on this repository.
