"""Build the arm64 RAR5 reader, without packaging game or MOD files."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
from urllib.request import urlopen


VERSION = '3.8.11'
SHA256 = 'a3dc0872b1b39da34dc18ef1b944f0f11ffd37425d70ae04be679279f1cf43d2'
SOURCE = Path(__file__).resolve().parent / 'sunset-archive'


def build(work):
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT') or SOURCE.parents[2] / 'tools/android/sdk')
    ndk = Path(os.environ.get('ANDROID_NDK_HOME') or sdk / 'ndk/26.3.11579264')
    cmake_dir = sdk / 'cmake/3.22.1/bin'
    cmake = str(cmake_dir / ('cmake.exe' if os.name == 'nt' else 'cmake'))
    ninja = cmake_dir / ('ninja.exe' if os.name == 'nt' else 'ninja')
    work.mkdir(parents=True, exist_ok=True)
    bundle = work / f'libarchive-{VERSION}.tar.xz'
    if not bundle.exists():
        remote = f'https://www.libarchive.org/downloads/{bundle.name}'
        with urlopen(remote, timeout=30) as reply:
            data = reply.read(8 << 20)
        if hashlib.sha256(data).hexdigest() != SHA256:
            raise RuntimeError('libarchive distribution hash differs')
        bundle.write_bytes(data)
    if hashlib.sha256(bundle.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError(f'libarchive distribution hash differs: {bundle}')
    upstream = work / f'libarchive-{VERSION}'
    if not (upstream / 'CMakeLists.txt').is_file():
        with tarfile.open(bundle) as tar:
            tar.extractall(work, filter='data')
    # Keep the small cache local on Windows where Android CMake may not be
    # able to create directories on the removable delivery drive.
    build_dir = SOURCE.parents[1] / 'build-sunset-archive'
    stage = work / 'jni/arm64-v8a'
    with (work / 'build.log').open('wb') as log:
        for command in (
            [cmake, '-S', str(SOURCE), '-B', str(build_dir), '-G', 'Ninja',
             f'-DCMAKE_MAKE_PROGRAM={ninja.as_posix()}', '-DCMAKE_BUILD_TYPE=MinSizeRel',
             f'-DCMAKE_TOOLCHAIN_FILE={(ndk / "build/cmake/android.toolchain.cmake").as_posix()}',
             f'-DLIBARCHIVE_SOURCE_DIR={upstream.as_posix()}', '-DANDROID_ABI=arm64-v8a',
             '-DANDROID_PLATFORM=21', '-DANDROID_USE_LEGACY_TOOLCHAIN_FILE=OFF'],
            [cmake, '--build', str(build_dir), '--parallel', '4'],
        ):
            result = subprocess.run(command, cwd=SOURCE, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                print((work / 'build.log').read_text(encoding='utf-8', errors='replace')[-7000:])
                raise SystemExit(result.returncode)
    stage.mkdir(parents=True, exist_ok=True)
    shutil.copy2(build_dir / 'libsunsetarchive.so', stage / 'libsunsetarchive.so')
    notices = work / 'licenses/META-INF'
    notices.mkdir(parents=True, exist_ok=True)
    shutil.copy2(upstream / 'COPYING', notices / 'sunset-libarchive-COPYING.txt')
    return stage.parent


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, required=True)
    print(build(parser.parse_args().work_dir.resolve()))
