#!/usr/bin/env python3
"""Mutate copied policy sources and run the public tests. GPL-2.0-only.

Never edits production sources. Build failures are errors, not killed mutants.
Uses the configured Ninja graph for compiler/link flags on host and Windows.
Usage: script --build BUILD --mode policy|inventory|integration --target BINARY
Integration also requires --load LOAD_TEST --rom AUTHORED_ROM.
"""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile
from rpc_ninja_commands import split_command

mutants = [
    ("crc-required", "d->crc32 == table[i].crc32 &&", "true &&"),
    ("sha-required", "!strcmp(d->sha1, table[i].sha1)", "true"),
    ("size-required", "d->size == table[i].size &&", "true &&"),
    ("unknown-cpu", "if (discs[i].cpu) cpu &= discs[i].cpu;", "cpu &= discs[i].cpu;"),
    ("unknown-os", "if (discs[i].os) os &= discs[i].os;", "os &= discs[i].os;"),
    ("cpu-intersection", "cpu &= discs[i].cpu", "cpu = discs[i].cpu"),
    ("os-intersection", "os &= discs[i].os", "os = discs[i].os"),
    ("legacy-arm3", "!(disc.cpu & 3u)", "!(disc.cpu & 2u)"),
    ("legacy-old-os", "!(disc.os & 3u)", "!(disc.os & 1u)"),
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
    ("manual-model-overrides-cpu", "if (manual_model) cpu = 0x3fu;", "if (false) cpu = 0x3fu;"),
    ("manual-os-overrides-os", "if (manual_os) os = 0x7fu;", "if (false) os = 0x7fu;"),
    ("manual-model-widens-os", "if (manual_model) cpu = 0x3fu;", "if (manual_model) { cpu = 0x3fu; os = 0x7fu; }"),
    ("manual-os-widens-cpu", "if (manual_os) os = 0x7fu;", "if (manual_os) { os = 0x7fu; cpu = 0x3fu; }"),
    ("manual-ignores-other-axis-conflict", "if (!cpu || !os)", "if (!manual && (!cpu || !os))"),
    ("manual-model", "(manual_model && m != manual_model)", "false"),
    ("manual-os", "(manual_os && f != manual_os)", "false"),
    ("qualified-610", "os == ARC_RPC_OS_350;", "os == ARC_RPC_OS_350 || os == ARC_RPC_OS_371;"),
    ("qualified-710", "case ARC_RPC_SELECT_ARM710: return os == ARC_RPC_OS_360 || os == ARC_RPC_OS_371;", "case ARC_RPC_SELECT_ARM710: return os == ARC_RPC_OS_360 || os == ARC_RPC_OS_370 || os == ARC_RPC_OS_371;"),
    ("qualified-sa", "case ARC_RPC_SELECT_SA110: return os == ARC_RPC_OS_370 || os == ARC_RPC_OS_371;", "case ARC_RPC_SELECT_SA110: return os == ARC_RPC_OS_350 || os == ARC_RPC_OS_371;"),
    ("qualified-7500", "case ARC_RPC_SELECT_ARM7500: return os == ARC_RPC_OS_360 || os == ARC_RPC_OS_371;", "case ARC_RPC_SELECT_ARM7500: return os == ARC_RPC_OS_360;"),
    ("os-family-map", "f <= ARC_RPC_OS_360 ? 4u : 8u", "f <= ARC_RPC_OS_350 ? 4u : 8u"),
    ("cpu-category-map", "cpu_bit[] = {0,4u,4u,16u,8u}", "cpu_bit[] = {0,4u,4u,8u,16u}"),
]

