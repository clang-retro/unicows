/* mpr.dll: network connections and resources.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"
#include <winnetwk.h>

static const struct ow_layout netres = {
    sizeof(NETRESOURCEA),
    {OW_OFF(NETRESOURCEA, lpLocalName), OW_OFF(NETRESOURCEA, lpRemoteName),
     OW_OFF(NETRESOURCEA, lpComment), OW_OFF(NETRESOURCEA, lpProvider),
     OW_END},
    {OW_END},
    OW_END,
    OW_END};

static const struct ow_layout universal = {
    sizeof(UNIVERSAL_NAME_INFOA),
    {OW_OFF(UNIVERSAL_NAME_INFOA, lpUniversalName), OW_END},
    {OW_END},
    OW_END,
    OW_END};

static const struct ow_layout remote = {
    sizeof(REMOTE_NAME_INFOA),
    {OW_OFF(REMOTE_NAME_INFOA, lpUniversalName),
     OW_OFF(REMOTE_NAME_INFOA, lpConnectionName),
     OW_OFF(REMOTE_NAME_INFOA, lpRemainingPath), OW_END},
    {OW_END},
    OW_END,
    OW_END};

#define NR_A(w) ((LPNETRESOURCEA)ow_info_a(&netres, (const BYTE *)(w)))

/* A name returned in a buffer of *cch characters, with ERROR_MORE_DATA and
 * the size needed when it does not fit. */
static DWORD name_out(DWORD r, LPCSTR a, LPWSTR buf, LPDWORD cch) {
  DWORD need;
  if (r != NO_ERROR)
    return r;
  need = ow_wlen_of(a, -1, CP_ACP) + 1;
  if (!buf || *cch < need) {
    *cch = need;
    return ERROR_MORE_DATA;
  }
  ow_tow(a, -1, buf, *cch);
  return NO_ERROR;
}

DWORD WINAPI WNetAddConnectionW(LPCWSTR remote_, LPCWSTR password,
                                LPCWSTR local) {
  LPSTR r = ow_a(remote_), p = ow_a(password), l = ow_a(local);
  DWORD e;
  OW_FN(OW_MPR, WNetAddConnectionA, return ERROR_NOT_SUPPORTED);
  e = pWNetAddConnectionA(r, p, l);
  ow_free(r);
  ow_free(p);
  ow_free(l);
  return e;
}

DWORD WINAPI WNetAddConnection2W(LPNETRESOURCEW res, LPCWSTR password,
                                 LPCWSTR user, DWORD flags) {
  LPNETRESOURCEA a = NR_A(res);
  LPSTR p = ow_a(password), u = ow_a(user);
  DWORD e;
  OW_FN(OW_MPR, WNetAddConnection2A, return ERROR_NOT_SUPPORTED);
  e = pWNetAddConnection2A(a, p, u, flags);
  ow_free(a);
  ow_free(p);
  ow_free(u);
  return e;
}

DWORD WINAPI WNetAddConnection3W(HWND h, LPNETRESOURCEW res,
                                 LPCWSTR password, LPCWSTR user,
                                 DWORD flags) {
  LPNETRESOURCEA a = NR_A(res);
  LPSTR p = ow_a(password), u = ow_a(user);
  DWORD e;
  OW_FN(OW_MPR, WNetAddConnection3A, return ERROR_NOT_SUPPORTED);
  e = pWNetAddConnection3A(h, a, p, u, flags);
  ow_free(a);
  ow_free(p);
  ow_free(u);
  return e;
}

DWORD WINAPI WNetUseConnectionW(HWND h, LPNETRESOURCEW res, LPCWSTR password,
                                LPCWSTR user, DWORD flags, LPWSTR access,
                                LPDWORD cch, LPDWORD result) {
  LPNETRESOURCEA a = NR_A(res);
  LPSTR p = ow_a(password), u = ow_a(user);
  char acc[MAX_PATH * 2];
  DWORD n = sizeof acc, e;
  OW_FN(OW_MPR, WNetUseConnectionA, return ERROR_NOT_SUPPORTED);
  acc[0] = 0;
  e = pWNetUseConnectionA(h, a, p, u, flags, access ? acc : NULL,
                          access ? &n : NULL, result);
  if (access && cch)
    e = name_out(e, acc, access, cch);
  ow_free(a);
  ow_free(p);
  ow_free(u);
  return e;
}

