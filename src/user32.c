/* user32: menus, accelerators, characters, keyboard, resources, message
 * boxes, text drawing, the clipboard, DDE, displays and system settings.
 * The window functions are in window.c.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"
#include <dbt.h>
#include <ddeml.h>

/* ---- menus ----------------------------------------------------------- */

static BOOL menu_string(UINT flags) {
  return !(flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR));
}

#define MENU_ITEM(item, flags)                                               \
  LPSTR a = menu_string(flags) ? ow_a(item) : (LPSTR)(item)
#define MENU_FREE(flags)                                                     \
  if (menu_string(flags))                                                    \
  ow_free(a)

BOOL WINAPI AppendMenuW(HMENU m, UINT flags, UINT_PTR id, LPCWSTR item) {
  MENU_ITEM(item, flags);
  BOOL r = AppendMenuA(m, flags, id, a);
  MENU_FREE(flags);
  return r;
}

BOOL WINAPI InsertMenuW(HMENU m, UINT pos, UINT flags, UINT_PTR id,
                        LPCWSTR item) {
  MENU_ITEM(item, flags);
  BOOL r = InsertMenuA(m, pos, flags, id, a);
  MENU_FREE(flags);
  return r;
}

BOOL WINAPI ModifyMenuW(HMENU m, UINT pos, UINT flags, UINT_PTR id,
                        LPCWSTR item) {
  MENU_ITEM(item, flags);
  BOOL r = ModifyMenuA(m, pos, flags, id, a);
  MENU_FREE(flags);
  return r;
}

BOOL WINAPI ChangeMenuW(HMENU m, UINT cmd, LPCWSTR item, UINT insert,
                        UINT flags) {
  BOOL str = menu_string(flags) && !(flags & (MF_DELETE | MF_REMOVE));
  LPSTR a = str ? ow_a(item) : (LPSTR)item;
  BOOL r = ChangeMenuA(m, cmd, a, insert, flags);
  if (str)
    ow_free(a);
  return r;
}

int WINAPI GetMenuStringW(HMENU m, UINT id, LPWSTR buf, int cch,
                          UINT flags) {
  int n = GetMenuStringA(m, id, NULL, 0, flags), r;
  LPSTR a;
  if (n <= 0) {
    if (buf && cch > 0)
      buf[0] = 0;
    return 0;
  }
  a = ow_alloc(n + 2);
  if (!a)
    return 0;
  n = GetMenuStringA(m, id, a, n + 1, flags);
  if (!buf)
    r = ow_wlen_of(a, n, CP_ACP);
  else
    r = ow_tow(a, n, buf, cch);
  ow_free(a);
  return r;
}

static BOOL mii_string(const MENUITEMINFOW *w) {
  if (w->fMask & MIIM_STRING)
    return TRUE;
  return (w->fMask & MIIM_TYPE) &&
         !(w->fType & (MFT_BITMAP | MFT_SEPARATOR | MFT_OWNERDRAW));
}

static BOOL set_item(HMENU m, UINT item, BOOL bypos, LPCMENUITEMINFOW w,
                     BOOL insert) {
  MENUITEMINFOA a;
  BOOL r, str;
  if (!w || w->cbSize > sizeof a) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  ow_memset(&a, 0, sizeof a);
  ow_memcpy(&a, w, w->cbSize);
  str = mii_string(w) && w->dwTypeData;
  if (str)
    a.dwTypeData = ow_a(w->dwTypeData);
  r = insert ? InsertMenuItemA(m, item, bypos, &a)
             : SetMenuItemInfoA(m, item, bypos, &a);
  if (str)
    ow_free(a.dwTypeData);
  return r;
}

BOOL WINAPI InsertMenuItemW(HMENU m, UINT item, BOOL bypos,
                            LPCMENUITEMINFOW w) {
  return set_item(m, item, bypos, w, TRUE);
}

BOOL WINAPI SetMenuItemInfoW(HMENU m, UINT item, BOOL bypos,
                             LPCMENUITEMINFOW w) {
  return set_item(m, item, bypos, w, FALSE);
}

BOOL WINAPI GetMenuItemInfoW(HMENU m, UINT item, BOOL bypos,
                             LPMENUITEMINFOW w) {
  MENUITEMINFOA a;
  LPWSTR wbuf;
  UINT wcch;
  BOOL want;
  if (!w || w->cbSize > sizeof a) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  wbuf = w->dwTypeData;
  wcch = w->cch;
  ow_memset(&a, 0, sizeof a);
  ow_memcpy(&a, w, w->cbSize);
  want = (w->fMask & (MIIM_TYPE | MIIM_STRING)) != 0;
  a.dwTypeData = NULL;
  a.cch = 0;
  if (!GetMenuItemInfoA(m, item, bypos, &a))
    return FALSE;
  if (want && !(a.fType & (MFT_BITMAP | MFT_SEPARATOR | MFT_OWNERDRAW))) {
    /* Get the text itself, to count or copy it. */
    UINT n = a.cch;
    LPSTR s = ow_alloc(n + 2);
    if (!s) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }
    a.dwTypeData = s;
    a.cch = n + 1;
    if (!GetMenuItemInfoA(m, item, bypos, &a)) {
      ow_free(s);
      return FALSE;
    }
    ow_memcpy(w, &a, w->cbSize);
    if (wbuf && wcch) {
      w->dwTypeData = wbuf;
      w->cch = ow_tow(s, -1, wbuf, wcch);
    } else {
      w->dwTypeData = wbuf;
      w->cch = ow_wlen_of(s, -1, CP_ACP);
    }
    ow_free(s);
    return TRUE;
  }
  ow_memcpy(w, &a, w->cbSize);
  /* For a bitmap or owner-drawn item, dwTypeData holds its handle or
     data; otherwise the caller's buffer stays. */
  if (!want) {
    w->dwTypeData = wbuf;
    w->cch = wcch;
  }
  return TRUE;
}

