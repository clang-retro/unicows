/* comdlg32.dll: the common dialogs. Each W function fills the A structure,
 * shows the A dialog and copies the results back. A W hook is called
 * through an A hook of ours, which converts its messages and gives it the
 * caller's W structure wherever the dialog passes the A one.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"
#include <commdlg.h>
#include <cderr.h>
#include <shlobj.h>

UINT ow_find_msg;
LONG ow_comdlg_open;

static const char prop_dlg[] = "ow.dlg"; /* the struct dlg of a hook */
static const char prop_kind[] = "ow.cd"; /* which messages h takes */
#define CD_OFN OW_CD_OFN
#define CD_FONT OW_CD_FONT
#define CD_BFF OW_CD_BFF

static DWORD ext_error(void) {
  OW_FN(OW_COMDLG32, CommDlgExtendedError, return 0);
  return pCommDlgExtendedError();
}

/* ---- hooks ---------------------------------------------------------------- */

struct dlg {
  WNDPROC hook;            /* the caller's W hook */
  void *w;                 /* the caller's W structure */
  void (*sync)(void *ctx); /* copies the A results into it */
  void *ctx;
  int kind;                /* CD_OFN or CD_FONT, or 0 */
};

/* The registered messages whose lParam is the dialog's structure. */
static UINT msg_fileok, msg_sharevi, msg_help, msg_colorok;

static void init_msgs(void) {
  if (msg_fileok)
    return;
  msg_sharevi = RegisterWindowMessageA(SHAREVISTRINGA);
  msg_help = RegisterWindowMessageA(HELPMSGSTRINGA);
  msg_colorok = RegisterWindowMessageA(COLOROKSTRINGA);
  msg_fileok = RegisterWindowMessageA(FILEOKSTRINGA);
}

static void set_kind(HWND h, int kind) {
  if (kind && SetPropA(h, prop_kind, (HANDLE)(INT_PTR)kind))
    InterlockedIncrement(&ow_comdlg_open);
}

static void clear_kind(HWND h) {
  if (RemovePropA(h, prop_kind))
    InterlockedDecrement(&ow_comdlg_open);
}

/* For windows made elsewhere (the folder browser, MCI and capture
 * windows), which go without telling: the count is taken down by
 * ow_cd_unmark, or stays up, which only costs a property lookup. */
void ow_cd_mark(HWND h, int kind) {
  if (SetPropA(h, prop_kind, (HANDLE)(INT_PTR)kind))
    InterlockedIncrement(&ow_comdlg_open);
}

void ow_cd_unmark(HWND h) {
  if (IsWindow(h))
    RemovePropA(h, prop_kind);
  InterlockedDecrement(&ow_comdlg_open);
}

/* The window that takes the dialog's messages: the dialog itself, or the
 * parent of the child dialog that an Explorer-style file dialog hooks. */
static HWND kind_window(HWND h, struct dlg *d) {
  return d->kind == CD_OFN ? GetParent(h) : h;
}

/* d is the hook's structure on WM_INITDIALOG, and NULL otherwise. */
static UINT_PTR dlg_hook(struct dlg *d, HWND h, UINT m, WPARAM wp,
                         LPARAM lp) {
  UINT_PTR r;
  if (m == WM_INITDIALOG) {
    if (!d)
      return 0;
    SetPropA(h, prop_dlg, d);
    set_kind(kind_window(h, d), d->kind);
    if (d->sync)
      d->sync(d->ctx);
    return ow_call_w(d->hook, h, m, wp, (LPARAM)d->w);
  }
  d = GetPropA(h, prop_dlg);
  if (!d)
    return 0;
  if (lp && (m == msg_fileok || m == msg_sharevi || m == msg_help ||
             m == msg_colorok)) {
    if (d->sync)
      d->sync(d->ctx);
    lp = (LPARAM)d->w;
  }
  r = ow_call_w(d->hook, h, m, wp, lp);
  if (m == WM_NCDESTROY) {
    if (d->kind)
      clear_kind(kind_window(h, d));
    RemovePropA(h, prop_dlg);
  }
  return r;
}

static void hook_init(struct dlg *d, void *hook, void *w,
                      void (*sync)(void *), void *ctx, int kind) {
  init_msgs();
  d->hook = (WNDPROC)hook;
  d->w = w;
  d->sync = sync;
  d->ctx = ctx;
  d->kind = kind;
}

/* Copies size bytes into the global block *g, reallocating it in place
 * when there is one (as the dialogs do), or allocating one. */
static void put_global(HGLOBAL *g, const void *data, SIZE_T size) {
  HGLOBAL h = NULL;
  void *p;
  if (*g)
    h = GlobalReAlloc(*g, size, GMEM_MOVEABLE);
  if (!h)
    h = GlobalAlloc(GMEM_MOVEABLE, size);
  if (!h)
    return;
  *g = h;
  p = GlobalLock(h);
  if (p) {
    ow_memcpy(p, data, size);
    GlobalUnlock(h);
  }
}

