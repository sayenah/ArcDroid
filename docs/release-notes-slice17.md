# Slice 17 — Native launch RAM disc

Native application launches through ArcDroid's desktop helper now request a 32 KiB RAM disc when RISC OS 3 has none. The request runs before the helper boots any application. An existing RAM disc is left at its current size, and a failed request does not stop the application launch.

The advanced **Native launch RAM disc** core option defaults to **Auto**. **Disabled** leaves the helper as it was before this slice. The RAM disc can add an icon to the desktop icon bar. Its contents are volatile: they survive floppy swaps within the running machine, but reset, recovery and content reload reconstruct the machine.

This applies to native desktop-helper launches on RISC OS 3.00, 3.10, 3.11 and 3.19. It does not run during ordinary self-boot, Launch: Desktop, ADFFS, install, RISC OS 2 or Arthur sessions. A self-boot session that later recovers through the native helper uses the option then. ArcDroid does not change CMOS to supply the RAM disc.
