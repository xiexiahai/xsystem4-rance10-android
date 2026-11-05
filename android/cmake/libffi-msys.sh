#!/bin/sh
# Native Windows CMake needs an explicit shell and POSIX paths for Autoconf.
set -eu
source_dir=$(cygpath -u "$1")
build_dir=$(cygpath -u "$2")
staging_dir=$(cygpath -u "$3")
toolchain_dir=$(cygpath -u "$4")
target=$5
phase=$6
mkdir -p "$build_dir"
cd "$build_dir"
case "$phase" in
  configure)
    "$source_dir/configure" \
      --host="$target" --prefix="$staging_dir" --with-pic \
      --disable-shared --disable-dependency-tracking --disable-builddir \
      --disable-multi-os-directory --disable-structs --disable-raw-api --disable-docs \
      "AR=$toolchain_dir/bin/llvm-ar.exe" \
      "CC=$toolchain_dir/bin/clang.exe --target=$target" \
      "AS=$toolchain_dir/bin/clang.exe --target=$target" \
      "LD=$toolchain_dir/bin/ld.lld.exe" \
      "RANLIB=$toolchain_dir/bin/llvm-ranlib.exe" \
      "STRIP=$toolchain_dir/bin/llvm-strip.exe"
    ;;
  build) make -j "${CMAKE_BUILD_PARALLEL_LEVEL:-4}" ;;
  install) make install ;;
  *) echo "Unknown libffi phase: $phase" >&2; exit 2 ;;
esac