/* A DEVMODE in a global block in the other encoding. */
static HGLOBAL devmode_ga(HGLOBAL hw) {
  DEVMODEW *w;
  LPDEVMODEA a;
  HGLOBAL g = NULL;
  if (!hw || !(w = GlobalLock(hw)))
    return NULL;
  a = ow_devmode_a(w);
  GlobalUnlock(hw);
  if (a)
    put_global(&g, a, a->dmSize + a->dmDriverExtra);
  ow_free(a);
  return g;
}

static void devmode_gw(HGLOBAL ha, HGLOBAL *hw) {
  DEVMODEA *a;
  LPDEVMODEW w;
  if (!ha || !(a = GlobalLock(ha)))
    return;
  w = ow_devmode_w(a);
  GlobalUnlock(ha);
  if (w)
    put_global(hw, w, w->dmSize + w->dmDriverExtra);
  ow_free(w);
}

/* DEVNAMES: offsets (in characters) of the driver, device and output
 * names that follow the header. */
static HGLOBAL devnames_ga(HGLOBAL hw) {
  DEVNAMES *w;
  LPSTR s[3];
  HGLOBAL g = NULL;
  BYTE *buf;
  int i, size = sizeof(DEVNAMES), off;
  if (!hw || !(w = GlobalLock(hw)))
    return NULL;
  s[0] = ow_a((LPCWSTR)w + w->wDriverOffset);
  s[1] = ow_a((LPCWSTR)w + w->wDeviceOffset);
  s[2] = ow_a((LPCWSTR)w + w->wOutputOffset);
  for (i = 0; i < 3; i++)
    size += (s[i] ? ow_strlen(s[i]) : 0) + 1;
  buf = ow_alloc(size);
  if (buf) {
    DEVNAMES *a = (DEVNAMES *)buf;
    WORD *offs = &a->wDriverOffset;
    off = sizeof(DEVNAMES);
    for (i = 0; i < 3; i++) {
      int n = s[i] ? ow_strlen(s[i]) : 0;
      offs[i] = (WORD)off;
      ow_memcpy(buf + off, s[i] ? s[i] : "", n);
      off += n + 1;
    }
    a->wDefault = w->wDefault;
    put_global(&g, buf, size);
  }
  GlobalUnlock(hw);
  for (i = 0; i < 3; i++)
    ow_free(s[i]);
  ow_free(buf);
  return g;
}

static void devnames_gw(HGLOBAL ha, HGLOBAL *hw) {
  DEVNAMES *a;
  LPWSTR s[3];
  WCHAR *buf;
  int i, size = sizeof(DEVNAMES) / sizeof(WCHAR), off;
  if (!ha || !(a = GlobalLock(ha)))
    return;
  s[0] = ow_w((LPCSTR)a + a->wDriverOffset);
  s[1] = ow_w((LPCSTR)a + a->wDeviceOffset);
  s[2] = ow_w((LPCSTR)a + a->wOutputOffset);
  for (i = 0; i < 3; i++)
    size += (s[i] ? ow_wcslen(s[i]) : 0) + 1;
  buf = ow_alloc(size * sizeof(WCHAR));
  if (buf) {
    DEVNAMES *w = (DEVNAMES *)buf;
    WORD *offs = &w->wDriverOffset;
    off = sizeof(DEVNAMES) / sizeof(WCHAR);
    for (i = 0; i < 3; i++) {
      int n = s[i] ? ow_wcslen(s[i]) : 0;
      offs[i] = (WORD)off;
      if (n)
        ow_memcpy(buf + off, s[i], n * sizeof(WCHAR));
      off += n + 1;
    }
    w->wDefault = a->wDefault;
    put_global(hw, buf, size * sizeof(WCHAR));
  }
  GlobalUnlock(ha);
  for (i = 0; i < 3; i++)
    ow_free(s[i]);
  ow_free(buf);
}

/* ---- GetOpenFileName, GetSaveFileName ---------------------------------- */

struct ofn {
  OPENFILENAMEA a;
  struct dlg d;
  OPENFILENAMEW *w;
  LPSTR filter, custom, file, ftitle, dir, title, defext, tmpl;
};

/* A custom filter: a description and a pattern, either may be empty. */
static void pair_a(LPCWSTR w, LPSTR a, int cb) {
  int n;
  if (!w || cb < 2)
    return;
  n = ow_toa(w, -1, a, cb - 1);
  ow_toa(w + ow_wcslen(w) + 1, -1, a + n + 1, cb - n - 1);
}

static void pair_w(LPCSTR a, LPWSTR w, int cch) {
  int n;
  if (!a || cch < 2)
    return;
  n = ow_tow(a, -1, w, cch - 1);
  ow_tow(a + ow_strlen(a) + 1, -1, w + n + 1, cch - n - 1);
}

