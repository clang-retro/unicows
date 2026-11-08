/* advapi32: the registry, the user name, hardware profiles, IsTextUnicode
 * and the CryptoAPI provider functions (from Windows 95 OSR2 and Internet
 * Explorer 3, loaded when first used).
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"
#include <wincrypt.h>

/* ---- registry values --------------------------------------------------- */

static BOOL is_string(DWORD type) {
  return type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ;
}

/* The W form of A registry data: strings converted, other types as they
 * are. Returns a heap buffer and its size in bytes. */
static BYTE *value_w(DWORD type, const BYTE *a, DWORD cb, DWORD *wcb) {
  BYTE *w;
  if (!is_string(type)) {
    w = ow_alloc(cb);
    if (w)
      ow_memcpy(w, a, cb);
    *wcb = cb;
    return w;
  }
  {
    int n = cb ? ow_mb2wc(CP_ACP, 0, (LPCSTR)a, cb, NULL, 0) : 0;
    w = ow_alloc((n + 1) * sizeof(WCHAR));
    if (w && n)
      ow_mb2wc(CP_ACP, 0, (LPCSTR)a, cb, (LPWSTR)w, n);
    *wcb = n * sizeof(WCHAR);
  }
  return w;
}

/* The A form of W registry data. */
static BYTE *value_a(DWORD type, const BYTE *w, DWORD cb, DWORD *acb) {
  BYTE *a;
  if (!is_string(type) || !w) {
    a = ow_alloc(cb);
    if (a && w)
      ow_memcpy(a, w, cb);
    *acb = cb;
    return a;
  }
  {
    int nw = cb / sizeof(WCHAR);
    int n = nw ? ow_wc2mb(CP_ACP, 0, (LPCWSTR)w, nw, NULL, 0) : 0;
    a = ow_alloc(n + 1);
    if (a && n)
      ow_wc2mb(CP_ACP, 0, (LPCWSTR)w, nw, (LPSTR)a, n);
    *acb = n;
  }
  return a;
}

/* Copies W data into the caller's buffer the way RegQueryValueEx returns
 * it. */
static LONG ret_value(const BYTE *w, DWORD wcb, LPBYTE data, LPDWORD cb) {
  if (!cb)
    return ERROR_SUCCESS;
  if (!data) {
    *cb = wcb;
    return ERROR_SUCCESS;
  }
  if (*cb < wcb) {
    *cb = wcb;
    return ERROR_MORE_DATA;
  }
  ow_memcpy(data, w, wcb);
  *cb = wcb;
  return ERROR_SUCCESS;
}

LONG WINAPI RegQueryValueExW(HKEY key, LPCWSTR name, LPDWORD reserved,
                             LPDWORD type, LPBYTE data, LPDWORD cb) {
  LPSTR n = ow_a(name);
  DWORD t = 0, acb = 0, wcb;
  BYTE *a, *w;
  LONG r = RegQueryValueExA(key, n, reserved, &t, NULL, &acb);
  if (r != ERROR_SUCCESS) {
    ow_free(n);
    return r;
  }
  if (type)
    *type = t;
  if (!data && !is_string(t)) {
    ow_free(n);
    if (cb)
      *cb = acb;
    return ERROR_SUCCESS;
  }
  if (!data && !cb) {
    ow_free(n);
    return ERROR_SUCCESS;
  }
  a = ow_alloc(acb + 2);
  if (!a) {
    ow_free(n);
    return ERROR_NOT_ENOUGH_MEMORY;
  }
  r = RegQueryValueExA(key, n, reserved, &t, a, &acb);
  ow_free(n);
  if (r != ERROR_SUCCESS) {
    ow_free(a);
    return r;
  }
  w = value_w(t, a, acb, &wcb);
  ow_free(a);
  if (!w)
    return ERROR_NOT_ENOUGH_MEMORY;
  r = ret_value(w, wcb, data, cb);
  ow_free(w);
  return r;
}

