"""Build the ARM64 engine with real absolute paths (no R: mapping required)."""
import os
import subprocess
import sys
import shutil
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')
port = Path(__file__).resolve().parents[1]
# Android SDK/JDK/NDK: use XSYSTEM4_ANDROID_TOOLS or a tools/android directory
# next to the workspace; standard ANDROID_* variables win when set.
tools = Path(os.environ.get('XSYSTEM4_ANDROID_TOOLS') or (port.parent / 'tools/android'))
sdk = Path(os.environ.get('ANDROID_SDK_ROOT') or tools / 'sdk')
ndk = Path(os.environ.get('ANDROID_NDK_HOME') or sdk / 'ndk/26.3.11579264')
msys2 = Path(os.environ.get('MSYS2_ROOT') or 'C:/msys64')
env = os.environ.copy()
env['PATH'] = ';'.join(str(x) for x in (msys2/'mingw64/bin', msys2/'usr/bin', Path('C:/Program Files/Git/cmd'))) + ';' + env.get('PATH', '')
tmpdir = port / 'probes/tmp'
tmpdir.mkdir(parents=True, exist_ok=True)
env.update(TEMP=str(tmpdir), TMP=str(tmpdir))
cmake = os.environ.get('CMAKE') or str(sdk / 'cmake/3.22.1/bin/cmake.exe')
build = port / 'build-android-arm64'
stage = port / 'android/deps/stage'
configure = [cmake, '-S', str(port / 'engine'), '-B', str(build), '-GNinja',
             '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DCMAKE_MAKE_PROGRAM=' + (os.environ.get('NINJA') or str(msys2/'mingw64/bin/ninja.exe')),
             '-DCMAKE_TOOLCHAIN_FILE=' + str(ndk / 'build/cmake/android.toolchain.cmake'),
             '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=21', '-DANDROID_USE_LEGACY_TOOLCHAIN_FILE=OFF',
             '-DCMAKE_STAGING_PREFIX=' + str(stage), '-DCMAKE_FIND_ROOT_PATH=' + str(stage),
             '-DCMAKE_MODULE_PATH=' + str(port / 'android/cmake'),
             '-DCMAKE_PROJECT_INCLUDE=' + str(port / 'tools/android-imports.cmake'),
             '-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384',
             '-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON', '-DCMAKE_POLICY_DEFAULT_CMP0069=NEW',
             '-DFETCHCONTENT_FULLY_DISCONNECTED=ON', '-DXSYSTEM4_RANCE10_CHEATS=OFF',
             '-DXSYSTEM4_RANCE03_MOD=OFF']
configure = [a.replace('\\', '/') for a in configure]
logfile = port / 'logs/android-native-build.txt'
with logfile.open('wb') as log:
    for command in (configure, [cmake, '--build', str(build), '--parallel', '4']):
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            log.flush()
            print(logfile.read_text(encoding='utf-8', errors='replace')[-7000:])
            raise SystemExit(result.returncode)
dest = port / 'android/project/app/src/main/jniLibs/arm64-v8a'
dest.mkdir(parents=True, exist_ok=True)
shutil.copy2(build / 'libxsystem4.so', dest / 'libxsystem4.so')
print('built', dest / 'libxsystem4.so', (dest / 'libxsystem4.so').stat().st_size)