HMENU WINAPI LoadMenuW(HINSTANCE inst, LPCWSTR name) {
  LPSTR a = ow_ares(name);
  HMENU r = LoadMenuA(inst, a);
  ow_free(a);
  return r;
}

HMENU WINAPI LoadMenuIndirectW(const MENUTEMPLATEW *t) {
  /* Menu templates are in Unicode for A and W alike. */
  return LoadMenuIndirectA(t);
}

/* ---- accelerators ------------------------------------------------------ */

static WORD key_a(WORD key) {
  WCHAR w = key;
  char b[4];
  int n = ow_wc2mb(CP_ACP, 0, &w, 1, b, sizeof b);
  return n == 1 ? (BYTE)b[0] : n == 2 ? (WORD)(((BYTE)b[0] << 8) | (BYTE)b[1])
                                      : key;
}

static WORD key_w(WORD key) {
  char b[2];
  WCHAR w;
  int n;
  if (key > 0xff) {
    b[0] = (char)(key >> 8);
    b[1] = (char)key;
    n = 2;
  } else {
    b[0] = (char)key;
    n = 1;
  }
  return ow_mb2wc(CP_ACP, 0, b, n, &w, 1) ? w : key;
}

HACCEL WINAPI CreateAcceleratorTableW(LPACCEL acc, int n) {
  LPACCEL a;
  HACCEL r;
  if (!acc || n <= 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  a = ow_alloc(n * sizeof *a);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  for (int i = 0; i < n; i++) {
    a[i] = acc[i];
    if (!(a[i].fVirt & FVIRTKEY))
      a[i].key = key_a(a[i].key);
  }
  r = CreateAcceleratorTableA(a, n);
  ow_free(a);
  return r;
}

int WINAPI CopyAcceleratorTableW(HACCEL h, LPACCEL acc, int n) {
  int r = CopyAcceleratorTableA(h, acc, n);
  if (acc)
    for (int i = 0; i < r && i < n; i++)
      if (!(acc[i].fVirt & FVIRTKEY))
        acc[i].key = key_w(acc[i].key);
  return r;
}

HACCEL WINAPI LoadAcceleratorsW(HINSTANCE inst, LPCWSTR name) {
  LPSTR a = ow_ares(name);
  HACCEL r = LoadAcceleratorsA(inst, a);
  ow_free(a);
  return r;
}

/* ---- characters ------------------------------------------------------ */

LPWSTR WINAPI CharUpperW(LPWSTR s) {
  if (IS_INTRESOURCE(s))
    return (LPWSTR)(UINT_PTR)ow_upper(LOWORD((UINT_PTR)s));
  for (LPWSTR p = s; *p; p++)
    *p = ow_upper(*p);
  return s;
}

LPWSTR WINAPI CharLowerW(LPWSTR s) {
  if (IS_INTRESOURCE(s))
    return (LPWSTR)(UINT_PTR)ow_lower(LOWORD((UINT_PTR)s));
  for (LPWSTR p = s; *p; p++)
    *p = ow_lower(*p);
  return s;
}

DWORD WINAPI CharUpperBuffW(LPWSTR s, DWORD n) {
  if (!s)
    return 0;
  for (DWORD i = 0; i < n; i++)
    s[i] = ow_upper(s[i]);
  return n;
}

DWORD WINAPI CharLowerBuffW(LPWSTR s, DWORD n) {
  if (!s)
    return 0;
  for (DWORD i = 0; i < n; i++)
    s[i] = ow_lower(s[i]);
  return n;
}

LPWSTR WINAPI CharNextW(LPCWSTR s) {
  if (!*s)
    return (LPWSTR)s;
  if (s[0] >= 0xd800 && s[0] < 0xdc00 && s[1] >= 0xdc00 && s[1] < 0xe000)
    s++;
  s++;
  while ((ow_ctype3(*s) & C3_NONSPACING) && *s)
    s++;
  return (LPWSTR)s;
}

LPWSTR WINAPI CharPrevW(LPCWSTR start, LPCWSTR cur) {
  if (cur <= start)
    return (LPWSTR)start;
  cur--;
  while (cur > start && (ow_ctype3(*cur) & C3_NONSPACING))
    cur--;
  if (cur > start && cur[0] >= 0xdc00 && cur[0] < 0xe000 &&
      cur[-1] >= 0xd800 && cur[-1] < 0xdc00)
    cur--;
  return (LPWSTR)cur;
}

BOOL WINAPI IsCharAlphaW(WCHAR c) { return (ow_ctype1(c) & C1_ALPHA) != 0; }
BOOL WINAPI IsCharAlphaNumericW(WCHAR c) {
  return (ow_ctype1(c) & (C1_ALPHA | C1_DIGIT)) != 0;
}
BOOL WINAPI IsCharUpperW(WCHAR c) { return (ow_ctype1(c) & C1_UPPER) != 0; }
BOOL WINAPI IsCharLowerW(WCHAR c) { return (ow_ctype1(c) & C1_LOWER) != 0; }

BOOL WINAPI CharToOemW(LPCWSTR s, LPSTR d) {
  int n;
  if (!s || !d)
    return FALSE;
  n = ow_wcslen(s) + 1;
  ow_wc2mb(CP_OEMCP, 0, s, n, d, n * 2);
  return TRUE;
}

BOOL WINAPI CharToOemBuffW(LPCWSTR s, LPSTR d, DWORD n) {
  int al;
  LPSTR a;
  if (!s || !d)
    return FALSE;
  a = ow_ancp(s, n, &al, CP_OEMCP);
  if (!a)
    return FALSE;
  ow_memcpy(d, a, (DWORD)al < n ? (DWORD)al : n);
  ow_free(a);
  return TRUE;
}

BOOL WINAPI OemToCharW(LPCSTR s, LPWSTR d) {
  int n;
  if (!s || !d)
    return FALSE;
  n = ow_strlen(s) + 1;
  ow_mb2wc(CP_OEMCP, 0, s, n, d, n);
  return TRUE;
}

BOOL WINAPI OemToCharBuffW(LPCSTR s, LPWSTR d, DWORD n) {
  int wl;
  LPWSTR w;
  if (!s || !d)
    return FALSE;
  if (!n)
    return TRUE;
  w = ow_wncp(s, n, &wl, CP_OEMCP);
  if (!w)
    return FALSE;
  ow_memcpy(d, w, ((DWORD)wl < n ? (DWORD)wl : n) * sizeof(WCHAR));
  ow_free(w);
  return TRUE;
}

/* ---- keyboard ---------------------------------------------------------- */

int WINAPI GetKeyNameTextW(LONG lp, LPWSTR buf, int cch) {
  char a[256];
  int n = GetKeyNameTextA(lp, a, sizeof a);
  if (!buf || cch <= 0)
    return 0;
  return ow_tow(a, n, buf, cch);
}

BOOL WINAPI GetKeyboardLayoutNameW(LPWSTR buf) {
  char a[KL_NAMELENGTH];
  if (!GetKeyboardLayoutNameA(a))
    return FALSE;
  ow_tow(a, -1, buf, KL_NAMELENGTH);
  return TRUE;
}

HKL WINAPI LoadKeyboardLayoutW(LPCWSTR id, UINT flags) {
  LPSTR a = ow_a(id);
  HKL r = LoadKeyboardLayoutA(a, flags);
  ow_free(a);
  return r;
}

static UINT vk_char_w(UINT r) {
  /* The character in the low word; the top bit marks a dead key. */
  WCHAR w;
  char c = (char)(r & 0xff);
  if (!(r & 0xffff) || !ow_mb2wc(CP_ACP, 0, &c, 1, &w, 1))
    return r;
  return (r & 0xffff0000u) | (r & 0x8000) | w;
}

UINT WINAPI MapVirtualKeyW(UINT code, UINT type) {
  UINT r = MapVirtualKeyA(code, type);
  return type == MAPVK_VK_TO_CHAR ? vk_char_w(r) : r;
}

UINT WINAPI MapVirtualKeyExW(UINT code, UINT type, HKL hkl) {
  UINT r = MapVirtualKeyExA(code, type, hkl);
  return type == MAPVK_VK_TO_CHAR ? vk_char_w(r) : r;
}

SHORT WINAPI VkKeyScanW(WCHAR c) {
  char b[4];
  if (ow_wc2mb(CP_ACP, 0, &c, 1, b, sizeof b) != 1)
    return -1;
  return VkKeyScanA(b[0]);
}

SHORT WINAPI VkKeyScanExW(WCHAR c, HKL hkl) {
  char b[4];
  if (ow_wc2mb(CP_ACP, 0, &c, 1, b, sizeof b) != 1)
    return -1;
  return VkKeyScanExA(b[0], hkl);
}

/* ---- resources --------------------------------------------------------- */

HBITMAP WINAPI LoadBitmapW(HINSTANCE inst, LPCWSTR name) {
  LPSTR a = ow_ares(name);
  HBITMAP r = LoadBitmapA(inst, a);
  ow_free(a);
  return r;
}

HCURSOR WINAPI LoadCursorW(HINSTANCE inst, LPCWSTR name) {
  LPSTR a = ow_ares(name);
  HCURSOR r = LoadCursorA(inst, a);
  ow_free(a);
  return r;
}

HICON WINAPI LoadIconW(HINSTANCE inst, LPCWSTR name) {
  LPSTR a = ow_ares(name);
  HICON r = LoadIconA(inst, a);
  ow_free(a);
  return r;
}

HANDLE WINAPI LoadImageW(HINSTANCE inst, LPCWSTR name, UINT type, int cx,
                         int cy, UINT flags) {
  LPSTR a = IS_INTRESOURCE(name)          ? (LPSTR)name
            : (flags & LR_LOADFROMFILE) ? ow_acp(name, ow_fcp())
                                          : ow_a(name);
  HANDLE r = LoadImageA(inst, a, type, cx, cy, flags);
  ow_free(a);
  return r;
}

HCURSOR WINAPI LoadCursorFromFileW(LPCWSTR name) {
  LPSTR a = ow_acp(name, ow_fcp());
  HCURSOR r = LoadCursorFromFileA(a);
  ow_free(a);
  return r;
}

/* String tables are in Unicode: read them directly, keeping characters
 * that the ANSI code page does not have. */
int WINAPI LoadStringW(HINSTANCE inst, UINT id, LPWSTR buf, int cch) {
  HRSRC res;
  HGLOBAL g;
  const WCHAR *p;
  int len;
  if (!buf) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  res = FindResourceA(inst, MAKEINTRESOURCEA((LOWORD(id) >> 4) + 1),
                      (LPCSTR)RT_STRING);
  if (!res || !(g = LoadResource(inst, res)) || !(p = LockResource(g))) {
    if (cch > 0)
      buf[0] = 0;
    return 0;
  }
  for (UINT i = 0; i < (id & 15); i++)
    p += *p + 1;
  len = *p++;
  if (cch == 0) {
    /* A pointer to the read-only string itself. */
    *(const WCHAR **)buf = p;
    return len;
  }
  if (len > cch - 1)
    len = cch - 1;
  ow_memcpy(buf, p, len * sizeof(WCHAR));
  buf[len] = 0;
  return len;
}

/* ---- message boxes ---------------------------------------------------- */

int WINAPI MessageBoxW(HWND h, LPCWSTR text, LPCWSTR cap, UINT type) {
  LPSTR a = ow_a(text), b = ow_a(cap);
  int r = MessageBoxA(h, a, b, type);
  ow_free(a);
  ow_free(b);
  return r;
}

int WINAPI MessageBoxExW(HWND h, LPCWSTR text, LPCWSTR cap, UINT type,
                         WORD lang) {
  LPSTR a = ow_a(text), b = ow_a(cap);
  int r = MessageBoxExA(h, a, b, type, lang);
  ow_free(a);
  ow_free(b);
  return r;
}

int WINAPI MessageBoxIndirectW(const MSGBOXPARAMSW *w) {
  MSGBOXPARAMSA a;
  int r;
  if (!w) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  ow_memcpy(&a, w, sizeof a);
  a.cbSize = sizeof a;
  a.lpszText = ow_a(w->lpszText);
  a.lpszCaption = ow_a(w->lpszCaption);
  a.lpszIcon = ow_ares(w->lpszIcon);
  r = MessageBoxIndirectA(&a);
  ow_free(a.lpszText);
  ow_free(a.lpszCaption);
  ow_free(a.lpszIcon);
  return r;
}

/* ---- drawing text ------------------------------------------------------- */

static int draw_text(HDC dc, LPWSTR s, int n, LPRECT rc, UINT fmt,
                     LPDRAWTEXTPARAMS params) {
  int al, r;
  LPSTR a, buf;
  if (!s)
    return 0;
  if (n < 0)
    n = ow_wcslen(s);
  a = ow_an(s, n, &al);
  if (!a)
    return 0;
  /* DT_MODIFYSTRING may add an ellipsis: four more characters. */
  buf = ow_alloc(al + 16);
  if (!buf) {
    ow_free(a);
    return 0;
  }
  ow_memcpy(buf, a, al);
  ow_free(a);
  r = params ? DrawTextExA(dc, buf, al, rc, fmt, params)
             : DrawTextA(dc, buf, al, rc, fmt);
  if (params)
    params->uiLengthDrawn = ow_wlen_of(buf, params->uiLengthDrawn, CP_ACP);
  if ((fmt & DT_MODIFYSTRING) &&
      (fmt & (DT_END_ELLIPSIS | DT_PATH_ELLIPSIS | DT_WORD_ELLIPSIS)))
    ow_tow(buf, -1, s, n + 5);
  ow_free(buf);
  return r;
}

int WINAPI DrawTextW(HDC dc, LPCWSTR s, int n, LPRECT rc, UINT fmt) {
  return draw_text(dc, (LPWSTR)s, n, rc, fmt, NULL);
}

int WINAPI DrawTextExW(HDC dc, LPWSTR s, int n, LPRECT rc, UINT fmt,
                       LPDRAWTEXTPARAMS params) {
  return draw_text(dc, s, n, rc, fmt, params);
}

BOOL WINAPI GrayStringW(HDC dc, HBRUSH br, GRAYSTRINGPROC proc, LPARAM lp,
                        int n, int x, int y, int cx, int cy) {
  int al;
  LPSTR a;
  BOOL r;
  if (proc)
    return GrayStringA(dc, br, proc, lp, n, x, y, cx, cy);
  a = ow_an((LPCWSTR)lp, n ? n : -1, &al);
  r = GrayStringA(dc, br, NULL, (LPARAM)a, al, x, y, cx, cy);
  ow_free(a);
  return r;
}

BOOL WINAPI DrawStateW(HDC dc, HBRUSH br, DRAWSTATEPROC proc, LPARAM lp,
                       WPARAM wp, int x, int y, int cx, int cy, UINT flags) {
  UINT type = flags & 0x000f;
  int al;
  LPSTR a;
  BOOL r;
  if ((type != DST_TEXT && type != DST_PREFIXTEXT) || proc)
    return DrawStateA(dc, br, proc, lp, wp, x, y, cx, cy, flags);
  a = ow_an((LPCWSTR)lp, wp ? (int)wp : -1, &al);
  r = DrawStateA(dc, br, NULL, (LPARAM)a, al, x, y, cx, cy, flags);
  ow_free(a);
  return r;
}

LONG WINAPI TabbedTextOutW(HDC dc, int x, int y, LPCWSTR s, int n, int ntabs,
                           const INT *tabs, int origin) {
  int al;
  LPSTR a = ow_an(s, n, &al);
  LONG r = TabbedTextOutA(dc, x, y, a ? a : "", al, ntabs, tabs, origin);
  ow_free(a);
  return r;
}

DWORD WINAPI GetTabbedTextExtentW(HDC dc, LPCWSTR s, int n, int ntabs,
                                  const INT *tabs) {
  int al;
  LPSTR a = ow_an(s, n, &al);
  DWORD r = GetTabbedTextExtentA(dc, a ? a : "", al, ntabs, tabs);
  ow_free(a);
  return r;
}

/* ---- clipboard ---------------------------------------------------------- */

OW_REAL_DECL(HANDLE, GetClipboardData, (UINT), 4);
OW_REAL_DECL(BOOL, IsClipboardFormatAvailable, (UINT), 4);
OW_REAL_DECL(UINT, EnumClipboardFormats, (UINT), 4);

/* Windows 9x makes no CF_UNICODETEXT from CF_TEXT: make it here. The
 * copy belongs to opencow and is replaced at the next call, as the
 * clipboard's own data is replaced when it changes. */
static HGLOBAL unicode_copy;

HANDLE WINAPI GetClipboardData(UINT fmt) {
  HANDLE h = REAL(GetClipboardData)(fmt);
  HANDLE t;
  const char *a;
  LPWSTR w;
  int n;
  if (h || fmt != CF_UNICODETEXT)
    return h;
  t = REAL(GetClipboardData)(CF_TEXT);
  if (!t || !(a = GlobalLock(t)))
    return NULL;
  n = ow_wlen_of(a, -1, CP_ACP);
  if (unicode_copy)
    GlobalFree(unicode_copy);
  unicode_copy = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE,
                             (n + 1) * sizeof(WCHAR));
  if (unicode_copy && (w = GlobalLock(unicode_copy)) != NULL) {
    ow_tow(a, -1, w, n + 1);
    GlobalUnlock(unicode_copy);
  }
  GlobalUnlock(t);
  return unicode_copy;
}

