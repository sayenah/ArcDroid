#!/usr/bin/env python3
"""Independent vectors and every-position proof, including mutants. GPL-2.0-only."""
import sys
sys.dont_write_bytecode = True
import rpc_lockstep_schedule as s


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def proof(positions):
    for length in (1, 2, 7, 64, 200):
        for k in range(401):
            require(sorted(positions(k, length)) == list(range(length)), ('missing position', k, length))


def main():
    # Published SplitMix64 recurrence, evaluated by hand-independent known vectors.
    require([s.mix(x) for x in (0, 1, 2, 0xffffffffffffffff)] ==
            [0xe220a8397b1dcdaf, 0x910a2dec89025cc1, 0x975835de1c9756ce, 0xe4d971771b652c20], 'PRNG vectors')
    vectors=[((0,0,0),0x238275bc38fcbe91,False,True),
             ((1,0,7),0xde98b4be0f3785fd,False,False),
             ((71,3,200),0xd7c8dea25630db22,True,False),
             ((s.MASK,2,1),0xeabcfa2c623c5a59,False,True)]
    for arguments,value,p0,p3 in vectors:
        require(s.sample(*arguments)==value and s.enabled(*arguments,0)==p0 and s.enabled(*arguments,3)==p3,'schedule vectors')
    proof(lambda k, length: [s.position(k, length, shift) for shift in range(length)])
    for length in (1,2,7,64,200):
        offset=s.sample(71,3,0)%length
        for k in range(401):
            for shift in range(length):
                require(s.force_exit(71,3,k,length,shift)==((k+shift+offset)%length==length-1),'actual forced-exit cadence')
    keys = [(seed, epoch, k, phase) for seed in (0, 71) for epoch in (0, 3)
            for k in range(217) for phase in (0, 3)]
    expected = {key: (s.force_exit(*key[:3], 7, 2), s.enabled(*key)) for key in keys}
    require(all(expected[key] == (s.force_exit(*key[:3], 7, 2), s.enabled(*key))
                for key in reversed(keys)), 'call order changed schedule')
    require(all(s.force_exit(1, 2, k) for k in range(20)), 'every instruction')
    # Committed single-defect reference mutants; each must violate the public property.
    try:
        proof(lambda k, length: [s.position(k, length, shift) for shift in range(max(0, length-1))])
    except RuntimeError:
        print('killed schedule missing-position')
    else:
        raise RuntimeError('missing-position mutant survived')
    calls = iter(range(len(keys)))
    mutant = {key: s.enabled(key[0], key[1], next(calls), key[3]) for key in reversed(keys)}
    require(any(mutant[key] != expected[key][1] for key in keys), 'call-count mutant survived')
    print('killed schedule call-count; vectors/order and all positions for L=1,2,7,64,200 pass')


if __name__ == '__main__':
    main()
