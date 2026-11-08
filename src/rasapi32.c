/* rasapi32.dll: dial-up networking. Its structures come in versions told
 * apart by dwSize, so each is described by its fields (their offsets in
 * the A and W forms) and the sizes of its versions, newest first. When
 * the system does not know a version, the next older one is tried.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"
#include <ras.h>
#include <raserror.h>

#define UP4(n) (((n) + 3) & ~3)

struct rf {
  WORD a, w; /* offsets */
  WORD n;    /* bytes, or characters of a string */
  BYTE str;
};
#define RAW(T, f) {offsetof(T##A, f), offsetof(T##W, f), sizeof(((T##A *)0)->f), 0}
#define STR(T, f) {offsetof(T##A, f), offsetof(T##W, f), sizeof(((T##A *)0)->f), 1}
#define VER(T, f) {UP4(offsetof(T##A, f)), UP4(offsetof(T##W, f))}
#define ALL(T) {sizeof(T##A), sizeof(T##W)}

struct rs {
  const struct rf *f;
  int nf;
  int nv;
  DWORD v[5][2]; /* A and W sizes */
};
#define FIELDS(x) x, sizeof x / sizeof *x

static const struct rf conn_f[] = {
    RAW(RASCONN, hrasconn),     STR(RASCONN, szEntryName),
    STR(RASCONN, szDeviceType), STR(RASCONN, szDeviceName),
    STR(RASCONN, szPhonebook),  RAW(RASCONN, dwSubEntry),
    RAW(RASCONN, guidEntry),    RAW(RASCONN, dwFlags),
    RAW(RASCONN, luid)};
static const struct rs conn = {FIELDS(conn_f), 4,
                               {ALL(RASCONN), VER(RASCONN, luid),
                                VER(RASCONN, guidEntry),
                                VER(RASCONN, szPhonebook)}};

static const struct rf status_f[] = {
    RAW(RASCONNSTATUS, rasconnstate), RAW(RASCONNSTATUS, dwError),
    STR(RASCONNSTATUS, szDeviceType), STR(RASCONNSTATUS, szDeviceName),
    STR(RASCONNSTATUS, szPhoneNumber)};
static const struct rs status = {FIELDS(status_f), 2,
                                 {ALL(RASCONNSTATUS),
                                  VER(RASCONNSTATUS, szPhoneNumber)}};

static const struct rf dial_f[] = {
    STR(RASDIALPARAMS, szEntryName),      STR(RASDIALPARAMS, szPhoneNumber),
    STR(RASDIALPARAMS, szCallbackNumber), STR(RASDIALPARAMS, szUserName),
    STR(RASDIALPARAMS, szPassword),       STR(RASDIALPARAMS, szDomain),
    RAW(RASDIALPARAMS, dwSubEntry),       RAW(RASDIALPARAMS, dwCallbackId)};
static const struct rs dial = {FIELDS(dial_f), 2,
                               {ALL(RASDIALPARAMS),
                                VER(RASDIALPARAMS, dwSubEntry)}};

static const struct rf ename_f[] = {STR(RASENTRYNAME, szEntryName),
                                    RAW(RASENTRYNAME, dwFlags),
                                    STR(RASENTRYNAME, szPhonebookPath)};
static const struct rs ename = {FIELDS(ename_f), 2,
                                {ALL(RASENTRYNAME),
                                 VER(RASENTRYNAME, dwFlags)}};

static const struct rf dev_f[] = {STR(RASDEVINFO, szDeviceType),
                                  STR(RASDEVINFO, szDeviceName)};
static const struct rs dev = {FIELDS(dev_f), 1, {ALL(RASDEVINFO)}};

