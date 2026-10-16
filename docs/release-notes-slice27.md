<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Slice 27 — left stick controls

Each controller’s left analog stick now acts as that port’s D-pad by default.
Directions follow the same Pad Mapping choices, including custom keys, Unbound
and the A3010 joystick. Diagonals hold two directions. A direction starts at
half throw and releases below 40% throw; centre the stick after loading,
resetting or changing its routing. The right stick continues to move the pointer.

**Pad 1 Left Stick** and **Pad 2 Left Stick** appear under **Pad Mapping**:
**D-pad** is the default; **Off** stops that port’s left stick from controlling
gameplay or navigating the on-screen keyboard. The launch menu and Pointer
Stick keep using it as before, as do existing checks for player input and for
released controls. Save Game Options in RetroArch saves these choices per game.
With Pointer Stick set to Left, Pad 1’s left stick belongs to the pointer.
Otherwise an eligible Pad 1 left stick also navigates the on-screen keyboard,
with the D-pad’s press and repeat behaviour.

ArcDroid uses the analog and digital values delivered by the frontend and does
not change RetroArch settings. Non-forced Analog to Digital becomes inactive on
a port once the core reads its stick, including Pad 2. Forced-left delivers
D-pad presses and zero left-stick values, so the core’s stick conversion is
inert. Forced-right converts the right stick to D-pad presses while ArcDroid
converts the left stick: both contributions combine, and the pointer loses its
right-stick input as a result of that frontend choice. “Same as the D-pad” means
the logical direction’s ArcDroid binding; a RetroArch remap of a physical D-pad
button does not remap the core’s analog direction conversion.
