#!/bin/sh
# Build unicows.dll, and the same DLL as opencow.dll (the name libunicows
# falls back to), for Windows 95, 98 and ME.
#
#   BIN      directory with clang and llvm-dlltool (a clang-dos toolchain)
#   SYSROOT  its win9x-msvcrt sysroot, for the headers and import libraries
#   OUT      build directory (default: ./build)
#   CPU      -march (default: i386)
#
# Copyright (C) clang-retro contributors.
# SPDX-License-Identifier: AGPL-3.0-only
set -e
: "${BIN:?set BIN}" "${SYSROOT:?set SYSROOT}"
here=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$PWD/build}
CPU=${CPU:-i386}
CC="$BIN/clang --target=i386-pc-windows4.0-gnu -march=$CPU --sysroot=$SYSROOT"
CFLAGS="-O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
  -ffreestanding -fno-builtin -fno-stack-protector $EXTRA_CFLAGS"
mkdir -p "$OUT/obj"

for f in "$here"/src/*.c; do
  $CC $CFLAGS -c "$f" -o "$OUT/obj/$(basename "$f" .c).o"
done
# The system functions that the DLL also exports, imported under names of
# their own so that they do not clash with the DLL's.
for d in "$here"/src/real-*.def; do
  "$BIN/llvm-dlltool" -m i386 -d "$d" -l "$OUT/obj/lib$(basename "$d" .def).a"
done
$CC -fuse-ld=lld -shared -nostdlib -o "$OUT/unicows.dll" "$OUT"/obj/*.o \
  "$here/src/unicows.def" -Wl,--kill-at -Wl,-e,_DllMain@12 \
  -L"$OUT/obj" -lreal-kernel32 -lreal-user32 -lreal-gdi32 \
  -lkernel32 -luser32 -lgdi32 -ladvapi32 -lgcc
cp "$OUT/unicows.dll" "$OUT/opencow.dll"
# An import library for linking to the DLL directly (the tests do).
"$BIN/llvm-dlltool" -m i386 -k -d "$here/src/unicows.def" \
  -l "$OUT/libunicows-dll.a"
echo "built $OUT/unicows.dll and $OUT/opencow.dll"
