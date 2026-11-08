/* kernel32's national language support: comparing and mapping strings,
 * character types, locale and calendar information, date, time, number
 * and currency formats and the enumerations.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"

OW_REAL_DECL(int, WideCharToMultiByte,
             (UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, LPBOOL), 32);

/* The A form of n WCHARs, and whether every character converted. */
static LPSTR exact_a(LPCWSTR w, int n, int *len, BOOL *lossy) {
  int l;
  BOOL used = FALSE;
  LPSTR a;
  *len = 0;
  *lossy = FALSE;
  if (!n)
    return ow_alloc(1);
  l = REAL(WideCharToMultiByte)(CP_ACP, 0, w, n, NULL, 0, NULL, &used);
  a = ow_alloc(l + 1);
  if (!a)
    return NULL;
  used = FALSE;
  REAL(WideCharToMultiByte)(CP_ACP, 0, w, n, a, l, NULL, &used);
  *len = l;
  *lossy = used;
  return a;
}

/* Returns an A result that counts its NUL (GetLocaleInfo, GetDateFormat
 * and the like) the way the W function does: the WCHARs with the NUL,
 * the size needed when cch is 0, or 0 with ERROR_INSUFFICIENT_BUFFER. */
static int ret_incl(LPCSTR a, int an, LPWSTR buf, int cch) {
  int wl;
  if (an <= 0)
    return 0;
  wl = ow_mb2wc(CP_ACP, 0, a, an, NULL, 0);
  if (!cch)
    return wl;
  if (!buf || cch < wl) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  ow_mb2wc(CP_ACP, 0, a, an, buf, cch);
  return wl;
}

/* ---- comparing strings ----------------------------------------------- */

static int skip(WCHAR c, DWORD flags) {
  WORD t;
  if (!(flags & (NORM_IGNORENONSPACE | NORM_IGNORESYMBOLS)))
    return 0;
  t = ow_ctype1(c);
  if ((flags & NORM_IGNORESYMBOLS) && (t & (C1_PUNCT | C1_SPACE)))
    return 1;
  if ((flags & NORM_IGNORENONSPACE) && (ow_ctype3(c) & C3_NONSPACING))
    return 1;
  return 0;
}

/* For text that the ANSI code page cannot hold: compare code points. */
static int compare_ordinal(DWORD flags, LPCWSTR a, int na, LPCWSTR b,
                           int nb) {
  int i = 0, j = 0;
  for (;;) {
    WCHAR x, y;
    while (i < na && skip(a[i], flags))
      i++;
    while (j < nb && skip(b[j], flags))
      j++;
    if (i >= na || j >= nb)
      break;
    x = a[i], y = b[j];
    if (flags & NORM_IGNORECASE)
      x = ow_lower(x), y = ow_lower(y);
    if (x != y)
      return x < y ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
    i++, j++;
  }
  if (i < na)
    return CSTR_GREATER_THAN;
  if (j < nb)
    return CSTR_LESS_THAN;
  return CSTR_EQUAL;
}

int WINAPI CompareStringW(LCID lcid, DWORD flags, LPCWSTR s1, int n1,
                          LPCWSTR s2, int n2) {
  LPSTR a, b;
  int la, lb, r;
  BOOL lossa, lossb;
  if (!s1 || !s2) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (n1 < 0)
    n1 = ow_wcslen(s1);
  if (n2 < 0)
    n2 = ow_wcslen(s2);
  a = exact_a(s1, n1, &la, &lossa);
  b = exact_a(s2, n2, &lb, &lossb);
  if (!a || !b || lossa || lossb) {
    ow_free(a);
    ow_free(b);
    return compare_ordinal(flags, s1, n1, s2, n2);
  }
  r = CompareStringA(lcid, flags, a, la, b, lb);
  ow_free(a);
  ow_free(b);
  return r;
}

/* ---- mapping strings ---------------------------------------------------- */

static WCHAR map_char(WCHAR c, DWORD flags) {
  if (flags & LCMAP_UPPERCASE)
    c = ow_upper(c);
  else if (flags & LCMAP_LOWERCASE)
    c = ow_lower(c);
  if ((flags & LCMAP_KATAKANA) && c >= 0x3041 && c <= 0x3096)
    c += 0x60;
  else if ((flags & LCMAP_HIRAGANA) && c >= 0x30a1 && c <= 0x30f6)
    c -= 0x60;
  if (flags & LCMAP_BYTEREV)
    c = (WCHAR)((c << 8) | (c >> 8));
  return c;
}

