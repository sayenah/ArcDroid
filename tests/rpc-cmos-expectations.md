<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Run E CMOS recovery and fixed generation epoch

R3d-4b / R-Y1: preparation refreshes physical `0xC0` (binary year modulo 100),
`0xC1` (binary century) on each requested session start, before any guest
instruction. AY-1 / L-1 adjusts `0x3F` by the sum of the two new year bytes
minus the two old bytes, modulo 256. A valid checksum stays valid; every
nonzero checksum error stays exactly the same, so a guest-corrupted record
is never silently healed. The store test exercises all 256 checksum errors
for every year 1970..2399, with a recomputation mutant. This is
the only seed patch. The session epoch already encodes local calendar time;
it is converted without another host-clock read. This supersedes the exact-seed
rule for these three bytes only, on ordinary and persisted-plan preparations.
Shared defaults, generation/factory captures, procedure revision 4 and bare v1
plans remain byte-identical. A running reset preserves the guest year; a
player-set year is overwritten at the next load. The guest owns rollover.

`rpc-session-calendar` runs an authored ARM guest over real IOMD/IIC on all
seven qualified pairs, both preparation clients, first use and reopen, for
1970/1999/2000/2001/2002/2003/2026/2099/2100/2399. The guest reads all physical
bytes, checks the RISC OS checksum, and reconstructs the full year using C0/C1
and the PCF8583 year-mod-four field. The retained drive setting and checksum
acceptance expose a seed that would make the kernel reset CMOS. These are
authored guest checks; actual RISC OS OS_Word 14/device evidence remains
Fable's private leg. Each reset also preserves an intentionally player-edited
year. Skipping either the preparation patch or its checksum is mutation-tested.
Under sanitizers the identical workload uses seven pair shards and one switching
shard; all eight shards are RUN_SERIAL (AY-1 / I-2). Another 200 preparation RPC/Arculator switches read guest year/checksum. The
ordinary-route harness also checks the guest's IIC result on all seven pairs
at first use, reset and persisted reopen. Its year oracle reads the exact
captured ordinary-load epoch through a test-only, read-only getter; the
harness verification clock deliberately advances to the following year.
Plan/seed tests deliberately expect session payloads to differ from defaults
only in these three bytes; arbitrary payload bytes remain authoritative.

Only `.0` and `.1` are published recovery roots. A `.tmp` is an unpublished
candidate regardless of its length, magic or key; load ignores it and the next
publication overwrites it. Invalid published slots still refuse writes and
remain untouched. Publication tests interrupt every write, flush, readback,
retirement, rename and final verification operation, then require successful
later publication both on the interrupted live handle and a restarted handle.
A pending live handle reconciles a completed rename before deciding whether
its payload changed. An unchanged payload still produces no publication.

Procedure revision 3 pins generation to 946684800, 2000-01-01 00:00:00 UTC.
Provenance records that epoch; it does not depend on content or the requested
plan's RTC epoch. Requested sessions retain their own epoch. The integration
oracle independently checks the 512-byte envelope, its revision/provenance,
CRC and payload digest. Two different plan epochs/content identities reuse one
unchanged default; an independently generated default in a fresh directory is
also byte-identical. The full guest samples the real RTC hour via IIC and
puts it in the payload: midnight generates zero, whereas the alternate plan
epoch is at 23:31 UTC, so incorrectly using it would invalidate the guest
checksum and fail generation. One-frame and eight-frame generation remain identical.
Plan v1's factory schedule and existing goldens are unchanged.

The checksum negative writes all 240 payload addresses through real IIC,
finishes before the stability window, and leaves all payload bytes zero. The
independent guest checksum rule starts its sum at one, so physical address 63
(payload offset 47) must contain one for this payload. Zero therefore fails
only checksum clause (c): no default or requested session may be published.

Defence tests recompute the envelope CRC after changing slot parity, reserved
bytes or provenance epoch, proving semantic validation independently of CRC.
Header model bytes must encode model+1 for every model. A one-shot corrupted
candidate readback prevents either existing slot from being retired or replaced.
The first rejected epoch is 13,569,465,600, one second beyond the declared RTC
range. AY-1 L-2 rejects it (and UINT64_MAX) before store acquisition, for both
resolved ordinary and low-level preparation, with absent and existing records;
seeds, records and defaults stay unchanged and no engine or VFS handle remains. Refused-store integration uses a valid fallback and an `ARCCMOS2`
published peer, then runs a writing guest: exactly one notice per load/state,
no repeated frame or unload notice, and neither file changes. QA mutants
C05/C07/C10/C11/C14 and the oracle checksum mutant C01 each fail these checks.
