/* The DLL's entry point, memory and string conversion helpers, the code
 * page functions (with UTF-8 for Windows 95, which lacks it),
 * GetProcAddress and the lstr and wsprintf functions.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"

HINSTANCE ow_self;
DWORD ow_tls = TLS_OUT_OF_INDEXES;
static HANDLE heap;

OW_REAL_DECL(FARPROC, GetProcAddress, (HMODULE, LPCSTR), 8);
OW_REAL_DECL(int, MultiByteToWideChar,
             (UINT, DWORD, LPCSTR, int, LPWSTR, int), 24);
OW_REAL_DECL(int, WideCharToMultiByte,
             (UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, LPBOOL), 32);
OW_REAL_DECL(BOOL, GetCPInfo, (UINT, LPCPINFO), 8);
OW_REAL_DECL(BOOL, IsValidCodePage, (UINT), 4);

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    ow_self = inst;
    heap = GetProcessHeap();
    ow_tls = TlsAlloc();
    ow_window_init();
    ow_comdlg_init();
  } else if (reason == DLL_PROCESS_DETACH) {
    if (ow_tls != TLS_OUT_OF_INDEXES)
      TlsFree(ow_tls);
  } else if (reason == DLL_THREAD_DETACH) {
    void *p = ow_tls != TLS_OUT_OF_INDEXES ? TlsGetValue(ow_tls) : NULL;
    if (p)
      HeapFree(heap, 0, p);
  }
  return TRUE;
}

struct ow_thread *ow_thread(void) {
  static struct ow_thread shared; /* when TLS or memory runs out */
  struct ow_thread *t;
  if (ow_tls == TLS_OUT_OF_INDEXES)
    return &shared;
  t = TlsGetValue(ow_tls);
  if (!t) {
    t = ow_alloc(sizeof *t);
    if (!t)
      return &shared;
    TlsSetValue(ow_tls, t);
  }
  return t;
}

/* ---- memory --------------------------------------------------------- */

void *ow_alloc(SIZE_T n) {
  if (!heap)
    heap = GetProcessHeap();
  return HeapAlloc(heap, HEAP_ZERO_MEMORY, n ? n : 1);
}

void *ow_realloc(void *p, SIZE_T n) {
  if (!p)
    return ow_alloc(n);
  return HeapReAlloc(heap, HEAP_ZERO_MEMORY, p, n ? n : 1);
}

void ow_free(const void *p) {
  if (!IS_INTRESOURCE(p))
    HeapFree(heap, 0, (void *)p);
}

/* clang calls these for structure copies and zeroing. */
void *memcpy(void *d, const void *s, size_t n) {
  unsigned char *dp = d;
  const unsigned char *sp = s;
  while (n--)
    *dp++ = *sp++;
  return d;
}

void *memmove(void *d, const void *s, size_t n) {
  unsigned char *dp = d;
  const unsigned char *sp = s;
  if (dp < sp) {
    while (n--)
      *dp++ = *sp++;
  } else {
    while (n--)
      dp[n] = sp[n];
  }
  return d;
}

void *memset(void *d, int c, size_t n) {
  unsigned char *dp = d;
  while (n--)
    *dp++ = (unsigned char)c;
  return d;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  for (; n; n--, x++, y++)
    if (*x != *y)
      return *x - *y;
  return 0;
}

void *ow_memcpy(void *d, const void *s, SIZE_T n) { return memcpy(d, s, n); }
void *ow_memset(void *d, int c, SIZE_T n) { return memset(d, c, n); }

/* ---- small string helpers ------------------------------------------- */

int ow_wcslen(LPCWSTR s) {
  int n = 0;
  if (s)
    while (s[n])
      n++;
  return n;
}

int ow_wcsnlen(LPCWSTR s, int max) {
  int n = 0;
  while (n < max && s[n])
    n++;
  return n;
}

int ow_strlen(LPCSTR s) {
  int n = 0;
  if (s)
    while (s[n])
      n++;
  return n;
}

LPWSTR ow_wcscpy(LPWSTR d, LPCWSTR s) {
  LPWSTR r = d;
  while ((*d++ = *s++) != 0) {
  }
  return r;
}