BOOL WINAPI IsClipboardFormatAvailable(UINT fmt) {
  if (REAL(IsClipboardFormatAvailable)(fmt))
    return TRUE;
  return fmt == CF_UNICODETEXT && REAL(IsClipboardFormatAvailable)(CF_TEXT);
}

UINT WINAPI EnumClipboardFormats(UINT fmt) {
  BOOL native = REAL(IsClipboardFormatAvailable)(CF_UNICODETEXT);
  UINT r;
  if (fmt == CF_UNICODETEXT && !native) {
    /* After the made-up one, go on from CF_TEXT. */
    return REAL(EnumClipboardFormats)(CF_TEXT);
  }
  r = REAL(EnumClipboardFormats)(fmt);
  if (r == CF_TEXT && fmt != CF_TEXT)
    return r;
  if (fmt == CF_TEXT && !native)
    return CF_UNICODETEXT;
  return r;
}

int WINAPI GetClipboardFormatNameW(UINT fmt, LPWSTR buf, int cch) {
  char a[512];
  int n = GetClipboardFormatNameA(fmt, a, sizeof a);
  if (!n)
    return 0;
  return ow_tow(a, n, buf, cch);
}

UINT WINAPI RegisterClipboardFormatW(LPCWSTR name) {
  LPSTR a = ow_a(name);
  UINT r = RegisterClipboardFormatA(a);
  ow_free(a);
  return r;
}