int WINAPI LCMapStringW(LCID lcid, DWORD flags, LPCWSTR src, int n,
                        LPWSTR dst, int cch) {
  int i;
  if (!src || cch < 0 || (cch && !dst)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (n < 0)
    n = ow_wcslen(src) + 1;
  if (flags & LCMAP_SORTKEY) {
    /* The key is bytes, the same for A and W. */
    int la, r;
    LPSTR a = ow_an(src, n, &la);
    if (!a)
      return 0;
    r = LCMapStringA(lcid, flags, a, la, (LPSTR)dst, cch);
    ow_free(a);
    return r;
  }
  if (flags & (LCMAP_HALFWIDTH | LCMAP_FULLWIDTH | LCMAP_SIMPLIFIED_CHINESE |
               LCMAP_TRADITIONAL_CHINESE)) {
    /* Width and Chinese mappings change the length: the system knows them
       for the characters of its code page. */
    int la, r, wl;
    LPSTR a = ow_an(src, n, &la), out;
    if (!a)
      return 0;
    r = LCMapStringA(lcid, flags, a, la, NULL, 0);
    out = r ? ow_alloc(r + 1) : NULL;
    if (!out) {
      ow_free(a);
      return 0;
    }
    r = LCMapStringA(lcid, flags, a, la, out, r);
    ow_free(a);
    wl = r ? ow_mb2wc(CP_ACP, 0, out, r, NULL, 0) : 0;
    if (cch && wl) {
      if (cch < wl) {
        ow_free(out);
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
      }
      ow_mb2wc(CP_ACP, 0, out, r, dst, cch);
    }
    ow_free(out);
    return wl;
  }
  if (!cch)
    return n;
  if (cch < n) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  for (i = 0; i < n; i++)
    dst[i] = map_char(src[i], flags);
  return n;
}

/* ---- character types ------------------------------------------------- */

BOOL WINAPI GetStringTypeW(DWORD type, LPCWSTR src, int n, LPWORD out) {
  int i;
  if (!src || !out) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (n < 0)
    n = ow_wcslen(src) + 1;
  for (i = 0; i < n; i++) {
    switch (type) {
    case CT_CTYPE1:
      out[i] = ow_ctype1(src[i]);
      break;
    case CT_CTYPE2:
      out[i] = ow_ctype2(src[i]);
      break;
    case CT_CTYPE3:
      out[i] = ow_ctype3(src[i]);
      break;
    default:
      SetLastError(ERROR_INVALID_FLAGS);
      return FALSE;
    }
  }
  return TRUE;
}

BOOL WINAPI GetStringTypeExW(LCID lcid, DWORD type, LPCWSTR src, int n,
                             LPWORD out) {
  (void)lcid;
  return GetStringTypeW(type, src, n, out);
}

/* ---- locale information ------------------------------------------------ */

static int parse_number(LPCSTR s, int hex) {
  int v = 0;
  for (; *s; s++) {
    int d;
    if (*s >= '0' && *s <= '9')
      d = *s - '0';
    else if (hex && *s >= 'a' && *s <= 'f')
      d = *s - 'a' + 10;
    else if (hex && *s >= 'A' && *s <= 'F')
      d = *s - 'A' + 10;
    else
      break;
    v = v * (hex ? 16 : 10) + d;
  }
  return v;
}

/* LOCALE_RETURN_NUMBER and CAL_RETURN_NUMBER results: a DWORD in the
 * buffer, which counts as two WCHARs. */
static int ret_number(DWORD v, LPWSTR buf, int cch) {
  if (!cch)
    return 2;
  if (cch < 2 || !buf) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  ow_memcpy(buf, &v, sizeof v);
  return 2;
}

int WINAPI GetLocaleInfoW(LCID lcid, LCTYPE type, LPWSTR buf, int cch) {
  char a[512];
  int n;
  LCTYPE base = type & ~(LOCALE_RETURN_NUMBER | LOCALE_NOUSEROVERRIDE |
                         LOCALE_USE_CP_ACP);
  if (cch < 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (base == LOCALE_FONTSIGNATURE) {
    /* Binary: 16 WCHARs of data. */
    n = GetLocaleInfoA(lcid, type, a, sizeof a);
    if (!n)
      return 0;
    if (n > 32)
      n = 32;
    if (!cch)
      return (n + 1) / 2;
    if (cch < (n + 1) / 2) {
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return 0;
    }
    ow_memcpy(buf, a, n);
    return (n + 1) / 2;
  }
  if (type & LOCALE_RETURN_NUMBER) {
    n = GetLocaleInfoA(lcid, type & ~LOCALE_RETURN_NUMBER, a, sizeof a);
    if (!n)
      return 0;
    return ret_number(parse_number(a, base == LOCALE_ILANGUAGE), buf, cch);
  }
  n = GetLocaleInfoA(lcid, type & ~LOCALE_USE_CP_ACP, a, sizeof a);
  return ret_incl(a, n, buf, cch);
}

BOOL WINAPI SetLocaleInfoW(LCID lcid, LCTYPE type, LPCWSTR data) {
  LPSTR a = ow_a(data);
  BOOL r = SetLocaleInfoA(lcid, type, a);
  ow_free(a);
  return r;
}

/* ---- formats ----------------------------------------------------------- */

int WINAPI GetDateFormatW(LCID lcid, DWORD flags, const SYSTEMTIME *st,
                          LPCWSTR fmt, LPWSTR buf, int cch) {
  char out[512];
  LPSTR f = ow_a(fmt);
  int n = GetDateFormatA(lcid, flags, st, f, out, sizeof out);
  ow_free(f);
  return ret_incl(out, n, buf, cch);
}

int WINAPI GetTimeFormatW(LCID lcid, DWORD flags, const SYSTEMTIME *st,
                          LPCWSTR fmt, LPWSTR buf, int cch) {
  char out[512];
  LPSTR f = ow_a(fmt);
  int n = GetTimeFormatA(lcid, flags, st, f, out, sizeof out);
  ow_free(f);
  return ret_incl(out, n, buf, cch);
}

int WINAPI GetNumberFormatW(LCID lcid, DWORD flags, LPCWSTR value,
                            const NUMBERFMTW *fmt, LPWSTR buf, int cch) {
  char out[512];
  NUMBERFMTA af;
  LPSTR v = ow_a(value), dec = NULL, thou = NULL;
  int n;
  if (fmt) {
    af.NumDigits = fmt->NumDigits;
    af.LeadingZero = fmt->LeadingZero;
    af.Grouping = fmt->Grouping;
    af.lpDecimalSep = dec = ow_a(fmt->lpDecimalSep);
    af.lpThousandSep = thou = ow_a(fmt->lpThousandSep);
    af.NegativeOrder = fmt->NegativeOrder;
  }
  n = GetNumberFormatA(lcid, flags, v, fmt ? &af : NULL, out, sizeof out);
  ow_free(v);
  ow_free(dec);
  ow_free(thou);
  return ret_incl(out, n, buf, cch);
}

int WINAPI GetCurrencyFormatW(LCID lcid, DWORD flags, LPCWSTR value,
                              const CURRENCYFMTW *fmt, LPWSTR buf, int cch) {
  char out[512];
  CURRENCYFMTA af;
  LPSTR v = ow_a(value), dec = NULL, thou = NULL, sym = NULL;
  int n;
  if (fmt) {
    af.NumDigits = fmt->NumDigits;
    af.LeadingZero = fmt->LeadingZero;
    af.Grouping = fmt->Grouping;
    af.lpDecimalSep = dec = ow_a(fmt->lpDecimalSep);
    af.lpThousandSep = thou = ow_a(fmt->lpThousandSep);
    af.NegativeOrder = fmt->NegativeOrder;
    af.PositiveOrder = fmt->PositiveOrder;
    af.lpCurrencySymbol = sym = ow_a(fmt->lpCurrencySymbol);
  }
  n = GetCurrencyFormatA(lcid, flags, v, fmt ? &af : NULL, out, sizeof out);
  ow_free(v);
  ow_free(dec);
  ow_free(thou);
  ow_free(sym);
  return ret_incl(out, n, buf, cch);
}

/* ---- calendars --------------------------------------------------------- */

/* The LOCALE_ type with the Gregorian value of a CAL_ type, for Windows
 * 95, which has no GetCalendarInfo. */
static LCTYPE cal_locale(CALTYPE t) {
  if (t == CAL_SSHORTDATE)
    return LOCALE_SSHORTDATE;
  if (t == CAL_SLONGDATE)
    return LOCALE_SLONGDATE;
  if (t == CAL_SYEARMONTH)
    return LOCALE_SYEARMONTH;
  if (t >= CAL_SDAYNAME1 && t <= CAL_SABBREVDAYNAME7)
    return LOCALE_SDAYNAME1 + (t - CAL_SDAYNAME1);
  if (t >= CAL_SMONTHNAME1 && t <= CAL_SMONTHNAME12)
    return LOCALE_SMONTHNAME1 + (t - CAL_SMONTHNAME1);
  if (t == CAL_SMONTHNAME13)
    return LOCALE_SMONTHNAME13;
  if (t >= CAL_SABBREVMONTHNAME1 && t <= CAL_SABBREVMONTHNAME12)
    return LOCALE_SABBREVMONTHNAME1 + (t - CAL_SABBREVMONTHNAME1);
  if (t == CAL_SABBREVMONTHNAME13)
    return LOCALE_SABBREVMONTHNAME13;
  return 0;
}

int WINAPI GetCalendarInfoW(LCID lcid, CALID cal, CALTYPE type, LPWSTR buf,
                            int cch, LPDWORD value) {
  char a[512];
  int n;
  CALTYPE base = type & ~(CAL_RETURN_NUMBER | CAL_NOUSEROVERRIDE |
                          CAL_USE_CP_ACP);
  OW_FN(OW_KERNEL32, GetCalendarInfoA, goto old);
  if (type & CAL_RETURN_NUMBER)
    return pGetCalendarInfoA(lcid, cal, type, NULL, 0, value);
  n = pGetCalendarInfoA(lcid, cal, type, a, sizeof a, NULL);
  return ret_incl(a, n, buf, cch);
old:
  if (cal != CAL_GREGORIAN && cal != 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (base == CAL_ICALINTVALUE || base == CAL_ITWODIGITYEARMAX ||
      base == CAL_IYEAROFFSETRANGE) {
    DWORD v = base == CAL_ICALINTVALUE ? 1
              : base == CAL_ITWODIGITYEARMAX ? 2029
                                              : 0;
    if (type & CAL_RETURN_NUMBER) {
      if (value)
        *value = v;
      return 2;
    }
    n = wsprintfA(a, "%lu", (unsigned long)v) + 1;
    return ret_incl(a, n, buf, cch);
  }
  if (base == CAL_SCALNAME) {
    lstrcpyA(a, "Gregorian Calendar");
    return ret_incl(a, ow_strlen(a) + 1, buf, cch);
  }
  if (base == CAL_SERASTRING) {
    a[0] = 0;
    return ret_incl(a, 1, buf, cch);
  }
  if (!cal_locale(base)) {
    SetLastError(ERROR_INVALID_FLAGS);
    return 0;
  }
  n = GetLocaleInfoA(lcid, cal_locale(base) | (type & CAL_NOUSEROVERRIDE), a,
                     sizeof a);
  return ret_incl(a, n, buf, cch);
}

int WINAPI SetCalendarInfoW(LCID lcid, CALID cal, CALTYPE type,
                            LPCWSTR data) {
  LPSTR a;
  int r;
  OW_FN(OW_KERNEL32, SetCalendarInfoA, return 0);
  a = ow_a(data);
  r = pSetCalendarInfoA(lcid, cal, type, a);
  ow_free(a);
  return r;
}

/* ---- enumerations ---------------------------------------------------- */

/* The A callbacks take no context: the W callback is kept per thread
 * while the enumeration runs. */
struct saved {
  void *cb;
  LPARAM lp;
  int extra;
};
static struct ow_thread *begin(struct saved *s, void *cb, int extra) {
  struct ow_thread *t = ow_thread();
  s->cb = t->cb;
  s->lp = t->lp;
  s->extra = t->extra;
  t->cb = cb;
  t->extra = extra;
  return t;
}
static void end(struct ow_thread *t, const struct saved *s) {
  t->cb = s->cb;
  t->lp = s->lp;
  t->extra = s->extra;
}

static BOOL CALLBACK str_cb(LPSTR s) {
  struct ow_thread *t = ow_thread();
  LPWSTR w = ow_w(s);
  BOOL r = ((BOOL(CALLBACK *)(LPWSTR))t->cb)(w ? w : (LPWSTR)L"");
  ow_free(w);
  return r;
}

static BOOL CALLBACK str_id_cb(LPSTR s, CALID id) {
  struct ow_thread *t = ow_thread();
  LPWSTR w = ow_w(s);
  BOOL r = ((BOOL(CALLBACK *)(LPWSTR, CALID))t->cb)(w ? w : (LPWSTR)L"", id);
  ow_free(w);
  return r;
}

BOOL WINAPI EnumCalendarInfoW(CALINFO_ENUMPROCW proc, LCID lcid, CALID cal,
                              CALTYPE type) {
  struct saved s;
  struct ow_thread *t;
  BOOL r;
  if (!proc) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  t = begin(&s, (void *)proc, 0);
  r = EnumCalendarInfoA((CALINFO_ENUMPROCA)str_cb, lcid, cal, type);
  end(t, &s);
  return r;
}

BOOL WINAPI EnumCalendarInfoExW(CALINFO_ENUMPROCEXW proc, LCID lcid,
                                CALID cal, CALTYPE type) {
  struct saved s;
  struct ow_thread *t;
  BOOL r;
  WCHAR buf[256];
  if (!proc) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  {
    OW_FN(OW_KERNEL32, EnumCalendarInfoExA, goto old);
    t = begin(&s, (void *)proc, 0);
    r = pEnumCalendarInfoExA((CALINFO_ENUMPROCEXA)str_id_cb, lcid, cal,
                             type);
    end(t, &s);
    return r;
  }
old:
  /* Windows 95 knows the Gregorian calendar only. */
  if (cal != ENUM_ALL_CALENDARS && cal != CAL_GREGORIAN)
    return TRUE;
  if (!GetCalendarInfoW(lcid, CAL_GREGORIAN, type, buf, 256, NULL))
    return FALSE;
  proc(buf, CAL_GREGORIAN);
  return TRUE;
}

BOOL WINAPI EnumDateFormatsW(DATEFMT_ENUMPROCW proc, LCID lcid,
                             DWORD flags) {
  struct saved s;
  struct ow_thread *t;
  BOOL r;
  if (!proc) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  t = begin(&s, (void *)proc, 0);
  r = EnumDateFormatsA((DATEFMT_ENUMPROCA)str_cb, lcid, flags);
  end(t, &s);
  return r;
}

static BOOL CALLBACK date_ex_old_cb(LPSTR f) {
  struct ow_thread *t = ow_thread();
  LPWSTR w = ow_w(f);
  BOOL r = ((DATEFMT_ENUMPROCEXW)t->cb)(w ? w : (LPWSTR)L"", CAL_GREGORIAN);
  ow_free(w);
  return r;
}

BOOL WINAPI EnumDateFormatsExW(DATEFMT_ENUMPROCEXW proc, LCID lcid,
                               DWORD flags) {
  struct saved s;
  struct ow_thread *t;
  BOOL r;
  if (!proc) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  t = begin(&s, (void *)proc, 0);
  {
    OW_FN(OW_KERNEL32, EnumDateFormatsExA, goto old);
    r = pEnumDateFormatsExA((DATEFMT_ENUMPROCEXA)str_id_cb, lcid, flags);
    end(t, &s);
    return r;
  }
old:
  r = EnumDateFormatsA(date_ex_old_cb, lcid, flags);
  end(t, &s);
  return r;
}

BOOL WINAPI EnumTimeFormatsW(TIMEFMT_ENUMPROCW proc, LCID lcid,
                             DWORD flags) {
  struct saved s;
  struct ow_thread *t;
  BOOL r;
  if (!proc) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  t = begin(&s, (void *)proc, 0);
  r = EnumTimeFormatsA((TIMEFMT_ENUMPROCA)str_cb, lcid, flags);
  end(t, &s);
  return r;
}

BOOL WINAPI EnumSystemCodePagesW(CODEPAGE_ENUMPROCW proc, DWORD flags) {
  struct saved s;
  struct ow_thread *t;
  BOOL r;
  if (!proc) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  t = begin(&s, (void *)proc, 0);
  r = EnumSystemCodePagesA((CODEPAGE_ENUMPROCA)str_cb, flags);
  end(t, &s);
  return r;
}

BOOL WINAPI EnumSystemLocalesW(LOCALE_ENUMPROCW proc, DWORD flags) {
  struct saved s;
  struct ow_thread *t;
  BOOL r;
  if (!proc) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  t = begin(&s, (void *)proc, 0);
  r = EnumSystemLocalesA((LOCALE_ENUMPROCA)str_cb, flags);
  end(t, &s);
  return r;
}
