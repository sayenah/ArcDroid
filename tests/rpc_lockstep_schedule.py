#!/usr/bin/env python3
"""Ordinal-keyed H-2 / CG-7 reference, contract v0. GPL-2.0-only."""
MASK = (1 << 64) - 1


def mix(value):
    value = (value + 0x9e3779b97f4a7c15) & MASK
    value = ((value ^ (value >> 30)) * 0xbf58476d1ce4e5b9) & MASK
    value = ((value ^ (value >> 27)) * 0x94d049bb133111eb) & MASK
    return value ^ (value >> 31)


def sample(seed, epoch, ordinal):
    if any(type(x) is not int or x < 0 or x > MASK for x in (seed, epoch, ordinal)):
        raise ValueError('schedule inputs must be u64')
    return mix(mix(mix(seed) ^ epoch) ^ ordinal)


def position(ordinal, length, shift):
    if ordinal < 0 or length < 1 or not 0 <= shift < length:
        raise ValueError('invalid cadence')
    return (ordinal + shift) % length


def force_exit(seed, epoch, ordinal, length=1, shift=0):
    # A seed-dependent permutation preserves the complete cadence family.
    return position(ordinal, length, (shift + sample(seed, epoch, 0) % length) % length) == length - 1


def enabled(seed, epoch, ordinal, phase):
    if phase not in (0, 3):
        raise ValueError('CG-7 decisions are P0/P3 only')
    return bool(mix(sample(seed, epoch, ordinal) ^ phase) & 1)


def ci_shifts(seed, epoch, length, count=4):
    if length < 1 or count < 1:
        raise ValueError('invalid sampling bound')
    start = sample(seed, epoch, 0) % length
    return tuple((start + i) % length for i in range(min(count, length)))