/* Copies the file name(s) into the W buffer; FALSE when they don't fit, in
 * which case its first WORD gets the size needed. */
static BOOL ofn_file_w(struct ofn *c) {
  OPENFILENAMEW *w = c->w;
  LPCSTR a = c->file;
  int alen, wn;
  if (!a || !w->lpstrFile || !w->nMaxFile)
    return TRUE;
  /* Several files: the folder, then the names, each with a NUL, then an
   * empty one. */
  if (c->a.nFileOffset && !a[c->a.nFileOffset - 1])
    alen = ow_alist_len(a);
  else
    alen = ow_strlen(a) + 1;
  wn = ow_mb2wc(CP_ACP, 0, a, alen, NULL, 0);
  if (wn > (int)w->nMaxFile) {
    if (w->nMaxFile >= 2)
      *(WORD *)w->lpstrFile = (WORD)wn;
    return FALSE;
  }
  ow_mb2wc(CP_ACP, 0, a, alen, w->lpstrFile, w->nMaxFile);
  w->nFileOffset = (WORD)ow_wlen_of(a, c->a.nFileOffset, CP_ACP);
  w->nFileExtension =
      c->a.nFileExtension
          ? (WORD)ow_wlen_of(a, c->a.nFileExtension, CP_ACP)
          : 0;
  return TRUE;
}

static void ofn_sync(void *ctx) {
  struct ofn *c = ctx;
  OPENFILENAMEW *w = c->w;
  ofn_file_w(c);
  if (c->ftitle && w->lpstrFileTitle)
    ow_tow(c->ftitle, -1, w->lpstrFileTitle, w->nMaxFileTitle);
  if (c->custom && w->lpstrCustomFilter)
    pair_w(c->custom, w->lpstrCustomFilter, w->nMaxCustFilter);
  w->nFilterIndex = c->a.nFilterIndex;
  w->Flags = c->a.Flags;
}

static UINT_PTR CALLBACK ofn_hook(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  if (m == WM_INITDIALOG)
    return lp ? dlg_hook((struct dlg *)((OPENFILENAMEA *)lp)->lCustData, h,
                         m, wp, lp)
              : 0;
  if (m == WM_NOTIFY && lp) {
    NMHDR *n = (NMHDR *)lp;
    struct dlg *d = GetPropA(h, prop_dlg);
    if (d && n->code <= CDN_FIRST && n->code >= CDN_LAST) {
      struct ofn *c = d->ctx;
      UINT_PTR r;
      LPWSTR file = NULL;
      union {
        OFNOTIFYW n;
        OFNOTIFYEXW x;
      } w;
      ofn_sync(c);
      if (n->code == CDN_INCLUDEITEM) {
        ow_memcpy(&w.x, (void *)lp, sizeof w.x);
        w.x.lpOFN = c->w;
      } else {
        ow_memcpy(&w.n, (void *)lp, sizeof w.n);
        w.n.lpOFN = c->w;
        w.n.pszFile = NULL;
        if (n->code == CDN_SHAREVIOLATION)
          w.n.pszFile = file = ow_w(((OFNOTIFYA *)lp)->pszFile);
      }
      r = ow_call_w(d->hook, h, m, wp, (LPARAM)&w);
      ow_free(file);
      return r;
    }
  }
  return dlg_hook(NULL, h, m, wp, lp);
}

