"""Build/install only the isolated desktop prototype (requires local MSYS2)."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
sys.stdout.reconfigure(encoding='utf-8')

port=Path(__file__).resolve().parents[1]
msys2=Path(os.environ.get('MSYS2_ROOT') or 'C:/msys64')
env=os.environ.copy();env['PATH']=';'.join(str(x) for x in (msys2/'mingw64/bin',msys2/'usr/bin'))+';'+env.get('PATH','')
tmpdir=port/'probes/tmp';tmpdir.mkdir(parents=True,exist_ok=True)
env['TMP']=env['TEMP']=str(tmpdir)
build=port/'build-desktop'
commands=[]
if not (build/'build.ninja').exists():
    commands.append([str(msys2/'mingw64/bin/meson.exe'),'setup',str(build),str(port/'engine'),
                     '--prefix='+str(port/'dist'),'--buildtype=debugoptimized','-Ddebugger=disabled'])
commands.extend([
    [str(msys2/'mingw64/bin/ninja.exe'),'-C',str(build),'-j','6'],
    [str(msys2/'mingw64/bin/meson.exe'),'install','--no-rebuild','-C',str(build)]])
with (port/'logs/build-port.txt').open('wb') as log:
    for command in commands:
        r=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
        if r.returncode:
            log.flush()
            print((port/'logs/build-port.txt').read_text(encoding='utf-8',errors='replace')[-3500:])
            raise SystemExit(r.returncode)
for name in ['glew32.dll','libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll']:
    shutil.copy2(Path('C:/msys64/mingw64/bin')/name,port/'dist/bin'/name)
print('Built and installed rance10-port/dist/bin/xsystem4.exe')
