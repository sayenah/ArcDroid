# Slice 25: controller answers for RISC OS error boxes

When a supported RISC OS 3.10, 3.11 or 3.19 `Wimp_ReportError` box reaches its
own input poll, Pad 1 A selects **OK** and Pad 1 B selects **Cancel**. A
short on-screen hint lists the buttons that the box offers. The selected
answer takes effect at the box's own key read; no guest keyboard or mouse
input is synthesized. Normal pad mappings resume after the box closes and
the buttons have been released.
If a physical key is already waiting in the guest's keyboard buffer when the
pad answer is taken, that key stays for whatever runs next.

This covers user-mode callers with standard flags and the measured modal
sequence. RISC OS 3.00, 2.x and Arthur, supervisor-mode callers, flags with
bit 5 or above, text or command-window reports that do not reach the modal
poll, and game-drawn prompts are outside this slice.