static const struct rf entry_f[] = {
    RAW(RASENTRY, dwfOptions),
    RAW(RASENTRY, dwCountryID),
    RAW(RASENTRY, dwCountryCode),
    STR(RASENTRY, szAreaCode),
    STR(RASENTRY, szLocalPhoneNumber),
    RAW(RASENTRY, dwAlternateOffset),
    RAW(RASENTRY, ipaddr),
    RAW(RASENTRY, ipaddrDns),
    RAW(RASENTRY, ipaddrDnsAlt),
    RAW(RASENTRY, ipaddrWins),
    RAW(RASENTRY, ipaddrWinsAlt),
    RAW(RASENTRY, dwFrameSize),
    RAW(RASENTRY, dwfNetProtocols),
    RAW(RASENTRY, dwFramingProtocol),
    STR(RASENTRY, szScript),
    STR(RASENTRY, szAutodialDll),
    STR(RASENTRY, szAutodialFunc),
    STR(RASENTRY, szDeviceType),
    STR(RASENTRY, szDeviceName),
    STR(RASENTRY, szX25PadType),
    STR(RASENTRY, szX25Address),
    STR(RASENTRY, szX25Facilities),
    STR(RASENTRY, szX25UserData),
    RAW(RASENTRY, dwChannels),
    RAW(RASENTRY, dwReserved1),
    RAW(RASENTRY, dwReserved2),
    RAW(RASENTRY, dwSubEntries),
    RAW(RASENTRY, dwDialMode),
    RAW(RASENTRY, dwDialExtraPercent),
    RAW(RASENTRY, dwDialExtraSampleSeconds),
    RAW(RASENTRY, dwHangUpExtraPercent),
    RAW(RASENTRY, dwHangUpExtraSampleSeconds),
    RAW(RASENTRY, dwIdleDisconnectSeconds),
    RAW(RASENTRY, dwType),
    RAW(RASENTRY, dwEncryptionType),
    RAW(RASENTRY, dwCustomAuthKey),
    RAW(RASENTRY, guidId),
    STR(RASENTRY, szCustomDialDll),
    RAW(RASENTRY, dwVpnStrategy),
    RAW(RASENTRY, dwfOptions2),
    RAW(RASENTRY, dwfOptions3),
    STR(RASENTRY, szDnsSuffix),
    RAW(RASENTRY, dwTcpWindowSize),
    STR(RASENTRY, szPrerequisitePbk),
    STR(RASENTRY, szPrerequisiteEntry),
    RAW(RASENTRY, dwRedialCount),
    RAW(RASENTRY, dwRedialPause)};
static const struct rs entry = {FIELDS(entry_f), 4,
                                {ALL(RASENTRY), VER(RASENTRY, dwfOptions2),
                                 VER(RASENTRY, dwType),
                                 VER(RASENTRY, dwSubEntries)}};

static const struct rf sub_f[] = {
    RAW(RASSUBENTRY, dwfFlags), STR(RASSUBENTRY, szDeviceType),
    STR(RASSUBENTRY, szDeviceName), STR(RASSUBENTRY, szLocalPhoneNumber),
    RAW(RASSUBENTRY, dwAlternateOffset)};
static const struct rs sub = {FIELDS(sub_f), 1, {ALL(RASSUBENTRY)}};

/* The projections. */
static const struct rf amb_f[] = {RAW(RASAMB, dwError),
                                  STR(RASAMB, szNetBiosError),
                                  RAW(RASAMB, bLana)};
static const struct rs amb = {FIELDS(amb_f), 1, {ALL(RASAMB)}};
static const struct rf nbf_f[] = {
    RAW(RASPPPNBF, dwError), RAW(RASPPPNBF, dwNetBiosError),
    STR(RASPPPNBF, szNetBiosError), STR(RASPPPNBF, szWorkstationName),
    RAW(RASPPPNBF, bLana)};
static const struct rs nbf = {FIELDS(nbf_f), 1, {ALL(RASPPPNBF)}};
static const struct rf ipx_f[] = {RAW(RASPPPIPX, dwError),
                                  STR(RASPPPIPX, szIpxAddress)};
static const struct rs ipx = {FIELDS(ipx_f), 1, {ALL(RASPPPIPX)}};
static const struct rf ip_f[] = {
    RAW(RASPPPIP, dwError), STR(RASPPPIP, szIpAddress),
    STR(RASPPPIP, szServerIpAddress), RAW(RASPPPIP, dwOptions),
    RAW(RASPPPIP, dwServerOptions)};
static const struct rs ip = {FIELDS(ip_f), 3,
                             {ALL(RASPPPIP), VER(RASPPPIP, dwOptions),
                              VER(RASPPPIP, szServerIpAddress)}};