int ow_wcscmp(LPCWSTR a, LPCWSTR b) {
  while (*a && *a == *b)
    a++, b++;
  return (int)*a - (int)*b;
}

int ow_wcsicmp_ascii(LPCWSTR a, LPCSTR b) {
  for (;; a++, b++) {
    WCHAR x = *a;
    unsigned char y = (unsigned char)*b;
    if (x >= 'A' && x <= 'Z')
      x += 32;
    if (y >= 'A' && y <= 'Z')
      y += 32;
    if (x != y)
      return (int)x - (int)y;
    if (!x)
      return 0;
  }
}

void ow_wcsncpyz(LPWSTR d, LPCWSTR s, int cch) {
  int i = 0;
  if (cch <= 0)
    return;
  if (s)
    for (; i < cch - 1 && s[i]; i++)
      d[i] = s[i];
  d[i] = 0;
}

/* ---- UTF-8 ------------------------------------------------------------ */

static int utf8_to_w(DWORD flags, const unsigned char *s, int n, LPWSTR w,
                     int cch) {
  int i = 0, out = 0;
  if (n < 0)
    n = ow_strlen((LPCSTR)s) + 1;
  while (i < n) {
    unsigned c = s[i], cp, need, j;
    if (c < 0x80) {
      cp = c, need = 0;
    } else if (c >= 0xc2 && c < 0xe0) {
      cp = c & 0x1f, need = 1;
    } else if (c >= 0xe0 && c < 0xf0) {
      cp = c & 0x0f, need = 2;
    } else if (c >= 0xf0 && c < 0xf5) {
      cp = c & 0x07, need = 3;
    } else {
      goto bad;
    }
    for (j = 1; j <= need; j++) {
      if (i + (int)j >= n || (s[i + j] & 0xc0) != 0x80)
        goto bad;
      cp = (cp << 6) | (s[i + j] & 0x3f);
    }
    if ((need == 2 && (cp < 0x800 || (cp >= 0xd800 && cp < 0xe000))) ||
        (need == 3 && (cp < 0x10000 || cp > 0x10ffff)))
      goto bad;
    i += need + 1;
    if (cp >= 0x10000) {
      if (cch) {
        if (out + 2 > cch)
          goto small;
        w[out] = (WCHAR)(0xd800 + ((cp - 0x10000) >> 10));
        w[out + 1] = (WCHAR)(0xdc00 + ((cp - 0x10000) & 0x3ff));
      }
      out += 2;
    } else {
      if (cch) {
        if (out + 1 > cch)
          goto small;
        w[out] = (WCHAR)cp;
      }
      out++;
    }
    continue;
  bad:
    if (flags & MB_ERR_INVALID_CHARS) {
      SetLastError(ERROR_NO_UNICODE_TRANSLATION);
      return 0;
    }
    i++;
    if (cch) {
      if (out + 1 > cch)
        goto small;
      w[out] = 0xfffd;
    }
    out++;
  }
  return out;
small:
  SetLastError(ERROR_INSUFFICIENT_BUFFER);
  return 0;
}