BOOL ow_ofn_call(OPENFILENAMEW *w, ow_ofn_fn fn) {
  struct ofn *c;
  OPENFILENAMEA *a;
  BOOL ok;
  int cb;
  if (!w || (w->lStructSize != sizeof(OPENFILENAMEW) &&
             w->lStructSize != OPENFILENAME_SIZE_VERSION_400W))
    return fn((OPENFILENAMEA *)w); /* its error */
  c = ow_alloc(sizeof *c);
  if (!c)
    return FALSE;
  a = &c->a;
  c->w = w;
  a->lStructSize = OPENFILENAME_SIZE_VERSION_400A;
  if (w->lStructSize == sizeof(OPENFILENAMEW) && w->FlagsEx) {
    a->lStructSize = sizeof(OPENFILENAMEA);
    a->FlagsEx = w->FlagsEx;
  }
  a->hwndOwner = w->hwndOwner;
  a->hInstance = w->hInstance;
  a->lpstrFilter = c->filter = ow_alist(w->lpstrFilter);
  if (w->lpstrCustomFilter && w->nMaxCustFilter) {
    cb = w->nMaxCustFilter * 2;
    a->lpstrCustomFilter = c->custom = ow_alloc(cb);
    a->nMaxCustFilter = cb;
    pair_a(w->lpstrCustomFilter, c->custom, cb);
  }
  a->nFilterIndex = w->nFilterIndex;
  if (w->lpstrFile && w->nMaxFile) {
    cb = w->nMaxFile * 2;
    if (cb > 0xffff * 2)
      cb = 0xffff * 2;
    a->lpstrFile = c->file = ow_alloc(cb);
    a->nMaxFile = cb;
    if (c->file)
      ow_toa(w->lpstrFile, -1, c->file, cb);
  }
  if (w->lpstrFileTitle && w->nMaxFileTitle) {
    cb = w->nMaxFileTitle * 2;
    a->lpstrFileTitle = c->ftitle = ow_alloc(cb);
    a->nMaxFileTitle = cb;
  }
  a->lpstrInitialDir = c->dir = ow_a(w->lpstrInitialDir);
  a->lpstrTitle = c->title = ow_a(w->lpstrTitle);
  a->Flags = w->Flags;
  a->lpstrDefExt = c->defext = ow_a(w->lpstrDefExt);
  a->lCustData = w->lCustData;
  if ((w->Flags & OFN_ENABLEHOOK) && w->lpfnHook) {
    hook_init(&c->d, w->lpfnHook, w, ofn_sync, c,
              (w->Flags & OFN_EXPLORER) ? CD_OFN : 0);
    a->lpfnHook = ofn_hook;
    a->lCustData = (LPARAM)&c->d;
  }
  if (w->Flags & OFN_ENABLETEMPLATE)
    a->lpTemplateName = c->tmpl = ow_ares(w->lpTemplateName);
  if ((w->lpstrFile && !c->file) || (w->lpstrFileTitle && !c->ftitle)) {
    ok = FALSE;
    goto out;
  }
  ok = fn(a);
  if (!ok && a->lStructSize == sizeof(OPENFILENAMEA) &&
      ext_error() == CDERR_STRUCTSIZE) {
    a->lStructSize = OPENFILENAME_SIZE_VERSION_400A; /* 95, 98 */
    ok = fn(a);
  }
  if (ok) {
    ofn_sync(c);
    ok = ofn_file_w(c);
  } else if (ext_error() == FNERR_BUFFERTOOSMALL && c->file &&
             w->nMaxFile >= 2) {
    *(WORD *)w->lpstrFile = *(WORD *)c->file;
  }
out:
  ow_free(c->filter);
  ow_free(c->custom);
  ow_free(c->file);
  ow_free(c->ftitle);
  ow_free(c->dir);
  ow_free(c->title);
  ow_free(c->defext);
  ow_free(c->tmpl);
  ow_free(c);
  return ok;
}

BOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW w) {
  OW_FN(OW_COMDLG32, GetOpenFileNameA, return FALSE);
  return ow_ofn_call(w, pGetOpenFileNameA);
}

BOOL WINAPI GetSaveFileNameW(LPOPENFILENAMEW w) {
  OW_FN(OW_COMDLG32, GetSaveFileNameA, return FALSE);
  return ow_ofn_call(w, pGetSaveFileNameA);
}

short WINAPI GetFileTitleW(LPCWSTR file, LPWSTR buf, WORD cch) {
  LPSTR a = ow_a(file);
  char t[MAX_PATH * 2];
  short r;
  OW_FN(OW_COMDLG32, GetFileTitleA, return -1);
  if (!a && file)
    return -1;
  r = pGetFileTitleA(a, t, sizeof t);
  ow_free(a);
  if (r)
    return r; /* an error, or the size it needs */
  if (!buf || !cch)
    return (short)(ow_wlen_of(t, -1, CP_ACP) + 1);
  if (ow_wlen_of(t, -1, CP_ACP) >= cch)
    return (short)(ow_wlen_of(t, -1, CP_ACP) + 1);
  ow_tow(t, -1, buf, cch);
  return 0;
}

