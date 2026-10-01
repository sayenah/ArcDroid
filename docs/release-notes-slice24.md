# Slice 24 release notes

Automatic input layout selection and the `elite` preset are removed. Every
content item now starts with the same default RetroPad mapping unless the
player has saved per-input Pad Mapping options.

To reproduce the former layout, open Quick Menu → Core Options → Pad Mapping
and set Pad 1 Up / Down / Left / Right / A / B / X / Y / Start / Select to
S / X / comma / period / Space / slash / A / M / F1 / T. Set Pad 2 the same
way if the device presents its controls across two ports. Then use Manage Core
Options → Save Game Options. Inside the application's Preferences, turn Mouse
off for key flight.

The on-screen keyboard and pointer remain available. Stale profile option
entries are harmless and need no removal. Explicit per-input settings survive.
Save Game Options stores all currently effective core-option values for that
content, including unrelated options; later core-wide changes do not propagate
into that file.