static const struct rf lcp_f[] = {
    RAW(RASPPPLCP, fBundled),
    RAW(RASPPPLCP, dwError),
    RAW(RASPPPLCP, dwAuthenticationProtocol),
    RAW(RASPPPLCP, dwAuthenticationData),
    RAW(RASPPPLCP, dwEapTypeId),
    RAW(RASPPPLCP, dwServerAuthenticationProtocol),
    RAW(RASPPPLCP, dwServerAuthenticationData),
    RAW(RASPPPLCP, dwServerEapTypeId),
    RAW(RASPPPLCP, fMultilink),
    RAW(RASPPPLCP, dwTerminateReason),
    RAW(RASPPPLCP, dwServerTerminateReason),
    STR(RASPPPLCP, szReplyMessage),
    RAW(RASPPPLCP, dwOptions),
    RAW(RASPPPLCP, dwServerOptions)};
static const struct rs lcp = {FIELDS(lcp_f), 3,
                              {ALL(RASPPPLCP), VER(RASPPPLCP, dwOptions),
                               VER(RASPPPLCP, dwError)}};
static const struct rf slip_f[] = {RAW(RASSLIP, dwError),
                                   STR(RASSLIP, szIpAddress)};
static const struct rs slip = {FIELDS(slip_f), 1, {ALL(RASSLIP)}};

static int version(const struct rs *s, DWORD wsize) {
  int i;
  for (i = 0; i < s->nv; i++)
    if (s->v[i][1] == wsize)
      return i;
  return -1;
}

static void to_a(const struct rs *s, const BYTE *w, DWORD wsize, BYTE *a,
                 DWORD asize) {
  int i;
  ow_memset(a, 0, asize);
  for (i = 0; i < s->nf; i++) {
    const struct rf *f = &s->f[i];
    DWORD wn = f->str ? f->n * 2u : f->n;
    if (f->w + wn > wsize || f->a + f->n > asize)
      continue;
    if (f->str)
      ow_toa((LPCWSTR)(w + f->w), ow_wcsnlen((LPCWSTR)(w + f->w), f->n),
             (LPSTR)(a + f->a), f->n);
    else
      ow_memcpy(a + f->a, w + f->w, f->n);
  }
  *(DWORD *)a = asize;
}

static void to_w(const struct rs *s, const BYTE *a, DWORD asize, BYTE *w,
                 DWORD wsize) {
  int i;
  ow_memset(w, 0, wsize);
  for (i = 0; i < s->nf; i++) {
    const struct rf *f = &s->f[i];
    DWORD wn = f->str ? f->n * 2u : f->n;
    if (f->w + wn > wsize || f->a + f->n > asize)
      continue;
    if (f->str) {
      int n = 0;
      while (n < f->n && a[f->a + n])
        n++;
      ow_tow((LPCSTR)(a + f->a), n, (LPWSTR)(w + f->w), f->n);
    } else {
      ow_memcpy(w + f->w, a + f->a, f->n);
    }
  }
  *(DWORD *)w = wsize;
}

static BOOL wrong_size(DWORD e) {
  return e == ERROR_INVALID_SIZE || e == ERROR_BUFFER_INVALID;
}

/* ---- the Enum functions -------------------------------------------- */

typedef DWORD (*enum_fn)(void *ctx, BYTE *a, LPDWORD cb, LPDWORD count);

static DWORD ras_enum(const struct rs *s, enum_fn fn, void *ctx, BYTE *w,
                      LPDWORD cb, LPDWORD count) {
  DWORD wsize, asize, n, acb, cnt, e = ERROR_INVALID_SIZE, i;
  int v;
  BYTE *a;
  if (!cb || !count)
    return ERROR_INVALID_PARAMETER;
  wsize = w ? *(DWORD *)w : s->v[0][1];
  v = version(s, wsize);
  if (v < 0)
    return ERROR_INVALID_SIZE;
  for (; v < s->nv; v++) {
    asize = s->v[v][0];
    n = w ? *cb / wsize : 0;
    acb = n * asize;
    a = ow_alloc(acb > asize ? acb : asize);
    if (!a)
      return ERROR_NOT_ENOUGH_MEMORY;
    *(DWORD *)a = asize;
    cnt = 0;
    e = fn(ctx, w ? a : NULL, &acb, &cnt);
    if (wrong_size(e) && v + 1 < s->nv) {
      ow_free(a);
      continue;
    }
    if (e == ERROR_BUFFER_TOO_SMALL) {
      *cb = (acb + asize - 1) / asize * wsize;
      *count = cnt;
    } else if (e == NO_ERROR) {
      for (i = 0; i < cnt && i < n; i++)
        to_w(s, a + i * asize, asize, w + i * wsize, wsize);
      *cb = cnt * wsize;
      *count = cnt;
    }
    ow_free(a);
    break;
  }
  return e;
}

