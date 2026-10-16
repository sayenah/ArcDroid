<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Slice 23 release notes

- Eligible native applications whose launch ends after a refused floppy write
  can be installed by a guest program into `ADFS::4.$.ArcDroid` and, when a
  single produced application is confirmed, launched directly on later loads.
- The installation is deliberately one-shot. If it is interrupted or fails,
  move the owned directory aside on the guest hard disc to try again; keep a
  copy if it contains saves. ArcDroid never silently retries or deletes it.
- A validated completed installation can launch from a read-only hard-disc
  store. Existing native and ADFFS routes and explicit launch choices remain
  available.