static int w_to_utf8(LPCWSTR w, int n, unsigned char *s, int cb) {
  int i, out = 0;
  if (n < 0)
    n = ow_wcslen(w) + 1;
  for (i = 0; i < n; i++) {
    unsigned cp = w[i], len;
    unsigned char b[4];
    if (cp >= 0xd800 && cp < 0xdc00 && i + 1 < n && w[i + 1] >= 0xdc00 &&
        w[i + 1] < 0xe000) {
      cp = 0x10000 + ((cp - 0xd800) << 10) + (w[i + 1] - 0xdc00);
      i++;
    } else if (cp >= 0xd800 && cp < 0xe000) {
      cp = 0xfffd;
    }
    if (cp < 0x80) {
      b[0] = (unsigned char)cp, len = 1;
    } else if (cp < 0x800) {
      b[0] = (unsigned char)(0xc0 | (cp >> 6));
      b[1] = (unsigned char)(0x80 | (cp & 0x3f));
      len = 2;
    } else if (cp < 0x10000) {
      b[0] = (unsigned char)(0xe0 | (cp >> 12));
      b[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
      b[2] = (unsigned char)(0x80 | (cp & 0x3f));
      len = 3;
    } else {
      b[0] = (unsigned char)(0xf0 | (cp >> 18));
      b[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
      b[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
      b[3] = (unsigned char)(0x80 | (cp & 0x3f));
      len = 4;
    }
    if (cb) {
      unsigned k;
      if (out + (int)len > cb) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
      }
      for (k = 0; k < len; k++)
        s[out + k] = b[k];
    }
    out += len;
  }
  return out;
}

int WINAPI MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR s, int n,
                               LPWSTR w, int cch) {
  if (!s || cch < 0 || (cch && !w) || n == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (cp == CP_UTF8)
    return utf8_to_w(flags, (const unsigned char *)s, n, w, cch);
  return REAL(MultiByteToWideChar)(cp, flags, s, n, w, cch);
}

int WINAPI WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR w, int n,
                               LPSTR s, int cb, LPCSTR def, LPBOOL used) {
  if (!w || cb < 0 || (cb && !s) || n == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (cp == CP_UTF8) {
    if (def || used) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return 0;
    }
    return w_to_utf8(w, n, (unsigned char *)s, cb);
  }
  return REAL(WideCharToMultiByte)(cp, flags, w, n, s, cb, def, used);
}

int ow_mb2wc(UINT cp, DWORD flags, LPCSTR s, int n, LPWSTR w, int cch) {
  return MultiByteToWideChar(cp, flags, s, n, w, cch);
}

int ow_wc2mb(UINT cp, DWORD flags, LPCWSTR w, int n, LPSTR s, int cb) {
  return WideCharToMultiByte(cp, flags, w, n, s, cb, NULL, NULL);
}

BOOL WINAPI GetCPInfo(UINT cp, LPCPINFO info) {
  if ((cp == CP_UTF8 || cp == CP_UTF7) && info) {
    ow_memset(info, 0, sizeof *info);
    info->MaxCharSize = cp == CP_UTF8 ? 4 : 5;
    info->DefaultChar[0] = '?';
    return TRUE;
  }
  return REAL(GetCPInfo)(cp, info);
}

BOOL WINAPI IsValidCodePage(UINT cp) {
  if (cp == CP_UTF8 || cp == CP_UTF7)
    return TRUE;
  return REAL(IsValidCodePage)(cp);
}

static const struct {
  WORD cp;
  const char *name;
} cp_names[] = {
    {37, "(IBM EBCDIC - U.S./Canada)"},
    {437, "(OEM - United States)"},
    {708, "(Arabic - ASMO)"},
    {720, "(Arabic - Transparent ASMO)"},
    {737, "(OEM - Greek 437G)"},
    {775, "(OEM - Baltic)"},
    {850, "(OEM - Multilingual Latin I)"},
    {852, "(OEM - Latin II)"},
    {855, "(OEM - Cyrillic)"},
    {857, "(OEM - Turkish)"},
    {860, "(OEM - Portuguese)"},
    {861, "(OEM - Icelandic)"},
    {862, "(OEM - Hebrew)"},
    {863, "(OEM - Canadian French)"},
    {864, "(OEM - Arabic)"},
    {865, "(OEM - Nordic)"},
    {866, "(OEM - Russian)"},
    {869, "(OEM - Modern Greek)"},
    {874, "(ANSI/OEM - Thai)"},
    {932, "(ANSI/OEM - Japanese Shift-JIS)"},
    {936, "(ANSI/OEM - Simplified Chinese GBK)"},
    {949, "(ANSI/OEM - Korean)"},
    {950, "(ANSI/OEM - Traditional Chinese Big5)"},
    {1250, "(ANSI - Central Europe)"},
    {1251, "(ANSI - Cyrillic)"},
    {1252, "(ANSI - Latin I)"},
    {1253, "(ANSI - Greek)"},
    {1254, "(ANSI - Turkish)"},
    {1255, "(ANSI - Hebrew)"},
    {1256, "(ANSI - Arabic)"},
    {1257, "(ANSI - Baltic)"},
    {1258, "(ANSI/OEM - Viet Nam)"},
    {1361, "(Korean - Johab)"},
    {10000, "(MAC - Roman)"},
    {20127, "(US-ASCII)"},
    {20866, "(Russian - KOI8)"},
    {28591, "(ISO 8859-1 Latin I)"},
    {65000, "(UTF-7)"},
    {65001, "(UTF-8)"},
};

BOOL WINAPI GetCPInfoExW(UINT cp, DWORD flags, LPCPINFOEXW info) {
  CPINFO ci;
  WCHAR *p;
  unsigned i, v, div;
  (void)flags;
  if (!info) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (cp == CP_ACP)
    cp = GetACP();
  else if (cp == CP_OEMCP)
    cp = GetOEMCP();
  if (!GetCPInfo(cp, &ci))
    return FALSE;
  info->MaxCharSize = ci.MaxCharSize;
  ow_memcpy(info->DefaultChar, ci.DefaultChar, sizeof info->DefaultChar);
  ow_memcpy(info->LeadByte, ci.LeadByte, sizeof info->LeadByte);
  info->UnicodeDefaultChar = '?';
  info->CodePage = cp;
  p = info->CodePageName;
  for (div = 10000, v = cp; div > 1 && v / div == 0; div /= 10) {
  }
  for (; div; div /= 10)
    *p++ = (WCHAR)('0' + v / div % 10);
  for (i = 0; i < sizeof cp_names / sizeof *cp_names; i++) {
    if (cp_names[i].cp == cp) {
      const char *n = cp_names[i].name;
      int pad = 6 - (int)(p - info->CodePageName);
      while (pad-- > 0)
        *p++ = ' ';
      while (*n && p < info->CodePageName + MAX_PATH - 1)
        *p++ = (unsigned char)*n++;
      break;
    }
  }
  *p = 0;
  return TRUE;
}

/* ---- conversions -------------------------------------------------- */

UINT ow_fcp(void) { return AreFileApisANSI() ? CP_ACP : CP_OEMCP; }

LPSTR ow_ancp(LPCWSTR w, int n, int *outn, UINT cp) {
  int len;
  LPSTR a;
  if (outn)
    *outn = 0;
  if (!w)
    return NULL;
  if (n < 0)
    n = ow_wcslen(w);
  len = n ? ow_wc2mb(cp, 0, w, n, NULL, 0) : 0;
  a = ow_alloc(len + 1);
  if (!a)
    return NULL;
  if (n)
    ow_wc2mb(cp, 0, w, n, a, len);
  a[len] = 0;
  if (outn)
    *outn = len;
  return a;
}

LPSTR ow_an(LPCWSTR w, int n, int *outn) {
  return ow_ancp(w, n, outn, CP_ACP);
}
LPSTR ow_acp(LPCWSTR w, UINT cp) { return ow_ancp(w, -1, NULL, cp); }
LPSTR ow_a(LPCWSTR w) { return ow_ancp(w, -1, NULL, CP_ACP); }

LPSTR ow_ares(LPCWSTR w) {
  if (IS_INTRESOURCE(w))
    return (LPSTR)w;
  return ow_a(w);
}

LPWSTR ow_wncp(LPCSTR a, int n, int *outn, UINT cp) {
  int len;
  LPWSTR w;
  if (outn)
    *outn = 0;
  if (!a)
    return NULL;
  if (n < 0)
    n = ow_strlen(a);
  len = n ? ow_mb2wc(cp, 0, a, n, NULL, 0) : 0;
  w = ow_alloc((len + 1) * sizeof(WCHAR));
  if (!w)
    return NULL;
  if (n)
    ow_mb2wc(cp, 0, a, n, w, len);
  w[len] = 0;
  if (outn)
    *outn = len;
  return w;
}

LPWSTR ow_wn(LPCSTR a, int n, int *outn) {
  return ow_wncp(a, n, outn, CP_ACP);
}
LPWSTR ow_wcp(LPCSTR a, UINT cp) { return ow_wncp(a, -1, NULL, cp); }
LPWSTR ow_w(LPCSTR a) { return ow_wncp(a, -1, NULL, CP_ACP); }

LPWSTR ow_wres(LPCSTR a) {
  if (IS_INTRESOURCE(a))
    return (LPWSTR)a;
  return ow_w(a);
}

int ow_alist_len(LPCSTR a) {
  int n = 0;
  if (!a)
    return 0;
  while (a[n])
    n += ow_strlen(a + n) + 1;
  return n + 1;
}

int ow_wlist_len(LPCWSTR w) {
  int n = 0;
  if (!w)
    return 0;
  while (w[n])
    n += ow_wcslen(w + n) + 1;
  return n + 1;
}

LPSTR ow_alist(LPCWSTR w) {
  if (!w)
    return NULL;
  return ow_an(w, ow_wlist_len(w), NULL);
}

LPWSTR ow_wlist(LPCSTR a) {
  if (!a)
    return NULL;
  return ow_wn(a, ow_alist_len(a), NULL);
}

int ow_wlen_of(LPCSTR a, int n, UINT cp) {
  if (!a)
    return 0;
  if (n < 0)
    n = ow_strlen(a);
  return n ? ow_mb2wc(cp, 0, a, n, NULL, 0) : 0;
}

int ow_towcp(LPCSTR a, int n, LPWSTR w, int cch, UINT cp) {
  int len;
  LPWSTR tmp;
  if (cch <= 0 || !w)
    return 0;
  if (!a) {
    w[0] = 0;
    return 0;
  }
  if (n < 0)
    n = ow_strlen(a);
  if (!n) {
    w[0] = 0;
    return 0;
  }
  len = ow_mb2wc(cp, 0, a, n, w, cch - 1);
  if (len == 0) {
    /* Too long: convert all of it and keep what fits. */
    tmp = ow_wncp(a, n, &len, cp);
    if (!tmp) {
      w[0] = 0;
      return 0;
    }
    if (len > cch - 1)
      len = cch - 1;
    ow_memcpy(w, tmp, len * sizeof(WCHAR));
    ow_free(tmp);
  }
  w[len] = 0;
  return len;
}

int ow_tow(LPCSTR a, int n, LPWSTR w, int cch) {
  return ow_towcp(a, n, w, cch, CP_ACP);
}

int ow_toa(LPCWSTR w, int n, LPSTR a, int cb) {
  int len, i;
  LPSTR tmp;
  if (cb <= 0 || !a)
    return 0;
  tmp = ow_an(w, n, &len);
  if (!tmp) {
    a[0] = 0;
    return 0;
  }
  if (len > cb - 1) {
    /* Cut at a character boundary. */
    for (i = 0; i < cb - 1;) {
      int step = IsDBCSLeadByte((BYTE)tmp[i]) ? 2 : 1;
      if (i + step > cb - 1)
        break;
      i += step;
    }
    len = i;
  }
  ow_memcpy(a, tmp, len);
  a[len] = 0;
  ow_free(tmp);
  return len;
}

DWORD ow_ret_str(LPCSTR a, LPWSTR buf, DWORD cch, UINT cp) {
  int wl;
  if (!a)
    return 0;
  wl = ow_wlen_of(a, -1, cp);
  if (!buf || (DWORD)wl + 1 > cch)
    return wl + 1;
  ow_towcp(a, -1, buf, cch, cp);
  return wl;
}

LPSTR ow_get(ow_getter fn, void *ctx) {
  DWORD size = MAX_PATH * 2, r;
  LPSTR buf;
  for (;;) {
    buf = ow_alloc(size);
    if (!buf) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return NULL;
    }
    SetLastError(0);
    r = fn(ctx, buf, size);
    if (r == 0 && GetLastError() != 0) {
      ow_free(buf);
      return NULL;
    }
    if (r < size)
      return buf;
    ow_free(buf);
    size = r + 1;
    if (size < MAX_PATH * 2)
      size = MAX_PATH * 2;
  }
}

