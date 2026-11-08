/* kernel32's console functions. Windows 9x consoles hold characters of
 * the console's code page (GetConsoleCP for input, GetConsoleOutputCP for
 * output).
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"

static WCHAR char_w(CHAR c, UINT cp) {
  WCHAR w = 0;
  if (!c)
    return 0;
  if (!ow_mb2wc(cp, 0, &c, 1, &w, 1))
    w = (unsigned char)c;
  return w;
}

static CHAR char_a(WCHAR w, UINT cp) {
  CHAR c[4];
  int n;
  if (!w)
    return 0;
  n = ow_wc2mb(cp, 0, &w, 1, c, sizeof c);
  return n == 1 ? c[0] : '?';
}

static void input_w(INPUT_RECORD *r, DWORD n) {
  UINT cp = GetConsoleCP();
  for (DWORD i = 0; i < n; i++)
    if (r[i].EventType == KEY_EVENT)
      r[i].Event.KeyEvent.uChar.UnicodeChar =
          char_w(r[i].Event.KeyEvent.uChar.AsciiChar, cp);
}

BOOL WINAPI PeekConsoleInputW(HANDLE h, PINPUT_RECORD buf, DWORD n,
                              LPDWORD read) {
  BOOL r = PeekConsoleInputA(h, buf, n, read);
  if (r && read)
    input_w(buf, *read);
  return r;
}

BOOL WINAPI ReadConsoleInputW(HANDLE h, PINPUT_RECORD buf, DWORD n,
                              LPDWORD read) {
  BOOL r = ReadConsoleInputA(h, buf, n, read);
  if (r && read)
    input_w(buf, *read);
  return r;
}

BOOL WINAPI WriteConsoleInputW(HANDLE h, const INPUT_RECORD *buf, DWORD n,
                               LPDWORD written) {
  UINT cp = GetConsoleCP();
  INPUT_RECORD *a = ow_alloc(n * sizeof *a);
  BOOL r;
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  ow_memcpy(a, buf, n * sizeof *a);
  for (DWORD i = 0; i < n; i++)
    if (a[i].EventType == KEY_EVENT) {
      WCHAR w = a[i].Event.KeyEvent.uChar.UnicodeChar;
      a[i].Event.KeyEvent.uChar.UnicodeChar = 0;
      a[i].Event.KeyEvent.uChar.AsciiChar = char_a(w, cp);
    }
  r = WriteConsoleInputA(h, a, n, written);
  ow_free(a);
  return r;
}

BOOL WINAPI ReadConsoleW(HANDLE h, LPVOID buf, DWORD n, LPDWORD read,
                         PCONSOLE_READCONSOLE_CONTROL control) {
  UINT cp = GetConsoleCP();
  char *a;
  DWORD got = 0;
  BOOL r;
  (void)control;
  if (read)
    *read = 0;
  if (!n)
    return TRUE;
  a = ow_alloc(n + 1);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  /* A DBCS character takes two bytes but one WCHAR: n bytes always fit. */
  r = ReadConsoleA(h, a, n, &got, NULL);
  if (r) {
    int w = got ? ow_mb2wc(cp, 0, a, got, buf, n) : 0;
    if (read)
      *read = w;
  }
  ow_free(a);
  return r;
}

BOOL WINAPI WriteConsoleW(HANDLE h, const VOID *buf, DWORD n,
                          LPDWORD written, LPVOID reserved) {
  UINT cp = GetConsoleOutputCP();
  int al = 0;
  LPSTR a = ow_ancp(buf, n, &al, cp);
  DWORD w = 0;
  BOOL r;
  if (written)
    *written = 0;
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  r = WriteConsoleA(h, a, al, &w, reserved);
  if (r && written)
    *written = w == (DWORD)al ? n : (DWORD)ow_wlen_of(a, w, cp);
  ow_free(a);
  return r;
}