/* The messages that an application sends to the dialogs. */
BOOL ow_comdlg_msg(ow_sender send, void *ctx, HWND h, UINT m, WPARAM wp,
                   LPARAM lp, LRESULT *r) {
  int kind = (int)(INT_PTR)GetPropA(h, prop_kind);
  if (kind >= OW_CD_MCIWND)
    return ow_vfw_msg(kind, send, ctx, h, m, wp, lp, r);
  if (kind == CD_OFN) {
    switch (m) {
    case CDM_GETSPEC:
    case CDM_GETFILEPATH:
    case CDM_GETFOLDERPATH: {
      LRESULT need = send(ctx, h, m, 0, 0);
      LPSTR a;
      if (need <= 0) {
        *r = need;
        return TRUE;
      }
      a = ow_alloc(need + 1);
      if (!a) {
        *r = -1;
        return TRUE;
      }
      if (send(ctx, h, m, need + 1, (LPARAM)a) < 0) {
        *r = -1;
      } else {
        int wl = ow_wlen_of(a, -1, CP_ACP) + 1;
        if (lp && wp)
          ow_tow(a, -1, (LPWSTR)lp, (int)wp);
        *r = wl;
      }
      ow_free(a);
      return TRUE;
    }
    case CDM_SETCONTROLTEXT:
    case CDM_SETDEFEXT: {
      LPSTR a = ow_a((LPCWSTR)lp);
      *r = send(ctx, h, m, wp, (LPARAM)a);
      ow_free(a);
      return TRUE;
    }
    }
  } else if (kind == CD_BFF) {
    /* The shell of 9x knows only the A forms of these. */
    if (m == BFFM_SETSELECTIONW || m == BFFM_SETSTATUSTEXTW) {
      LPSTR a = NULL;
      if (m == BFFM_SETSTATUSTEXTW || wp)
        lp = (LPARAM)(a = ow_a((LPCWSTR)lp));
      *r = send(ctx, h,
                m == BFFM_SETSELECTIONW ? BFFM_SETSELECTIONA
                                        : BFFM_SETSTATUSTEXTA,
                wp, lp);
      ow_free(a);
      return TRUE;
    }
  } else if (kind == CD_FONT) {
    LOGFONTA a;
    switch (m) {
    case WM_CHOOSEFONT_GETLOGFONT:
      *r = send(ctx, h, m, wp, (LPARAM)&a);
      if (lp)
        ow_logfont_w(&a, (LOGFONTW *)lp);
      return TRUE;
    case WM_CHOOSEFONT_SETLOGFONT:
      if (!lp)
        return FALSE;
      ow_logfont_a((const LOGFONTW *)lp, &a);
      *r = send(ctx, h, m, wp, (LPARAM)&a);
      return TRUE;
    }
  }
  return FALSE;
}

/* ---- ChooseColor -------------------------------------------------------- */

struct cc {
  CHOOSECOLORA a;
  struct dlg d;
  CHOOSECOLORW *w;
};

static void cc_sync(void *ctx) {
  struct cc *c = ctx;
  c->w->rgbResult = c->a.rgbResult;
  c->w->Flags = c->a.Flags;
}

static UINT_PTR CALLBACK cc_hook(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  return dlg_hook(m == WM_INITDIALOG && lp
                      ? (struct dlg *)((CHOOSECOLORA *)lp)->lCustData
                      : NULL,
                  h, m, wp, lp);
}

BOOL WINAPI ChooseColorW(LPCHOOSECOLORW w) {
  struct cc c;
  BOOL ok;
  OW_FN(OW_COMDLG32, ChooseColorA, return FALSE);
  if (!w || w->lStructSize != sizeof *w)
    return pChooseColorA((LPCHOOSECOLORA)w);
  ow_memset(&c, 0, sizeof c);
  c.w = w;
  c.a.lStructSize = sizeof c.a;
  c.a.hwndOwner = w->hwndOwner;
  c.a.hInstance = w->hInstance;
  c.a.rgbResult = w->rgbResult;
  c.a.lpCustColors = w->lpCustColors;
  c.a.Flags = w->Flags;
  c.a.lCustData = w->lCustData;
  if ((w->Flags & CC_ENABLEHOOK) && w->lpfnHook) {
    hook_init(&c.d, w->lpfnHook, w, cc_sync, &c, 0);
    c.a.lpfnHook = cc_hook;
    c.a.lCustData = (LPARAM)&c.d;
  }
  if (w->Flags & CC_ENABLETEMPLATE)
    c.a.lpTemplateName = ow_ares(w->lpTemplateName);
  ok = pChooseColorA(&c.a);
  if (ok)
    cc_sync(&c);
  ow_free(c.a.lpTemplateName);
  return ok;
}

/* ---- ChooseFont ----------------------------------------------------------- */

struct cf {
  CHOOSEFONTA a;
  LOGFONTA lf;
  struct dlg d;
  CHOOSEFONTW *w;
  char style[LF_FACESIZE * 2];
};

static void cf_sync(void *ctx) {
  struct cf *c = ctx;
  CHOOSEFONTW *w = c->w;
  if (w->lpLogFont)
    ow_logfont_w(&c->lf, w->lpLogFont);
  w->iPointSize = c->a.iPointSize;
  w->Flags = c->a.Flags;
  w->rgbColors = c->a.rgbColors;
  w->nFontType = c->a.nFontType;
  if (c->a.lpszStyle && w->lpszStyle)
    ow_tow(c->style, -1, w->lpszStyle, LF_FACESIZE);
}

static UINT_PTR CALLBACK cf_hook(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  return dlg_hook(m == WM_INITDIALOG && lp
                      ? (struct dlg *)((CHOOSEFONTA *)lp)->lCustData
                      : NULL,
                  h, m, wp, lp);
}