/* ---- the optional DLLs --------------------------------------------- */

static const char *const dll_names[OW_NDLLS] = {
    "comdlg32.dll", "winspool.drv", "shell32.dll",  "winmm.dll",
    "version.dll",  "mpr.dll",      "oleacc.dll",   "oledlg.dll",
    "rasapi32.dll", "msvfw32.dll",  "sensapi.dll",  "avicap32.dll",
    "secur32.dll",  "ole32.dll",    "kernel32.dll", "user32.dll",
    "gdi32.dll",    "advapi32.dll", "imm32.dll",
};
static HMODULE dll_handles[OW_NDLLS];
static BYTE dll_failed[OW_NDLLS];

FARPROC ow_proc(int dll, const char *name) {
  HMODULE h = dll_handles[dll];
  if (!h) {
    if (dll_failed[dll])
      return NULL;
    h = GetModuleHandleA(dll_names[dll]);
    if (!h)
      h = LoadLibraryA(dll_names[dll]);
    /* Windows 95 has its security functions in secur32.dll only on
       some installs; Windows 98 and ME have them in secur32.dll or
       security.dll. */
    if (!h && dll == OW_SECUR32)
      h = LoadLibraryA("security.dll");
    if (!h) {
      dll_failed[dll] = 1;
      return NULL;
    }
    dll_handles[dll] = h;
  }
  return REAL(GetProcAddress)(h, name);
}

