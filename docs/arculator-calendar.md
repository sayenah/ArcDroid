<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Archimedes session calendar (AY-1)

At Arculator session start, after `cmos_init` captures local time and before
any guest instruction, the backend applies that clock's full year to the live
CMOS image and recomputes its checksum. It does so once per frontend session. The frontend caches the live year
before persistence and passes it into later engine constructions, including
`retro_reset` and helper recovery, so reconstruction retains the guest's year
instead of sampling a new year. The cache is cleared on content release. Guest OS_Word 14,0 probes with independently seeded
2000 and 2040 establish use of raw C0/C1 in Arthur 1.20, RISC OS 2.00/2.01,
and RISC OS 3.00/3.10/3.11/3.19 on old I/O and 3.10/3.11/3.19 on new I/O.
Arthur 0.30 also uses these bytes, established through OS_Word 14 in an
isolated 1 MiB MEMC1 probe; the shipping machine profiles are unchanged. The A500 RISC OS 2 build likewise reads C0/C1 from its EEPROM, verified
in an isolated guest probe. Its PCF8573 has no hardware year field, so it
gets no RTC year bits; the full-year overlay still applies to that kernel.
A500 remains outside the shipping backend's machine set. The PCF8583 date register gains the year
modulo four from the same clock. Embedded defaults remain unchanged. The
device tracks guest writes to the patched bytes so a persistence snapshot
restores their pre-patch values until the guest writes them; backend reset preserves
that tracking and frontend reconstruction rebases it on the persisted payload. `arc_cmos_boot_stamp_payload` excludes C0/C1 along with the
checksum, covering boot stamps, chooser selections and boot records across
New Year. Synthetic fixtures cover these contracts in CI; private firmware
probes remain local.

On upgrade from a pre-AY-1 build, an existing RISC OS 2/3 boot record has a
different CMOS stamp because C0/C1 are now excluded. It is discarded once on
the first load after upgrade; that launch behaves like first use and the boot
record can be learned again. Remembered chooser picks are unaffected. No
compatibility conversion is performed.

The guest date interface is documented in the
[RISC OS PRM, Time and Date](https://www.riscos.com/support/developers/prm/timedate.html).

The RPC host-static census lists the two Archimedes-only cache objects and
the test-only load-epoch oracle explicitly under R-AY1/I-1. Neither changes
RPC guest comparison fields, projections or wire compatibility.