UINT WINAPI RegisterWindowMessageW(LPCWSTR name) {
  LPSTR a = ow_a(name);
  UINT r = RegisterWindowMessageA(a);
  ow_free(a);
  return r;
}

/* ---- DDE ---------------------------------------------------------------- */

OW_REAL_DECL(HCONV, DdeConnect, (DWORD, HSZ, HSZ, PCONVCONTEXT), 16);
OW_REAL_DECL(HCONVLIST, DdeConnectList,
             (DWORD, HSZ, HSZ, HCONVLIST, PCONVCONTEXT), 20);
OW_REAL_DECL(UINT, DdeQueryConvInfo, (HCONV, DWORD, PCONVINFO), 12);

UINT WINAPI DdeInitializeW(LPDWORD inst, PFNCALLBACK cb, DWORD cmd,
                           DWORD res) {
  return DdeInitializeA(inst, cb, cmd, res);
}

HSZ WINAPI DdeCreateStringHandleW(DWORD inst, LPCWSTR s, int cp) {
  LPSTR a;
  HSZ r;
  if (cp != CP_WINUNICODE)
    return DdeCreateStringHandleA(inst, (LPCSTR)s, cp);
  a = ow_a(s);
  r = DdeCreateStringHandleA(inst, a, CP_WINANSI);
  ow_free(a);
  return r;
}

