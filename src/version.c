/* version.dll: version resources and installing files.
 *
 * The version resource of a 32-bit program is in W form whatever system
 * reads it, so VerQueryValueW finds its values itself. A 16-bit program's
 * is in A form: its strings are converted into room that
 * GetFileVersionInfoSizeW asks for after the block.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"
#include <winver.h>

#define SPARE_MAGIC 0x5356574f /* "OWVS" */

struct spare {
  DWORD magic;
  DWORD used;  /* bytes taken after this header */
  DWORD total; /* bytes after this header */
};

static DWORD a_size(LPCWSTR file, LPSTR *a) {
  DWORD h;
  OW_FN(OW_VERSION, GetFileVersionInfoSizeA, return 0);
  *a = ow_a(file);
  return pGetFileVersionInfoSizeA(*a, &h);
}

DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR file, LPDWORD handle) {
  LPSTR a;
  DWORD n = a_size(file, &a);
  ow_free(a);
  if (handle)
    *handle = 0;
  return n ? ((n + 3) & ~3) * 3 + sizeof(struct spare) : 0;
}

BOOL WINAPI GetFileVersionInfoW(LPCWSTR file, DWORD handle, DWORD len,
                                LPVOID data) {
  LPSTR a;
  DWORD n = a_size(file, &a);
  BOOL r;
  OW_FN(OW_VERSION, GetFileVersionInfoA, ow_free(a); return FALSE);
  r = pGetFileVersionInfoA(a, handle, len, data);
  ow_free(a);
  n = (n + 3) & ~3;
  if (r && n && len >= n + sizeof(struct spare)) {
    struct spare *s = (struct spare *)((BYTE *)data + n);
    s->magic = SPARE_MAGIC;
    s->used = 0;
    s->total = len - n - sizeof *s;
  }
  return r;
}

/* ---- the W form of a version block ---------------------------------------
 * Each node is WORD wLength, WORD wValueLength, WORD wType, its key, then
 * its value and its children, each starting on a 4-byte boundary. */

#define ALIGN4(p) ((const BYTE *)(((ULONG_PTR)(p) + 3) & ~(ULONG_PTR)3))

static BOOL is_w_block(const BYTE *b) {
  return b && b[6] == 'V' && b[7] == 0;
}

static BOOL key_is(LPCWSTR key, LPCWSTR part, int n) {
  int i;
  for (i = 0; i < n; i++)
    if (ow_upper(key[i]) != ow_upper(part[i]) || !key[i])
      return FALSE;
  return key[n] == 0;
}

static BOOL query_w(const BYTE *node, LPCWSTR path, LPVOID *out, PUINT len) {
  for (;;) {
    WORD wlen = *(const WORD *)node, vlen = *(const WORD *)(node + 2);
    WORD type = *(const WORD *)(node + 4);
    LPCWSTR key = (LPCWSTR)(node + 6);
    const BYTE *value = ALIGN4((const BYTE *)(key + ow_wcslen(key) + 1));
    const BYTE *child, *end = node + wlen;
    LPCWSTR part;
    int n;
    while (*path == '\\')
      path++;
    if (!*path) {
      *out = (LPVOID)value;
      *len = vlen;
      return TRUE;
    }
    part = path;
    for (n = 0; part[n] && part[n] != '\\'; n++)
      ;
    path += n;
    child = ALIGN4(value + (type == 1 ? vlen * sizeof(WCHAR) : vlen));
    while (child < end && *(const WORD *)child) {
      if (key_is((LPCWSTR)(child + 6), part, n))
        break;
      child = ALIGN4(child + *(const WORD *)child);
    }
    if (child >= end || !*(const WORD *)child)
      return FALSE;
    node = child;
  }
}

static BOOL starts_with(LPCWSTR w, const char *a) {
  for (; *a; a++, w++)
    if (ow_upper(*w) != ow_upper((BYTE)*a))
      return FALSE;
  return TRUE;
}