static DWORD call_conns(void *ctx, BYTE *a, LPDWORD cb, LPDWORD count) {
  OW_FN(OW_RASAPI32, RasEnumConnectionsA, return ERROR_CALL_NOT_IMPLEMENTED);
  return pRasEnumConnectionsA((LPRASCONNA)a, cb, count);
}

DWORD WINAPI RasEnumConnectionsW(LPRASCONNW w, LPDWORD cb, LPDWORD count) {
  return ras_enum(&conn, call_conns, NULL, (BYTE *)w, cb, count);
}

static DWORD call_devices(void *ctx, BYTE *a, LPDWORD cb, LPDWORD count) {
  OW_FN(OW_RASAPI32, RasEnumDevicesA, return ERROR_CALL_NOT_IMPLEMENTED);
  return pRasEnumDevicesA((LPRASDEVINFOA)a, cb, count);
}

DWORD WINAPI RasEnumDevicesW(LPRASDEVINFOW w, LPDWORD cb, LPDWORD count) {
  return ras_enum(&dev, call_devices, NULL, (BYTE *)w, cb, count);
}

struct names_ctx {
  LPSTR reserved, pbk;
};

static DWORD call_entries(void *ctx, BYTE *a, LPDWORD cb, LPDWORD count) {
  struct names_ctx *c = ctx;
  OW_FN(OW_RASAPI32, RasEnumEntriesA, return ERROR_CALL_NOT_IMPLEMENTED);
  return pRasEnumEntriesA(c->reserved, c->pbk, (LPRASENTRYNAMEA)a, cb, count);
}

DWORD WINAPI RasEnumEntriesW(LPCWSTR reserved, LPCWSTR pbk,
                             LPRASENTRYNAMEW w, LPDWORD cb, LPDWORD count) {
  struct names_ctx c;
  DWORD e;
  c.reserved = ow_a(reserved);
  c.pbk = ow_a(pbk);
  e = ras_enum(&ename, call_entries, &c, (BYTE *)w, cb, count);
  ow_free(c.reserved);
  ow_free(c.pbk);
  return e;
}

/* ---- one structure ------------------------------------------------- */

typedef DWORD (*one_fn)(void *ctx, BYTE *a, LPDWORD cb);

/* Calls fn with the A form of w, converted from it first when in is set,
 * and converts the result back when out is set. */
static DWORD ras_one(const struct rs *s, one_fn fn, void *ctx, BYTE *w,
                     LPDWORD cb, BOOL in, BOOL out) {
  BYTE a[4096];
  DWORD wsize, asize, acb, e = ERROR_INVALID_SIZE;
  int v;
  if (!w)
    return ERROR_INVALID_PARAMETER;
  wsize = *(DWORD *)w;
  v = version(s, wsize);
  if (v < 0)
    return ERROR_INVALID_SIZE;
  for (; v < s->nv; v++) {
    asize = s->v[v][0];
    if (in)
      to_a(s, w, wsize, a, asize);
    else {
      ow_memset(a, 0, asize);
      *(DWORD *)a = asize;
    }
    acb = asize;
    e = fn(ctx, a, &acb);
    if (wrong_size(e) && v + 1 < s->nv)
      continue;
    if (e == NO_ERROR && out)
      to_w(s, a, asize, w, wsize);
    if (cb && e == NO_ERROR)
      *cb = wsize;
    break;
  }
  return e;
}

