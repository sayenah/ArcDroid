<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Memory-system calibration additions (runs E–G)

`rpc_timing_oracle.py` consumes declared model, rate, address and instruction
counts only. It imports no core and reads no core output. The original 4,127
rational expectations remain unchanged. Cache refill counts are now computed
from the documented cyclic set occupancy, including fetches before and after
each load. The run-E oracle incorrectly grouped ARM710/7500 as 64-way caches;
run F corrects this to contract §4.1: ARM610 4×64, ARM710 128×4 and ARM7500
64×4, all with 16-byte lines. SA has separate I/D caches, 16×32 with 32-byte
lines; data does not evict its instruction tags. Existing stream 1/0/0/1 and
random 4/2/0/4 refills at K=1024 coincide under the old/new unified geometries.
All 4,671 pre-F rational expectations remain unchanged.

Run F adds one `cache-geometry` ROM interval per unified model. Code begins at
VA 0x10001010 and spans sets 1/2; it is warmed by executing six instructions
(eight on ARM610)
with V=1/Z=1, so conditional loads/arithmetic and the loop branch fail and no
data tag is allocated. The measured three-instruction loop visits N distinct
data lines in set zero at VA 0x20100000+i*stride, then probes the second and
first lines, in that order. ARM610 additionally probes the second and fourth
lines, distinguishing its geometry from a same-capacity 64×4 cache.
ADDS 0x80000000+0x80000000 establishes warm flags;
CMP 0,#1 establishes measured V=0/Z=0. SUBSVC decrements a positive count
without overflow, so all VC probes execute even after the final SUBS sets Z.
Warm/setup instructions and the return are outside the measured interval.

| Model | N | Stride (sets×16) | Measured instructions (3N+probes) | Fills | Exact duration, ns |
|---|---:|---:|---:|---:|---:|
| ARM610 | 65 | 64 | 199 | 67 | 131925/4 |
| ARM710 | 5 | 2048 | 17 | 6 | 35675/14 |
| ARM7500 | 5 | 1024 | 17 | 6 | 35675/14 |

Every fill costs (5+2+2+2)×31.25=343.75 ns; ARM7500 setup selects CLKCTRL=7.
At ways+1 lines the first is evicted and the second retained. Hits leave the
cyclic cursor unchanged, so probing second hits, then probing first misses:
N+1 fills on ARM710/7500. On ARM610 that first miss evicts the second line;
its extra second probe misses, while the fourth hits, giving N+2=67 fills.
ARM610 with 4 or 63 ways gives 68 fills, with 65 ways 65 fills, and with 64×4
or 8×32 gives 66 fills, discriminating both its way boundary and geometry.
M39/M40's 64-way geometries retain all five lines: five fills and 61725/28 ns,
343.75 ns too short. The corrected oracle simulates every code/data lookup and reset cursor
independently; no core source, output or measured duration supplies a result.

Run G adds three same-capacity geometry probes. Every one of the 4,674 pre-G
rational expectations and guest instruction streams/ordinals is unchanged.

| Probe | Model | Distinct lines | Stride | Measured instructions | Fills | Exact duration, ns |
|---|---|---:|---:|---:|---:|---:|
| `cache-geometry-fewer-ways` | ARM7500 | 3 | 2048 | 10 | 3 | 36875/28 |
| `cache-geometry-sa-data` | SA-110 D | 17 | 1024 | 52 | 17 | 24415/2 |
| `cache-geometry-sa-instruction` | SA-110 I | 17 | 1024 | 18 | 17 | 23735/2 |

The two data probes use the same conditional warm/three-instruction loop,
then read the first line once. ARM7500's 2048-byte stride is 128 16-byte
lines: all three data lines occupy set zero in both 64×4 and G2's 128×2.
Three lines fit four ways, but the third evicts the first with two ways;
the final probe adds one fill under G2. Code remains in sets 1/2.

SA's D probe enables M/C/I with C=1 RAM descriptors and clock switching in
unmeasured setup. Its five-instruction warm body at VA 0x10001020 fits one
32-byte I line; failed VC data accesses leave D cold. The 1024-byte data
stride is 32 32-byte lines: set zero under both 16×32 and G8's 32×16.
Seventeen lines fit 32 ways, while the seventeenth evicts the first with
16 ways. The first-line probe adds one D fill only under G8.