BOOL WINAPI ChooseFontW(LPCHOOSEFONTW w) {
  struct cf *c;
  BOOL ok;
  OW_FN(OW_COMDLG32, ChooseFontA, return FALSE);
  if (!w || w->lStructSize != sizeof *w)
    return pChooseFontA((LPCHOOSEFONTA)w);
  c = ow_alloc(sizeof *c);
  if (!c)
    return FALSE;
  c->w = w;
  c->a.lStructSize = sizeof c->a;
  c->a.hwndOwner = w->hwndOwner;
  c->a.hDC = w->hDC;
  if (w->lpLogFont)
    ow_logfont_a(w->lpLogFont, &c->lf);
  c->a.lpLogFont = &c->lf;
  c->a.iPointSize = w->iPointSize;
  c->a.Flags = w->Flags;
  c->a.rgbColors = w->rgbColors;
  c->a.lCustData = w->lCustData;
  c->a.hInstance = w->hInstance;
  if ((w->Flags & CF_USESTYLE) && w->lpszStyle) {
    ow_toa(w->lpszStyle, -1, c->style, sizeof c->style);
    c->a.lpszStyle = c->style;
  }
  c->a.nFontType = w->nFontType;
  c->a.nSizeMin = w->nSizeMin;
  c->a.nSizeMax = w->nSizeMax;
  if ((w->Flags & CF_ENABLEHOOK) && w->lpfnHook) {
    hook_init(&c->d, w->lpfnHook, w, cf_sync, c, CD_FONT);
    c->a.lpfnHook = cf_hook;
    c->a.lCustData = (LPARAM)&c->d;
  }
  if (w->Flags & CF_ENABLETEMPLATE)
    c->a.lpTemplateName = ow_ares(w->lpTemplateName);
  ok = pChooseFontA(&c->a);
  if (ok)
    cf_sync(c);
  ow_free(c->a.lpTemplateName);
  ow_free(c);
  return ok;
}

/* ---- FindText, ReplaceText -------------------------------------------------
 * These dialogs are modeless: their structures stay until the dialog has
 * gone, and the owner gets FINDMSGSTRING messages with the A structure,
 * which ow_call_w changes to the W one. */

struct fr {
  struct fr *next;
  FINDREPLACEA a;
  struct dlg d;
  FINDREPLACEW *w;
  BOOL done; /* the dialog has said FR_DIALOGTERM */
};

static struct fr *frs;
static CRITICAL_SECTION fr_lock;

void ow_comdlg_init(void) { InitializeCriticalSection(&fr_lock); }

static void fr_enter(void) { EnterCriticalSection(&fr_lock); }

static void fr_free(struct fr *c) {
  ow_free(c->a.lpstrFindWhat);
  ow_free(c->a.lpstrReplaceWith);
  ow_free(c->a.lpTemplateName);
  ow_free(c);
}

static void fr_sync(void *ctx) {
  struct fr *c = ctx;
  FINDREPLACEW *w = c->w;
  w->Flags = c->a.Flags;
  if (w->lpstrFindWhat && c->a.lpstrFindWhat)
    ow_tow(c->a.lpstrFindWhat, -1, w->lpstrFindWhat, w->wFindWhatLen);
  if (w->lpstrReplaceWith && c->a.lpstrReplaceWith)
    ow_tow(c->a.lpstrReplaceWith, -1, w->lpstrReplaceWith,
           w->wReplaceWithLen);
}

LPARAM ow_find_w(LPARAM lp) {
  struct fr *c;
  fr_enter();
  for (c = frs; c; c = c->next)
    if ((LPARAM)&c->a == lp && !c->done) {
      fr_sync(c);
      if (c->a.Flags & FR_DIALOGTERM)
        c->done = TRUE;
      lp = (LPARAM)c->w;
      break;
    }
  LeaveCriticalSection(&fr_lock);
  return lp;
}

static UINT_PTR CALLBACK fr_hook(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  return dlg_hook(m == WM_INITDIALOG && lp
                      ? (struct dlg *)((FINDREPLACEA *)lp)->lCustData
                      : NULL,
                  h, m, wp, lp);
}

static LPSTR fr_buf(LPCWSTR w, WORD cch, WORD *cb) {
  DWORD n = (DWORD)cch * 2;
  LPSTR a;
  if (!w || !cch)
    return NULL;
  if (n > 0xffff)
    n = 0xffff;
  a = ow_alloc(n);
  if (a)
    ow_toa(w, -1, a, n);
  *cb = (WORD)n;
  return a;
}