static DWORD call_status(void *ctx, BYTE *a, LPDWORD cb) {
  OW_FN(OW_RASAPI32, RasGetConnectStatusA, return ERROR_CALL_NOT_IMPLEMENTED);
  return pRasGetConnectStatusA(*(HRASCONN *)ctx, (LPRASCONNSTATUSA)a);
}

DWORD WINAPI RasGetConnectStatusW(HRASCONN h, LPRASCONNSTATUSW w) {
  return ras_one(&status, call_status, &h, (BYTE *)w, NULL, FALSE, TRUE);
}

struct proj_ctx {
  HRASCONN h;
  RASPROJECTION p;
};

static DWORD call_proj(void *ctx, BYTE *a, LPDWORD cb) {
  struct proj_ctx *c = ctx;
  OW_FN(OW_RASAPI32, RasGetProjectionInfoA,
        return ERROR_CALL_NOT_IMPLEMENTED);
  return pRasGetProjectionInfoA(c->h, c->p, a, cb);
}

DWORD WINAPI RasGetProjectionInfoW(HRASCONN h, RASPROJECTION p, LPVOID w,
                                   LPDWORD cb) {
  struct proj_ctx c = {h, p};
  const struct rs *s;
  switch (p) {
  case RASP_Amb:
    s = &amb;
    break;
  case RASP_PppNbf:
    s = &nbf;
    break;
  case RASP_PppIpx:
    s = &ipx;
    break;
  case RASP_PppIp:
    s = &ip;
    break;
  case RASP_PppLcp:
    s = &lcp;
    break;
  case RASP_Slip:
    s = &slip;
    break;
  default: {
    OW_FN(OW_RASAPI32, RasGetProjectionInfoA,
          return ERROR_CALL_NOT_IMPLEMENTED);
    return pRasGetProjectionInfoA(h, p, w, cb);
  }
  }
  if (!cb)
    return ERROR_INVALID_PARAMETER;
  if (!w || *cb < *(DWORD *)w) {
    *cb = w ? *(DWORD *)w : s->v[0][1];
    return ERROR_BUFFER_TOO_SMALL;
  }
  return ras_one(s, call_proj, &c, w, cb, FALSE, TRUE);
}

struct params_ctx {
  LPSTR pbk;
  LPBOOL pwd;
  BOOL remove;
  LPRASDIALEXTENSIONS ext;
  DWORD ntype;
  LPVOID notifier;
  LPHRASCONN h;
};

static DWORD call_getparams(void *ctx, BYTE *a, LPDWORD cb) {
  struct params_ctx *c = ctx;
  OW_FN(OW_RASAPI32, RasGetEntryDialParamsA,
        return ERROR_CALL_NOT_IMPLEMENTED);
  return pRasGetEntryDialParamsA(c->pbk, (LPRASDIALPARAMSA)a, c->pwd);
}

DWORD WINAPI RasGetEntryDialParamsW(LPCWSTR pbk, LPRASDIALPARAMSW w,
                                    LPBOOL pwd) {
  struct params_ctx c = {0};
  DWORD e;
  c.pbk = ow_a(pbk);
  c.pwd = pwd;
  e = ras_one(&dial, call_getparams, &c, (BYTE *)w, NULL, TRUE, TRUE);
  ow_free(c.pbk);
  return e;
}

static DWORD call_setparams(void *ctx, BYTE *a, LPDWORD cb) {
  struct params_ctx *c = ctx;
  OW_FN(OW_RASAPI32, RasSetEntryDialParamsA,
        return ERROR_CALL_NOT_IMPLEMENTED);
  return pRasSetEntryDialParamsA(c->pbk, (LPRASDIALPARAMSA)a, c->remove);
}

DWORD WINAPI RasSetEntryDialParamsW(LPCWSTR pbk, LPRASDIALPARAMSW w,
                                    BOOL remove) {
  struct params_ctx c = {0};
  DWORD e;
  c.pbk = ow_a(pbk);
  c.remove = remove;
  e = ras_one(&dial, call_setparams, &c, (BYTE *)w, NULL, TRUE, FALSE);
  ow_free(c.pbk);
  return e;
}