/* ---- GetProcAddress -------------------------------------------------- */

/* Asked for a function of a system DLL that opencow implements, it returns
 * opencow's, so that programs that look up W functions at run time get
 * working ones. */
static const char *const wrapped[] = {
    "kernel32.dll", "user32.dll",   "gdi32.dll",    "advapi32.dll",
    "comdlg32.dll", "winspool.drv", "shell32.dll",  "winmm.dll",
    "version.dll",  "mpr.dll",      "oleacc.dll",   "oledlg.dll",
    "rasapi32.dll", "msvfw32.dll",  "sensapi.dll",  "avicap32.dll",
    "secur32.dll",
};

FARPROC WINAPI GetProcAddress(HMODULE h, LPCSTR name) {
  FARPROC sys = REAL(GetProcAddress)(h, name);
  unsigned i;
  if (!sys || IS_INTRESOURCE(name) || h == ow_self)
    return sys;
  for (i = 0; i < sizeof wrapped / sizeof *wrapped; i++) {
    if (GetModuleHandleA(wrapped[i]) == h) {
      FARPROC mine = REAL(GetProcAddress)(ow_self, name);
      return mine ? mine : sys;
    }
  }
  return sys;
}

/* ---- lstr functions ---------------------------------------------------- */

int WINAPI lstrlenW(LPCWSTR s) { return ow_wcslen(s); }

