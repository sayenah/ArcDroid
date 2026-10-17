<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Slice 10d: persistent saves for proven native JFD titles

Ten exact decoded-JFD identities now boot on ArcDroid's native path by default,
where Slice 10a retains their disc writes across sessions. The route needs a
single image, disc 1 of 1, and matching decoded SHA-1, size and JASPP game id.
It is based on per-title gameplay and saved-value reload evidence; other JFDs
retain their prior path.

`JFD Boot: ADFFS preferred` bypasses these routes and otherwise behaves like
Auto. An existing native save still takes precedence. See [ADFFS use](adffs.md)
for recovery steps if a title needs ADFFS instead.

Option changes on install titles take effect on reset; the machine moves to the A3020 when needed and otherwise changes only on reload. If Temporary needs a different machine, ArcDroid asks you to reload the content.

Routing remains evidence-limited. Titles whose saves could not be reached or
distinguished, titles that fail natively, and titles without saves remain on
their previous path. Two-application discs still use the one-time launch
chooser.
