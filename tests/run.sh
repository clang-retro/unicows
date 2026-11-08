#!/bin/sh
# Build tests/test.c against the DLL that build.sh made and run it in Wine.
#
#   BIN, SYSROOT, OUT  as for build.sh
#
# Copyright (C) clang-retro contributors.
# SPDX-License-Identifier: AGPL-3.0-only
set -e
: "${BIN:?set BIN}" "${SYSROOT:?set SYSROOT}"
here=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$PWD/build}
CC="$BIN/clang --target=i386-pc-windows4.0-gnu --sysroot=$SYSROOT -fuse-ld=lld"
# The DLL's import library comes first, so that every function it exports
# is taken from it rather than from the system.
$CC -O1 -Wall "$here/test.c" -o "$OUT/test.exe" -L"$OUT" -lunicows-dll \
  -lgdi32 -lcomdlg32 -luser32 -lkernel32
if "$BIN/llvm-readobj" --coff-imports "$OUT/test.exe" |
  awk '/Name:/ {dll = $2} /Symbol:/ && dll != "unicows.dll" && $2 ~ /W$/' |
  grep .; then
  echo "test.exe takes the W functions above from the system" >&2
  exit 1
fi
export WINEDEBUG=${WINEDEBUG:--all}
# Wine has a unicows.dll of its own, which hands W calls to its W functions.
# mscoree and mshtml are left out so that a new prefix does not ask to
# install Mono and Gecko.
export WINEDLLOVERRIDES="unicows=n;mscoree=;mshtml=${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
# The tests expect code page 1252, which Wine takes from an English locale.
export LC_ALL=C
cd "$OUT"
if [ -n "$DISPLAY" ]; then
  wine test.exe
else
  xvfb-run -a wine test.exe
fi
