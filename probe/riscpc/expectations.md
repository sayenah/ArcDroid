# Initial mechanism gate — independent expected behavior

Run-3 cold-state expectations: factory capture starts with all 256 CMOS bytes
zero and a machine reset restores exactly those zeros. An ordinary Space
switch held through a PS/2 FF reset must produce FA at 40 us, AA at 5.04 ms,
then make 29 at 5.09 ms after the guest consumes POR. F3 followed by parameter
7F acknowledges each byte, sets a 1000 ms first-repeat delay and 500 ms repeat
period. A repeat is emitted through the PS/2 queue at its virtual deadline,
with the ordinary 50 us serial-delivery delay. Release and machine reset
cancel repetition. These expectations are stated independently of a firmware
reset result; no Delete-specific device behavior is permitted.
The unprotected RiscPC board latch at physical 03310000 powers on at 70 with
unpressed buttons, VGA ID low and 16-bit sound presence low. Byte write FF
reads F0 (only bit 7 is writable); word write zero reads 70. Select plus a
bit-7 write reads B0, preserving active-low buttons. Reset restores 70.

Project-authored tests; no firmware or game data. These expectations follow ARM MMU descriptors and IOMD/VIDC programming, rather than extracting a golden result from the emulator. Cross-platform equality remains required after the host mechanism gate. The cases below are independently specified before their tests.

