#!/usr/bin/env python3
"""Mutate copied policy sources and run the public tests. GPL-2.0-only.

Never edits production sources. Build failures are errors, not killed mutants.
Usage: script ROOT COMPILER CORE_ARCHIVE [OUTPUT_DIRECTORY]
"""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1]).resolve()
compiler, archive = sys.argv[2:4]
source = (root / "core/rpc_selection.c").read_text()
mutants = [
    ("crc-required", "d->crc32 == table[i].crc32 &&", "true &&"),
    ("sha-required", "!strcmp(d->sha1, table[i].sha1)", "true"),
    ("size-required", "d->size == table[i].size &&", "true &&"),
    ("unknown-cpu", "if (discs[i].cpu) cpu &= discs[i].cpu;", "cpu &= discs[i].cpu;"),
    ("unknown-os", "if (discs[i].os) os &= discs[i].os;", "os &= discs[i].os;"),
    ("cpu-intersection", "cpu &= discs[i].cpu", "cpu = discs[i].cpu"),
    ("os-intersection", "os &= discs[i].os", "os = discs[i].os"),
    ("legacy-arm3", "!(discs[i].cpu & 3u)", "!(discs[i].cpu & 2u)"),
    ("legacy-old-os", "!(discs[i].os & 3u)", "!(discs[i].os & 1u)"),
    ("rpc-notice-gate", "if (!manual && !rpc_notice) return r;", "if (false) return r;"),
    ("cpu-rank", "ARC_RPC_SELECT_ARM710,\n      ARC_RPC_SELECT_ARM610", "ARC_RPC_SELECT_ARM610,\n      ARC_RPC_SELECT_ARM710"),
    ("sa-before-a7000", "ARC_RPC_SELECT_SA110, ARC_RPC_SELECT_ARM7500 };", "ARC_RPC_SELECT_ARM7500, ARC_RPC_SELECT_SA110 };"),
    ("os-rank", "j=ARC_RPC_OS_371; j>=ARC_RPC_OS_350; --j", "j=ARC_RPC_OS_350; j<=ARC_RPC_OS_371; ++j"),
    ("qualified-gate", "if (!arc_riscpc_selection_qualified(m,f)) continue;", "if (false) continue;"),
    ("installed-gate", "(inventory->installed & ARC_RPC_OS_BIT(f))", "true"),
    ("unsupported-before-unqualified", "!feasible ? ARC_RPC_SELECTION_UNSUPPORTED", "!feasible ? ARC_RPC_SELECTION_UNQUALIFIED"),
    ("conflict-reason", "r.reason = ARC_RPC_SELECTION_CONFLICTING_DISCS", "r.reason = ARC_RPC_SELECTION_UNSUPPORTED"),
    ("missing-vs-unreadable", "inventory && inventory->vfs_failure ?", "false ?"),
    ("partial-inventory", "if (r.reason == ARC_RPC_SELECTION_SELECTED) return r;", "if (r.reason == ARC_RPC_SELECTION_SELECTED && !r.incomplete) return r;"),
    ("manual-overrides-hints", "if (manual) { cpu = 0x3fu; os = 0x7fu; }", "if (false) { cpu = 0x3fu; os = 0x7fu; }"),
    ("manual-model", "(manual_model && m != manual_model)", "false"),
    ("manual-os", "(manual_os && f != manual_os)", "false"),
    ("qualified-610", "os == ARC_RPC_OS_350;", "os == ARC_RPC_OS_350 || os == ARC_RPC_OS_371;"),
    ("qualified-710", "case ARC_RPC_SELECT_ARM710: return os == ARC_RPC_OS_360 || os == ARC_RPC_OS_371;", "case ARC_RPC_SELECT_ARM710: return os == ARC_RPC_OS_360 || os == ARC_RPC_OS_370 || os == ARC_RPC_OS_371;"),
    ("qualified-sa", "case ARC_RPC_SELECT_SA110: return os == ARC_RPC_OS_370 || os == ARC_RPC_OS_371;", "case ARC_RPC_SELECT_SA110: return os == ARC_RPC_OS_350 || os == ARC_RPC_OS_371;"),
    ("qualified-7500", "case ARC_RPC_SELECT_ARM7500: return os == ARC_RPC_OS_360 || os == ARC_RPC_OS_371;", "case ARC_RPC_SELECT_ARM7500: return os == ARC_RPC_OS_360;"),
    ("os-family-map", "f <= ARC_RPC_OS_360 ? 4u : 8u", "f <= ARC_RPC_OS_350 ? 4u : 8u"),
    ("cpu-category-map", "cpu_bit[] = {0,4u,4u,16u,8u}", "cpu_bit[] = {0,4u,4u,8u,16u}"),
]

def run(folder):
    survivors = []
    for name, old, new in [("control", None, None)] + mutants:
        text = source
        if old:
            assert old in source, name
            text = source.replace(old,new,1)
        file = folder / (name + ".c")
        exe = folder / (name + (".exe" if sys.platform == "win32" else ""))
        file.write_text(text)
        command = [compiler,"-std=c11","-UNDEBUG", "-I"+str(root / "core"),
                   "-I"+str(root / "libretro/include"), str(root / "tests/rpc_selection_test.c"),
                   str(file),archive,"-o",str(exe)]
        subprocess.run(command,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        result = subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        if name == "control":
            assert result.returncode == 0, result.stderr.decode()
        elif result.returncode == 0:
            survivors.append(name)
        print(name + ": " + ("PASS" if name == "control" else "SURVIVED" if result.returncode == 0 else "KILLED"),flush=True)
    assert not survivors, survivors
    print(f"{len(mutants)}/{len(mutants)} policy mutants killed; control passed")

if len(sys.argv)>4:
    output = pathlib.Path(sys.argv[4]);output.mkdir(parents=True,exist_ok=True);run(output)
else:
    with tempfile.TemporaryDirectory(prefix="arcdroid-r3a-mutants-") as tmp:
        run(pathlib.Path(tmp))
