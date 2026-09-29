"""Guest-visible power-on Shift checks for the authored KART boot ROMs."""

POWER_ON = "Power-on: boot-flag=clear shift=held"
RELEASE = "Power-on: shift released at="
DOWN = "KART device->guest values=C4,CC"
UP = "KART device->guest values=D4,DC"
ACK = "KART guest->device value=33 state=3"


def assert_guest_shift_each_construction(log: str, expected: int) -> None:
    lines = log.splitlines()
    starts = [i for i, line in enumerate(lines) if line == POWER_ON]
    assert len(starts) == expected, (expected, starts, log)
    for ordinal, start in enumerate(starts):
        segment = lines[start:starts[ordinal + 1] if ordinal + 1 < expected
                        else len(lines)]
        def at(marker: str) -> int:
            return next((i for i, line in enumerate(segment) if marker in line), -1)
        down, release, up = at(DOWN), at(RELEASE), at(UP)
        assert 0 <= down < release < up, (ordinal, down, release, up, segment)
        assert sum(ACK in line for line in segment) == 2, (ordinal, segment)
