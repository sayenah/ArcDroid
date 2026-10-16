# SPDX-License-Identifier: GPL-2.0-only
"""Compare the no-request guest frame trace with main 933c28f."""
import subprocess
import sys


def main() -> None:
    harness, core, rom, media, data = sys.argv[1:]
    run = subprocess.run(
        [harness, core, rom, media, data, "--idle-trace"],
        check=True, capture_output=True, text=True,
    )
    expected = "idle trace e1b332a8b3692a41 frames=330"
    assert run.stdout.strip() == expected, (run.stdout, run.stderr)
    print(run.stdout.strip())


if __name__ == "__main__":
    main()
