# R1 D1 independent video expectations

The committed generator emits authored ARM instructions, never firmware. All
register, palette, cursor and memory changes execute through the real test
core's interpreter/MMIO bus on each of ARM610, ARM710, SA-110 and ARM7500.
No test changes a device register from the host. Expected colours, coordinates
and counts below are derived from packed data and declared geometry, not an
upstream renderer or captured implementation output.

The leading scheduler edge samples an entire line's registers and memory.
Events at an instruction timestamp precede that instruction. A subsequent
write cannot change stored pixels from that line. This is line accuracy, not
pixel-cycle accuracy. Timing writes preserve the next already scheduled edge;
its successor uses the new period. HCR encodes horizontal period minus 8;
progressive VCR encodes vertical period minus 2. RiscPC RCLK is 24 MHz;
A7000 RCLK is 32 MHz, synthesised by 3/4 for the same 24 MHz fixtures. Pixel
clock ratios and periods are integer/rational; reported rate is the programmed
ratio, not the reciprocal of rounded elapsed nanoseconds.

Primary references: [ARM VIDC20 DDI 0030E](https://www.bitsavers.org/components/arm/VIDC20_Datasheet_199502.pdf),
[ARM7500 DDI 0050C](https://www.bitsavers.org/components/arm/DDI_0050C_ARM7500_Data_Sheet_199510.pdf)
sections 9–11, and [IOMD functional specification issue 4](https://www.bitsavers.org/components/arm/IOMD_Functional_Specification_199406.pdf)
sections 4 and 7. Native board clock choices follow the trial's documented
board model; no unconnected HCLK frequency is invented.

## Uniform fields and depth

- 320×256, 640×480 and 800×600 each use exactly 50 and 60 Hz. 320/640 use
  24 MHz and 800 clocks/line, 600 or 500 lines/field; 800 uses 48 MHz and
  1000 clocks/line, 960 or 800 lines/field. All six produce opaque grey 0x55.
- Each supported depth (control codes 0,1,2,3,4,6: 1,2,4,8,16,32 bits)
  scans 32×8 pixels. Memory words contain 0x55555555, least-significant bits
  first. Palette entry i has R=G=B=i. At 1 bpp the pixels alternate 1 and 0;
  at 2 bpp all are 1, at 4 bpp all 5, at 8/16/32 all 85. In 16 bpp the
  three lookup indices are bits 7:0, 11:4, 15:8; in 32 bpp they are 7:0,
  15:8, 23:16. No hard-coded RGB555 conversion substitutes for the palette.

## Splits, ownership and DMA

32×8 split fixtures use 100 us lines, VDSR=2, VDER=10: active rasters 3–10.
A guest timer expires 650 us after the clock starts, between active rows 3
and 4, with enough margin for guest bus instructions on all four rates.

- Palette 85 becomes blue: first four rows grey, final four blue.
- Video DMA disabled: border changes red to green at the same split.
- Cursor initially covers all 32 columns in red. Its palette becomes green
  and its X moves from 0 to 4: final four rows have four grey columns followed
  by green. Cursor pattern is 2 bpp index 1 in guest DRAM.
- Rewriting display memory cannot erase the first four grey rows. Later rows
  read zero and become black.
- VIDCUR changes to a second region of 0xAAAAAAAA: final four rows grey 170.
  VIDINIT changes to that region only take effect on flyback, so the first
  field stays grey 85 and the second starts grey 170.
- Ring START/END change after row 3: row 4 ends at byte 160 and wraps; rows
  5–7 use the second region. This ring-write fixture uses DRAM on all models.
  A 4096-byte ring uses END=base+4096-16 in DRAM
  or base+4096-2048 in the accepted two-bank VRAM configuration.
- Depth becomes 4 bpp: first four rows grey 85, final four grey 5. A delayed
  VDSR=7 blanks physical row 4 in the current green border, then resumes at
  row 5 with INIT reloaded. Flyback rises once on entry, falls once on resume;
  clearing an IRQ while blank cannot reassert it without a new edge.
- A two-column DRAM fixture consumes six bits across its first three 1 bpp
  rows, then changes to 16 bpp. Packed 0x76543210 words produce first changed
  pixel 0xffc80c50. Later pixels cross the single-quad ring boundary without
  losing partial-byte bits: 16 guest pixels and exactly one wrap per field.
- HCR doubles after row 3: the pending 700 us edge remains; subsequent line
  durations double. The resulting programmed field rate is 125 Hz.
- RiscPC interlace uses fields of VCR+1.5 lines and publishes field-sized
  images; this is not a deinterlacer. The half-line participates in rational
  timing. ARM7500 ignores the reserved INT bit. HIR concerns sync phase and
  does not move active samples at line resolution.

## Clipping and contention

The viewport is fixed at the active top-left at field start. Mid-field growth
extends its right/bottom extent; completed samples are retained. Presentation
stores at most 2048×2048. This bound never limits guest width/height or DMA.

- 2064×8: all 16512 guest pixels consume data; four 4096-byte wraps occur.
  After four active lines DRAM VIDCUR=base+64; VRAM VIDCUR=base,
  with 64 bytes already consumed from the current half-SAM. The final stored column of row 7
  remains grey. The next field's first row also remains grey.
- 32×2056: all 65792 guest pixels consume data, including the eight hidden
  bottom rows; 16 wraps occur. Just before flyback DRAM VIDCUR=base+256; VRAM VIDCUR=base, with
  256 bytes consumed from its current half-SAM. The last
  stored row and the next field's first row remain grey.
- Width grows from 32 to 2064 at row 4: guest consumption is 8384 pixels,
  with two wraps. Prior rows keep 32 grey columns and black sampled border;
  later rows fill all 2048 stored columns. One clipping log per reset/session,
  including across subsequent fields.
- Powerdown suppresses data/DMA, but counts lines and fields, toggles flyback
  and stores bounded border output. It cannot hang run_frame.
- The deliberately line-granular bus model aggregates transfer occupancy at
  each sampling edge: 350 ns per DRAM quadword, 156 ns per two-bank VRAM
  half-SAM copy (IOMD section 7), cursor quadword 350 ns every two rows.
  The contention tests also compare interpreter instruction counts with virtual
  elapsed time: lost credit must equal transfer occupancy within one rounding
  instruction per sampled line; disabled data must lose no credit. The model
  carries CPU blockage across scheduler boundaries without moving device time
  or accruing catch-up CPU credit. This is not a cycle-accurate bus-arbitration
  claim. Every hidden column/row contributes equally to this occupancy.

## Frontend and bounds

Only completed fields are published. An initial full AV notification is followed
by one full AV notification for a 60→50 Hz change (including its geometry)
and one geometry-only notification for the later 640→800 change. Rejected
notifications do not repeat every frame and do not stop rendering. Missing
callbacks are permitted. Installing a replacement environment callback allows
one new attempt. A 50→49.993334 Hz change is below the 0.01 Hz hysteresis:
no restart; cumulative changes compare against the last attempted rate.

VRAM INIT in the last half-SAM, with its last-transfer flag set, wraps
correctly after 2048 serial bytes. A two-pixel 1 bpp DRAM fixture with HDWR=1
pads each line to 32 bits: VIDCUR advances 16 bytes after four lines, 32 after
eight, regardless of the two stored pixels. Goldens are regenerated by
`python3 tests/rpc_video_goldens.py`, which computes declared colours and FNV
hashes without running the core.

One lazily allocated 16 MiB buffer plus 2048-byte row flags and 8192-byte row
border history. Fault injection covers its allocation under the live session
boundary; reset reuses it and stop frees it. Each guest advance is bounded to
16,666,667 ns, 1,666,667 instructions and 131072 events and 33,554,432 sampled pixels. run_frame makes at
most six such advances (100,000,002 ns), returning at the first field boundary.
No frontend callback occurs on the protected guest stack. The frontend's
existing presentation copy owns pixels beyond that return/next engine entry.
