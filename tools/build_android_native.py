"""Build only the independent ARM64 engine against isolated cached dependencies."""
import os
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
from native_build_paths import verified_cached_paths
sys.stdout.reconfigure(encoding='utf-8')
port=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--cheats',action='store_true');options=parser.parse_args()
_msys2=Path(os.environ.get('MSYS2_ROOT') or 'C:/msys64')
env=os.environ.copy();env['PATH']=';'.join(str(x) for x in (_msys2/'mingw64/bin',_msys2/'usr/bin',Path('C:/Program Files/Git/cmd')))+';'+env.get('PATH','')
env.update(TEMP=str(port/'probes/tmp'),TMP=str(port/'probes/tmp'))
_tools=Path(os.environ.get('XSYSTEM4_ANDROID_TOOLS') or (port.parent/'tools/android'))
_sdk=Path(os.environ.get('ANDROID_SDK_ROOT') or _tools/'sdk')
_ndk=Path(os.environ.get('ANDROID_NDK_HOME') or _sdk/'ndk/26.3.11579264')
cmake=os.environ.get('CMAKE') or str(_sdk/'cmake/3.22.1/bin/cmake.exe')
build=port/('build-android-arm64-cheats' if options.cheats else 'build-android-arm64');stage=port/'android/deps/stage'
try:
    cached=verified_cached_paths(build/'CMakeCache.txt',port/'engine',build,_ndk/'build/cmake/android.toolchain.cmake')
except ValueError as error:
    raise SystemExit(str(error))
configure=[cmake,'-S',cached['CMAKE_HOME_DIRECTORY'],'-B',cached['CMAKE_CACHEFILE_DIR'],'-GNinja',
 '-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DCMAKE_MAKE_PROGRAM='+(os.environ.get('NINJA') or str(_msys2/'mingw64/bin/ninja.exe')),
 '-DCMAKE_TOOLCHAIN_FILE='+cached['CMAKE_TOOLCHAIN_FILE'],
 '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=21','-DANDROID_USE_LEGACY_TOOLCHAIN_FILE=OFF',
 '-DCMAKE_STAGING_PREFIX='+str(stage),'-DCMAKE_FIND_ROOT_PATH='+str(stage),
 '-DCMAKE_MODULE_PATH='+str(port/'android/cmake'),'-DCMAKE_PROJECT_INCLUDE='+str(port/'tools/android-imports.cmake'),
 '-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384','-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON',
 '-DCMAKE_POLICY_DEFAULT_CMP0069=NEW','-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
 '-DXSYSTEM4_RANCE10_CHEATS='+('ON' if options.cheats else 'OFF'),'-DXSYSTEM4_RANCE03_MOD=OFF']
configure=[argument.replace('\\','/') for argument in configure]
logfile=port/('logs/android-cheats-native-build.txt' if options.cheats else 'logs/android-native-build.txt')
with logfile.open('wb') as log:
 for command in (configure,[cmake,'--build',cached['CMAKE_CACHEFILE_DIR'],'--parallel','4']):
    result=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
    if result.returncode:
        log.flush();print(logfile.read_text(encoding='utf-8',errors='replace')[-7000:]);raise SystemExit(result.returncode)
dest=port/('android/cheats-jni/arm64-v8a' if options.cheats else 'android/project/app/src/main/jniLibs/arm64-v8a')
dest.mkdir(parents=True,exist_ok=True)
shutil.copy2(build/'libxsystem4.so',dest/'libxsystem4.so')
if options.cheats:
    for name in ('libSDL2.so','libcglm.so'):
        shutil.copy2(port/'android/project/app/src/main/jniLibs/arm64-v8a'/name,dest/name)
print('Built isolated ARM64 '+('cheat' if options.cheats else 'normal')+' libxsystem4.so')