DWORD WINAPI DdeQueryStringW(DWORD inst, HSZ hsz, LPWSTR buf, DWORD cch,
                             int cp) {
  DWORD n;
  LPSTR a;
  if (cp != CP_WINUNICODE)
    return DdeQueryStringA(inst, hsz, (LPSTR)buf, cch, cp);
  n = DdeQueryStringA(inst, hsz, NULL, 0, CP_WINANSI);
  if (!n)
    return 0;
  a = ow_alloc(n + 2);
  if (!a)
    return 0;
  n = DdeQueryStringA(inst, hsz, a, n + 1, CP_WINANSI);
  if (!buf)
    n = ow_wlen_of(a, n, CP_ACP);
  else
    n = ow_tow(a, n, buf, cch);
  ow_free(a);
  return n;
}

HCONV WINAPI DdeConnect(DWORD inst, HSZ svc, HSZ topic, PCONVCONTEXT cc) {
  return REAL(DdeConnect)(inst, svc, topic, cc);
}

HCONVLIST WINAPI DdeConnectList(DWORD inst, HSZ svc, HSZ topic,
                                HCONVLIST list, PCONVCONTEXT cc) {
  return REAL(DdeConnectList)(inst, svc, topic, list, cc);
}

UINT WINAPI DdeQueryConvInfo(HCONV conv, DWORD id, PCONVINFO ci) {
  return REAL(DdeQueryConvInfo)(conv, id, ci);
}

/* ---- displays and monitors ---------------------------------------------- */

/* The DEVMODE that Windows 9x knows ends before dmPanningWidth. */
#define DEVMODEA_9X offsetof(DEVMODEA, dmPanningWidth)

LONG WINAPI ChangeDisplaySettingsW(LPDEVMODEW dm, DWORD flags) {
  LPDEVMODEA a = ow_devmode_a(dm);
  LONG r = ChangeDisplaySettingsA(a, flags);
  ow_free(a);
  return r;
}

LONG WINAPI ChangeDisplaySettingsExW(LPCWSTR name, LPDEVMODEW dm, HWND h,
                                     DWORD flags, LPVOID lp) {
  LPDEVMODEA a = ow_devmode_a(dm);
  LPSTR n = ow_a(name);
  LONG r;
  OW_FN(OW_USER32, ChangeDisplaySettingsExA, goto old);
  r = pChangeDisplaySettingsExA(n, a, h, flags, lp);
  goto done;
old:
  r = name ? DISP_CHANGE_BADPARAM : ChangeDisplaySettingsA(a, flags);
done:
  ow_free(a);
  ow_free(n);
  return r;
}

