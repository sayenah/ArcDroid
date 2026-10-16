<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Write-protect Tab

For a JFD game that asks for a writable boot floppy, set **Write-protect Tab** to **Off** and reset or reload. Save the choice per game through RetroArch's core-option manager. Changes go to ArcDroid's save folder; the original image stays untouched. **Disk Writes: Off** still blocks writes.

Keep the default **As imaged** for games that require a write-protected floppy. Returning to As imaged hides saved disc changes on images that originally forbid writes; choosing Off again restores them.

The option affects ArcDroid's native JFD reader. An ADFFS boot does not keep these writes; select **JFD Boot: Native** when needed. ADF and ADL images are unchanged.