static DWORD call_dial(void *ctx, BYTE *a, LPDWORD cb) {
  struct params_ctx *c = ctx;
  OW_FN(OW_RASAPI32, RasDialA, return ERROR_CALL_NOT_IMPLEMENTED);
  return pRasDialA(c->ext, c->pbk, (LPRASDIALPARAMSA)a, c->ntype,
                   c->notifier, c->h);
}

/* The notifiers get no strings, so they are passed as they are. */
DWORD WINAPI RasDialW(LPRASDIALEXTENSIONS ext, LPCWSTR pbk,
                      LPRASDIALPARAMSW w, DWORD ntype, LPVOID notifier,
                      LPHRASCONN h) {
  struct params_ctx c = {0};
  DWORD e;
  c.pbk = ow_a(pbk);
  c.ext = ext;
  c.ntype = ntype;
  c.notifier = notifier;
  c.h = h;
  e = ras_one(&dial, call_dial, &c, (BYTE *)w, NULL, TRUE, FALSE);
  ow_free(c.pbk);
  return e;
}

/* ---- entries and their alternate numbers ------------------------------
 * The alternate phone numbers follow the structure, at dwAlternateOffset,
 * as a list of strings ending with an empty one. */

typedef DWORD (*get_props_fn)(void *ctx, BYTE *a, LPDWORD cb);

struct props_ctx {
  LPSTR pbk, name;
  DWORD sub;
  LPBYTE dev;
  LPDWORD devcb;
  DWORD devn;
};

static DWORD call_getentry(void *ctx, BYTE *a, LPDWORD cb) {
  struct props_ctx *c = ctx;
  OW_FN(OW_RASAPI32, RasGetEntryPropertiesA,
        return ERROR_CALL_NOT_IMPLEMENTED);
  return pRasGetEntryPropertiesA(c->pbk, c->name, (LPRASENTRYA)a, cb, c->dev,
                                 c->devcb);
}

DWORD WINAPI RasGetEntryPropertiesW(LPCWSTR pbk, LPCWSTR name,
                                    LPRASENTRYW w, LPDWORD cb, LPBYTE devinfo,
                                    LPDWORD devcb) {
  struct props_ctx c = {0};
  DWORD wsize, asize, acb = 0, e, need;
  BYTE *a;
  int v;
  if (!cb)
    return ERROR_INVALID_PARAMETER;
  c.pbk = ow_a(pbk);
  c.name = ow_a(name);
  c.dev = devinfo;
  c.devcb = devcb;
  /* The size of the system's entry, with its alternate numbers. */
  {
    struct props_ctx q = c;
    q.dev = NULL;
    q.devcb = NULL;
    call_getentry(&q, NULL, &acb);
  }
  wsize = w ? w->dwSize : sizeof(RASENTRYW);
  v = version(&entry, wsize);
  if (v < 0) {
    e = ERROR_INVALID_SIZE;
    goto out;
  }
  if (!w || *cb < wsize) {
    *cb = wsize + acb * 2;
    if (devcb && !w) {
      DWORD t = 0;
      call_getentry(&c, NULL, &t); /* for the size of the device data */
    }
    e = ERROR_BUFFER_TOO_SMALL;
    goto out;
  }
  for (;; v++) {
    DWORD n = acb > entry.v[v][0] ? acb : entry.v[v][0];
    asize = entry.v[v][0];
    a = ow_alloc(n + 2);
    if (!a) {
      e = ERROR_NOT_ENOUGH_MEMORY;
      goto out;
    }
    *(DWORD *)a = asize;
    e = call_getentry(&c, a, &n);
    if (wrong_size(e) && v + 1 < entry.nv) {
      ow_free(a);
      continue;
    }
    if (e == ERROR_BUFFER_TOO_SMALL) {
      *cb = wsize + n * 2;
    } else if (e == NO_ERROR) {
      DWORD off = ((LPRASENTRYA)a)->dwAlternateOffset;
      LPCSTR alt = off && off < n ? (LPCSTR)a + off : NULL;
      int altn = alt ? ow_wlen_of(alt, ow_alist_len(alt), CP_ACP) : 0;
      need = wsize + altn * sizeof(WCHAR);
      if (*cb < need) {
        *cb = need;
        e = ERROR_BUFFER_TOO_SMALL;
      } else {
        to_w(&entry, a, asize, (BYTE *)w, wsize);
        w->dwAlternateOffset = 0;
        if (alt) {
          ow_mb2wc(CP_ACP, 0, alt, ow_alist_len(alt),
                   (LPWSTR)((BYTE *)w + wsize), altn);
          w->dwAlternateOffset = wsize;
        }
        *cb = need;
      }
    }
    ow_free(a);
    break;
  }
out:
  ow_free(c.pbk);
  ow_free(c.name);
  return e;
}

