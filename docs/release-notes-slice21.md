# Slice 21 — native hard disc

Native A3020 sessions can attach a persistent, writable hard disc at `ADFS::4`.
The `Hard Disc` option defaults to `Auto`; `Off` prevents attachment without
changing boot routing. `Disk Writes: Off` attaches an existing disc read-only
and does not create a new one. ADFFS and install sessions retain their previous
disc behavior. A3000, A3010 and A4000 native sessions do not attach this disc.

Each disc lives at `<save>/arcdroid/hd/<content_id>/`. The content ID follows
the existing media identity: a playlist and a standalone image can have
different IDs, while sets that share image 1 can share a disc. Disc swaps and
resets do not change the ID during a content session. Each identity costs about
16 MiB for the base image, plus an append journal capped at 8 MiB; journals
above 1 MiB compact on reopen. Completed IDE commands commit to the journal
before success is returned. Storage assumes one writer and does not promise
power-loss durability.

An unavailable new disc leaves the native machine running without IDE and
shows a warning. A damaged saved disc is preserved, left unattached and
reported as an error. A READY disc with unavailable write access attaches
read-only and warns that saves cannot change. A write failure or full journal
fails that IDE command, retains committed data and disables later writes until
a successful frontend reset reopens the store.

With `JFD Boot: Auto`, a single-image high-density JFD on the A3020 now boots
natively even when an ADFFS package is present. `ADFFS preferred` retains the
older route. Saved games made by title 159 on the old install path are not
visible on the default native path in this slice. Its install store remains
on disk; `ADFFS preferred` can still reach it until Slice 22.
