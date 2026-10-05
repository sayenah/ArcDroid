#!/usr/bin/env python3
"""R7-1 private cold audit: guest FileSwitch reports, no embedded assets.

GPL-2.0-only. Read EnvAudit/ModAudit saved with normal Show/Modules commands.
Check standard variables and exact ROM module code addresses independently
of a runner COMPLETE footer. This does not replace desktop/input inspection.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
from disc_inventory import Disc
from period_boot import SHA1S, module_inventory


def root_file(disc, name):
    root = disc.read(disc.root, 2048)
    for i in range(77):
        e = root[5 + 26*i:31 + 26*i]
        if not e[0]:
            break
        leaf = e[:10].split(b'\r')[0].split(b'\0')[0].decode('latin1')
        if leaf.lower() == name.lower() and not e[25] & 8:
            return disc.read(int.from_bytes(e[22:25], 'little'),
                             int.from_bytes(e[18:22], 'little'))
    raise ValueError('guest audit file missing: ' + name)


def directory_exists(disc, parts):
    address = disc.root
    for part in parts:
        directory = disc.read(address, 2048)
        for i in range(77):
            entry = directory[5 + 26*i:31 + 26*i]
            if not entry[0]:
                return False
            name = entry[:10].split(b'\r')[0].split(b'\0')[0].decode('latin1')
            if name.lower() == part.lower():
                if not entry[25] & 8:
                    return False
                address = int.from_bytes(entry[22:25], 'little')
                break
        else:
            return False
    return True


def scrap_leaf_valid(leaf, inet):
    if inet is not None:
        return leaf == 'ID' + inet
    if leaf == 'ScrapDir':
        return True
    match = re.fullmatch(r'ID([0-9]{3})!([0-9]{3})', leaf)
    return bool(match) and all(int(n) <= 255 for n in match.groups())


def audit(image, firmware, resources=False):
    rom = firmware.read_bytes()
    sha1 = hashlib.sha1(rom).hexdigest()
    if sha1 not in SHA1S:
        raise ValueError('unapproved audit firmware')
    inventory = module_inventory(rom)
    disc = Disc(image.read_bytes())
    env_data, mod_data = root_file(disc, 'EnvAudit'), root_file(disc, 'ModAudit')
    variables, macro_variables = {}, {}
    for line in env_data.decode('latin1').splitlines():
        if ' : ' in line:
            k, v = line.split(' : ', 1)
            key, value = k.strip(), v.strip()
            variables[key] = value
            # Show identifies macro variables with a type suffix. Preserve
            # that observation and expose the actual variable name as well.
            if key.endswith('(Macro)'):
                macro_variables[key[:-7]] = value
                variables[key[:-7]] = value
    boot = 'ADFS::4.$.!Boot'
    choices = 'ADFS::4.$.Choices'
    expected = {
        'Boot$Dir': boot, 'Boot$Path': boot + '.', 'Boot$State': 'desktop',
        'Boot$Unique': 'Local', 'Choices$Dir': choices,
        'Choices$Path': choices + '.', 'Choices$Write': choices,
        'Boot$ToBeLoaded': choices + '.Boot.PreDesk',
        'Boot$ToBeTasks': choices + '.Boot.Tasks',
        'BootResources$Dir': boot + '.Resources',
        'BootResources$Path': boot + '.Resources.',
        'System$Dir': boot + '.Resources.!System',
        'Sys$Path': boot + '.Resources.!System.',
        'Boot$OSVersion': str(int(float(inventory['UtilityModule']['version']) * 100 + .01) // 10 * 10),
    }
    problems = [k + ' missing/different' for k, v in expected.items()
                if variables.get(k) != v]
    if not variables.get('Run$Path', '').startswith(boot + '.Library.,'):
        problems.append('Run$Path missing boot Library prefix')
    modules = []
    for line in mod_data.decode('latin1').splitlines():
        m = re.fullmatch(r'\s*\d+\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+(\S+)\s*', line)
        if not m:
            continue
        address, name = int(m[1], 16), m[3]
        # Modules includes resource modules without version help and module
        # incarnations (title%instance); the version-only scanner omits these.
        # Validate the title at the observed ROM header itself, never accept a
        # RAM module merely because its name matches a ROM module.
        header = address - 0x03800000
        exact = False
        if 0 <= header <= len(rom) - 24:
            title = header + int.from_bytes(rom[header + 16:header + 20], 'little')
            if header + 24 <= title < len(rom):
                end = rom.find(b'\0', title, min(title + 64, len(rom)))
                exact = end >= 0 and rom[title:end].decode('latin1') == name.split('%', 1)[0]
        modules.append(dict(name=name, code_address=m[1], rom_header_match=exact))
        if not exact:
            problems.append('non-ROM or unresolved module: ' + name)
    if not {'WindowManager', 'SharedCLibrary'} <= {m['name'] for m in modules}:
        problems.append('required module observations missing')
    # System search order must match supplied SysPaths, including existing
    # numeric directories <= OS version, descending, followed by System$Dir.
    files = disc.inventory()
    prefix = '!Boot/Resources/!System/'
    versions = {int(k[len(prefix):].split('/')[0]) for k in files
                if k.startswith(prefix) and k[len(prefix):].split('/')[0].isdigit()}
    version = int(expected['Boot$OSVersion'])
    path = ''.join('Sys:' + str(v) + '.,' for v in sorted(versions, reverse=True) if v <= version)
    path += expected['System$Dir'] + '.'
    if variables.get('System$Path') != path:
        problems.append('System$Path missing/different')
    resource_audit = None
    if resources:
        # Independently stated R11-1 expectation for the approved release tree.
        # !ThemeDefs is an application directory with no own !Boot.
        expected_order = ['START', '!Fonts BOOT', '!Internet BOOT', '!PrintDefs BOOT',
                          '!Scrap BOOT', '!System BOOT', '!ThemeDefs NO_BOOT',
                          '!Unicode BOOT', 'END']
        resource_data = root_file(disc, 'ResAudit')
        observed_order = []
        if len(resource_data) > 8192:
            problems.append('Resources audit exceeds expected bound')
        else:
            try:
                # Echo/redirection adds trailing spaces and blank CR/LF lines.
                # These are separators, not additional dispatch records.
                observed_order = [line.strip() for line in resource_data.decode('ascii').splitlines()
                                  if line.strip()]
            except UnicodeError:
                problems.append('Resources audit is not ASCII')
        if observed_order != expected_order:
            problems.append('Resources dispatch order incomplete/different')
        scrap = variables.get('Wimp$ScrapDir', '')
        # Supplied !Scrap.!RunImage FN Evaluate selects ScrapDir when no
        # network identity exists, ID<INet$LocalAddr> when that variable exists,
        # otherwise IDnnn!sss after a successful Econet station/net query.
        # 3.60's ROM Econet returns the ordinary zero station/network identity.
        prefix, separator, leaf = scrap.rpartition('.!Boot.Resources.!Scrap.ScrapDirs.')
        inet = variables.get('INet$LocalAddr')
        valid_leaf = scrap_leaf_valid(leaf, inet)
        scrap_entry = '!Boot/Resources/!Scrap/ScrapDirs/' + leaf
        if (not separator or not prefix.startswith('ADFS::') or not valid_leaf
                or not directory_exists(disc, scrap_entry.split('/'))):
            problems.append('Wimp$ScrapDir missing/different or directory absent')
        if variables.get('Wimp$Scrap') != scrap + '.ScrapFile':
            problems.append('Wimp$Scrap missing/different')
        if '.!Boot.Resources.!Fonts.' not in variables.get('Font$Path', ''):
            problems.append('Font$Path missing resource Fonts directory')
        resource_audit = dict(expected_order=expected_order, observed_order=observed_order,
                              bytes=len(resource_data),
                              scrap_directory=scrap, scrap_file=variables.get('Wimp$Scrap'),
                              font_path=variables.get('Font$Path'))
    return dict(status='FAIL' if problems else 'PASS', problems=problems,
                audit_tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                firmware_sha1=sha1, image_sha256=hashlib.sha256(disc.data).hexdigest(),
                environment_sha256=hashlib.sha256(env_data).hexdigest(),
                modules_sha256=hashlib.sha256(mod_data).hexdigest(),
                expected_variables=expected, runtime_variables=variables,
                runtime_macro_variables=macro_variables,
                system_search_expected=path, modules=modules, resource_audit=resource_audit,
                desktop_input='MISSING: separate visual qualification required')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--image', type=Path, required=True)
    p.add_argument('--firmware', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--resources', action='store_true', help='require R11-1 dispatch and Scrap/Fonts registration')
    a = p.parse_args()
    result = audit(a.image, a.firmware, a.resources)
    a.out.write_text(json.dumps(result, indent=2) + '\n')
    print(result['status'], 'modules', len(result['modules']), 'problems', len(result['problems']))
    raise SystemExit(bool(result['problems']))