DWORD WINAPI WNetCancelConnectionW(LPCWSTR name, BOOL force) {
  LPSTR a = ow_a(name);
  DWORD e;
  OW_FN(OW_MPR, WNetCancelConnectionA, return ERROR_NOT_SUPPORTED);
  e = pWNetCancelConnectionA(a, force);
  ow_free(a);
  return e;
}

DWORD WINAPI WNetCancelConnection2W(LPCWSTR name, DWORD flags, BOOL force) {
  LPSTR a = ow_a(name);
  DWORD e;
  OW_FN(OW_MPR, WNetCancelConnection2A, return ERROR_NOT_SUPPORTED);
  e = pWNetCancelConnection2A(a, flags, force);
  ow_free(a);
  return e;
}

DWORD WINAPI WNetConnectionDialog1W(LPCONNECTDLGSTRUCTW w) {
  CONNECTDLGSTRUCTA a;
  DWORD e;
  OW_FN(OW_MPR, WNetConnectionDialog1A, return ERROR_NOT_SUPPORTED);
  if (!w)
    return ERROR_INVALID_PARAMETER;
  a.cbStructure = sizeof a;
  a.hwndOwner = w->hwndOwner;
  a.lpConnRes = NR_A(w->lpConnRes);
  a.dwFlags = w->dwFlags;
  a.dwDevNum = w->dwDevNum;
  e = pWNetConnectionDialog1A(&a);
  w->dwDevNum = a.dwDevNum;
  ow_free(a.lpConnRes);
  return e;
}

DWORD WINAPI WNetDisconnectDialog1W(LPDISCDLGSTRUCTW w) {
  DISCDLGSTRUCTA a;
  DWORD e;
  OW_FN(OW_MPR, WNetDisconnectDialog1A, return ERROR_NOT_SUPPORTED);
  if (!w)
    return ERROR_INVALID_PARAMETER;
  a.cbStructure = sizeof a;
  a.hwndOwner = w->hwndOwner;
  a.lpLocalName = ow_a(w->lpLocalName);
  a.lpRemoteName = ow_a(w->lpRemoteName);
  a.dwFlags = w->dwFlags;
  e = pWNetDisconnectDialog1A(&a);
  ow_free(a.lpLocalName);
  ow_free(a.lpRemoteName);
  return e;
}

DWORD WINAPI WNetOpenEnumW(DWORD scope, DWORD type, DWORD usage,
                           LPNETRESOURCEW res, LPHANDLE h) {
  LPNETRESOURCEA a = NR_A(res);
  DWORD e;
  OW_FN(OW_MPR, WNetOpenEnumA, return ERROR_NOT_SUPPORTED);
  e = pWNetOpenEnumA(scope, type, usage, a, h);
  ow_free(a);
  return e;
}

/* The A entries take at most half the space of the W ones, so asking for
 * what fits in half the buffer never takes entries that can't be given. */
DWORD WINAPI WNetEnumResourceW(HANDLE h, LPDWORD count, LPVOID buf,
                               LPDWORD size) {
  DWORD acb, e, need;
  BYTE *a;
  OW_FN(OW_MPR, WNetEnumResourceA, return ERROR_NOT_SUPPORTED);
  if (!count || !size)
    return ERROR_INVALID_PARAMETER;
  acb = *size / 2;
  a = ow_alloc(acb ? acb : 1);
  if (!a)
    return ERROR_NOT_ENOUGH_MEMORY;
  e = pWNetEnumResourceA(h, count, a, &acb);
  if (e == ERROR_MORE_DATA) {
    *size = acb * 2;
  } else if (e == NO_ERROR) {
    if (!ow_info_w(&netres, a, *count, buf, *size, &need)) {
      *size = need;
      e = ERROR_MORE_DATA;
    }
  }
  ow_free(a);
  return e;
}

