# Run E memory-system calibration additions

`rpc_timing_oracle.py` consumes declared model, rate, address and instruction
counts only. It imports no core and reads no core output. The original 4,127
rational expectations remain unchanged. Cache refill counts are now computed
from the documented cyclic set occupancy, including fetches before and after
each load: stream 1/0/0/1 and random 4/2/0/4 refills at K=1024.

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