static BOOL enum_settings(LPCWSTR name, DWORD mode, LPDEVMODEW w, DWORD flags,
                          BOOL ex) {
  DEVMODEA a;
  LPSTR n;
  BOOL r;
  if (!w) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  ow_memset(&a, 0, sizeof a);
  a.dmSize = DEVMODEA_9X;
  n = ow_a(name);
  if (ex) {
    OW_FN(OW_USER32, EnumDisplaySettingsExA, goto old);
    a.dmSize = sizeof a;
    r = pEnumDisplaySettingsExA(n, mode, &a, flags);
    goto done;
  }
old:
  r = EnumDisplaySettingsA(n, mode, &a);
done:
  ow_free(n);
  if (r)
    ow_devmode_to_w(&a, w);
  return r;
}

BOOL WINAPI EnumDisplaySettingsW(LPCWSTR name, DWORD mode, LPDEVMODEW w) {
  return enum_settings(name, mode, w, 0, FALSE);
}

BOOL WINAPI EnumDisplaySettingsExW(LPCWSTR name, DWORD mode, LPDEVMODEW w,
                                   DWORD flags) {
  return enum_settings(name, mode, w, flags, TRUE);
}

BOOL WINAPI EnumDisplayDevicesW(LPCWSTR name, DWORD i, PDISPLAY_DEVICEW w,
                                DWORD flags) {
  DISPLAY_DEVICEA a;
  LPSTR n;
  BOOL r;
  OW_FN(OW_USER32, EnumDisplayDevicesA, return FALSE);
  if (!w || w->cb < offsetof(DISPLAY_DEVICEW, DeviceID)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  ow_memset(&a, 0, sizeof a);
  a.cb = w->cb >= sizeof *w ? sizeof a : offsetof(DISPLAY_DEVICEA, DeviceID);
  n = ow_a(name);
  r = pEnumDisplayDevicesA(n, i, &a, flags);
  ow_free(n);
  if (!r)
    return FALSE;
  ow_tow(a.DeviceName, -1, w->DeviceName, 32);
  ow_tow(a.DeviceString, -1, w->DeviceString, 128);
  w->StateFlags = a.StateFlags;
  if (w->cb >= sizeof *w) {
    ow_tow(a.DeviceID, -1, w->DeviceID, 128);
    ow_tow(a.DeviceKey, -1, w->DeviceKey, 128);
  }
  return TRUE;
}

BOOL WINAPI GetMonitorInfoW(HMONITOR mon, LPMONITORINFO w) {
  MONITORINFOEXA a;
  BOOL r;
  OW_FN(OW_USER32, GetMonitorInfoA, return FALSE);
  if (!w) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (w->cbSize != sizeof(MONITORINFOEXW))
    return pGetMonitorInfoA(mon, w);
  a.cbSize = sizeof a;
  r = pGetMonitorInfoA(mon, (LPMONITORINFO)&a);
  if (r) {
    LPMONITORINFOEXW x = (LPMONITORINFOEXW)w;
    x->rcMonitor = a.rcMonitor;
    x->rcWork = a.rcWork;
    x->dwFlags = a.dwFlags;
    ow_tow(a.szDevice, -1, x->szDevice, CCHDEVICENAME);
  }
  return r;
}

UINT WINAPI GetWindowModuleFileNameW(HWND h, LPWSTR buf, UINT cch) {
  char a[MAX_PATH * 2];
  UINT n;
  OW_FN(OW_USER32, GetWindowModuleFileNameA, return 0);
  n = pGetWindowModuleFileNameA(h, a, sizeof a);
  if (!n)
    return 0;
  return ow_towcp(a, n, buf, cch, ow_fcp());
}

BOOL WINAPI GetAltTabInfoW(HWND h, int i, PALTTABINFO info, LPWSTR buf,
                           UINT cch) {
  LPSTR a = NULL;
  BOOL r;
  OW_FN(OW_USER32, GetAltTabInfoA, return FALSE);
  if (buf && cch) {
    a = ow_alloc(cch * 2 + 2);
    if (!a) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }
  }
  r = pGetAltTabInfoA(h, i, info, a, a ? cch * 2 + 1 : 0);
  if (r && a)
    ow_tow(a, -1, buf, cch);
  ow_free(a);
  return r;
}

HDEVNOTIFY WINAPI RegisterDeviceNotificationW(HANDLE h, LPVOID filter,
                                              DWORD flags) {
  DEV_BROADCAST_HDR *hdr = filter;
  HDEVNOTIFY r;
  OW_FN(OW_USER32, RegisterDeviceNotificationA, return NULL);
  if (hdr && hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
    DEV_BROADCAST_DEVICEINTERFACE_W *w = filter;
    int nl = (int)(w->dbcc_size - offsetof(DEV_BROADCAST_DEVICEINTERFACE_W,
                                           dbcc_name)) /
             2;
    int al;
    LPSTR name = ow_an(w->dbcc_name, nl > 0 ? ow_wcsnlen(w->dbcc_name, nl) : 0,
                       &al);
    DWORD size = offsetof(DEV_BROADCAST_DEVICEINTERFACE_A, dbcc_name) + al + 1;
    DEV_BROADCAST_DEVICEINTERFACE_A *a = ow_alloc(size);
    if (!a || !name) {
      ow_free(a);
      ow_free(name);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return NULL;
    }
    a->dbcc_size = size;
    a->dbcc_devicetype = w->dbcc_devicetype;
    a->dbcc_classguid = w->dbcc_classguid;
    ow_memcpy(a->dbcc_name, name, al + 1);
    r = pRegisterDeviceNotificationA(h, a, flags);
    ow_free(a);
    ow_free(name);
    return r;
  }
  return pRegisterDeviceNotificationA(h, filter, flags);
}