LONG WINAPI RegSetValueExW(HKEY key, LPCWSTR name, DWORD reserved,
                           DWORD type, const BYTE *data, DWORD cb) {
  LPSTR n = ow_a(name);
  DWORD acb;
  BYTE *a = value_a(type, data, cb, &acb);
  LONG r;
  if (!a && cb) {
    ow_free(n);
    return ERROR_NOT_ENOUGH_MEMORY;
  }
  r = RegSetValueExA(key, n, reserved, type, a, acb);
  ow_free(n);
  ow_free(a);
  return r;
}

LONG WINAPI RegQueryValueW(HKEY key, LPCWSTR sub, LPWSTR data, PLONG cb) {
  LPSTR s = ow_a(sub);
  LONG acb = 0, r;
  LPSTR a;
  int wl;
  r = RegQueryValueA(key, s, NULL, &acb);
  if (r != ERROR_SUCCESS) {
    ow_free(s);
    return r;
  }
  a = ow_alloc(acb + 2);
  if (!a) {
    ow_free(s);
    return ERROR_NOT_ENOUGH_MEMORY;
  }
  r = RegQueryValueA(key, s, a, &acb);
  ow_free(s);
  if (r != ERROR_SUCCESS) {
    ow_free(a);
    return r;
  }
  wl = ow_wlen_of(a, -1, CP_ACP) + 1;
  if (!cb) {
    ow_free(a);
    return ERROR_SUCCESS;
  }
  if (data) {
    if (*cb < (LONG)(wl * sizeof(WCHAR))) {
      *cb = wl * sizeof(WCHAR);
      ow_free(a);
      return ERROR_MORE_DATA;
    }
    ow_tow(a, -1, data, wl);
  }
  *cb = wl * sizeof(WCHAR);
  ow_free(a);
  return ERROR_SUCCESS;
}

LONG WINAPI RegSetValueW(HKEY key, LPCWSTR sub, DWORD type, LPCWSTR data,
                         DWORD cb) {
  LPSTR s = ow_a(sub), d = ow_a(data);
  LONG r;
  (void)cb;
  r = RegSetValueA(key, s, type, d, d ? ow_strlen(d) : 0);
  ow_free(s);
  ow_free(d);
  return r;
}

LONG WINAPI RegDeleteValueW(HKEY key, LPCWSTR name) {
  LPSTR n = ow_a(name);
  LONG r = RegDeleteValueA(key, n);
  ow_free(n);
  return r;
}

LONG WINAPI RegEnumValueW(HKEY key, DWORD i, LPWSTR name, LPDWORD cchname,
                          LPDWORD reserved, LPDWORD type, LPBYTE data,
                          LPDWORD cb) {
  DWORD maxname = 0, maxdata = 0, ncb, dcb, t = 0, wl, wcb;
  LPSTR n;
  BYTE *d, *w;
  LONG r;
  if (!cchname)
    return ERROR_INVALID_PARAMETER;
  r = RegQueryInfoKeyA(key, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                       &maxname, &maxdata, NULL, NULL);
  if (r != ERROR_SUCCESS)
    return r;
  ncb = maxname + 2;
  dcb = maxdata + 2;
  n = ow_alloc(ncb);
  d = ow_alloc(dcb);
  if (!n || !d) {
    ow_free(n);
    ow_free(d);
    return ERROR_NOT_ENOUGH_MEMORY;
  }
  r = RegEnumValueA(key, i, n, &ncb, reserved, &t, d, &dcb);
  if (r != ERROR_SUCCESS) {
    ow_free(n);
    ow_free(d);
    return r;
  }
  wl = ow_wlen_of(n, ncb, CP_ACP);
  if (!name || *cchname < wl + 1) {
    *cchname = wl + 1;
    ow_free(n);
    ow_free(d);
    return ERROR_MORE_DATA;
  }
  ow_tow(n, ncb, name, *cchname);
  *cchname = wl;
  ow_free(n);
  if (type)
    *type = t;
  w = value_w(t, d, dcb, &wcb);
  ow_free(d);
  if (!w)
    return ERROR_NOT_ENOUGH_MEMORY;
  r = ret_value(w, wcb, data, cb);
  ow_free(w);
  return r;
}