/* The A form of an entry or sub-entry with its alternate numbers. */
static BYTE *with_alternates(const struct rs *s, const BYTE *w, DWORD wsize,
                             DWORD walt_off, DWORD asize, DWORD aalt_field,
                             DWORD *size) {
  DWORD off = *(const DWORD *)(w + walt_off);
  LPSTR alt = off ? ow_alist((LPCWSTR)(w + off)) : NULL;
  int an = alt ? ow_alist_len(alt) : 0;
  BYTE *a = ow_alloc(asize + an + 1);
  if (!a) {
    ow_free(alt);
    return NULL;
  }
  to_a(s, w, wsize, a, asize);
  *(DWORD *)(a + aalt_field) = 0;
  if (alt) {
    ow_memcpy(a + asize, alt, an);
    *(DWORD *)(a + aalt_field) = asize;
  }
  ow_free(alt);
  *size = asize + an;
  return a;
}

DWORD WINAPI RasSetEntryPropertiesW(LPCWSTR pbk, LPCWSTR name, LPRASENTRYW w,
                                    DWORD cb, LPBYTE devinfo, DWORD devcb) {
  LPSTR p = ow_a(pbk), n = ow_a(name);
  DWORD e = ERROR_INVALID_SIZE, size;
  int v = w ? version(&entry, w->dwSize) : -1;
  OW_FN(OW_RASAPI32, RasSetEntryPropertiesA, goto out);
  for (; v >= 0 && v < entry.nv; v++) {
    BYTE *a = with_alternates(&entry, (BYTE *)w, w->dwSize,
                              offsetof(RASENTRYW, dwAlternateOffset),
                              entry.v[v][0],
                              offsetof(RASENTRYA, dwAlternateOffset), &size);
    if (!a) {
      e = ERROR_NOT_ENOUGH_MEMORY;
      break;
    }
    e = pRasSetEntryPropertiesA(p, n, (LPRASENTRYA)a, size, devinfo, devcb);
    ow_free(a);
    if (!wrong_size(e))
      break;
  }
out:
  ow_free(p);
  ow_free(n);
  return e;
}

DWORD WINAPI RasSetSubEntryPropertiesW(LPCWSTR pbk, LPCWSTR name, DWORD i,
                                       LPRASSUBENTRYW w, DWORD cb,
                                       LPBYTE devinfo, DWORD devcb) {
  LPSTR p = ow_a(pbk), n = ow_a(name);
  DWORD e = ERROR_INVALID_SIZE, size;
  BYTE *a;
  OW_FN(OW_RASAPI32, RasSetSubEntryPropertiesA, goto out);
  if (!w || w->dwSize != sizeof *w)
    goto out;
  a = with_alternates(&sub, (BYTE *)w, w->dwSize,
                      offsetof(RASSUBENTRYW, dwAlternateOffset),
                      sizeof(RASSUBENTRYA),
                      offsetof(RASSUBENTRYA, dwAlternateOffset), &size);
  if (!a) {
    e = ERROR_NOT_ENOUGH_MEMORY;
    goto out;
  }
  e = pRasSetSubEntryPropertiesA(p, n, i, (LPRASSUBENTRYA)a, size, devinfo,
                                 devcb);
  ow_free(a);
out:
  ow_free(p);
  ow_free(n);
  return e;
}

/* ---- the rest -------------------------------------------------------- */

DWORD WINAPI RasConnectionNotificationW(HRASCONN h, HANDLE ev, DWORD flags) {
  OW_FN(OW_RASAPI32, RasConnectionNotificationA,
        return ERROR_CALL_NOT_IMPLEMENTED);
  return pRasConnectionNotificationA(h, ev, flags);
}