/* ---- system settings --------------------------------------------------- */

static void ncm_w(const NONCLIENTMETRICSA *a, NONCLIENTMETRICSW *w) {
  w->iBorderWidth = a->iBorderWidth;
  w->iScrollWidth = a->iScrollWidth;
  w->iScrollHeight = a->iScrollHeight;
  w->iCaptionWidth = a->iCaptionWidth;
  w->iCaptionHeight = a->iCaptionHeight;
  ow_logfont_w(&a->lfCaptionFont, &w->lfCaptionFont);
  w->iSmCaptionWidth = a->iSmCaptionWidth;
  w->iSmCaptionHeight = a->iSmCaptionHeight;
  ow_logfont_w(&a->lfSmCaptionFont, &w->lfSmCaptionFont);
  w->iMenuWidth = a->iMenuWidth;
  w->iMenuHeight = a->iMenuHeight;
  ow_logfont_w(&a->lfMenuFont, &w->lfMenuFont);
  ow_logfont_w(&a->lfStatusFont, &w->lfStatusFont);
  ow_logfont_w(&a->lfMessageFont, &w->lfMessageFont);
}

static void ncm_a(const NONCLIENTMETRICSW *w, NONCLIENTMETRICSA *a) {
  a->cbSize = sizeof *a;
  a->iBorderWidth = w->iBorderWidth;
  a->iScrollWidth = w->iScrollWidth;
  a->iScrollHeight = w->iScrollHeight;
  a->iCaptionWidth = w->iCaptionWidth;
  a->iCaptionHeight = w->iCaptionHeight;
  ow_logfont_a(&w->lfCaptionFont, &a->lfCaptionFont);
  a->iSmCaptionWidth = w->iSmCaptionWidth;
  a->iSmCaptionHeight = w->iSmCaptionHeight;
  ow_logfont_a(&w->lfSmCaptionFont, &a->lfSmCaptionFont);
  a->iMenuWidth = w->iMenuWidth;
  a->iMenuHeight = w->iMenuHeight;
  ow_logfont_a(&w->lfMenuFont, &a->lfMenuFont);
  ow_logfont_a(&w->lfStatusFont, &a->lfStatusFont);
  ow_logfont_a(&w->lfMessageFont, &a->lfMessageFont);
}