static HWND fr_run(FINDREPLACEW *w, BOOL replace) {
  struct fr *c, **pp;
  HWND r;
  OW_FN(OW_COMDLG32, FindTextA, return NULL);
  OW_FN(OW_COMDLG32, ReplaceTextA, return NULL);
  if (!w || w->lStructSize != sizeof *w)
    return replace ? pReplaceTextA((LPFINDREPLACEA)w)
                   : pFindTextA((LPFINDREPLACEA)w);
  if (!ow_find_msg)
    ow_find_msg = RegisterWindowMessageA(FINDMSGSTRINGA);
  /* Free the structures of the dialogs that have gone. */
  fr_enter();
  for (pp = &frs; *pp;) {
    c = *pp;
    if (c->done) {
      *pp = c->next;
      fr_free(c);
    } else {
      pp = &c->next;
    }
  }
  LeaveCriticalSection(&fr_lock);
  c = ow_alloc(sizeof *c);
  if (!c)
    return NULL;
  c->w = w;
  c->a.lStructSize = sizeof c->a;
  c->a.hwndOwner = w->hwndOwner;
  c->a.hInstance = w->hInstance;
  c->a.Flags = w->Flags;
  c->a.lpstrFindWhat = fr_buf(w->lpstrFindWhat, w->wFindWhatLen,
                              &c->a.wFindWhatLen);
  if (replace)
    c->a.lpstrReplaceWith = fr_buf(w->lpstrReplaceWith, w->wReplaceWithLen,
                                   &c->a.wReplaceWithLen);
  c->a.lCustData = w->lCustData;
  if ((w->Flags & FR_ENABLEHOOK) && w->lpfnHook) {
    hook_init(&c->d, w->lpfnHook, w, fr_sync, c, 0);
    c->a.lpfnHook = fr_hook;
    c->a.lCustData = (LPARAM)&c->d;
  }
  if (w->Flags & FR_ENABLETEMPLATE)
    c->a.lpTemplateName = ow_ares(w->lpTemplateName);
  r = replace ? pReplaceTextA(&c->a) : pFindTextA(&c->a);
  if (!r) {
    fr_free(c);
    return NULL;
  }
  fr_enter();
  c->next = frs;
  frs = c;
  LeaveCriticalSection(&fr_lock);
  return r;
}

HWND WINAPI FindTextW(LPFINDREPLACEW w) { return fr_run(w, FALSE); }
HWND WINAPI ReplaceTextW(LPFINDREPLACEW w) { return fr_run(w, TRUE); }

/* ---- PrintDlg ------------------------------------------------------------- */

struct pd {
  PRINTDLGA a;
  struct dlg dp, ds; /* the print and setup hooks */
  PRINTDLGW *w;
};

static void pd_sync(void *ctx) {
  struct pd *c = ctx;
  PRINTDLGW *w = c->w;
  w->Flags = c->a.Flags;
  w->nFromPage = c->a.nFromPage;
  w->nToPage = c->a.nToPage;
  w->nCopies = c->a.nCopies;
}

static UINT_PTR CALLBACK pd_print_hook(HWND h, UINT m, WPARAM wp,
                                       LPARAM lp) {
  return dlg_hook(m == WM_INITDIALOG && lp
                      ? &((struct pd *)((PRINTDLGA *)lp)->lCustData)->dp
                      : NULL,
                  h, m, wp, lp);
}

static UINT_PTR CALLBACK pd_setup_hook(HWND h, UINT m, WPARAM wp,
                                       LPARAM lp) {
  return dlg_hook(m == WM_INITDIALOG && lp
                      ? &((struct pd *)((PRINTDLGA *)lp)->lCustData)->ds
                      : NULL,
                  h, m, wp, lp);
}

/* The device mode and names that a dialog chose, into the W structure's
 * global blocks; the A ones are freed. */
static void devices_w(HGLOBAL adm, HGLOBAL adn, HGLOBAL *wdm,
                      HGLOBAL *wdn) {
  if (wdm)
    devmode_gw(adm, wdm);
  if (wdn)
    devnames_gw(adn, wdn);
  if (adm)
    GlobalFree(adm);
  if (adn)
    GlobalFree(adn);
}

BOOL WINAPI PrintDlgW(LPPRINTDLGW w) {
  struct pd c;
  BOOL ok;
  OW_FN(OW_COMDLG32, PrintDlgA, return FALSE);
  if (!w || w->lStructSize != sizeof *w)
    return pPrintDlgA((LPPRINTDLGA)w);
  ow_memset(&c, 0, sizeof c);
  c.w = w;
  c.a.lStructSize = sizeof c.a;
  c.a.hwndOwner = w->hwndOwner;
  c.a.hDevMode = devmode_ga(w->hDevMode);
  c.a.hDevNames = devnames_ga(w->hDevNames);
  c.a.Flags = w->Flags;
  c.a.nFromPage = w->nFromPage;
  c.a.nToPage = w->nToPage;
  c.a.nMinPage = w->nMinPage;
  c.a.nMaxPage = w->nMaxPage;
  c.a.nCopies = w->nCopies;
  c.a.hInstance = w->hInstance;
  c.a.lCustData = w->lCustData;
  c.a.hPrintTemplate = w->hPrintTemplate;
  c.a.hSetupTemplate = w->hSetupTemplate;
  if ((w->Flags & PD_ENABLEPRINTHOOK) && w->lpfnPrintHook) {
    hook_init(&c.dp, w->lpfnPrintHook, w, pd_sync, &c, 0);
    c.a.lpfnPrintHook = pd_print_hook;
    c.a.lCustData = (LPARAM)&c;
  }
  if ((w->Flags & PD_ENABLESETUPHOOK) && w->lpfnSetupHook) {
    hook_init(&c.ds, w->lpfnSetupHook, w, pd_sync, &c, 0);
    c.a.lpfnSetupHook = pd_setup_hook;
    c.a.lCustData = (LPARAM)&c;
  }
  if (w->Flags & PD_ENABLEPRINTTEMPLATE)
    c.a.lpPrintTemplateName = ow_ares(w->lpPrintTemplateName);
  if (w->Flags & PD_ENABLESETUPTEMPLATE)
    c.a.lpSetupTemplateName = ow_ares(w->lpSetupTemplateName);
  ok = pPrintDlgA(&c.a);
  if (ok) {
    pd_sync(&c);
    w->hDC = c.a.hDC;
    devices_w(c.a.hDevMode, c.a.hDevNames, &w->hDevMode, &w->hDevNames);
  } else {
    devices_w(c.a.hDevMode, c.a.hDevNames, NULL, NULL);
  }
  ow_free(c.a.lpPrintTemplateName);
  ow_free(c.a.lpSetupTemplateName);
  return ok;
}

