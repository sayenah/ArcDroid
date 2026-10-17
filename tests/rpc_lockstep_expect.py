#!/usr/bin/env python3
"""Independent arithmetic/architectural expectations; never executes a core. GPL-2.0-only."""
MODELS = ('arm610', 'arm710', 'sa110', 'arm7500')
FEATURES = ('modes', 'carry26', 'domains', 'permission-hit', 'fetch-evict', 'denied-evict', 'smc-str', 'smc-strb', 'smc-stm',
            'smc-strt', 'smc-swp', 'partial-stm', 'pc-next', 'va-key', 'maintenance', 'interrupts', 'irq-ordinal', 'irq-event', 'masked-refresh',
            'buffer', 'cache-conflict', 'clock', 'timing-registers', 'idle', 'prefetch', 'delay-zero', 'burst-rom')
ROWS = {
    'G-1': ('prefetch', 'delay-zero'), 'G-2': ('modes', 'carry26'), 'G-3': ('interrupts', 'irq-ordinal', 'irq-event', 'masked-refresh'),
    'G-4': ('domains', 'permission-hit', 'fetch-evict', 'denied-evict'), 'G-6': ('cache-conflict',),
    'G-8': ('timing-registers',), 'G-9': ('smc-str', 'smc-strb', 'smc-stm', 'smc-strt', 'smc-swp', 'partial-stm', 'pc-next'),
    'G-10-E11': ('maintenance',), 'G-14': ('idle',),
    'VA-key': ('va-key',), 'AC-M2': ('pc-next', 'smc-str', 'denied-evict', 'burst-rom'),
    'AC-H2': ('domains', 'modes', 'buffer', 'clock'),
}


def supports(feature, model):
    if feature == 'clock': return model == 2
    if feature == 'idle': return model in (2, 3)
    return True


def domain(access, domain=1):
    # AP=3; DACR 0 and reserved 2 fault, client and manager permit.
    return access in (1, 3), (domain << 4) | 9


def smc_result(value, byte=False):
    return value & 255 if byte else value


def structural_zeros(model):
    zeros = {feature: ('SA-110-only c15 clock switch' if feature == 'clock' else 'No c15 wait/SUSMODE on ARM610/710')
             for feature in FEATURES if not supports(feature, model)}
    if model == 3: zeros['smc-vram'] = 'ARM7500 session has no VRAM allocation'
    return zeros