* Domain 3/client/AP2: user read succeeds, user write faults with FSR `0x3d`, FAR equal to the requested address; supervisor write succeeds. The same outcome must hold after a cached successful translation, through an installed direct pointer and through user transfers in supervisor mode.
* Domain manager ignores AP; no access and reserved domain fault. Sections use status 9; pages use 11, OR domain shifted by four. AP1 is supervisor only. AP0 with S/R 00 denies all, 01 supervisor reads only, 10 reads for both privileges, 11 is not accepted.
* A small page has independently protected 1 KiB subpages. AP3/2/1/0 yield unrestricted, user read only, supervisor only, and no access respectively. Large-page AP boundaries are 16 KiB. Privilege changes must also invalidate instruction access permission assumptions.
* Invalid first/second-level descriptors fault with status 5/7 respectively. A page-table write alone need not evict TLB contents. Explicit TLB invalidation, TTBR, DACR and relevant control changes must discard stale translations. Prefetch aborts do not change data FAR/FSR; data aborts do.
* Authored ROM: MOV base `0x10000000`; MOV counter 0; repeated ADD/STR/B. After 100 instructions, counter and RAM are both 33. RAM instruction MOV R2,7 must execute in 32-bit supervisor mode. Replacing it through normal memory with MOV R2,9 must execute 9 upon return.
* Timer latch 10, GO at zero: counter starts at 9, no interrupt at 4500 ns, IRQ at 5000 ns; at 100 M instructions/s 500 instructions execute by 5000 ns. With timer IRQ cleared and its mask set, idle consumes no instructions until the next expiry at 10000 ns, then wakes.
* VIDC reference clock 24 MHz on RiscPC; ARM7500 reference 32 MHz with synthesizer ratio 3/4 (v=3,r=4, register 0x203) produces the same 24 MHz pixel clock. RiscPC ratio 1, prescaler 1; 800 total pixels × 600/500 rasters gives exactly 50 Hz / floor(1e9/60) ns. Geometry 320×256, 640×480, 800×600 must match the active display registers, indexed pixels 0/1/2/3 must map to their palette, a subsequent normal VRAM write must appear in the next frame. Independent mode timing source: [ARM DDI 0050C](https://documentation-service.arm.com/static/5ed11babca06a95ce53f8fee), §§9.7, 9.16, 9.26, 9.27 and §11.1.
* Sound RAM word `0x1234fedc`: left -292, right 4660. Enabling serial DMA from the two valid 4 KiB blocks must set the sound DMA IRQ, alternate buffers, and produce stereo output at fixed 48 kHz. Inclusive output timestamp zero gives 481 samples through 10 ms and 480 in (10,20] ms. Changing input to 24 kHz must preserve output count and values. No thread or frontend acceptance affects DMA ordering.
* Quadrature move (10,7) changes IOMD X by 10 and Y by -7. Clicking after the move must not repeat the delta. Keyboard make 0x29, then break 0xf0 0x29, are delivered through normal PS/2 RX; reset clears held buttons and queue state.


Expanded authored expectations (before firmware acceptance): all four models run the MMU/CPU/device cases. Large-page AP checks occur at `0x400123`, `0x404123`, `0x408123`, `0x40c123`. TTBR replacement changes the physical result to `0x10200123`. A real guest STR through a previously cached AP2 user read must enter ABT32 at vector `0x10`, with LR `0x10010008`, SPSR `0xd0`, FAR `0x200120`, FSR `0x3d`, and unchanged destination. A user instruction fetch into AP1 in the second 1 KiB subpage must enter vector `0x0c` without executing its MOV and without modifying data FAR/FSR. For A7000, normal F4 enable returns FA, move (10,7) delivers packet 28/0A/F9; a later Select press delivers 09/00/00 (no repeated motion), and reset clears the device queue.

The authored DMA workload has 1,114 setup instructions then a six-instruction loop (counter, framebuffer STR, sound-buffer STR, offset increment, mask, branch). At 100 M instructions/s after 25 ms, exactly 2,500,000 instructions have executed: counter 416,481; framebuffer and sound-buffer word zero 415,745; offset `(416481*4)&4092`. The initial sound DMA snapshot precedes the CPU writes, so audio must be tested after a complete 1024/44100-second input block, rather than expecting nonzero samples in the first 10 ms. Through 25 ms the fixed output includes timestamp zero: 1,201 stereo samples, with the final left sample nonzero. Indexed pixel zero must be the low byte of 415,745 mapped through the grayscale palette. A7000 displays DRAM; the RiscPC models display VRAM.

For the distinct stereo stream, RAM word i contains left -1000+i, right 1000+i. At 48 kHz through 10 ms output samples 0..480 equal those input values. A change to 24 kHz at 10 ms preserves the already scheduled next sample; the following 480 output pairs begin at (-519,1481) and end at (-280,1720). CPU is idle throughout so guest writes cannot replace this fixture. This tests a rate transition with different adjacent samples, rather than just counting a constant signal.

Horizontal display endpoints have a reserved low bit; the valid width 322 × 257, unaligned to a 32-pixel conversion chunk, uses an internal pitch of 352 pixels while reporting exact geometry. The first four indexed pixels are 0/1/2/3. A7000 uses SIMM bank 1 at offset 12 MiB (physical 0x14c00000); writing 0x04040404 there must change pixel zero to palette entry 4 without an out-of-bounds dirty-buffer access. These deliberately avoid the low VRAM addresses and aligned widths of the ordinary mode fixtures.

Controller cases (authored guest ARM code, no firmware): a synthetic 160-side
DD5 JFD contains 800 unique 1024-byte records. Byte i of sector k is
`(i + 16*k)&255`, CHR comes from physical track/side and descriptor ID, N=3.
The guest programs DOR, waits past its 50 us reset, senses reset, then issues
commands through Super I/O, polls IOMD FIQ status, and consumes/provides DMA
bytes; it reads the seven result bytes through the ordinary FIFO.

* READ ID encounters the first future ID after the reset/sense delay (ID 1 at 36,352 us, since ID 0 at phase zero has passed), and returns 00/00/00/00/00/01/03 and no DMA payload.
* Reading sector 0 returns 1024 exact bytes; reading sectors 0..1 returns 2048
  exact bytes, including the different start byte of sector 1. Final DMA uses
  terminal count. ST0/1/2 all zero; no stale DMA after completion.
* An absent ID or mismatched N on a track with visible IDs returns ST1=04;
  a wrong density on this DD track returns ST1=05; wrong cylinder returns
  ST1=04, ST2=10. Search ends at the second index, bounded by 400 ms.
* Header CRC failure transfers no bytes, ST1=20, ST2=00. Data CRC failure
  transfers the payload, then ST1=20, ST2=20. N remains 3 in either result.
* RW sector write commits exactly one RAM replacement with bytes 55,56,...;
  WP or deleted-data write refuses synchronously with ST1=02, no DMA, no
  replacement. A short write using TC before byte 1024 gives overrun ST1=10
  and commits nothing. Floppy input bytes never change.
* Eject in the middle of a read completes not-found; reset cancels without
  retaining DMA/IRQ/data or command callbacks. Advancing further cannot
  deliver the cancelled record or commit a partial write.
# Passive mode observer

An authored section maps VA 00200000 to six independently specified words
at PA 10100000 (1, 640, 480, 3, 60, FFFFFFFF). An X OS_ScreenMode request must log all six words; an
unmapped selector must log zero words. Neither request may alter guest
registers, event/abort state, FAR/FSR or the translation-cache miss count.
The same selector is logged with the translation table in fitted VRAM.
This is a diagnostic observer, not a guest memory access.

Public lifecycle tests refuse a 255-byte CMOS, a mismatched digest, and a
factory-reset request supplying a nonempty baseline. An independently
hashed 256-byte authored zero image is accepted. Firmware loads without a
factory-generated baseline are refused; authored tests are exempt.

Run-4 authored helper expectations (ordinary guest FDC commands on drive 1):
Video timing correction expectations come from ARM VIDC20 data sheet 4.1.15
and ARM7500 DDI0050C 9.16: progressive N lines are programmed as VCR=N-2.
At 8 MHz, HCR=504 and VCR=310 produce 512*312/8000000 = 19,968,000 ns;
with 256 displayed lines, 56 blank lines take 3,584,000 ns. These expectations
apply to all four model mechanisms, independently of any private MDF/game.

A spinning drive-1 helper produces an index pulse at 200 ms even with no
FDC read command (ADFS waits for index before identification). READ ID returns C=H=0, R=1 at the first 12.5 ms slot after command issue,
N=1 (256 bytes), MFM DD. Reading sector 2 returns the project-authored old
FileCore root (`Hugo`, !Boot, Multisync entries). Sector 7 contains an Obey
file invoking `LoadModeFile ADFS::1.$.Multisync`; a two-sector READ DATA of
sectors 7..8 transfers all 512 bytes and includes the ordinary BASIC invocation
of the project-authored BootEnv startup. Its Choices/Tasks declarations are
separately exercised by the title-neutral firmware query. Intel 8272 READ DATA result table (MT=0): TC
at EOT=8 returns C=1,H=0,R=1,N=1; TC after sector 8 before EOT=15 returns
C=0,H=0,R=9,N=1. Source: Intel 8272 specification reproduced at
https://cpctech.cpcwiki.de/docs/i8272/8272sp.htm . This distinguishes media
CHRN during transfer (7,8) from the controller's post-transfer result ID.
A WRITE DATA command ends
with ST1 write-protect, transfers no byte and leaves DMA clear. Hardware
FDC reset cancels the transfer with no later DMA or completion. No private
firmware/content is used for these assertions. Successful terminal-count
results retain ST0 unit=1 and head=0. READ DATA R=16 is outside the authored
0..15 sector set: not-found ST0=0x41, nonzero ST1, no DMA bytes; it must never
be confused with READ ID's internal sentinel.

## Run-5 standard-disc IDE bounds

The enlarged trial disc is independently declared as 63 sectors per track,
16 heads and 256 cylinders: 258,048 sectors, exactly 132,120,576 bytes.
ATA IDENTIFY must report those CHS values and capacity words 60/61 = 61,440/3.
An authored guest writes two sectors starting at LBA 100 with 512 incrementing
words beginning at 0x3157; a separate cold clone must read all words unchanged.
LBA 258,048 is outside the image and must return READY|ERR without host growth.
Truncating the owned test backing to 512 bytes must cause a detected short-read
session failure, never zero-filled successful completion. No private bytes
are used by this test. The standard boot image must separately be catalogued
and booted through firmware; ATA readback alone does not prove FileCore validity.

## Run-8 drive ownership and disk-change expectations (declared before fix)

All commands below are authored ARM code through Super I/O/IOMD/FIFO/DMA;
media changes use the same disk-control entry points as the frontend. Two
authored DD5 images differ in every payload byte by 0x40. No private input.

* Drive 1 READ ID, issued after reset/sense and still pending at 1 ms,
  encounters helper R=1 at 12.5 ms. Swapping drive 0 at 1 ms must leave
  drive 1 command state, DOR motor bits, IRQ/DMA and deadline unchanged:
  result 01/00/00/00/00/01/01, identical guest state at 20 ms to no swap.
  Eight repeated content swaps before that ID must have the same result.
  An empty helper search must still fail normally after 400 ms, not when
  the other drive swaps.
* Drive 0 sector-0 read at 201 ms has transferred a strict prefix. Swapping
  it cancels that command with abnormal termination (ST0=40, ST1=05),
  leaves a nonzero remaining byte count, and drops pending DMA/callback.
  Later polling cannot transfer additional bytes from either medium into
  that command. An early swap before a new command instead reads the
  replacement image: exactly 1024 bytes (i+0x40)&255, normal completion.
* DIR bit 7 reports only the DOR-selected drive's active-low nDSKCHG
  input inverted. DOR drive select and motor enables are independent.
  Reading DIR, selecting a drive, controller reset and a SEEK to its
  current cylinder do not clear the latch. A SEEK that issues a step
  clears only that selected drive's latch, only with media inserted.
  A step with no disc cannot clear change. Insertion retains change
  until a later step; repeated swaps set drive 0 only.

Sources: SMSC FDC37C665GT/666GT specification, pp. 8 (nDSKCHG/nSTEP),
27-30 (DOR drive/motor decode), 36 (DIR inversion), 69 (SEEK issues steps
only if PCN differs from NCN):
https://bitsavers.org/components/standardMicrosystems/_SMSC/_dataSheets/FDC37C665GT_SuperIO.pdf .
TEAC FD-235HG-C805/C817/C897 specification §8.3.12: change persists until a disc is
installed and a STEP command is received with DRIVE SELECT true:
https://ftpmirror.infania.net/sites/bitsavers/pdf/teac/floppy/FD235/FD-235HG-C805_C817_C897_Specification.pdf .
These drive-interface rules do not imply a guest cache flush or forced retry.

Run-9 seek identity expectations, stated before changing the controller:
guest SEEK on unit 1 to cylinder 5, with H=1 in its parameter, followed by
SENSE INTERRUPT STATUS returns ST0=21 and PCN=05. Unit 0 SEEK to 9 returns
20/09; unit 1 RECALIBRATE returns 21/00; unit 0 RECALIBRATE returns 20/00.
Every completion has H=0. These alternate two units through ordinary
DOR/FIFO accesses; no firmware or content is involved. SMSC
FDC37C665GT/666GT, Table 17 (p.39) assigns ST0 bits 1:0 to the drive;
Seek/Sense Interrupt Status (pp.69-70) specifies SE and the zero H bit.
Source: https://bitsavers.org/components/standardMicrosystems/_SMSC/_dataSheets/FDC37C665GT_SuperIO.pdf .

The same two-unit program queries SENSE DRIVE STATUS after unit 1 seeks to
5: unit 0 is still at 0 (T0=1), unit 1 is at 5 (T0=0). After unit 0 seeks
to 9 and unit 1 recalibrates, T0 is respectively 0/1. Check DS and T0 with
mask 0x13; expect 0x10/0x01/0x00/0x11. Table 20 (p.42) defines T0 from that drive's
TRK0 input; another drive's operation cannot change it.

Run-10 CMOS ownership expectations, stated before the new assertions: an
authored, hashed 256-byte baseline with ADFS configuration byte 0x41
(one floppy and one IDE disc) must retain that byte across cold machine
reset even when no IDE backing is enabled in the mechanism harness. This
tests that reset no longer substitutes host attachment state for guest
configuration. Capturing firmware factory reset still starts with exactly
zeroed CMOS; RTC epoch and the per-model mouse device contract are unchanged.