LPWSTR WINAPI lstrcpyW(LPWSTR d, LPCWSTR s) {
  if (!d || !s)
    return NULL;
  return ow_wcscpy(d, s);
}

LPWSTR WINAPI lstrcpynW(LPWSTR d, LPCWSTR s, int n) {
  if (!d || !s)
    return NULL;
  if (n > 0)
    ow_wcsncpyz(d, s, n);
  return d;
}

LPWSTR WINAPI lstrcatW(LPWSTR d, LPCWSTR s) {
  if (!d || !s)
    return NULL;
  ow_wcscpy(d + ow_wcslen(d), s);
  return d;
}

int WINAPI lstrcmpW(LPCWSTR a, LPCWSTR b) {
  if (!a || !b)
    return a ? 1 : b ? -1 : 0;
  return CompareStringW(GetThreadLocale(), 0, a, -1, b, -1) - 2;
}

int WINAPI lstrcmpiW(LPCWSTR a, LPCWSTR b) {
  if (!a || !b)
    return a ? 1 : b ? -1 : 0;
  return CompareStringW(GetThreadLocale(), NORM_IGNORECASE, a, -1, b, -1) -
         2;
}

/* ---- wsprintf ---------------------------------------------------------- */

#define WSPRINTF_MAX 1024

struct out {
  LPWSTR p;
  int n;
};

static void put(struct out *o, WCHAR c) {
  if (o->n < WSPRINTF_MAX - 1)
    o->p[o->n++] = c;
}

static void pad(struct out *o, WCHAR c, int n) {
  while (n-- > 0)
    put(o, c);
}