BOOL WINAPI SystemParametersInfoW(UINT action, UINT ui, PVOID pv, UINT ini) {
  BOOL r;
  switch (action) {
  case SPI_GETDESKWALLPAPER: {
    char a[MAX_PATH * 2];
    if (!pv)
      break;
    r = SystemParametersInfoA(action, sizeof a, a, ini);
    if (r)
      ow_towcp(a, -1, pv, ui ? (int)ui : MAX_PATH, ow_fcp());
    return r;
  }
  case SPI_SETDESKWALLPAPER: {
    LPSTR a;
    if (!pv || pv == (PVOID)-1 || IS_INTRESOURCE(pv))
      break;
    a = ow_acp(pv, ow_fcp());
    r = SystemParametersInfoA(action, ui, a, ini);
    ow_free(a);
    return r;
  }
  case SPI_GETICONTITLELOGFONT: {
    LOGFONTA a;
    if (!pv)
      break;
    r = SystemParametersInfoA(action, sizeof a, &a, ini);
    if (r)
      ow_logfont_w(&a, pv);
    return r;
  }
  case SPI_SETICONTITLELOGFONT: {
    LOGFONTA a;
    if (!pv)
      break;
    ow_logfont_a(pv, &a);
    return SystemParametersInfoA(action, sizeof a, &a, ini);
  }
  case SPI_GETNONCLIENTMETRICS: {
    NONCLIENTMETRICSA a;
    NONCLIENTMETRICSW *w = pv;
    if (!w || w->cbSize < sizeof *w) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }
    a.cbSize = sizeof a;
    r = SystemParametersInfoA(action, sizeof a, &a, ini);
    if (r)
      ncm_w(&a, w);
    return r;
  }
  case SPI_SETNONCLIENTMETRICS: {
    NONCLIENTMETRICSA a;
    if (!pv)
      break;
    ncm_a(pv, &a);
    return SystemParametersInfoA(action, sizeof a, &a, ini);
  }
  case SPI_GETICONMETRICS: {
    ICONMETRICSA a;
    ICONMETRICSW *w = pv;
    if (!w)
      break;
    a.cbSize = sizeof a;
    r = SystemParametersInfoA(action, sizeof a, &a, ini);
    if (r) {
      w->iHorzSpacing = a.iHorzSpacing;
      w->iVertSpacing = a.iVertSpacing;
      w->iTitleWrap = a.iTitleWrap;
      ow_logfont_w(&a.lfFont, &w->lfFont);
    }
    return r;
  }
  case SPI_SETICONMETRICS: {
    ICONMETRICSA a;
    const ICONMETRICSW *w = pv;
    if (!w)
      break;
    a.cbSize = sizeof a;
    a.iHorzSpacing = w->iHorzSpacing;
    a.iVertSpacing = w->iVertSpacing;
    a.iTitleWrap = w->iTitleWrap;
    ow_logfont_a(&w->lfFont, &a.lfFont);
    return SystemParametersInfoA(action, sizeof a, &a, ini);
  }
  case SPI_GETHIGHCONTRAST: {
    HIGHCONTRASTA a;
    HIGHCONTRASTW *w = pv;
    if (!w)
      break;
    a.cbSize = sizeof a;
    a.lpszDefaultScheme = NULL;
    r = SystemParametersInfoA(action, sizeof a, &a, ini);
    if (r) {
      w->dwFlags = a.dwFlags;
      w->lpszDefaultScheme =
          a.lpszDefaultScheme ? (LPWSTR)ow_intern_w(a.lpszDefaultScheme)
                              : NULL;
    }
    return r;
  }
  case SPI_SETHIGHCONTRAST: {
    HIGHCONTRASTA a;
    const HIGHCONTRASTW *w = pv;
    if (!w)
      break;
    a.cbSize = sizeof a;
    a.dwFlags = w->dwFlags;
    a.lpszDefaultScheme = ow_a(w->lpszDefaultScheme);
    r = SystemParametersInfoA(action, sizeof a, &a, ini);
    ow_free(a.lpszDefaultScheme);
    return r;
  }
  case SPI_GETSOUNDSENTRY: {
    SOUNDSENTRYA a;
    SOUNDSENTRYW *w = pv;
    if (!w)
      break;
    ow_memset(&a, 0, sizeof a);
    a.cbSize = sizeof a;
    r = SystemParametersInfoA(action, sizeof a, &a, ini);
    if (r) {
      LPWSTR keep = w->lpszWindowsEffectDLL;
      ow_memcpy(w, &a, sizeof a);
      w->cbSize = sizeof *w;
      w->lpszWindowsEffectDLL =
          a.lpszWindowsEffectDLL ? (LPWSTR)ow_intern_w(a.lpszWindowsEffectDLL)
                                 : keep;
    }
    return r;
  }
  case SPI_SETSOUNDSENTRY: {
    SOUNDSENTRYA a;
    const SOUNDSENTRYW *w = pv;
    if (!w)
      break;
    ow_memcpy(&a, w, sizeof a);
    a.cbSize = sizeof a;
    a.lpszWindowsEffectDLL = ow_a(w->lpszWindowsEffectDLL);
    r = SystemParametersInfoA(action, sizeof a, &a, ini);
    ow_free(a.lpszWindowsEffectDLL);
    return r;
  }
  case SPI_GETSERIALKEYS: {
    SERIALKEYSA a;
    SERIALKEYSW *w = pv;
    if (!w)
      break;
    ow_memset(&a, 0, sizeof a);
    a.cbSize = sizeof a;
    r = SystemParametersInfoA(action, sizeof a, &a, ini);
    if (r) {
      w->dwFlags = a.dwFlags;
      w->lpszActivePort =
          a.lpszActivePort ? (LPWSTR)ow_intern_w(a.lpszActivePort) : NULL;
      w->lpszPort = a.lpszPort ? (LPWSTR)ow_intern_w(a.lpszPort) : NULL;
      w->iBaudRate = a.iBaudRate;
      w->iPortState = a.iPortState;
      w->iActive = a.iActive;
    }
    return r;
  }
  case SPI_SETSERIALKEYS: {
    SERIALKEYSA a;
    const SERIALKEYSW *w = pv;
    if (!w)
      break;
    a.cbSize = sizeof a;
    a.dwFlags = w->dwFlags;
    a.lpszActivePort = ow_a(w->lpszActivePort);
    a.lpszPort = ow_a(w->lpszPort);
    a.iBaudRate = w->iBaudRate;
    a.iPortState = w->iPortState;
    a.iActive = w->iActive;
    r = SystemParametersInfoA(action, sizeof a, &a, ini);
    ow_free(a.lpszActivePort);
    ow_free(a.lpszPort);
    return r;
  }
  }
  return SystemParametersInfoA(action, ui, pv, ini);
}

/* ---- help --------------------------------------------------------------- */

BOOL WINAPI WinHelpW(HWND h, LPCWSTR file, UINT cmd, ULONG_PTR data) {
  LPSTR f = ow_acp(file, ow_fcp());
  void *tmp = NULL;
  BOOL r;
  switch (cmd) {
  case HELP_KEY:
  case HELP_PARTIALKEY:
  case HELP_COMMAND:
    tmp = ow_a((LPCWSTR)data);
    data = (ULONG_PTR)tmp;
    break;
  case HELP_MULTIKEY: {
    const MULTIKEYHELPW *w = (const MULTIKEYHELPW *)data;
    int al;
    LPSTR k;
    MULTIKEYHELPA *a;
    if (!w)
      break;
    k = ow_an(w->szKeyphrase, -1, &al);
    a = ow_alloc(sizeof *a + al + 1);
    if (!a || !k) {
      ow_free(a);
      ow_free(k);
      ow_free(f);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }
    a->mkSize = sizeof *a + al;
    a->mkKeylist = (CHAR)key_a(w->mkKeylist);
    ow_memcpy(a->szKeyphrase, k, al + 1);
    ow_free(k);
    tmp = a;
    data = (ULONG_PTR)a;
    break;
  }
  case HELP_SETWINPOS: {
    const HELPWININFOW *w = (const HELPWININFOW *)data;
    HELPWININFOA *a;
    int al;
    LPSTR m;
    if (!w)
      break;
    m = ow_an(w->rgchMember, -1, &al);
    a = ow_alloc(sizeof *a + al + 1);
    if (!a || !m) {
      ow_free(a);
      ow_free(m);
      ow_free(f);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }
    a->wStructSize = sizeof *a + al;
    a->x = w->x;
    a->y = w->y;
    a->dx = w->dx;
    a->dy = w->dy;
    a->wMax = w->wMax;
    ow_memcpy(a->rgchMember, m, al + 1);
    ow_free(m);
    tmp = a;
    data = (ULONG_PTR)a;
    break;
  }
  }
  r = WinHelpA(h, f, cmd, data);
  ow_free(tmp);
  ow_free(f);
  return r;
}