LONG WINAPI RegQueryMultipleValuesW(HKEY key, PVALENTW list, DWORD n,
                                    LPWSTR buf, LPDWORD total) {
  DWORD need = 0, pos = 0;
  LONG r = ERROR_SUCCESS;
  if (!list || !total)
    return ERROR_INVALID_PARAMETER;
  for (DWORD i = 0; i < n; i++) {
    DWORD cb = 0, type;
    r = RegQueryValueExW(key, list[i].ve_valuename, NULL, &type, NULL, &cb);
    if (r != ERROR_SUCCESS)
      return r;
    list[i].ve_type = type;
    list[i].ve_valuelen = cb;
    need += cb;
  }
  if (!buf || *total < need) {
    *total = need;
    return ERROR_MORE_DATA;
  }
  for (DWORD i = 0; i < n; i++) {
    DWORD cb = list[i].ve_valuelen;
    r = RegQueryValueExW(key, list[i].ve_valuename, NULL, NULL,
                         (BYTE *)buf + pos, &cb);
    if (r != ERROR_SUCCESS)
      return r;
    list[i].ve_valueptr = (DWORD_PTR)((BYTE *)buf + pos);
    list[i].ve_valuelen = cb;
    pos += cb;
  }
  *total = pos;
  return ERROR_SUCCESS;
}

/* ---- registry keys ------------------------------------------------------- */

LONG WINAPI RegConnectRegistryW(LPCWSTR machine, HKEY key, PHKEY out) {
  LPSTR m = ow_a(machine);
  LONG r = RegConnectRegistryA(m, key, out);
  ow_free(m);
  return r;
}

LONG WINAPI RegCreateKeyExW(HKEY key, LPCWSTR sub, DWORD reserved,
                            LPWSTR cls, DWORD opts, REGSAM sam,
                            LPSECURITY_ATTRIBUTES sa, PHKEY out,
                            LPDWORD disp) {
  LPSTR s = ow_a(sub), c = ow_a(cls);
  LONG r = RegCreateKeyExA(key, s, reserved, c, opts, sam, sa, out, disp);
  ow_free(s);
  ow_free(c);
  return r;
}

LONG WINAPI RegCreateKeyW(HKEY key, LPCWSTR sub, PHKEY out) {
  LPSTR s = ow_a(sub);
  LONG r = RegCreateKeyA(key, s, out);
  ow_free(s);
  return r;
}

LONG WINAPI RegOpenKeyExW(HKEY key, LPCWSTR sub, DWORD opts, REGSAM sam,
                          PHKEY out) {
  LPSTR s = ow_a(sub);
  LONG r = RegOpenKeyExA(key, s, opts, sam, out);
  ow_free(s);
  return r;
}

LONG WINAPI RegOpenKeyW(HKEY key, LPCWSTR sub, PHKEY out) {
  LPSTR s = ow_a(sub);
  LONG r = RegOpenKeyA(key, s, out);
  ow_free(s);
  return r;
}

LONG WINAPI RegDeleteKeyW(HKEY key, LPCWSTR sub) {
  LPSTR s = ow_a(sub);
  LONG r = RegDeleteKeyA(key, s);
  ow_free(s);
  return r;
}

LONG WINAPI RegEnumKeyExW(HKEY key, DWORD i, LPWSTR name, LPDWORD cchname,
                          LPDWORD reserved, LPWSTR cls, LPDWORD cchcls,
                          PFILETIME ft) {
  char n[512], c[512];
  DWORD nl = sizeof n, cl = sizeof c, wl;
  LONG r;
  if (!cchname)
    return ERROR_INVALID_PARAMETER;
  r = RegEnumKeyExA(key, i, n, &nl, reserved, cls ? c : NULL,
                    cls ? &cl : NULL, ft);
  if (r != ERROR_SUCCESS)
    return r;
  wl = ow_wlen_of(n, nl, CP_ACP);
  if (!name || *cchname < wl + 1) {
    *cchname = wl + 1;
    return ERROR_MORE_DATA;
  }
  ow_tow(n, nl, name, *cchname);
  *cchname = wl;
  if (cls && cchcls) {
    DWORD cwl = ow_wlen_of(c, cl, CP_ACP);
    if (*cchcls < cwl + 1) {
      *cchcls = cwl + 1;
      return ERROR_MORE_DATA;
    }
    ow_tow(c, cl, cls, *cchcls);
    *cchcls = cwl;
  }
  return ERROR_SUCCESS;
}