BOOL WINAPI FillConsoleOutputCharacterW(HANDLE h, WCHAR c, DWORD n,
                                        COORD at, LPDWORD written) {
  return FillConsoleOutputCharacterA(h, char_a(c, GetConsoleOutputCP()), n,
                                     at, written);
}

BOOL WINAPI ReadConsoleOutputCharacterW(HANDLE h, LPWSTR buf, DWORD n,
                                        COORD at, LPDWORD read) {
  UINT cp = GetConsoleOutputCP();
  char *a = ow_alloc(n * 2 + 1);
  DWORD got = 0;
  BOOL r;
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  r = ReadConsoleOutputCharacterA(h, a, n, at, &got);
  if (r) {
    int w = got ? ow_mb2wc(cp, 0, a, got, buf, n) : 0;
    if (read)
      *read = w;
  }
  ow_free(a);
  return r;
}

BOOL WINAPI WriteConsoleOutputCharacterW(HANDLE h, LPCWSTR buf, DWORD n,
                                         COORD at, LPDWORD written) {
  UINT cp = GetConsoleOutputCP();
  int al;
  LPSTR a = ow_ancp(buf, n, &al, cp);
  DWORD w = 0;
  BOOL r;
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  r = WriteConsoleOutputCharacterA(h, a, al, at, &w);
  if (r && written)
    *written = w == (DWORD)al ? n : (DWORD)ow_wlen_of(a, w, cp);
  ow_free(a);
  return r;
}

static void cells_w(CHAR_INFO *c, int n) {
  UINT cp = GetConsoleOutputCP();
  for (int i = 0; i < n; i++)
    c[i].Char.UnicodeChar = char_w(c[i].Char.AsciiChar, cp);
}

static CHAR_INFO *cells_a(const CHAR_INFO *c, int n) {
  UINT cp = GetConsoleOutputCP();
  CHAR_INFO *a = ow_alloc(n * sizeof *a);
  if (!a)
    return NULL;
  for (int i = 0; i < n; i++) {
    a[i].Attributes = c[i].Attributes;
    a[i].Char.UnicodeChar = 0;
    a[i].Char.AsciiChar = char_a(c[i].Char.UnicodeChar, cp);
  }
  return a;
}

BOOL WINAPI ReadConsoleOutputW(HANDLE h, PCHAR_INFO buf, COORD size,
                               COORD at, PSMALL_RECT rect) {
  BOOL r = ReadConsoleOutputA(h, buf, size, at, rect);
  if (r)
    cells_w(buf, size.X * size.Y);
  return r;
}

BOOL WINAPI WriteConsoleOutputW(HANDLE h, const CHAR_INFO *buf, COORD size,
                                COORD at, PSMALL_RECT rect) {
  CHAR_INFO *a = cells_a(buf, size.X * size.Y);
  BOOL r;
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  r = WriteConsoleOutputA(h, a, size, at, rect);
  ow_free(a);
  return r;
}

BOOL WINAPI ScrollConsoleScreenBufferW(HANDLE h, const SMALL_RECT *rect,
                                       const SMALL_RECT *clip, COORD to,
                                       const CHAR_INFO *fill) {
  CHAR_INFO a;
  a.Attributes = fill->Attributes;
  a.Char.UnicodeChar = 0;
  a.Char.AsciiChar = char_a(fill->Char.UnicodeChar, GetConsoleOutputCP());
  return ScrollConsoleScreenBufferA(h, rect, clip, to, &a);
}

DWORD WINAPI GetConsoleTitleW(LPWSTR buf, DWORD cch) {
  char a[1024];
  DWORD n = GetConsoleTitleA(a, sizeof a);
  if (!n)
    return 0;
  return ow_tow(a, -1, buf, cch);
}

BOOL WINAPI SetConsoleTitleW(LPCWSTR title) {
  LPSTR a = ow_a(title);
  BOOL r = SetConsoleTitleA(a);
  ow_free(a);
  return r;
}