inventory_mutants = [
    ("depth-unbounded", "if (depth) scan_directory(s,child,depth-1);",
     "if (true) scan_directory(s,child,depth ? depth-1 : 0);"),
    ("depth-too-shallow", "scan_directory(&s,directory,ARC_RPC_SCAN_DEPTH)", "scan_directory(&s,directory,1)"),
    ("depth-not-flagged", "else s->result.bounded = true;", "else s->result.bounded = false;"),
    ("dot-recursion", ' || !strcmp(name,".") || !strcmp(name,"..")', ""),
    ("resource-not-reported", "if (!data) s->result.resource_failure = true;", "if (!data) s->result.resource_failure = false;"),
    ("resource-leaks-file", "if (!data) s->result.resource_failure = true;", "if (!data) { s->result.resource_failure = true; return; }"),
    ("resource-as-vfs-failure", "if (!data) s->result.resource_failure = true;", "if (!data) s->result.vfs_failure = true;"),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=pathlib.Path)
    parser.add_argument("--mode", required=True, choices=("policy", "inventory", "integration"))
    parser.add_argument("--target", required=True, type=pathlib.Path)
    parser.add_argument("--load", type=pathlib.Path)
    parser.add_argument("--rom", type=pathlib.Path)
    args = parser.parse_args()
    if args.mode == "integration" and (not args.load or not args.rom):
        parser.error("integration requires --load and --rom")
    root = pathlib.Path(__file__).resolve().parent.parent
    build, target = args.build.resolve(), args.target.resolve()
    cache = (build / "CMakeCache.txt").read_text()
    ninja = next(line.split("=", 1)[1] for line in cache.splitlines()
                 if line.startswith("CMAKE_MAKE_PROGRAM:FILEPATH="))
    database = json.loads(subprocess.check_output([ninja, "-t", "compdb", "-x"], cwd=build, text=True))
    graph = {(build / row["output"]).resolve(): row for row in database}

    def parts(command):
        return [split_command(segment, os.name == "nt") for segment in command.split("&&")]

    link = next(row for row in parts(graph[target]["command"]) if "-o" in row and "-c" not in row)
    # Follow archives as well as direct objects: the pure policy lives inside
    # the core archive in both the unit executable and the ordinary-load DLL.
    inputs = set()
    pending = [pathlib.Path(build / word).resolve() for word in link if word.endswith((".o", ".obj", ".a", ".lib"))]
    while pending:
        item = pending.pop()
        if item in inputs:
            continue
        inputs.add(item)
        if item in graph and item.suffix in (".a", ".lib"):
            pending.extend((build / word).resolve() for row in parts(graph[item]["command"])
                           for word in row if word.endswith((".o", ".obj")))

    def run(command):
        return subprocess.run([str(x) for x in command], cwd=build, capture_output=True, text=True, timeout=180)

    def must_pass(command):
        result = run(command)
        assert result.returncode == 0, (command, result.stdout, result.stderr)

    def binary(relative, old, new, folder, label):
        source = root / relative
        text = source.read_text()
        if old is not None:
            assert text.count(old) == 1, (label, "missing or ambiguous recipe")
            text = text.replace(old, new, 1)
        copied = folder / source.name
        copied.write_text(text)
        if relative == "libretro/rpc_inventory.c":
            # Keep the allocation seam in the test-only wrapper when compiling
            # the copied scanner; the production scanner gains no test hooks.
            source = root / "tests/rpc_inventory_scan.c"
            wrapper = folder / source.name
            wrapper.write_text(source.read_text().replace('"../libretro/rpc_inventory.c"',
                                                         '"' + copied.as_posix() + '"'))
            copied = wrapper
        compile_command = next(row for item in inputs if item in graph
                               for row in parts(graph[item]["command"])
                               if "-c" in row and (build / row[row.index("-c") + 1]).resolve() == source)
        compile_command = compile_command[:]
        original = (build / compile_command[compile_command.index("-o") + 1]).resolve()
        obj = folder / "mutation.o"
        for option, value in (("-c", copied), ("-o", obj), ("-MF", folder / "mutation.d"), ("-MT", obj)):
            if option in compile_command:
                compile_command[compile_command.index(option) + 1] = str(value)
        must_pass(compile_command)
        output = folder / target.name
        command = link[:]
        direct = next((i for i, word in enumerate(command) if word.endswith((".o", ".obj"))
                       and (build / word).resolve() == original), None)
        if direct is None:
            # Supply all policy symbols before the archive, so its unchanged
            # policy object is not extracted. Everything else links normally.
            command.insert(1, str(obj))
        else:
            command[direct] = str(obj)
        command[command.index("-o") + 1] = str(output)
        command = ["-Wl,--out-implib," + str(folder / "mutation.dll.a")
                   if word.startswith("-Wl,--out-implib,") else word for word in command]
        if "-install_name" in command:
            command[command.index("-install_name") + 1] = str(output)
        must_pass(command)
        return output

    if args.mode == "policy":
        cases = [(name, "core/rpc_selection.c", old, new) for name, old, new in mutants]
    elif args.mode == "inventory":
        cases = [(name, "libretro/rpc_inventory.c", old, new) for name, old, new in inventory_mutants]
    else:
        cases = [
            ("cpu-shift", "core/rpc_selection.c", "(flags >> 8u)", "(flags >> 9u)"),
            ("scan-always", "libretro/arcdroid_libretro.c", "if (rpc_scan_needed) rpc_inventory =", "if (true) rpc_inventory ="),
            ("scan-never", "libretro/arcdroid_libretro.c", "if (rpc_scan_needed) rpc_inventory =", "if (false) rpc_inventory ="),
            ("scan-first-disc", "libretro/arcdroid_libretro.c", "if (arc_riscpc_notice_required(rpc_discs[i]))", "if (i == 0 && arc_riscpc_notice_required(rpc_discs[i]))"),
        ]
    with tempfile.TemporaryDirectory(prefix="arcdroid-r3a2-mutants-") as temporary:
        parent = pathlib.Path(temporary)
        for name, relative, old, new in [("control", cases[0][1], None, None)] + cases:
            folder = parent / name
            folder.mkdir()
            executable = binary(relative, old, new, folder, name)
            command = ([sys.executable, "-B", root / "tests/rpc_selection_load_test.py",
                        args.load.resolve(), executable, args.rom.resolve(), folder / "data"]
                       if args.mode == "integration" else [executable])
            result = run(command)
            if name == "control":
                assert result.returncode == 0, (result.stdout, result.stderr)
            else:
                assert result.returncode != 0, (name, "SURVIVED", result.stdout)
            print(name + ": " + ("PASS" if name == "control" else "KILLED"), flush=True)
    print(f"{len(cases)}/{len(cases)} {args.mode} mutants killed; control passed")


if __name__ == "__main__":
    main()