/* ---- PageSetupDlg --------------------------------------------------------- */

struct psd {
  PAGESETUPDLGA a;
  struct dlg d;
  PAGESETUPDLGW *w;
  WNDPROC paint;
};

static void psd_sync(void *ctx) {
  struct psd *c = ctx;
  PAGESETUPDLGW *w = c->w;
  w->Flags = c->a.Flags;
  w->ptPaperSize = c->a.ptPaperSize;
  w->rtMinMargin = c->a.rtMinMargin;
  w->rtMargin = c->a.rtMargin;
}

static UINT_PTR CALLBACK psd_hook(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  return dlg_hook(m == WM_INITDIALOG && lp
                      ? (struct dlg *)((PAGESETUPDLGA *)lp)->lCustData
                      : NULL,
                  h, m, wp, lp);
}

/* The paint hook's messages carry no text; the first one has the
 * structure. The dialog is modal, so it is the thread's. */
static UINT_PTR CALLBACK psd_paint(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  struct psd *c = ow_thread()->dlg;
  if (!c)
    return 0;
  if (m == WM_PSD_PAGESETUPDLG) {
    psd_sync(c);
    lp = (LPARAM)c->w;
  }
  return c->paint(h, m, wp, lp);
}

BOOL WINAPI PageSetupDlgW(LPPAGESETUPDLGW w) {
  struct psd c;
  struct ow_thread *t = ow_thread();
  void *prev = t->dlg;
  BOOL ok;
  OW_FN(OW_COMDLG32, PageSetupDlgA, return FALSE);
  if (!w || w->lStructSize != sizeof *w)
    return pPageSetupDlgA((LPPAGESETUPDLGA)w);
  ow_memset(&c, 0, sizeof c);
  c.w = w;
  c.a.lStructSize = sizeof c.a;
  c.a.hwndOwner = w->hwndOwner;
  c.a.hDevMode = devmode_ga(w->hDevMode);
  c.a.hDevNames = devnames_ga(w->hDevNames);
  c.a.Flags = w->Flags;
  c.a.ptPaperSize = w->ptPaperSize;
  c.a.rtMinMargin = w->rtMinMargin;
  c.a.rtMargin = w->rtMargin;
  c.a.hInstance = w->hInstance;
  c.a.lCustData = w->lCustData;
  c.a.hPageSetupTemplate = w->hPageSetupTemplate;
  if ((w->Flags & PSD_ENABLEPAGESETUPHOOK) && w->lpfnPageSetupHook) {
    hook_init(&c.d, w->lpfnPageSetupHook, w, psd_sync, &c, 0);
    c.a.lpfnPageSetupHook = psd_hook;
    c.a.lCustData = (LPARAM)&c.d;
  }
  if ((w->Flags & PSD_ENABLEPAGEPAINTHOOK) && w->lpfnPagePaintHook) {
    c.paint = (WNDPROC)w->lpfnPagePaintHook;
    c.a.lpfnPagePaintHook = psd_paint;
  }
  if (w->Flags & PSD_ENABLEPAGESETUPTEMPLATE)
    c.a.lpPageSetupTemplateName = ow_ares(w->lpPageSetupTemplateName);
  t->dlg = &c;
  ok = pPageSetupDlgA(&c.a);
  t->dlg = prev;
  if (ok) {
    psd_sync(&c);
    devices_w(c.a.hDevMode, c.a.hDevNames, &w->hDevMode, &w->hDevNames);
  } else {
    devices_w(c.a.hDevMode, c.a.hDevNames, NULL, NULL);
  }
  ow_free(c.a.lpPageSetupTemplateName);
  return ok;
}