BOOL WINAPI VerQueryValueW(LPCVOID block, LPCWSTR sub, LPVOID *out,
                           PUINT len) {
  LPSTR a;
  LPVOID v = NULL;
  UINT n = 0;
  BOOL r;
  if (!block || !sub || !out || !len)
    return FALSE;
  if (is_w_block(block))
    return query_w(block, sub, out, len);
  /* A 16-bit program's block. */
  {
    OW_FN(OW_VERSION, VerQueryValueA, return FALSE);
    a = ow_a(sub);
    r = pVerQueryValueA(block, a, &v, &n);
    ow_free(a);
  }
  if (!r)
    return FALSE;
  while (*sub == '\\')
    sub++;
  if (starts_with(sub, "StringFileInfo\\")) {
    /* A string: converted into the spare room. */
    WORD wl = *(const WORD *)block;
    struct spare *s = (struct spare *)((BYTE *)block + ((wl + 3) & ~3));
    int need;
    LPWSTR w;
    /* The spare room follows the size GetFileVersionInfoSizeA gave, which
     * may be more than the block: look for it in the next bytes. */
    while (s->magic != SPARE_MAGIC &&
           (BYTE *)s < (BYTE *)block + ((wl + 3) & ~3) * 3)
      s = (struct spare *)((BYTE *)s + 4);
    if (s->magic != SPARE_MAGIC)
      return FALSE;
    need = (ow_wlen_of(v, n ? (int)n : -1, CP_ACP) + 1) * sizeof(WCHAR);
    if (s->used + need > s->total)
      return FALSE;
    w = (LPWSTR)((BYTE *)(s + 1) + s->used);
    ow_tow(v, n ? (int)n : -1, w, need / sizeof(WCHAR));
    s->used += (need + 3) & ~3;
    *out = w;
    *len = ow_wcslen(w) + 1;
    return TRUE;
  }
  *out = v;
  *len = n;
  return TRUE;
}

/* ---- finding and installing files -------------------------------------- */

DWORD WINAPI VerFindFileW(DWORD flags, LPWSTR file, LPWSTR windir,
                          LPWSTR appdir, LPWSTR curdir, PUINT curlen,
                          LPWSTR destdir, PUINT destlen) {
  LPSTR f = ow_a(file), w = ow_a(windir), d = ow_a(appdir);
  char cur[MAX_PATH * 2], dst[MAX_PATH * 2];
  UINT cl = sizeof cur, dl = sizeof dst;
  DWORD r;
  OW_FN(OW_VERSION, VerFindFileA, r = VFF_BUFFTOOSMALL; goto out);
  cur[0] = dst[0] = 0;
  r = pVerFindFileA(flags, f, w, d, cur, &cl, dst, &dl);
  if (curlen) {
    UINT need = ow_wlen_of(cur, -1, CP_ACP) + 1;
    if (curdir && *curlen)
      ow_tow(cur, -1, curdir, *curlen);
    if (need > *curlen)
      r |= VFF_BUFFTOOSMALL;
    *curlen = need;
  }
  if (destlen) {
    UINT need = ow_wlen_of(dst, -1, CP_ACP) + 1;
    if (destdir && *destlen)
      ow_tow(dst, -1, destdir, *destlen);
    if (need > *destlen)
      r |= VFF_BUFFTOOSMALL;
    *destlen = need;
  }
out:
  ow_free(f);
  ow_free(w);
  ow_free(d);
  return r;
}

DWORD WINAPI VerInstallFileW(DWORD flags, LPWSTR src, LPWSTR dst,
                             LPWSTR srcdir, LPWSTR dstdir, LPWSTR curdir,
                             LPWSTR tmp, PUINT tmplen) {
  LPSTR s = ow_a(src), d = ow_a(dst), sd = ow_a(srcdir), dd = ow_a(dstdir),
        cd = ow_a(curdir);
  char t[MAX_PATH * 2];
  UINT tl = sizeof t;
  DWORD r;
  OW_FN(OW_VERSION, VerInstallFileA, r = VIF_CANNOTREADSRC; goto out);
  t[0] = 0;
  /* With VIFF_DONTDELETEOLD the temporary name is also an input. */
  if (tmp && tmplen && *tmplen)
    ow_toa(tmp, ow_wcsnlen(tmp, *tmplen), t, sizeof t);
  r = pVerInstallFileA(flags, s, d, sd, dd, cd, t, &tl);
  if (tmplen) {
    UINT need = ow_wlen_of(t, -1, CP_ACP) + 1;
    if (tmp && *tmplen)
      ow_tow(t, -1, tmp, *tmplen);
    if (need > *tmplen && (r & VIF_TEMPFILE))
      r |= VIF_BUFFTOOSMALL;
    *tmplen = need;
  }
out:
  ow_free(s);
  ow_free(d);
  ow_free(sd);
  ow_free(dd);
  ow_free(cd);
  return r;
}

DWORD WINAPI VerLanguageNameW(DWORD lang, LPWSTR buf, DWORD cch) {
  char a[256];
  DWORD n;
  OW_FN(OW_VERSION, VerLanguageNameA, return 0);
  a[0] = 0;
  n = pVerLanguageNameA(lang, a, sizeof a);
  if (!n)
    return 0;
  if (!buf || !cch)
    return ow_wlen_of(a, -1, CP_ACP);
  return ow_tow(a, -1, buf, cch);
}