LONG WINAPI RegEnumKeyW(HKEY key, DWORD i, LPWSTR name, DWORD cch) {
  char n[512];
  LONG r = RegEnumKeyA(key, i, n, sizeof n);
  if (r != ERROR_SUCCESS)
    return r;
  if (!name || (DWORD)ow_wlen_of(n, -1, CP_ACP) + 1 > cch)
    return ERROR_MORE_DATA;
  ow_tow(n, -1, name, cch);
  return ERROR_SUCCESS;
}

LONG WINAPI RegQueryInfoKeyW(HKEY key, LPWSTR cls, LPDWORD cchcls,
                             LPDWORD reserved, LPDWORD subkeys,
                             LPDWORD maxsub, LPDWORD maxcls, LPDWORD values,
                             LPDWORD maxname, LPDWORD maxdata,
                             LPDWORD secdesc, PFILETIME ft) {
  char c[512];
  DWORD cl = sizeof c, md = 0;
  LONG r = RegQueryInfoKeyA(key, cls ? c : NULL, cls ? &cl : NULL, reserved,
                            subkeys, maxsub, maxcls, values, maxname, &md,
                            secdesc, ft);
  if (r != ERROR_SUCCESS)
    return r;
  /* Lengths of names in bytes are at least those in WCHARs; string data
     takes twice the bytes in Unicode. */
  if (maxdata)
    *maxdata = md * 2;
  if (cls && cchcls) {
    DWORD wl = ow_wlen_of(c, cl, CP_ACP);
    if (*cchcls < wl + 1) {
      *cchcls = wl + 1;
      return ERROR_MORE_DATA;
    }
    ow_tow(c, cl, cls, *cchcls);
    *cchcls = wl;
  }
  return ERROR_SUCCESS;
}

LONG WINAPI RegLoadKeyW(HKEY key, LPCWSTR sub, LPCWSTR file) {
  LPSTR s = ow_a(sub), f = ow_acp(file, ow_fcp());
  LONG r = RegLoadKeyA(key, s, f);
  ow_free(s);
  ow_free(f);
  return r;
}

LONG WINAPI RegUnLoadKeyW(HKEY key, LPCWSTR sub) {
  LPSTR s = ow_a(sub);
  LONG r = RegUnLoadKeyA(key, s);
  ow_free(s);
  return r;
}

LONG WINAPI RegReplaceKeyW(HKEY key, LPCWSTR sub, LPCWSTR file,
                           LPCWSTR old) {
  UINT cp = ow_fcp();
  LPSTR s = ow_a(sub), f = ow_acp(file, cp), o = ow_acp(old, cp);
  LONG r = RegReplaceKeyA(key, s, f, o);
  ow_free(s);
  ow_free(f);
  ow_free(o);
  return r;
}

LONG WINAPI RegSaveKeyW(HKEY key, LPCWSTR file, LPSECURITY_ATTRIBUTES sa) {
  LPSTR f = ow_acp(file, ow_fcp());
  LONG r = RegSaveKeyA(key, f, sa);
  ow_free(f);
  return r;
}

/* ---- users and profiles ------------------------------------------------- */

BOOL WINAPI GetUserNameW(LPWSTR buf, LPDWORD size) {
  char a[512];
  DWORD n = sizeof a, wl;
  if (!size) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!GetUserNameA(a, &n))
    return FALSE;
  wl = ow_wlen_of(a, -1, CP_ACP) + 1;
  if (!buf || *size < wl) {
    *size = wl;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  ow_tow(a, -1, buf, *size);
  *size = wl;
  return TRUE;
}

BOOL WINAPI GetCurrentHwProfileW(LPHW_PROFILE_INFOW w) {
  HW_PROFILE_INFOA a;
  OW_FN(OW_ADVAPI32, GetCurrentHwProfileA, return FALSE);
  if (!w || !pGetCurrentHwProfileA(&a))
    return FALSE;
  w->dwDockInfo = a.dwDockInfo;
  ow_tow(a.szHwProfileGuid, -1, w->szHwProfileGuid, HW_PROFILE_GUIDLEN);
  ow_tow(a.szHwProfileName, -1, w->szHwProfileName, MAX_PROFILE_LEN);
  return TRUE;
}