DWORD WINAPI RasHangUpW(HRASCONN h) {
  OW_FN(OW_RASAPI32, RasHangUpA, return ERROR_CALL_NOT_IMPLEMENTED);
  return pRasHangUpA(h);
}

DWORD WINAPI RasCreatePhonebookEntryW(HWND h, LPCWSTR pbk) {
  LPSTR p = ow_a(pbk);
  DWORD e;
  OW_FN(OW_RASAPI32, RasCreatePhonebookEntryA,
        e = ERROR_CALL_NOT_IMPLEMENTED;
        goto out);
  e = pRasCreatePhonebookEntryA(h, p);
out:
  ow_free(p);
  return e;
}

DWORD WINAPI RasEditPhonebookEntryW(HWND h, LPCWSTR pbk, LPCWSTR name) {
  LPSTR p = ow_a(pbk), n = ow_a(name);
  DWORD e;
  OW_FN(OW_RASAPI32, RasEditPhonebookEntryA,
        e = ERROR_CALL_NOT_IMPLEMENTED;
        goto out);
  e = pRasEditPhonebookEntryA(h, p, n);
out:
  ow_free(p);
  ow_free(n);
  return e;
}

DWORD WINAPI RasDeleteEntryW(LPCWSTR pbk, LPCWSTR name) {
  LPSTR p = ow_a(pbk), n = ow_a(name);
  DWORD e;
  OW_FN(OW_RASAPI32, RasDeleteEntryA, e = ERROR_CALL_NOT_IMPLEMENTED;
        goto out);
  e = pRasDeleteEntryA(p, n);
out:
  ow_free(p);
  ow_free(n);
  return e;
}

typedef DWORD(WINAPI *delsub_fn)(LPCSTR, LPCSTR, DWORD);
DWORD WINAPI RasDeleteSubEntryW(LPCWSTR pbk, LPCWSTR name, DWORD i);

DWORD WINAPI RasDeleteSubEntryW(LPCWSTR pbk, LPCWSTR name, DWORD i) {
  LPSTR p = ow_a(pbk), n = ow_a(name);
  DWORD e;
  OW_FNT(OW_RASAPI32, delsub_fn, pdel, "RasDeleteSubEntryA",
         e = ERROR_CALL_NOT_IMPLEMENTED;
         goto out);
  e = pdel(p, n, i);
out:
  ow_free(p);
  ow_free(n);
  return e;
}

DWORD WINAPI RasRenameEntryW(LPCWSTR pbk, LPCWSTR from, LPCWSTR to) {
  LPSTR p = ow_a(pbk), a = ow_a(from), b = ow_a(to);
  DWORD e;
  OW_FN(OW_RASAPI32, RasRenameEntryA, e = ERROR_CALL_NOT_IMPLEMENTED;
        goto out);
  e = pRasRenameEntryA(p, a, b);
out:
  ow_free(p);
  ow_free(a);
  ow_free(b);
  return e;
}

DWORD WINAPI RasValidateEntryNameW(LPCWSTR pbk, LPCWSTR name) {
  LPSTR p = ow_a(pbk), n = ow_a(name);
  DWORD e;
  OW_FN(OW_RASAPI32, RasValidateEntryNameA, e = ERROR_CALL_NOT_IMPLEMENTED;
        goto out);
  e = pRasValidateEntryNameA(p, n);
out:
  ow_free(p);
  ow_free(n);
  return e;
}

DWORD WINAPI RasGetErrorStringW(UINT err, LPWSTR buf, DWORD cch) {
  char a[512];
  DWORD e;
  OW_FN(OW_RASAPI32, RasGetErrorStringA, return ERROR_CALL_NOT_IMPLEMENTED);
  if (!buf)
    return ERROR_INVALID_PARAMETER;
  a[0] = 0;
  e = pRasGetErrorStringA(err, a, sizeof a);
  if (e == NO_ERROR) {
    if ((DWORD)ow_wlen_of(a, -1, CP_ACP) >= cch)
      return ERROR_INSUFFICIENT_BUFFER;
    ow_tow(a, -1, buf, cch);
  }
  return e;
}
