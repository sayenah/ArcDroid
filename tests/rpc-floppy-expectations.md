# R2b authored floppy expectations

Authority: SMSC FDC37C665GT datasheet, printed pages 43–45, command tables on
46–61, data commands and result tables on 62–68, controls on 69–75. The
repository-local evidence is not an artifact or fixture. No private firmware,
captured output or trial implementation is an oracle.

The physical drive is DOR bits 1:0. The command's unit addresses controller
PCN/status, and HDS selects the physical head. Requested C/H/R/N compare with
IDs on that physical track. A seek with unit 1 and DOR 0 changes PCN1 and drive
0, leaving PCN0 and drive 1 alone. Changing the requested H without changing
HDS produces no data; it must not redirect the read. Wrong-cylinder applies
only to C mismatch (status table 19, p.41). Error searches retain head-load
and rotational ID deadlines.

Table 27 advances the transferred CHRN: below EOT, R increments; at EOT,
R becomes 1 and C increments without MT. With MT, head 0 continues on head 1,
R=1; completing head 1 increments C and complements H. TC uses the same table.
The TC byte is consumed even if queued in the FIFO. A complete JFD sector
publishes one store generation, including final-byte TC. Early TC discards an
incomplete JFD sector; raw early TC writes zeros to the remainder. Earlier full
sectors persist. Reset, eject, replacement, failed insert and close prevent a
pending partial sector from publishing on either drive.

VERSION is 0x90. DUMPREG returns ten bytes, including four separate PCNs,
SPECIFY, EOT, LOCK/PERPENDICULAR, CONFIGURE and PRETRK. LOCK retains FIFO and
PRETRK through software reset; EIS/POLL reset. Software reset preserves drive
designations while clearing perpendicular GAP/WGATE. Relative seeks wrap PCN,
with physical movement kept separate. SCAN and reserved encodings return 0x80.
VERIFY never raises a host data request. READ TRACK begins at index, including
after an implied seek, returns
rotational sector data and accumulates ID/data CRC errors. FORMAT accepts N,
SC, GPL, fill and supplied tuples. D-143 accepts a raw track's complete ID set
in any order, with exact physical C/H and geometry N; each sector receives the
fill at its fixed offset. Duplicate, missing, out-of-range or mismatched IDs
and wrong counts give write protect before any bytes change. Raw images cannot
store the supplied skew/interleave: later reads retain the geometry's sequential
rotational timing. JFD FORMAT/deleted writes are protected. Normal data read by
READ DELETED has CM; SK skips such data.

In the default AT interface, DOR bit 3 gates FINTR/DRQ and ignores nDACK/TC
while disabled (p.28). Re-enabling exposes a still-pending interrupt/service.
ND service and command completion retain separate interrupt conditions.

FIFO has sixteen bytes when enabled and one when disabled. Reads request
service at 16-threshold bytes or at the sector end, remaining active until
empty. Writes request service until full, resume at threshold, and stop
requesting after the TC byte is queued. Non-DMA service uses RQM/FINT, with
NDMA set in execution MSR; FIQ DMA is absent. RQM/DIO together qualify result
reads. An execution-phase DIO alone does not indicate results.

`rpc_fdc_test` checks the controller and typed drive boundary with an authored
RAM VFS and integer scheduler. `rpc-floppy-guest` executes authored ARM ROMs
through SuperIO, IOMD and FIQ DMA. Raw data is independently constructed from
geometry. JFDs use the established authored fixture encoders and shipping
preparation/store code. Policy expectations are the D-136 matrix: Disk Writes
Off always protects; otherwise header 2 or tab Off authorizes eligible JFD
sectors. A map veto gives data CRC; store failure gives write protect. Cross
backend checks compare full snapshot bytes and have WD1770, 82C711 and RPC
read each other's committed store. RPC flat/burst traces must be identical on all four CPU models. Live tab and
Disk Writes changes remain pending through disk-control reinsertion; reset
samples the new policy and re-stages Shadow/Original raw ownership.

The R1 device guest previously polled DIO alone. Its encoding now requires
RQM and DIO; its expected data and golden remain unchanged. The R1 mechanical
probe now writes DOR for each physical drive, and includes the 80-pulse absent
drive recalibration duration in its horizon. These are workload corrections.

## Plan v2 media keys

Existing v1 and v2 CMOS keys keep their meanings. Only v2 accepts:

- `media=PATH`: shipping content preparation, including raw, JFD, zip and m3u.
- `drive1=PATH`: a raw helper prepared with the same Disk Writes staging.
- `disk_control=PATH`: bounded ASCII disk-control script.

Paths resolve against the declared system directory, as existing plan paths
do. Script rows are `FRAME eject`, `FRAME insert`, `FRAME select INDEX`,
`FRAME replace INDEX PATH`, `FRAME remove INDEX`, or `FRAME add`. FRAME is the
requested session's frontend frame ordinal (default generation remains
media-free); rows are nondecreasing and ties retain file order. Paths in
script rows are single tokens. Events call shipping libretro disk control and
report a refusal. Reset restarts the script and re-prepares mounted media with
the newly sampled policy. Teardown releases controller mounts before freeing
frontend JFD views. Shipping cores reject plans.