SA's I probe has D disabled, M/I enabled and clock switching enabled. It
starts cold at VA 0x10008020, follows 17 authored unconditional RAM branches
1024 bytes apart, then executes the first once more. All lines occupy set
one under either geometry; ROM setup has descriptor C=0 and allocates no
I tags. Seventeen lines fit 32 ways; G9's 16 ways evict the first before
its revisit. The branches are outside the translation-table storage.

Durations use contract §§2.4/4.1/5: base=1000/35 ns on ARM7500 and 10 ns
on switched SA; each unified fill is 343.75 ns, each SA fill consists of
two standard-mode four-word bursts, 2×343.75=687.5 ns. Thus G2 adds
343.75 ns (mutated duration 23250/14), and G8/G9 add 687.5 ns (12895 and
12555 ns). The oracle models declared independent I/D occupancy, reset
cursors and every measured lookup; neither core output nor core source
supplies any expected duration. There are now 4,677 calibration intervals.

Small scalar streaming/random runs use K=1/2/8/9/32 with the existing three/four
instruction cadence. New buffer-boundary guests execute conditional stores with
Z=1 to warm all instruction lines, then repeat with Z=0 and an empty FIFO.
Unmeasured setup changes no transfer cadence. Consecutive nonadjacent STRs use
K=1/2/3/5/8/9/32; each has a CPU-retirement and a final-drain expectation. The
independent FIFO recurrence retains address slots and data words until each
packet's final word, exposing ARM610's third, ARM710/7500's fifth and SA's ninth
address-slot stalls. STM8 followed immediately by STR exposes older models'
ninth data-word stall; SA uses its two four-word packets. M02–M05 and M38 are
killed by retirement, even where drained endpoints remain bus-bound.

SA clock-disable guests warm code by execution. Disable plus 1024 hot branches
costs 1025 fast bases. Disable, one C=0 load, then 1024 hot branches costs two
fast bases + N + 1024 MCLK bases: 64,176.25 ns at 100 MIPS. This independently
separates disable (M10) from the first external read miss (M08). An M=0, I=1
ROM branch loop includes one eight-word ROM fill plus 1024 fast bases:
11,990 ns; the clock-disable pair also runs at test rate 20 without changing
its instruction cadence. M23's suppressed allocation instead emits 1024 external words.
LDM/STM4 starting at bank-local offset 0x3f8 crosses the 1 KiB boundary without
crossing a bank: older models use N,S,N,S (14q); SA's uncached words use 20q.
These expose M14 independently of bank transitions.

The request-level authored opcode harness supplies one contained DRAM quad
hold and one VRAM half-SAM hold at a declared device edge (350+156 ns). Cached
ALU/LDM sequences have no CPU bus grant and take K*b+350D+156V. A cache-hit
SA STM also has no grant; older unified STM still writes through and adds its
four-word drain. Full-FIFO cases queue the hold during the first scalar grant;
backpressure retires the next store at b+N+506 ns, and final drain is
b+(capacity+1)*N+506 ns. Idle drains an unchanged instruction count in
b+capacity*N+506 ns, then wakes with one fresh base. Every model is checked.
Seven additional actual raster-ROM intervals execute one million cache-hot
ALU instructions with DRAM video on every model, and two-bank VRAM on the
three RiscPC models. `rpc_hot_dma_rom.py` independently walks uncached setup,
one executed code fill and the declared raster deadlines, then sums contained
holds across the hot interval. No CPU data/fetch service occurs in that interval.
M21 is killed both by these real ROM intervals and the request-level boundaries.
The original raster-ROM suite also verifies cursor-generated holds.

All rational ROM intervals compare exact integer ledger ticks (stronger than
≤1 ns). Drained request-level observations permit strictly less than one ns
for the externally visible ceiling. Counts, packet occupancy and cache fills
are exact. The mutation driver first verifies every unmodified check passes,
then compiles and links isolated replacements using the build's actual flags;
compile/link errors and timeouts never count as kills.
