#!/usr/bin/env python3
"""Exact gate-closed metadata; R6-1/R6-5 open differences only. GPL-2.0-only."""
import subprocess, sys, tempfile
exe, closed, opened, *baseline = sys.argv[1:]
arch = ' Archimedes sessions only. RiscPC starts manually with its hard disc attached.'
selector = ' Archimedes sessions only. A non-Auto choice takes precedence over the RiscPC options at load. During RiscPC sessions, changes apply after reloading the content; Restart keeps the current model and operating system.'
memory = ' Archimedes sessions only; ignored on RiscPC.'
media = ' On RiscPC, changes take effect after Restart or reloading the content.'
suffixes = {**dict.fromkeys(('joystick_if','launch','launch_chooser','launch_ramdisc','jfd_boot','adffs_package','hard_disc'), arch),
            **dict.fromkeys(('machine','cpu','os'), selector), 'memory': memory,
            **dict.fromkeys(('disk_writes','jfd_write_protect','short_tail','jfd_omitted'), media)}
new = {
    'arcdroid_rpc_model': ('RiscPC model', 'Auto selects a qualified installed RiscPC model from the content. A manual choice overrides CPU hints only. A non-Auto Archimedes Machine, CPU or Operating System takes precedence. Changes apply after reloading the content; Restart keeps the admitted combination.', [('auto','Auto'),('arm610','ARM610'),('arm710','ARM710'),('sa110','StrongARM'),('arm7500','ARM7500')]),
    'arcdroid_rpc_os': ('RiscPC operating system', 'Auto selects qualified installed RISC OS firmware from the content. A manual choice overrides OS hints only. A non-Auto Archimedes Machine, CPU or Operating System takes precedence. Changes apply after reloading the content; Restart keeps the admitted combination.', [('auto','Auto'),('350','RISC OS 3.50'),('360','RISC OS 3.60'),('370','RISC OS 3.70'),('371','RISC OS 3.71')])}
with tempfile.TemporaryDirectory(prefix='rpc-route-metadata-') as temporary:
    def read(core):
        run = subprocess.run([exe,core,temporary,temporary,'unused','metadata','unused'],check=True,text=True,capture_output=True)
        assert 'option|' in run.stdout and 'system|' in run.stdout
        return run.stdout
    original = read(closed); candidate = read(opened)
    keys = [line.split('|')[1] for line in candidate.splitlines() if line.startswith('option|')]
    assert len(keys) == 45 and keys[20:22] == list(new), keys
    assert original.count('option|') == 43
    projected = []; seen = set()
    for line in candidate.splitlines():
        fields = line.split('|')
        if fields[0] in ('option','value'):
            key = fields[1]
            if key in new:
                label, help_text, values = new[key]
                if fields[0] == 'option':
                    assert fields == ['option',key,label,'',help_text,'','','auto'], fields
                    actual = [v.split('|')[2:] for v in candidate.splitlines() if v.startswith('value|'+key+'|')]
                    assert actual == [list(v) for v in values], actual
                continue
            suffix = suffixes.get(key.removeprefix('arcdroid_'))
            if fields[0] == 'option' and suffix:
                assert fields[4].endswith(suffix), fields
                fields[4] = fields[4][:-len(suffix)]; seen.add(key.removeprefix('arcdroid_'))
                line = '|'.join(fields)
        projected.append(line)
    assert seen == suffixes.keys(), seen
    assert '\n'.join(projected)+'\n' == original
    for core in baseline: assert read(core) == original
print('rpc-route-metadata: closed exact; open admits exactly two ordered keys and fifteen exact help suffixes')
