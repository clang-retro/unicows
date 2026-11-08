# unicows

This is a free replacement for the Microsoft Layer for Unicode
(`unicows.dll`). It lets Unicode programs (ones that call the W functions of
the Windows API) run on Windows 95, 98 and ME, which implement almost none of
those functions. Each W function converts its strings to the ANSI code page,
calls the A function that Windows 9x has, and converts the results back.

It exports all 506 functions of Microsoft's `unicows.dll`, from kernel32,
user32, gdi32, advapi32, shell32, comdlg32, winspool, winmm, version, mpr,
rasapi32, secur32, oleacc, oledlg, msvfw32, avicap32 and sensapi, so that
[libunicows](https://libunicows.sourceforge.net) (or Microsoft's own
`unicows.lib`) can load it in place of Microsoft's DLL.

## License

It is free software under the GNU Affero General Public License,
version 3 only ([LICENSE](LICENSE)), like the other clang-retro
repositories. Its copyright belongs to the clang-retro contributors; see
[CONTRIBUTING.md](CONTRIBUTING.md).

This is a new implementation, written from scratch. It replaces
[opencow](https://github.com/clang-retro/opencow) by Brodie Thiesfield, which
is under the Mozilla Public License 1.1, GPL 2.0 or LGPL 2.1. That code could
not be relicensed without its authors, and it covered about 160 functions, so
none of it is used here. The source keeps the name opencow in places.

A program that ships `unicows.dll` (or `opencow.dll`) built from this
repository passes on a copy of this code, and the AGPL applies to that copy:
its source has to be offered with it.

## Building

It is built with the [clang-dos](https://github.com/clang-retro/dos-sdk)
toolchain and its `win9x-msvcrt` sysroot:

```sh
BIN=~/clang-dos/bin SYSROOT=~/clang-dos/sysroots/win9x-msvcrt sh build.sh
```

`build/unicows.dll` is the DLL. `build/opencow.dll` is the same file, under
the name that libunicows loads when there is no `unicows.dll`.
`build/libunicows-dll.a` links a program to the DLL directly, without
libunicows (which uses the system's own W functions on Windows NT).

The DLL imports only functions that Windows 95 has. Functions that only
later versions have (such as `GetFileAttributesExA` on 95, or the
security and dial-up functions where they are not installed) are found at
run time, and the W function fails with `ERROR_CALL_NOT_IMPLEMENTED` where
they are missing. The DLLs other than kernel32, user32, gdi32 and advapi32
are loaded the first time one of their functions is called.

## Using it

Link a Unicode program with libunicows (see the
[win32-sdk](https://github.com/clang-retro/win32-sdk) README) and ship
`unicows.dll` with it. On Windows NT, libunicows uses the system's W
functions and the DLL is not loaded.

## How it works

* Strings are converted with the ANSI code page, or the OEM code page for
  the file functions after `SetFileApisToOEM`, and the console's code pages
  for the console functions. `MultiByteToWideChar` and
  `WideCharToMultiByte` handle UTF-7 and UTF-8 themselves, as Windows 95
  does not.
* Window procedures: a W window procedure is given to the system as an A
  one, a small thunk that converts each message (text, characters,
  `CREATESTRUCT`, edit, list box and combo box messages, DBCS characters)
  before calling it. `SendMessageW` and the like convert the other way for
  A windows. `GetWindowLongW` and `IsWindowUnicode` see through the thunks.
* The structures with strings (`OPENFILENAME`, `CHOOSEFONT`, `PRINTER_INFO_2`,
  `RASENTRY`, `NETRESOURCE`, ...) are copied field by field. Their hook
  procedures get the caller's W structure, and the messages that carry
  structures (`CDM_GETFILEPATH`, `WM_CHOOSEFONT_GETLOGFONT`,
  `FINDMSGSTRING`, ...) are converted.
* Some functions are implemented here rather than converted: the `lstr`
  functions, `wsprintfW`, `FormatMessageW`, `LoadStringW`, `CharUpperW` and
  the character classes (with their own case tables), `IsTextUnicode`, the
  resource update functions (`BeginUpdateResource` and the rest, which
  Windows 9x does not have, also as A functions) and `VerQueryValueW`.
* `GetProcAddress` gives the W functions of this DLL when a program looks
  them up in a system DLL at run time.

## Limits

* Text that the ANSI code page cannot show is lost on the way to the A
  functions, as with Microsoft's DLL.
* `mmioOpenW` with an I/O procedure given in its `MMIOINFO` passes it on
  unchanged; procedures installed with `mmioInstallIOProcW` get W names.
* The notifications of MCI windows (`MCIWNDM_NOTIFYMEDIA`) reach the parent
  with A strings.
* `OleUIPromptUserW` converts the string argument of the standard templates
  only.

## Tests

`tests/run.sh` builds `tests/test.c` against the DLL and runs it in Wine
(32-bit Wine and, without a display, `xvfb-run`):

```sh
BIN=... SYSROOT=... sh tests/run.sh
```

Wine has a `unicows.dll` of its own, so the script tells Wine to load this
one. The test calls the DLL directly, as Wine reports itself as Windows NT
and libunicows would not use the DLL there. CI runs the build, the import
check of win32-sdk's `tools/check-imports.py` and the tests on every pull
request.