DWORD WINAPI WNetGetConnectionW(LPCWSTR local, LPWSTR remote_, LPDWORD cch) {
  LPSTR l = ow_a(local);
  char a[MAX_PATH * 2];
  DWORD n = sizeof a, e;
  OW_FN(OW_MPR, WNetGetConnectionA, return ERROR_NOT_SUPPORTED);
  if (!cch)
    return ERROR_INVALID_PARAMETER;
  e = name_out(pWNetGetConnectionA(l, a, &n), a, remote_, cch);
  ow_free(l);
  return e;
}

DWORD WINAPI WNetGetLastErrorW(LPDWORD err, LPWSTR buf, DWORD n,
                               LPWSTR name, DWORD nn) {
  char a[1024], b[256];
  DWORD e;
  OW_FN(OW_MPR, WNetGetLastErrorA, return ERROR_NOT_SUPPORTED);
  a[0] = b[0] = 0;
  e = pWNetGetLastErrorA(err, a, sizeof a, b, sizeof b);
  if (buf && n)
    ow_tow(a, -1, buf, n);
  if (name && nn)
    ow_tow(b, -1, name, nn);
  return e;
}

DWORD WINAPI WNetGetNetworkInformationW(LPCWSTR provider,
                                        LPNETINFOSTRUCT info) {
  LPSTR a = ow_a(provider);
  DWORD e;
  OW_FN(OW_MPR, WNetGetNetworkInformationA, return ERROR_NOT_SUPPORTED);
  e = pWNetGetNetworkInformationA(a, info);
  ow_free(a);
  return e;
}

DWORD WINAPI WNetGetProviderNameW(DWORD type, LPWSTR buf, LPDWORD cch) {
  char a[256];
  DWORD n = sizeof a;
  OW_FN(OW_MPR, WNetGetProviderNameA, return ERROR_NOT_SUPPORTED);
  if (!cch)
    return ERROR_INVALID_PARAMETER;
  return name_out(pWNetGetProviderNameA(type, a, &n), a, buf, cch);
}

DWORD WINAPI WNetGetUserW(LPCWSTR name, LPWSTR user, LPDWORD cch) {
  LPSTR a = ow_a(name);
  char u[256];
  DWORD n = sizeof u, e;
  OW_FN(OW_MPR, WNetGetUserA, return ERROR_NOT_SUPPORTED);
  if (!cch)
    return ERROR_INVALID_PARAMETER;
  e = name_out(pWNetGetUserA(a, u, &n), u, user, cch);
  ow_free(a);
  return e;
}

/* Calls an A function that fills a buffer with a structure and its
 * strings, growing the buffer while it asks for more. */
typedef DWORD (*fill_fn)(void *ctx, void *buf, LPDWORD cb);

static DWORD fill(fill_fn fn, void *ctx, BYTE **out) {
  DWORD cb = 1024, size, e;
  BYTE *a = NULL;
  for (;;) {
    BYTE *n = ow_realloc(a, cb);
    if (!n) {
      ow_free(a);
      return ERROR_NOT_ENOUGH_MEMORY;
    }
    a = n;
    size = cb;
    e = fn(ctx, a, &cb);
    if (e != ERROR_MORE_DATA)
      break;
    if (cb <= size)
      cb = size * 2;
  }
  if (e != NO_ERROR) {
    ow_free(a);
    a = NULL;
  }
  *out = a;
  return e;
}

static DWORD fill_out(DWORD e, const struct ow_layout *l, BYTE *a, void *buf,
                      LPDWORD cb) {
  DWORD need;
  if (e == NO_ERROR && !ow_info_w(l, a, 1, buf, *cb, &need)) {
    *cb = need;
    e = ERROR_MORE_DATA;
  }
  ow_free(a);
  return e;
}