/* ---- IsTextUnicode ------------------------------------------------------ */

BOOL WINAPI IsTextUnicode(const VOID *buf, int len, LPINT flags) {
  const WCHAR *s = buf;
  const BYTE *b = buf;
  int n = len / 2, want = flags ? *flags : ~0, got = 0;
  int ascii = 0, rascii = 0, ctl = 0, rctl = 0;
  if (!buf || len < 2) {
    if (flags)
      *flags = 0;
    return FALSE;
  }
  if (len & 1)
    got |= IS_TEXT_UNICODE_ODD_LENGTH;
  if (s[0] == 0xfeff)
    got |= IS_TEXT_UNICODE_SIGNATURE;
  if (s[0] == 0xfffe)
    got |= IS_TEXT_UNICODE_REVERSE_SIGNATURE;
  for (int i = 0; i < n; i++) {
    WCHAR c = s[i], r = (WCHAR)((c << 8) | (c >> 8));
    if (c && c < 0x80)
      ascii++;
    if (r && r < 0x80)
      rascii++;
    if (c == '\r' || c == '\n' || c == '\t' || c == ' ')
      ctl++;
    if (r == '\r' || r == '\n' || r == '\t' || r == ' ')
      rctl++;
    if (c == 0xffff || c == 0xfffe || (c == 0 && i < n - 1) || c == 0x0a0d ||
        c == 0x0d0a)
      got |= IS_TEXT_UNICODE_ILLEGAL_CHARS;
  }
  for (int i = 0; i < len; i++)
    if (!b[i]) {
      got |= IS_TEXT_UNICODE_NULL_BYTES;
      break;
    }
  if (ascii == n)
    got |= IS_TEXT_UNICODE_ASCII16;
  if (rascii == n)
    got |= IS_TEXT_UNICODE_REVERSE_ASCII16;
  if (ctl)
    got |= IS_TEXT_UNICODE_CONTROLS;
  if (rctl)
    got |= IS_TEXT_UNICODE_REVERSE_CONTROLS;
  /* Text with many bytes of 0 in odd places is little-endian Unicode. */
  {
    int high0 = 0, low0 = 0;
    for (int i = 0; i < n; i++) {
      if (!(s[i] & 0xff00))
        high0++;
      if (!(s[i] & 0x00ff))
        low0++;
    }
    if (n && high0 * 3 >= n * 2)
      got |= IS_TEXT_UNICODE_STATISTICS;
    if (n && low0 * 3 >= n * 2)
      got |= IS_TEXT_UNICODE_REVERSE_STATISTICS;
  }
  if (flags)
    *flags = got & want;
  if (got & (IS_TEXT_UNICODE_REVERSE_MASK | IS_TEXT_UNICODE_NOT_UNICODE_MASK |
             IS_TEXT_UNICODE_ODD_LENGTH))
    return (got & IS_TEXT_UNICODE_SIGNATURE) != 0;
  return (got & (IS_TEXT_UNICODE_SIGNATURE | IS_TEXT_UNICODE_ASCII16 |
                 IS_TEXT_UNICODE_STATISTICS)) != 0;
}

/* ---- CryptoAPI providers ------------------------------------------------ */

BOOL WINAPI CryptAcquireContextW(HCRYPTPROV *prov, LPCWSTR container,
                                 LPCWSTR provider, DWORD type, DWORD flags) {
  LPSTR c, p;
  BOOL r;
  OW_FN(OW_ADVAPI32, CryptAcquireContextA, return FALSE);
  c = ow_a(container);
  p = ow_a(provider);
  r = pCryptAcquireContextA(prov, c, p, type, flags);
  ow_free(c);
  ow_free(p);
  return r;
}

/* Returns a name from an A function that fills a buffer of *cb bytes, as
 * W in name (*cb bytes). */