int WINAPI wvsprintfW(LPWSTR buf, LPCWSTR fmt, va_list ap) {
  struct out o;
  if (!buf || !fmt)
    return 0;
  o.p = buf;
  o.n = 0;
  while (*fmt) {
    int left = 0, alt = 0, zero = 0, width = 0, prec = -1, size = 0;
    WCHAR type;
    if (*fmt != '%') {
      put(&o, *fmt++);
      continue;
    }
    fmt++;
    for (;; fmt++) {
      if (*fmt == '-')
        left = 1;
      else if (*fmt == '#')
        alt = 1;
      else if (*fmt == '0')
        zero = 1;
      else
        break;
    }
    while (*fmt >= '0' && *fmt <= '9')
      width = width * 10 + (*fmt++ - '0');
    if (*fmt == '.') {
      fmt++;
      prec = 0;
      while (*fmt >= '0' && *fmt <= '9')
        prec = prec * 10 + (*fmt++ - '0');
    }
    if (*fmt == 'l' || *fmt == 'L' || *fmt == 'w') {
      size = 'l';
      fmt++;
    } else if (*fmt == 'h') {
      size = 'h';
      fmt++;
    } else if (fmt[0] == 'I' && fmt[1] == '6' && fmt[2] == '4') {
      size = '6';
      fmt += 3;
    }
    type = *fmt;
    if (!type)
      break;
    fmt++;
    switch (type) {
    case 'c':
    case 'C': {
      WCHAR c;
      int wide = size == 'l' || (type == 'c' && size != 'h');
      unsigned v = va_arg(ap, unsigned);
      if (wide) {
        c = (WCHAR)v;
      } else {
        char ch = (char)v;
        if (!ow_mb2wc(CP_ACP, 0, &ch, 1, &c, 1))
          c = (unsigned char)ch;
      }
      if (!left)
        pad(&o, ' ', width - 1);
      put(&o, c);
      if (left)
        pad(&o, ' ', width - 1);
      break;
    }
    case 's':
    case 'S': {
      int wide = size == 'l' || (type == 's' && size != 'h');
      void *v = va_arg(ap, void *);
      LPWSTR tmp = NULL;
      LPCWSTR s;
      int len;
      if (!v)
        s = L"(null)";
      else if (wide)
        s = v;
      else
        s = tmp = ow_w(v);
      if (!s)
        s = L"";
      len = ow_wcslen(s);
      if (prec >= 0 && len > prec)
        len = prec;
      if (!left)
        pad(&o, ' ', width - len);
      for (int i = 0; i < len; i++)
        put(&o, s[i]);
      if (left)
        pad(&o, ' ', width - len);
      ow_free(tmp);
      break;
    }
    case 'd':
    case 'i':
    case 'u':
    case 'x':
    case 'X':
    case 'p': {
      unsigned long long v;
      int neg = 0, base = 10, len, digits;
      char tmp[24];
      const char *dig = type == 'X' || type == 'p' ? "0123456789ABCDEF"
                                                   : "0123456789abcdef";
      if (type == 'p') {
        v = (UINT_PTR)va_arg(ap, void *);
        base = 16;
        prec = 8;
      } else if (size == '6') {
        v = va_arg(ap, unsigned long long);
        if ((type == 'd' || type == 'i') && (long long)v < 0) {
          neg = 1;
          v = -(long long)v;
        }
      } else {
        unsigned x = va_arg(ap, unsigned);
        if (size == 'h')
          x = (type == 'd' || type == 'i') ? (unsigned)(int)(short)x
                                           : (unsigned short)x;
        if ((type == 'd' || type == 'i') && (int)x < 0) {
          neg = 1;
          v = (unsigned)(-(int)x);
        } else {
          v = x;
        }
      }
      if (type == 'x' || type == 'X')
        base = 16;
      len = 0;
      do {
        tmp[len++] = dig[v % base];
        v /= base;
      } while (v);
      digits = len;
      if (prec > digits)
        digits = prec;
      {
        int total = digits + neg + (alt && base == 16 ? 2 : 0);
        if (!left && !(zero && prec < 0))
          pad(&o, ' ', width - total);
        if (neg)
          put(&o, '-');
        if (alt && base == 16) {
          put(&o, '0');
          put(&o, type == 'X' ? 'X' : 'x');
        }
        if (!left && zero && prec < 0)
          pad(&o, '0', width - total);
        pad(&o, '0', digits - len);
        while (len)
          put(&o, tmp[--len]);
        if (left)
          pad(&o, ' ', width - total);
      }
      break;
    }
    default:
      put(&o, type);
      break;
    }
  }
  buf[o.n] = 0;
  return o.n;
}

int WINAPIV wsprintfW(LPWSTR buf, LPCWSTR fmt, ...) {
  va_list ap;
  int r;
  va_start(ap, fmt);
  r = wvsprintfW(buf, fmt, ap);
  va_end(ap);
  return r;
}