struct name_ctx {
  LPCSTR local;
  DWORD level;
  LPNETRESOURCEA res;
  LPSTR system;
};

static DWORD call_universal(void *ctx, void *buf, LPDWORD cb) {
  struct name_ctx *c = ctx;
  OW_FN(OW_MPR, WNetGetUniversalNameA, return ERROR_NOT_SUPPORTED);
  return pWNetGetUniversalNameA(c->local, c->level, buf, cb);
}

DWORD WINAPI WNetGetUniversalNameW(LPCWSTR local, DWORD level, LPVOID buf,
                                   LPDWORD cb) {
  struct name_ctx c = {0};
  LPSTR l = ow_a(local);
  BYTE *a;
  DWORD e;
  if (!cb)
    return ERROR_INVALID_PARAMETER;
  if (level != UNIVERSAL_NAME_INFO_LEVEL && level != REMOTE_NAME_INFO_LEVEL)
    return ERROR_INVALID_LEVEL;
  c.local = l;
  c.level = level;
  e = fill(call_universal, &c, &a);
  ow_free(l);
  return fill_out(e, level == UNIVERSAL_NAME_INFO_LEVEL ? &universal : &remote,
                  a, buf, cb);
}

static DWORD call_parent(void *ctx, void *buf, LPDWORD cb) {
  struct name_ctx *c = ctx;
  OW_FN(OW_MPR, WNetGetResourceParentA, return ERROR_NOT_SUPPORTED);
  return pWNetGetResourceParentA(c->res, buf, cb);
}

DWORD WINAPI WNetGetResourceParentW(LPNETRESOURCEW res, LPVOID buf,
                                    LPDWORD cb) {
  struct name_ctx c = {0};
  BYTE *a;
  DWORD e;
  if (!cb)
    return ERROR_INVALID_PARAMETER;
  c.res = NR_A(res);
  e = fill(call_parent, &c, &a);
  ow_free(c.res);
  return fill_out(e, &netres, a, buf, cb);
}

static DWORD call_info(void *ctx, void *buf, LPDWORD cb) {
  struct name_ctx *c = ctx;
  OW_FN(OW_MPR, WNetGetResourceInformationA, return ERROR_NOT_SUPPORTED);
  return pWNetGetResourceInformationA(c->res, buf, cb, &c->system);
}

DWORD WINAPI WNetGetResourceInformationW(LPNETRESOURCEW res, LPVOID buf,
                                         LPDWORD cb, LPWSTR *system) {
  struct name_ctx c = {0};
  BYTE *a;
  DWORD e, need, sys;
  if (!cb)
    return ERROR_INVALID_PARAMETER;
  c.res = NR_A(res);
  e = fill(call_info, &c, &a);
  ow_free(c.res);
  if (system)
    *system = NULL;
  if (e != NO_ERROR)
    return e;
  /* The rest of the path, after the structure's strings. */
  sys = c.system ? (ow_wlen_of(c.system, -1, CP_ACP) + 1) * sizeof(WCHAR) : 0;
  ow_info_w(&netres, a, 1, NULL, 0, &need);
  if (!buf || *cb < need + sys) {
    *cb = need + sys;
    ow_free(a);
    return ERROR_MORE_DATA;
  }
  ow_info_w(&netres, a, 1, buf, *cb, &need);
  if (c.system) {
    LPWSTR s = (LPWSTR)((BYTE *)buf + need);
    ow_tow(c.system, -1, s, sys / sizeof(WCHAR));
    if (system)
      *system = s;
  }
  ow_free(a);
  return NO_ERROR;
}

DWORD WINAPI MultinetGetConnectionPerformanceW(LPNETRESOURCEW res,
                                               LPNETCONNECTINFOSTRUCT info) {
  LPNETRESOURCEA a = NR_A(res);
  DWORD e;
  OW_FN(OW_MPR, MultinetGetConnectionPerformanceA,
        return ERROR_NOT_SUPPORTED);
  e = pMultinetGetConnectionPerformanceA(a, info);
  ow_free(a);
  return e;
}