static BOOL ret_name(LPCSTR a, LPWSTR name, DWORD *cb) {
  DWORD need = (ow_wlen_of(a, -1, CP_ACP) + 1) * sizeof(WCHAR);
  if (!cb) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!name) {
    *cb = need;
    return TRUE;
  }
  if (*cb < need) {
    *cb = need;
    SetLastError(ERROR_MORE_DATA);
    return FALSE;
  }
  ow_tow(a, -1, name, *cb / sizeof(WCHAR));
  *cb = need;
  return TRUE;
}

BOOL WINAPI CryptEnumProvidersW(DWORD i, DWORD *reserved, DWORD flags,
                                DWORD *type, LPWSTR name, DWORD *cb) {
  DWORD n = 0;
  LPSTR a;
  BOOL r;
  OW_FN(OW_ADVAPI32, CryptEnumProvidersA, return FALSE);
  if (!pCryptEnumProvidersA(i, reserved, flags, type, NULL, &n))
    return FALSE;
  a = ow_alloc(n + 1);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  r = pCryptEnumProvidersA(i, reserved, flags, type, a, &n) &&
      ret_name(a, name, cb);
  ow_free(a);
  return r;
}

BOOL WINAPI CryptEnumProviderTypesW(DWORD i, DWORD *reserved, DWORD flags,
                                    DWORD *type, LPWSTR name, DWORD *cb) {
  DWORD n = 0;
  LPSTR a;
  BOOL r;
  OW_FN(OW_ADVAPI32, CryptEnumProviderTypesA, return FALSE);
  if (!pCryptEnumProviderTypesA(i, reserved, flags, type, NULL, &n))
    return FALSE;
  a = ow_alloc(n + 1);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  r = pCryptEnumProviderTypesA(i, reserved, flags, type, a, &n) &&
      ret_name(a, name, cb);
  ow_free(a);
  return r;
}

BOOL WINAPI CryptGetDefaultProviderW(DWORD type, DWORD *reserved, DWORD flags,
                                     LPWSTR name, DWORD *cb) {
  DWORD n = 0;
  LPSTR a;
  BOOL r;
  OW_FN(OW_ADVAPI32, CryptGetDefaultProviderA, return FALSE);
  if (!pCryptGetDefaultProviderA(type, reserved, flags, NULL, &n))
    return FALSE;
  a = ow_alloc(n + 1);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  r = pCryptGetDefaultProviderA(type, reserved, flags, a, &n) &&
      ret_name(a, name, cb);
  ow_free(a);
  return r;
}

BOOL WINAPI CryptSetProviderExW(LPCWSTR name, DWORD type, DWORD *reserved,
                                DWORD flags) {
  LPSTR a;
  BOOL r;
  OW_FN(OW_ADVAPI32, CryptSetProviderExA, return FALSE);
  a = ow_a(name);
  r = pCryptSetProviderExA(a, type, reserved, flags);
  ow_free(a);
  return r;
}

BOOL WINAPI CryptSetProviderW(LPCWSTR name, DWORD type) {
  LPSTR a;
  BOOL r;
  OW_FN(OW_ADVAPI32, CryptSetProviderA, return FALSE);
  a = ow_a(name);
  r = pCryptSetProviderA(a, type);
  ow_free(a);
  return r;
}

BOOL WINAPI CryptSignHashW(HCRYPTHASH hash, DWORD spec, LPCWSTR desc,
                           DWORD flags, BYTE *sig, DWORD *len) {
  LPSTR a;
  BOOL r;
  OW_FN(OW_ADVAPI32, CryptSignHashA, return FALSE);
  a = ow_a(desc);
  r = pCryptSignHashA(hash, spec, a, flags, sig, len);
  ow_free(a);
  return r;
}

BOOL WINAPI CryptVerifySignatureW(HCRYPTHASH hash, const BYTE *sig,
                                  DWORD len, HCRYPTKEY key, LPCWSTR desc,
                                  DWORD flags) {
  LPSTR a;
  BOOL r;
  OW_FN(OW_ADVAPI32, CryptVerifySignatureA, return FALSE);
  a = ow_a(desc);
  r = pCryptVerifySignatureA(hash, sig, len, key, a, flags);
  ow_free(a);
  return r;
}
