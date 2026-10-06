# Contained session expectations (C2)

The instruction arrays in `rpc_session_rom.py` are authored for these tests;
they contain no firmware. Guest RAM records results of MMIO and an IRQ handler.

- ARM610, ARM710, SA-110 and ARM7500 receive respectively 20, 35, 100 and 35
  instructions per microsecond. ARM610/710 continue executing; SA-110's CP15
  wait and ARM7500's SUSMODE suspend until a permitted interrupt. Suspended time
  receives no instruction credit. Eight milliseconds cannot exceed 160000,
  280000, 800000 and 280000 instructions respectively.
- Timer periods are `(latch+1)/2 MHz` (IOMD specification, section 4). GO0 with
  latch 399 and GO1 with latch 397, exactly one microsecond later, expire at the
  same timestamp. The order is timer0 then timer1. Both requests must reach the
  guest IRQ handler, whose accumulated request bits equal 0x60. Events due at
  an instruction timestamp precede that instruction; its MMIO takes effect
  immediately. The standalone scheduler tests check replacement before/at/after
  deadlines and fractional credit independently.
- Keyboard enable returns ACK 0xFA after one millisecond. An unsupported mouse
  command on ARM7500 returns RESEND 0xFE after five milliseconds through its
  integrated PS/2 registers. Both events occur once. RiscPC uses quadrature;
  its full host-input adapter remains for D. Reset must repeat the exchange
  without retaining queued data.
- Empty-drive READ ID completes asynchronously with ST0 0x40, ST1 0x05 and
  ST2 0. It never raises DMA. IDE status is zero, IDE data and podule halfword
  reads are 0xFFFF, EASI word reads are 0xFFFFFFFF. None terminates the session.
- The unrendered raster uses RCLK=24 MHz divided by six, HCR=92 (100 pixels),
  VCR=38 (40 lines), VDSR=1 and VDER=19. Lines take 25 microseconds; a field
  takes one millisecond. Flyback sets IRQA bit 3. This tests timing hooks, not
  scanout, DMA, interlace, geometry publication or pixels, which belong to D.
- Every allocation stage fails atomically with no retained ownership; invalid
  admission allocates nothing. An injected reset/execute fault and a real guest
  invalid-mode fault prevent further execution, produce silence, and recover
  after unload/load. Fifty load/unload cycles retain no owned allocations.
- One loaded library runs Arculator, every RPC model and Arculator again. Eight
  Arculator frames hash width, height, elapsed time, every framebuffer byte and
  drained PCM. Each return matches the initial trace and a separate fresh
  process using the same authored Arculator fixture. This fixture has no media
  and produces blank video/silent PCM; the required existing harness separately
  verifies rendered frames, audio, input and mounted media against its goldens.
- Shipping exports contain no test entry and the shipping registry rejects
  every private RPC model. No shipping route selects the RPC backend.

Hardware sources: [IOMD specification](https://www.bitsavers.org/components/arm/IOMD_Functional_Specification_199406.pdf),
[ARM7500 datasheet](https://documentation-service.arm.com/static/5ed11babca06a95ce53f8fee),
[SA-110 reference](https://bitsavers.trailing-edge.com/pdf/dec/semiconductor/arm/EC-QPWLC-TE_SA110_Technical_Reference_199610.pdf).
