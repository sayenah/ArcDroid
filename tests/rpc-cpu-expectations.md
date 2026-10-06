# Authored R1 component expectations

These are component checks, not a qualified RPCEmu session. `rpc_cpu_rom.py`
generates explicit ARM instructions as C arrays; no firmware or executable
guest asset is stored. The same test executes each on ARM610, ARM710, ARM7500
and SA-110. Assertions remain enabled in every build.

Five instructions at zero move the physical RAM base into R0, set R1 to 42,
store it, load it into R2, then add one. Expected PC is 20, R1 is 42, R2 is
43, RAM contains 42 and no event is pending, both in SVC26 and SVC32. MRC
CP15 ID returns 41560610, 41007100, 41027100 or 4401a102 respectively.
Executing a RAM MOV yields R3=7; replacing that instruction with MOV 9 and
executing it again yields 9. LDRH is an ARMv4 instruction: SA-110 returns
1234 from abcd1234; the ARMv3 models enter Undefined. Reading unsupported
CP15 c9 enters Undefined without overwriting R4. Its LR is 4 and vector is 4.

The AP/S/R matrix is stated in `permission_tests` independently of the
implementation. Supervisor-only AP1 permits supervisor reads/writes and
denies User reads/writes. AP2 grants User reads only. AP3 grants all four.
For AP0: S/R=00 denies all, 01 grants supervisor reads, 10 grants all reads,
11 is treated as no access. Manager domains bypass AP; domain 00 and 10
fault. All sixteen domain fields are exercised. Section faults are 5/9/d
for translation/domain/permission; page faults are 7/b/f. FAR names the
original virtual access, including its offset; FSR includes the domain.

Small pages have four 1 KiB AP fields; large pages have four 16 KiB AP fields.
The test descriptors use APs 3,2,1,0 in that order. User reads succeed only
in the first two; writes only in the first. Type 0 and reserved type 3
descriptors produce translation faults. Privileged cached reads cannot
authorize User accesses, byte accesses or LDRT/STRT, and forbidden writes
leave the physical word unchanged. Descriptor mutations remain cached until
invalidation. ARMv3 c5 and SA-110 c8 invalidations, TTBR writes, DACR changes
and MMU disable are exercised. No translated host pointer bypass remains.

Guest LDR, LDRT and STRT permission faults enter ABT32 at vector 10, LR=8
for an instruction at zero, with SPSR equal to the source CPSR (10 or 13),
IRQ disabled, destination/memory unchanged, FSR=d and FAR=00400100. A fetch
from 004003fc succeeds, then the next instruction at 00400400 crosses into a
supervisor-only subpage and aborts at vector c, LR=00400404, SPSR=10. Prefetch
faults preserve FAR/FSR. Architectural faults clear the transient abort flag
and continue the interpreter; they never invoke the component fault boundary.

`fixtures/rpc-cpu.trace` encodes those guest observations in stable hexadecimal
fields. Its expected values were derived from the instructions and exception
rules, not captured from an emulator. It leaves explicit pending video/audio
fields. This is not the final CPU/device/IRQ/AV canonical session format.

Memory tests compare every byte of the 8 MiB aperture against repeated authored
2/4/8 MiB patterns, reject 6 MiB/NULL without loading, prove the two 32 MiB
RAM banks' physical aliases, and exercise A7000's absent VRAM. Each of the
three A7000 or four RiscPC memory allocation stages fails in turn; cleanup
twice clears every owned pointer. Fifty allocate/reset/free cycles per model
prove fresh CPU/CP15 state. These do not qualify whole-session allocation,
fault/reload, switching or handle growth.

CMOS is tested by bit-level I2C traffic, including a 256-byte authored pattern,
write/readback, register wrap, missing-address NACK, reset mid-byte and fresh
seed reload. Reset preserves volatile data and the write counter. RTC uses
epoch plus integer emulated nanoseconds. Tests cover Thursday 1970-01-01,
Tuesday 2000-02-29, non-leap 2100-03-01, the final centisecond of 2399 and
out-of-range rejection. A device RTC read at 2000-02-29 01:01:01.98 returns
BCD 98/01/01/01/29/42. Guest year bytes c0/c1 are not silently rewritten.
Factory Delete input/capture and the guest IOMD route remain session work.
