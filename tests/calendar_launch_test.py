"""Private-free frontend setup for AY-1 launch persistence. GPL-2.0-only."""
from pathlib import Path
import os, shutil, subprocess, sys, tempfile
from arc_calendar_fixture import firmware as calendar_firmware

exe, core, fixtures = sys.argv[1:]
fixtures=Path(fixtures)
with tempfile.TemporaryDirectory(prefix='arc-calendar-launch-') as temp:
    for base in (2025,2099):
        for mode,rom,content in (
            ('chooser','arcdroid-chooser-test.rom','arcdroid-chooser-l.adl'),
            ('boot','arcdroid-boot-prompt.rom','launch-family/boot-file-3-l.adl')):
            root=Path(temp)/(mode+str(base));system=root/'system';save=root/'save'
            firmware=system/'arcdroid/firmware';firmware.mkdir(parents=True);save.mkdir()
            shutil.copyfile(fixtures/rom,firmware/'authored.rom')
            subprocess.run([exe,core,str(system),str(save),str(fixtures/content),mode,str(base)],
                           env={**os.environ,'ARCDROID_TEST_POWER_ON_SHIFT':'1'},check=True,timeout=90)
    for mode,base in (('session',2025),('construction',2025),('construction',2099)):
        root=Path(temp)/(mode+str(base));system=root/'system';save=root/'save'
        firmware=system/'arcdroid/firmware';firmware.mkdir(parents=True);save.mkdir()
        (firmware/'authored.rom').write_bytes(calendar_firmware())
        subprocess.run([exe,core,str(system),str(save),str(fixtures/'arcdroid-test.adf'),mode,str(base)],
                       check=True,timeout=90)
