/* Unicode windows on Windows 9x.
 *
 * Windows 9x keeps only A window procedures. A W procedure (from
 * RegisterClassW, SetWindowLongW, CreateDialogParamW and the like) is
 * given to the system as a thunk: a small A procedure that converts the
 * message to its W form, calls the W procedure and converts the results
 * back. A window whose procedure is a thunk is a Unicode window
 * (IsWindowUnicode). W messages sent to A procedures (SendMessageW,
 * DefWindowProcW, CallWindowProcW) are converted the other way.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"

OW_REAL_DECL(LRESULT, CallWindowProcA,
             (WNDPROC, HWND, UINT, WPARAM, LPARAM), 20);
OW_REAL_DECL(LONG, GetWindowLongA, (HWND, int), 8);
OW_REAL_DECL(LONG, SetWindowLongA, (HWND, int, LONG), 12);

/* ---- thunks ------------------------------------------------------------ */

#define THUNK_SIZE 16
#define PAGE 4096
#define PER_PAGE (PAGE / THUNK_SIZE)

static CRITICAL_SECTION lock;
static BYTE **pages;
static int npages, used; /* thunks in use, all pages but the last full */

static LRESULT WINAPI a2w_entry(WNDPROC wproc, HWND h, UINT m, WPARAM wp,
                                LPARAM lp) {
  return ow_call_w(wproc, h, m, wp, lp);
}

void ow_window_init(void) { InitializeCriticalSection(&lock); }

static BYTE *thunk_at(int i) {
  return pages[i / PER_PAGE] + (i % PER_PAGE) * THUNK_SIZE;
}

static WNDPROC thunk_target(const BYTE *t) {
  WNDPROC p;
  ow_memcpy(&p, t + 2, sizeof p);
  return p;
}

BOOL ow_is_thunk(WNDPROC p) {
  const BYTE *b = (const BYTE *)p;
  BOOL r = FALSE;
  if (!p)
    return FALSE;
  EnterCriticalSection(&lock);
  for (int i = 0; i < npages; i++) {
    if (b >= pages[i] && b < pages[i] + PAGE) {
      int idx = i * PER_PAGE + (int)(b - pages[i]) / THUNK_SIZE;
      r = (b - pages[i]) % THUNK_SIZE == 0 && idx < used;
      break;
    }
  }
  LeaveCriticalSection(&lock);
  return r;
}

/* The thunk that calls wproc, or NULL. */
static WNDPROC find_thunk(WNDPROC wproc) {
  for (int i = 0; i < used; i++)
    if (thunk_target(thunk_at(i)) == wproc)
      return (WNDPROC)thunk_at(i);
  return NULL;
}

WNDPROC ow_wndproc_a(WNDPROC wproc) {
  WNDPROC r;
  BYTE *t;
  DWORD rel;
  if (!wproc || ow_is_thunk(wproc))
    return wproc;
  EnterCriticalSection(&lock);
  r = find_thunk(wproc);
  if (r) {
    LeaveCriticalSection(&lock);
    return r;
  }
  if (used == npages * PER_PAGE) {
    BYTE **np = ow_realloc(pages, (npages + 1) * sizeof *np);
    BYTE *page = np ? VirtualAlloc(NULL, PAGE, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE)
                    : NULL;
    if (np)
      pages = np;
    if (!page) {
      LeaveCriticalSection(&lock);
      return NULL;
    }
    pages[npages++] = page;
  }
  t = thunk_at(used);
  t[0] = 0x58;   /* pop eax: the return address */
  t[1] = 0x68;   /* push wproc */
  ow_memcpy(t + 2, &wproc, 4);
  t[6] = 0x50;   /* push eax */
  t[7] = 0xe9;   /* jmp a2w_entry */
  rel = (DWORD)((BYTE *)a2w_entry - (t + 12));
  ow_memcpy(t + 8, &rel, 4);
  t[12] = t[13] = t[14] = t[15] = 0xcc;
  FlushInstructionCache(GetCurrentProcess(), t, THUNK_SIZE);
  used++;
  LeaveCriticalSection(&lock);
  return (WNDPROC)t;
}

WNDPROC ow_wndproc_w(WNDPROC p) {
  if (ow_is_thunk(p))
    return thunk_target((const BYTE *)p);
  return p;
}

/* Whether p is a W procedure that a thunk calls. */
static BOOL is_wproc(WNDPROC p) {
  BOOL r;
  if (!p)
    return FALSE;
  EnterCriticalSection(&lock);
  r = find_thunk(p) != NULL;
  LeaveCriticalSection(&lock);
  return r;
}

/* ---- interned strings ------------------------------------------------ */

struct interned {
  struct interned *next;
  LPSTR a;
  LPWSTR w;
};
static struct interned *interned;

static struct interned *intern(LPCSTR a) {
  struct interned *i;
  EnterCriticalSection(&lock);
  for (i = interned; i; i = i->next)
    if (!lstrcmpA(i->a, a))
      break;
  if (!i && (i = ow_alloc(sizeof *i)) != NULL) {
    i->a = ow_alloc(ow_strlen(a) + 1);
    i->w = ow_w(a);
    if (i->a)
      lstrcpyA(i->a, a);
    if (!i->a || !i->w) {
      ow_free(i->a);
      ow_free(i->w);
      ow_free(i);
      i = NULL;
    } else {
      i->next = interned;
      interned = i;
    }
  }
  LeaveCriticalSection(&lock);
  return i;
}

LPCWSTR ow_intern_w(LPCSTR a) {
  struct interned *i;
  if (IS_INTRESOURCE(a))
    return (LPCWSTR)a;
  i = intern(a);
  return i ? i->w : NULL;
}

/* ---- what kind of window ------------------------------------------- */

enum { K_OTHER, K_EDIT, K_LISTBOX, K_COMBOBOX, K_NATIVE_W };

static int kind(HWND h) {
  char c[32];
  if (!GetClassNameA(h, c, sizeof c))
    return K_OTHER;
  if (!lstrcmpiA(c, "Edit"))
    return K_EDIT;
  if (!lstrcmpiA(c, "ListBox") || !lstrcmpiA(c, "ComboLBox"))
    return K_LISTBOX;
  if (!lstrcmpiA(c, "ComboBox"))
    return K_COMBOBOX;
  /* Rich edit registers W classes on 9x whose procedures take W text. */
  if (!lstrcmpiA(c, "RichEdit20W") || !lstrcmpiA(c, "RichEdit50W"))
    return K_NATIVE_W;
  if (c[0] == 'R' && !lstrcmpiA(c, "RichEdit20A"))
    return K_EDIT;
  if (!lstrcmpiA(c, "RICHEDIT") || !lstrcmpiA(c, "RichEdit50A"))
    return K_EDIT;
  return K_OTHER;
}

/* Whether a list box or combo box keeps strings (not owner-drawn data). */
static BOOL has_strings(HWND h, int k) {
  LONG s = REAL(GetWindowLongA)(h, GWL_STYLE);
  if (k == K_LISTBOX)
    return !(s & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) ||
           (s & LBS_HASSTRINGS);
  return !(s & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) ||
         (s & CBS_HASSTRINGS);
}

enum {
  M_NONE,
  M_STR_IN,      /* lParam is a string */
  M_TEXT_OUT,    /* WM_GETTEXT: wParam is the size, lParam the buffer */
  M_TEXT_LEN,    /* WM_GETTEXTLENGTH */
  M_ITEM_OUT,    /* LB_GETTEXT: wParam is the item, lParam the buffer */
  M_ITEM_LEN,    /* LB_GETTEXTLEN */
  M_LINE_OUT,    /* EM_GETLINE */
  M_CHAR,        /* wParam is a character */
  M_CHAR_LO,     /* LOWORD(wParam) is a character */
  M_IMECHAR,     /* wParam holds a DBCS character in two bytes */
  M_CHAR_RESULT, /* the result is a character */
  M_CREATE,      /* CREATESTRUCT */
  M_MDICREATE,   /* MDICREATESTRUCT */
};

/* How message m to window h carries text. */
static int classify(HWND h, UINT m) {
  int k;
  switch (m) {
  case WM_SETTEXT:
  case WM_SETTINGCHANGE:
  case WM_DEVMODECHANGE:
    return M_STR_IN;
  case WM_GETTEXT:
  case WM_ASKCBFORMATNAME:
    return M_TEXT_OUT;
  case WM_GETTEXTLENGTH:
    return M_TEXT_LEN;
  case WM_CHAR:
  case WM_DEADCHAR:
  case WM_SYSCHAR:
  case WM_SYSDEADCHAR:
    return M_CHAR;
  case WM_MENUCHAR:
  case WM_CHARTOITEM:
    return M_CHAR_LO;
  case WM_IME_CHAR:
    return M_IMECHAR;
  case WM_NCCREATE:
  case WM_CREATE:
    return M_CREATE;
  case WM_MDICREATE:
    return M_MDICREATE;
  }
  if (m < EM_GETSEL || m > LB_MSGMAX)
    return M_NONE;
  k = kind(h);
  if (k == K_NATIVE_W)
    return M_NONE;
  if (k == K_EDIT) {
    switch (m) {
    case EM_REPLACESEL:
      return M_STR_IN;
    case EM_GETLINE:
      return M_LINE_OUT;
    case EM_SETPASSWORDCHAR:
      return M_CHAR;
    case EM_GETPASSWORDCHAR:
      return M_CHAR_RESULT;
    }
  } else if (k == K_LISTBOX) {
    switch (m) {
    case LB_DIR:
      return M_STR_IN;
    case LB_ADDSTRING:
    case LB_INSERTSTRING:
    case LB_FINDSTRING:
    case LB_FINDSTRINGEXACT:
    case LB_SELECTSTRING:
      return has_strings(h, k) ? M_STR_IN : M_NONE;
    case LB_GETTEXT:
      return has_strings(h, k) ? M_ITEM_OUT : M_NONE;
    case LB_GETTEXTLEN:
      return has_strings(h, k) ? M_ITEM_LEN : M_NONE;
    }
  } else if (k == K_COMBOBOX) {
    switch (m) {
    case CB_DIR:
      return M_STR_IN;
    case CB_ADDSTRING:
    case CB_INSERTSTRING:
    case CB_FINDSTRING:
    case CB_FINDSTRINGEXACT:
    case CB_SELECTSTRING:
      return has_strings(h, k) ? M_STR_IN : M_NONE;
    case CB_GETLBTEXT:
      return has_strings(h, k) ? M_ITEM_OUT : M_NONE;
    case CB_GETLBTEXTLEN:
      return has_strings(h, k) ? M_ITEM_LEN : M_NONE;
    }
  }
  return M_NONE;
}

static UINT item_len_msg(UINT m) {
  return m == LB_GETTEXT ? LB_GETTEXTLEN : CB_GETLBTEXTLEN;
}
static UINT item_get_msg(UINT m) {
  return m == LB_GETTEXTLEN ? LB_GETTEXT : CB_GETLBTEXT;
}

/* ---- characters ------------------------------------------------------- */

/* A W character as one or two ANSI bytes (lead byte first). */
static int char_a(WCHAR w, BYTE out[2]) {
  char b[4];
  int n = w ? ow_wc2mb(CP_ACP, 0, &w, 1, b, sizeof b) : 0;
  if (n == 2) {
    out[0] = (BYTE)b[0];
    out[1] = (BYTE)b[1];
    return 2;
  }
  out[0] = n == 1 ? (BYTE)b[0] : (BYTE)(w ? '?' : 0);
  return 1;
}

static WCHAR char_w2(BYTE lead, BYTE trail) {
  char b[2] = {(char)lead, (char)trail};
  WCHAR w = 0;
  if (!ow_mb2wc(CP_ACP, 0, b, 2, &w, 1))
    w = trail;
  return w;
}

static WCHAR char_w(BYTE c) {
  char b = (char)c;
  WCHAR w = 0;
  if (!c)
    return 0;
  if (!ow_mb2wc(CP_ACP, 0, &b, 1, &w, 1))
    w = c;
  return w;
}

/* The ordinal form of a window name in a dialog template ({0xff, id} in
 * A, {0xffff, id} in W). */
static BOOL ordinal_a(LPCSTR s) { return s && (BYTE)s[0] == 0xff; }
static BOOL ordinal_w(LPCWSTR s) { return s && s[0] == 0xffff; }

static LPWSTR name_w(LPCSTR a) {
  if (IS_INTRESOURCE(a))
    return (LPWSTR)a;
  if (ordinal_a(a)) {
    LPWSTR w = ow_alloc(3 * sizeof(WCHAR));
    if (w) {
      w[0] = 0xffff;
      w[1] = (WORD)((BYTE)a[1] | ((BYTE)a[2] << 8));
    }
    return w;
  }
  return ow_w(a);
}

static LPSTR name_a(LPCWSTR w) {
  if (IS_INTRESOURCE(w))
    return (LPSTR)w;
  if (ordinal_w(w)) {
    LPSTR a = ow_alloc(4);
    if (a) {
      a[0] = (char)0xff;
      a[1] = (char)(w[1] & 0xff);
      a[2] = (char)(w[1] >> 8);
    }
    return a;
  }
  return ow_a(w);
}

/* ---- A messages to W procedures ---------------------------------------- */

static void mdi_w(const MDICREATESTRUCTA *a, MDICREATESTRUCTW *w) {
  w->szClass = name_w(a->szClass);
  w->szTitle = name_w(a->szTitle);
  w->hOwner = a->hOwner;
  w->x = a->x;
  w->y = a->y;
  w->cx = a->cx;
  w->cy = a->cy;
  w->style = a->style;
  w->lParam = a->lParam;
}

static void mdi_free(MDICREATESTRUCTW *w) {
  ow_free(w->szClass);
  ow_free(w->szTitle);
}

LRESULT ow_call_w(WNDPROC wproc, HWND h, UINT m, WPARAM wp, LPARAM lp) {
  LRESULT r;
  if (m && m == ow_find_msg)
    return wproc(h, m, wp, ow_find_w(lp));
  switch (classify(h, m)) {
  case M_STR_IN: {
    LPWSTR w = ow_w((LPCSTR)lp);
    r = wproc(h, m, wp, lp ? (LPARAM)w : 0);
    ow_free(w);
    return r;
  }
  case M_TEXT_OUT: {
    LPWSTR w;
    if (!lp || !wp)
      return wproc(h, m, wp, lp);
    w = ow_alloc((wp + 1) * sizeof(WCHAR));
    if (!w)
      return 0;
    r = wproc(h, m, wp, (LPARAM)w);
    w[wp] = 0;
    r = ow_toa(w, r >= 0 && (WPARAM)r < wp ? (int)r : -1, (LPSTR)lp, (int)wp);
    ow_free(w);
    return r;
  }
  case M_TEXT_LEN: {
    /* The length in bytes: get the text to count them. */
    LRESULT n = wproc(h, m, wp, lp);
    LPWSTR w;
    int al;
    if (n <= 0)
      return n;
    w = ow_alloc((n + 1) * sizeof(WCHAR));
    if (!w)
      return n * 2;
    r = wproc(h, WM_GETTEXT, n + 1, (LPARAM)w);
    al = r > 0 ? ow_wc2mb(CP_ACP, 0, w, (int)r, NULL, 0) : 0;
    ow_free(w);
    return al > n ? al : n;
  }
  case M_ITEM_OUT: {
    LRESULT n = wproc(h, item_len_msg(m), wp, 0);
    LPWSTR w;
    if (n < 0 || !lp)
      return n < 0 ? n : wproc(h, m, wp, lp);
    w = ow_alloc((n + 1) * sizeof(WCHAR));
    if (!w)
      return LB_ERR;
    r = wproc(h, m, wp, (LPARAM)w);
    if (r < 0) {
      ow_free(w);
      return r;
    }
    {
      int al;
      LPSTR a = ow_an(w, (int)r, &al);
      if (a)
        ow_memcpy((LPSTR)lp, a, al + 1);
      ow_free(a);
      r = a ? al : 0;
    }
    ow_free(w);
    return r;
  }
  case M_ITEM_LEN: {
    LRESULT n = wproc(h, m, wp, lp);
    LPWSTR w;
    int al;
    if (n <= 0)
      return n;
    w = ow_alloc((n + 1) * sizeof(WCHAR));
    if (!w)
      return n * 2;
    r = wproc(h, item_get_msg(m), wp, (LPARAM)w);
    al = r > 0 ? ow_wc2mb(CP_ACP, 0, w, (int)r, NULL, 0) : 0;
    ow_free(w);
    return al > n ? al : n;
  }
  case M_LINE_OUT: {
    WORD size = lp ? *(WORD *)lp : 0;
    LPWSTR w;
    if (!size)
      return 0;
    w = ow_alloc((size + 1) * sizeof(WCHAR));
    if (!w)
      return 0;
    *(WORD *)w = size;
    r = wproc(h, m, wp, (LPARAM)w);
    if (r > 0) {
      int al;
      LPSTR a = ow_an(w, (int)r, &al);
      if (a) {
        if (al > size)
          al = size;
        ow_memcpy((LPSTR)lp, a, al);
        ow_free(a);
        r = al;
      }
    }
    ow_free(w);
    return r;
  }
  case M_CHAR: {
    struct ow_thread *t = ow_thread();
    BYTE c = (BYTE)wp;
    WCHAR w;
    if (t->lead_in) {
      w = char_w2(t->lead_in, c);
      t->lead_in = 0;
    } else if (IsDBCSLeadByte(c) && m != EM_SETPASSWORDCHAR) {
      t->lead_in = c;
      return 0;
    } else {
      w = char_w(c);
    }
    return wproc(h, m, w, lp);
  }
  case M_CHAR_LO:
    return wproc(h, m,
                           MAKEWPARAM(char_w((BYTE)wp), HIWORD(wp)), lp);
  case M_IMECHAR: {
    WCHAR w = (wp & 0xff00) ? char_w2((BYTE)(wp >> 8), (BYTE)wp)
                            : char_w((BYTE)wp);
    return wproc(h, m, w, lp);
  }
  case M_CHAR_RESULT: {
    BYTE b[2];
    r = wproc(h, m, wp, lp);
    char_a((WCHAR)r, b);
    return b[0];
  }
  case M_CREATE: {
    const CREATESTRUCTA *a = (const CREATESTRUCTA *)lp;
    CREATESTRUCTW cw;
    MDICREATESTRUCTW mw;
    BOOL mdi;
    if (!a)
      break;
    cw.lpCreateParams = a->lpCreateParams;
    cw.hInstance = a->hInstance;
    cw.hMenu = a->hMenu;
    cw.hwndParent = a->hwndParent;
    cw.cy = a->cy;
    cw.cx = a->cx;
    cw.y = a->y;
    cw.x = a->x;
    cw.style = a->style;
    cw.lpszName = name_w(a->lpszName);
    cw.lpszClass = name_w(a->lpszClass);
    cw.dwExStyle = a->dwExStyle;
    mdi = (a->dwExStyle & WS_EX_MDICHILD) && a->lpCreateParams;
    if (mdi) {
      mdi_w(a->lpCreateParams, &mw);
      cw.lpCreateParams = &mw;
    }
    r = wproc(h, m, wp, (LPARAM)&cw);
    if (mdi)
      mdi_free(&mw);
    ow_free(cw.lpszName);
    ow_free(cw.lpszClass);
    return r;
  }
  case M_MDICREATE: {
    MDICREATESTRUCTW mw;
    if (!lp)
      break;
    mdi_w((const MDICREATESTRUCTA *)lp, &mw);
    r = wproc(h, m, wp, (LPARAM)&mw);
    mdi_free(&mw);
    return r;
  }
  }
  return wproc(h, m, wp, lp);
}

/* ---- W messages to A procedures -------------------------------------- */

static void mdi_a(const MDICREATESTRUCTW *w, MDICREATESTRUCTA *a) {
  a->szClass = name_a(w->szClass);
  a->szTitle = name_a(w->szTitle);
  a->hOwner = w->hOwner;
  a->x = w->x;
  a->y = w->y;
  a->cx = w->cx;
  a->cy = w->cy;
  a->style = w->style;
  a->lParam = w->lParam;
}

static void mdi_afree(MDICREATESTRUCTA *a) {
  ow_free(a->szClass);
  ow_free(a->szTitle);
}

static LRESULT send_w(ow_sender send, void *ctx, HWND h, UINT m, WPARAM wp,
                      LPARAM lp, BOOL async) {
  LRESULT r;
  int type;
  if (ow_comdlg_open > 0 && m >= WM_USER && m < WM_APP && !async &&
      ow_comdlg_msg(send, ctx, h, m, wp, lp, &r))
    return r;
  type = classify(h, m);
  if (async && type != M_CHAR && type != M_CHAR_LO && type != M_IMECHAR)
    return send(ctx, h, m, wp, lp);
  switch (type) {
  case M_STR_IN: {
    LPSTR a = ow_a((LPCWSTR)lp);
    r = send(ctx, h, m, wp, lp ? (LPARAM)a : 0);
    ow_free(a);
    return r;
  }
  case M_TEXT_OUT: {
    LPSTR a;
    int cb;
    if (!lp || !wp)
      return send(ctx, h, m, wp, lp);
    cb = (int)wp * 2 + 1;
    a = ow_alloc(cb + 1);
    if (!a)
      return 0;
    r = send(ctx, h, m, cb, (LPARAM)a);
    a[r >= 0 && r < cb ? r : cb] = 0;
    r = ow_tow(a, -1, (LPWSTR)lp, (int)wp);
    ow_free(a);
    return r;
  }
  case M_TEXT_LEN: {
    LRESULT n = send(ctx, h, m, wp, lp);
    LPSTR a;
    if (n <= 0)
      return n;
    a = ow_alloc(n + 2);
    if (!a)
      return n;
    r = send(ctx, h, WM_GETTEXT, n + 1, (LPARAM)a);
    n = r > 0 ? ow_wlen_of(a, (int)r, CP_ACP) : n;
    ow_free(a);
    return n;
  }
  case M_ITEM_OUT: {
    LRESULT n = send(ctx, h, item_len_msg(m), wp, 0);
    LPSTR a;
    if (n < 0 || !lp)
      return n < 0 ? n : send(ctx, h, m, wp, lp);
    a = ow_alloc(n + 2);
    if (!a)
      return LB_ERR;
    r = send(ctx, h, m, wp, (LPARAM)a);
    if (r >= 0) {
      int wl;
      LPWSTR w = ow_wn(a, (int)r, &wl);
      if (w)
        ow_memcpy((LPWSTR)lp, w, (wl + 1) * sizeof(WCHAR));
      ow_free(w);
      r = w ? wl : 0;
    }
    ow_free(a);
    return r;
  }
  case M_ITEM_LEN: {
    LRESULT n = send(ctx, h, m, wp, lp);
    LPSTR a;
    if (n <= 0)
      return n;
    a = ow_alloc(n + 2);
    if (!a)
      return n;
    r = send(ctx, h, item_get_msg(m), wp, (LPARAM)a);
    n = r > 0 ? ow_wlen_of(a, (int)r, CP_ACP) : n;
    ow_free(a);
    return n;
  }
  case M_LINE_OUT: {
    WORD size = lp ? *(WORD *)lp : 0;
    LPSTR a;
    if (!size)
      return 0;
    a = ow_alloc(size * 2 + 2);
    if (!a)
      return 0;
    *(WORD *)a = (WORD)(size * 2);
    r = send(ctx, h, m, wp, (LPARAM)a);
    if (r > 0) {
      int wl;
      LPWSTR w = ow_wn(a, (int)r, &wl);
      if (w) {
        if (wl > size)
          wl = size;
        ow_memcpy((LPWSTR)lp, w, wl * sizeof(WCHAR));
        ow_free(w);
        r = wl;
      }
    }
    ow_free(a);
    return r;
  }
  case M_CHAR: {
    BYTE b[2];
    if (char_a((WCHAR)wp, b) == 2 && m != EM_SETPASSWORDCHAR) {
      send(ctx, h, m, b[0], lp);
      return send(ctx, h, m, b[1], lp);
    }
    return send(ctx, h, m, b[0], lp);
  }
  case M_CHAR_LO: {
    BYTE b[2];
    char_a(LOWORD(wp), b);
    return send(ctx, h, m, MAKEWPARAM(b[0], HIWORD(wp)), lp);
  }
  case M_IMECHAR: {
    BYTE b[2];
    if (char_a((WCHAR)wp, b) == 2)
      return send(ctx, h, m, (b[0] << 8) | b[1], lp);
    return send(ctx, h, m, b[0], lp);
  }
  case M_CHAR_RESULT: {
    r = send(ctx, h, m, wp, lp);
    return char_w((BYTE)r);
  }
  case M_CREATE: {
    const CREATESTRUCTW *w = (const CREATESTRUCTW *)lp;
    CREATESTRUCTA ca;
    MDICREATESTRUCTA ma;
    BOOL mdi;
    if (!w)
      break;
    ca.lpCreateParams = w->lpCreateParams;
    ca.hInstance = w->hInstance;
    ca.hMenu = w->hMenu;
    ca.hwndParent = w->hwndParent;
    ca.cy = w->cy;
    ca.cx = w->cx;
    ca.y = w->y;
    ca.x = w->x;
    ca.style = w->style;
    ca.lpszName = name_a(w->lpszName);
    ca.lpszClass = name_a(w->lpszClass);
    ca.dwExStyle = w->dwExStyle;
    mdi = (w->dwExStyle & WS_EX_MDICHILD) && w->lpCreateParams;
    if (mdi) {
      mdi_a(w->lpCreateParams, &ma);
      ca.lpCreateParams = &ma;
    }
    r = send(ctx, h, m, wp, (LPARAM)&ca);
    if (mdi)
      mdi_afree(&ma);
    ow_free(ca.lpszName);
    ow_free(ca.lpszClass);
    return r;
  }
  case M_MDICREATE: {
    MDICREATESTRUCTA ma;
    if (!lp)
      break;
    mdi_a((const MDICREATESTRUCTW *)lp, &ma);
    r = send(ctx, h, m, wp, (LPARAM)&ma);
    mdi_afree(&ma);
    return r;
  }
  }
  return send(ctx, h, m, wp, lp);
}

LRESULT ow_send_w(ow_sender send, void *ctx, HWND h, UINT m, WPARAM wp,
                  LPARAM lp) {
  return send_w(send, ctx, h, m, wp, lp, FALSE);
}

LRESULT ow_post_w(ow_sender send, void *ctx, HWND h, UINT m, WPARAM wp,
                  LPARAM lp) {
  return send_w(send, ctx, h, m, wp, lp, TRUE);
}

/* ---- messages in MSG structures ---------------------------------------- */

void ow_msg_w2a(const MSG *w, MSG *a) {
  BYTE b[2];
  *a = *w;
  switch (w->message) {
  case WM_CHAR:
  case WM_DEADCHAR:
  case WM_SYSCHAR:
  case WM_SYSDEADCHAR:
    char_a((WCHAR)w->wParam, b);
    a->wParam = b[0];
    break;
  case WM_IME_CHAR:
    if (char_a((WCHAR)w->wParam, b) == 2)
      a->wParam = (b[0] << 8) | b[1];
    else
      a->wParam = b[0];
    break;
  }
}

/* WM_CHAR and the like from A to W. A DBCS lead byte is joined with the
 * WM_CHAR that holds its trail byte, which is removed from the queue. */
void ow_msg_a2w(MSG *m) {
  switch (m->message) {
  case WM_CHAR:
  case WM_DEADCHAR:
  case WM_SYSCHAR:
  case WM_SYSDEADCHAR: {
    BYTE c = (BYTE)m->wParam;
    if (IsDBCSLeadByte(c)) {
      MSG next;
      if (PeekMessageA(&next, m->hwnd, m->message, m->message, PM_REMOVE))
        m->wParam = char_w2(c, (BYTE)next.wParam);
      else
        m->wParam = char_w(c);
    } else {
      m->wParam = char_w(c);
    }
    break;
  }
  case WM_IME_CHAR:
    m->wParam = (m->wParam & 0xff00)
                    ? char_w2((BYTE)(m->wParam >> 8), (BYTE)m->wParam)
                    : char_w((BYTE)m->wParam);
    break;
  }
}

/* ---- window procedures -------------------------------------------------- */

struct proc_ctx {
  WNDPROC proc;
};
static LRESULT send_callproc(void *ctx, HWND h, UINT m, WPARAM wp,
                             LPARAM lp) {
  return REAL(CallWindowProcA)(((struct proc_ctx *)ctx)->proc, h, m, wp, lp);
}

LRESULT WINAPI CallWindowProcW(WNDPROC proc, HWND h, UINT m, WPARAM wp,
                               LPARAM lp) {
  struct proc_ctx c;
  if (!proc)
    return 0;
  if (ow_is_thunk(proc))
    proc = ow_wndproc_w(proc);
  else if (!is_wproc(proc)) {
    c.proc = proc;
    return ow_send_w(send_callproc, &c, h, m, wp, lp);
  }
  return proc(h, m, wp, lp);
}

LRESULT WINAPI CallWindowProcA(WNDPROC proc, HWND h, UINT m, WPARAM wp,
                               LPARAM lp) {
  if (proc && !ow_is_thunk(proc) && is_wproc(proc))
    return ow_call_w(proc, h, m, wp, lp);
  return REAL(CallWindowProcA)(proc, h, m, wp, lp);
}

static LRESULT send_defwnd(void *ctx, HWND h, UINT m, WPARAM wp, LPARAM lp) {
  (void)ctx;
  return DefWindowProcA(h, m, wp, lp);
}

LRESULT WINAPI DefWindowProcW(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  /* Common controls ask their parent which notifications it wants. */
  if (m == WM_NOTIFYFORMAT && lp == NF_QUERY)
    return NFR_UNICODE;
  return ow_send_w(send_defwnd, NULL, h, m, wp, lp);
}

static LRESULT send_defdlg(void *ctx, HWND h, UINT m, WPARAM wp, LPARAM lp) {
  (void)ctx;
  return DefDlgProcA(h, m, wp, lp);
}

LRESULT WINAPI DefDlgProcW(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  return ow_send_w(send_defdlg, NULL, h, m, wp, lp);
}

static LRESULT send_defframe(void *ctx, HWND h, UINT m, WPARAM wp,
                             LPARAM lp) {
  return DefFrameProcA(h, *(HWND *)ctx, m, wp, lp);
}

LRESULT WINAPI DefFrameProcW(HWND h, HWND client, UINT m, WPARAM wp,
                             LPARAM lp) {
  return ow_send_w(send_defframe, &client, h, m, wp, lp);
}

static LRESULT send_defmdi(void *ctx, HWND h, UINT m, WPARAM wp, LPARAM lp) {
  (void)ctx;
  return DefMDIChildProcA(h, m, wp, lp);
}

LRESULT WINAPI DefMDIChildProcW(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  return ow_send_w(send_defmdi, NULL, h, m, wp, lp);
}

static BOOL is_dialog(HWND h) {
  return GetClassLongA(h, GCW_ATOM) == (DWORD)(UINT_PTR)WC_DIALOG;
}

LONG WINAPI GetWindowLongW(HWND h, int i) {
  LONG v = REAL(GetWindowLongA)(h, i);
  if (i == GWL_WNDPROC || (i == DWL_DLGPROC && is_dialog(h)))
    v = (LONG)ow_wndproc_w((WNDPROC)v);
  return v;
}

LONG WINAPI SetWindowLongW(HWND h, int i, LONG v) {
  if (i == GWL_WNDPROC || (i == DWL_DLGPROC && is_dialog(h))) {
    WNDPROC p = ow_wndproc_a((WNDPROC)v);
    if (v && !p) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return 0;
    }
    return (LONG)ow_wndproc_w((WNDPROC)REAL(SetWindowLongA)(h, i, (LONG)p));
  }
  return REAL(SetWindowLongA)(h, i, v);
}

LONG WINAPI GetWindowLongA(HWND h, int i) {
  return REAL(GetWindowLongA)(h, i);
}

LONG WINAPI SetWindowLongA(HWND h, int i, LONG v) {
  return REAL(SetWindowLongA)(h, i, v);
}

/* A window is W when its procedure is, or for a dialog, its dialog
 * procedure. */
BOOL WINAPI IsWindowUnicode(HWND h) {
  if (!IsWindow(h))
    return FALSE;
  if (ow_is_thunk((WNDPROC)REAL(GetWindowLongA)(h, GWL_WNDPROC)))
    return TRUE;
  return is_dialog(h) &&
         ow_is_thunk((WNDPROC)REAL(GetWindowLongA)(h, DWL_DLGPROC));
}

/* ---- classes ------------------------------------------------------------ */

ATOM WINAPI RegisterClassW(const WNDCLASSW *w) {
  WNDCLASSA a;
  ATOM r;
  if (!w) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  ow_memcpy(&a, w, sizeof a);
  a.lpfnWndProc = ow_wndproc_a(w->lpfnWndProc);
  a.lpszMenuName = ow_ares(w->lpszMenuName);
  a.lpszClassName = ow_ares(w->lpszClassName);
  r = a.lpfnWndProc || !w->lpfnWndProc ? RegisterClassA(&a) : 0;
  ow_free(a.lpszMenuName);
  ow_free(a.lpszClassName);
  return r;
}

ATOM WINAPI RegisterClassExW(const WNDCLASSEXW *w) {
  WNDCLASSEXA a;
  ATOM r;
  if (!w || w->cbSize != sizeof *w) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  ow_memcpy(&a, w, sizeof a);
  a.cbSize = sizeof a;
  a.lpfnWndProc = ow_wndproc_a(w->lpfnWndProc);
  a.lpszMenuName = ow_ares(w->lpszMenuName);
  a.lpszClassName = ow_ares(w->lpszClassName);
  r = a.lpfnWndProc || !w->lpfnWndProc ? RegisterClassExA(&a) : 0;
  ow_free(a.lpszMenuName);
  ow_free(a.lpszClassName);
  return r;
}

BOOL WINAPI UnregisterClassW(LPCWSTR name, HINSTANCE inst) {
  LPSTR a = ow_ares(name);
  BOOL r = UnregisterClassA(a, inst);
  ow_free(a);
  return r;
}

BOOL WINAPI GetClassInfoW(HINSTANCE inst, LPCWSTR name, LPWNDCLASSW w) {
  WNDCLASSA a;
  LPSTR n = ow_ares(name);
  BOOL r = GetClassInfoA(inst, n, &a);
  ow_free(n);
  if (!r || !w)
    return r;
  ow_memcpy(w, &a, sizeof *w);
  w->lpfnWndProc = ow_wndproc_w(a.lpfnWndProc);
  w->lpszMenuName = ow_intern_w(a.lpszMenuName);
  w->lpszClassName = name;
  return r;
}

BOOL WINAPI GetClassInfoExW(HINSTANCE inst, LPCWSTR name, LPWNDCLASSEXW w) {
  WNDCLASSEXA a;
  LPSTR n = ow_ares(name);
  BOOL r;
  a.cbSize = sizeof a;
  r = GetClassInfoExA(inst, n, &a);
  ow_free(n);
  if (!r || !w)
    return r;
  ow_memcpy(w, &a, sizeof *w);
  w->cbSize = sizeof *w;
  w->lpfnWndProc = ow_wndproc_w(a.lpfnWndProc);
  w->lpszMenuName = ow_intern_w(a.lpszMenuName);
  w->lpszClassName = name;
  return r;
}

DWORD WINAPI GetClassLongW(HWND h, int i) {
  DWORD v = GetClassLongA(h, i);
  if (i == GCL_WNDPROC)
    v = (DWORD)ow_wndproc_w((WNDPROC)v);
  else if (i == GCL_MENUNAME && v)
    v = (DWORD)ow_intern_w((LPCSTR)v);
  return v;
}

DWORD WINAPI SetClassLongW(HWND h, int i, LONG v) {
  if (i == GCL_WNDPROC) {
    WNDPROC p = ow_wndproc_a((WNDPROC)v);
    return (DWORD)ow_wndproc_w((WNDPROC)SetClassLongA(h, i, (LONG)p));
  }
  if (i == GCL_MENUNAME) {
    /* The class keeps the pointer: give it a copy that stays. */
    LPSTR a = ow_ares((LPCWSTR)v);
    struct interned *it;
    DWORD old;
    if (IS_INTRESOURCE(a))
      return SetClassLongA(h, i, (LONG)a);
    it = intern(a);
    ow_free(a);
    if (!it) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return 0;
    }
    old = SetClassLongA(h, i, (LONG)it->a);
    return old ? (DWORD)ow_intern_w((LPCSTR)old) : 0;
  }
  return SetClassLongA(h, i, v);
}

int WINAPI GetClassNameW(HWND h, LPWSTR buf, int cch) {
  char a[512];
  int n = GetClassNameA(h, a, sizeof a);
  if (!n)
    return 0;
  return ow_tow(a, n, buf, cch);
}

/* ---- windows ------------------------------------------------------------ */

HWND WINAPI CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style,
                            int x, int y, int w, int h, HWND parent,
                            HMENU menu, HINSTANCE inst, LPVOID param) {
  LPSTR c = ow_ares(cls), n = name_a(name);
  HWND r;
  /* An MDI child's parameter is an MDICREATESTRUCT only when the MDI
     client creates it; here it is the caller's. */
  r = CreateWindowExA(ex, c, n, style, x, y, w, h, parent, menu, inst, param);
  ow_free(c);
  ow_free(n);
  return r;
}

HWND WINAPI CreateMDIWindowW(LPCWSTR cls, LPCWSTR name, DWORD style, int x,
                             int y, int w, int h, HWND parent, HINSTANCE inst,
                             LPARAM lp) {
  LPSTR c = ow_ares(cls), n = ow_a(name);
  HWND r = CreateMDIWindowA(c, n, style, x, y, w, h, parent, inst, lp);
  ow_free(c);
  ow_free(n);
  return r;
}

BOOL WINAPI SetWindowTextW(HWND h, LPCWSTR s) {
  LPSTR a = ow_a(s);
  BOOL r = SetWindowTextA(h, a);
  ow_free(a);
  return r;
}

int WINAPI GetWindowTextW(HWND h, LPWSTR buf, int cch) {
  LPSTR a;
  int n;
  if (!buf || cch <= 0)
    return 0;
  a = ow_alloc(cch * 2 + 2);
  if (!a) {
    buf[0] = 0;
    return 0;
  }
  n = GetWindowTextA(h, a, cch * 2 + 1);
  n = ow_tow(a, n, buf, cch);
  ow_free(a);
  return n;
}

int WINAPI GetWindowTextLengthW(HWND h) {
  int n = GetWindowTextLengthA(h), r;
  LPSTR a;
  if (n <= 0)
    return n;
  a = ow_alloc(n + 2);
  if (!a)
    return n;
  r = GetWindowTextA(h, a, n + 1);
  n = r > 0 ? ow_wlen_of(a, r, CP_ACP) : n;
  ow_free(a);
  return n;
}

HWND WINAPI FindWindowW(LPCWSTR cls, LPCWSTR name) {
  LPSTR c = ow_ares(cls), n = ow_a(name);
  HWND r = FindWindowA(c, n);
  ow_free(c);
  ow_free(n);
  return r;
}

HWND WINAPI FindWindowExW(HWND parent, HWND after, LPCWSTR cls,
                          LPCWSTR name) {
  LPSTR c = ow_ares(cls), n = ow_a(name);
  HWND r = FindWindowExA(parent, after, c, n);
  ow_free(c);
  ow_free(n);
  return r;
}

BOOL WINAPI EnableWindow(HWND h, BOOL enable) {
  OW_REAL_DECL(BOOL, EnableWindow, (HWND, BOOL), 8);
  return REAL(EnableWindow)(h, enable);
}

/* ---- sending and posting --------------------------------------------- */

static LRESULT send_message(void *ctx, HWND h, UINT m, WPARAM wp, LPARAM lp) {
  (void)ctx;
  return SendMessageA(h, m, wp, lp);
}

LRESULT WINAPI SendMessageW(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  return ow_send_w(send_message, NULL, h, m, wp, lp);
}

struct timeout_ctx {
  UINT flags, timeout;
  PDWORD_PTR result;
  BOOL ok;
};
static LRESULT send_timeout(void *ctx, HWND h, UINT m, WPARAM wp,
                            LPARAM lp) {
  struct timeout_ctx *c = ctx;
  DWORD_PTR res = 0;
  c->ok = SendMessageTimeoutA(h, m, wp, lp, c->flags, c->timeout, &res) != 0;
  return res;
}

LRESULT WINAPI SendMessageTimeoutW(HWND h, UINT m, WPARAM wp, LPARAM lp,
                                   UINT flags, UINT timeout,
                                   PDWORD_PTR result) {
  struct timeout_ctx c = {flags, timeout, result, FALSE};
  LRESULT r = ow_send_w(send_timeout, &c, h, m, wp, lp);
  if (result)
    *result = r;
  return c.ok;
}

struct callback_ctx {
  SENDASYNCPROC proc;
  ULONG_PTR data;
  BOOL ok;
};
static LRESULT send_callback(void *ctx, HWND h, UINT m, WPARAM wp,
                             LPARAM lp) {
  struct callback_ctx *c = ctx;
  c->ok = SendMessageCallbackA(h, m, wp, lp, c->proc, c->data);
  return c->ok;
}

BOOL WINAPI SendMessageCallbackW(HWND h, UINT m, WPARAM wp, LPARAM lp,
                                 SENDASYNCPROC proc, ULONG_PTR data) {
  struct callback_ctx c = {proc, data, FALSE};
  ow_post_w(send_callback, &c, h, m, wp, lp);
  return c.ok;
}

static LRESULT send_notify(void *ctx, HWND h, UINT m, WPARAM wp, LPARAM lp) {
  (void)ctx;
  return SendNotifyMessageA(h, m, wp, lp);
}

BOOL WINAPI SendNotifyMessageW(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  return (BOOL)ow_post_w(send_notify, NULL, h, m, wp, lp);
}

static LRESULT post_message(void *ctx, HWND h, UINT m, WPARAM wp, LPARAM lp) {
  (void)ctx;
  return PostMessageA(h, m, wp, lp);
}

BOOL WINAPI PostMessageW(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  return (BOOL)ow_post_w(post_message, NULL, h, m, wp, lp);
}

static LRESULT post_thread(void *ctx, HWND h, UINT m, WPARAM wp, LPARAM lp) {
  (void)h;
  return PostThreadMessageA(*(DWORD *)ctx, m, wp, lp);
}

BOOL WINAPI PostThreadMessageW(DWORD tid, UINT m, WPARAM wp, LPARAM lp) {
  return (BOOL)ow_post_w(post_thread, &tid, NULL, m, wp, lp);
}

struct bsm_ctx {
  DWORD flags;
  LPDWORD recipients;
};
static LRESULT send_bsm(void *ctx, HWND h, UINT m, WPARAM wp, LPARAM lp) {
  struct bsm_ctx *c = ctx;
  (void)h;
  return BroadcastSystemMessageA(c->flags, c->recipients, m, wp, lp);
}

long WINAPI BroadcastSystemMessageW(DWORD flags, LPDWORD recipients, UINT m,
                                    WPARAM wp, LPARAM lp) {
  struct bsm_ctx c = {flags, recipients};
  if (flags & BSF_POSTMESSAGE)
    return (long)ow_post_w(send_bsm, &c, NULL, m, wp, lp);
  return (long)ow_send_w(send_bsm, &c, NULL, m, wp, lp);
}

LRESULT WINAPI SendDlgItemMessageW(HWND dlg, int id, UINT m, WPARAM wp,
                                   LPARAM lp) {
  HWND h = GetDlgItem(dlg, id);
  return h ? SendMessageW(h, m, wp, lp) : 0;
}

/* ---- the message loop ---------------------------------------------- */

BOOL WINAPI GetMessageW(LPMSG msg, HWND h, UINT min, UINT max) {
  BOOL r = GetMessageA(msg, h, min, max);
  if (r != -1 && msg)
    ow_msg_a2w(msg);
  return r;
}

BOOL WINAPI PeekMessageW(LPMSG msg, HWND h, UINT min, UINT max, UINT flags) {
  BOOL r = PeekMessageA(msg, h, min, max, flags);
  if (r && msg) {
    if (flags & PM_REMOVE) {
      ow_msg_a2w(msg);
    } else if (msg->message == WM_CHAR || msg->message == WM_DEADCHAR ||
               msg->message == WM_SYSCHAR || msg->message == WM_SYSDEADCHAR ||
               msg->message == WM_IME_CHAR) {
      /* Leave the queue alone: a lead byte stays a byte. */
      BYTE c = (BYTE)msg->wParam;
      if (msg->message == WM_IME_CHAR || !IsDBCSLeadByte(c))
        ow_msg_a2w(msg);
    }
  }
  return r;
}

LRESULT WINAPI DispatchMessageW(const MSG *msg) {
  MSG a;
  BYTE b[2];
  if (!msg)
    return 0;
  if ((msg->message == WM_CHAR || msg->message == WM_SYSCHAR ||
       msg->message == WM_DEADCHAR || msg->message == WM_SYSDEADCHAR) &&
      char_a((WCHAR)msg->wParam, b) == 2) {
    a = *msg;
    a.wParam = b[0];
    DispatchMessageA(&a);
    a.wParam = b[1];
    return DispatchMessageA(&a);
  }
  ow_msg_w2a(msg, &a);
  return DispatchMessageA(&a);
}

BOOL WINAPI IsDialogMessageW(HWND dlg, LPMSG msg) {
  MSG a;
  if (!msg)
    return FALSE;
  ow_msg_w2a(msg, &a);
  return IsDialogMessageA(dlg, &a);
}

int WINAPI TranslateAcceleratorW(HWND h, HACCEL acc, LPMSG msg) {
  MSG a;
  if (!msg)
    return 0;
  ow_msg_w2a(msg, &a);
  return TranslateAcceleratorA(h, acc, &a);
}

BOOL WINAPI CallMsgFilterW(LPMSG msg, int code) {
  MSG a;
  if (!msg)
    return FALSE;
  ow_msg_w2a(msg, &a);
  return CallMsgFilterA(&a, code);
}

/* ---- dialogs ------------------------------------------------------------ */

HWND WINAPI CreateDialogParamW(HINSTANCE inst, LPCWSTR name, HWND parent,
                               DLGPROC proc, LPARAM lp) {
  LPSTR n = ow_ares(name);
  HWND r = CreateDialogParamA(inst, n, parent,
                              (DLGPROC)ow_wndproc_a((WNDPROC)proc), lp);
  ow_free(n);
  return r;
}

HWND WINAPI CreateDialogIndirectParamW(HINSTANCE inst, LPCDLGTEMPLATEW t,
                                       HWND parent, DLGPROC proc, LPARAM lp) {
  /* Dialog templates are in Unicode for A and W alike. */
  return CreateDialogIndirectParamA(inst, t, parent,
                                    (DLGPROC)ow_wndproc_a((WNDPROC)proc), lp);
}

INT_PTR WINAPI DialogBoxParamW(HINSTANCE inst, LPCWSTR name, HWND parent,
                               DLGPROC proc, LPARAM lp) {
  LPSTR n = ow_ares(name);
  INT_PTR r = DialogBoxParamA(inst, n, parent,
                              (DLGPROC)ow_wndproc_a((WNDPROC)proc), lp);
  ow_free(n);
  return r;
}

INT_PTR WINAPI DialogBoxIndirectParamW(HINSTANCE inst, LPCDLGTEMPLATEW t,
                                       HWND parent, DLGPROC proc, LPARAM lp) {
  return DialogBoxIndirectParamA(inst, t, parent,
                                 (DLGPROC)ow_wndproc_a((WNDPROC)proc), lp);
}

UINT WINAPI GetDlgItemTextW(HWND dlg, int id, LPWSTR buf, int cch) {
  HWND h = GetDlgItem(dlg, id);
  if (!h) {
    if (buf && cch > 0)
      buf[0] = 0;
    return 0;
  }
  return GetWindowTextW(h, buf, cch);
}

BOOL WINAPI SetDlgItemTextW(HWND dlg, int id, LPCWSTR s) {
  LPSTR a = ow_a(s);
  BOOL r = SetDlgItemTextA(dlg, id, a);
  ow_free(a);
  return r;
}

/* DlgDirList changes the path it is given to the directory part. */
int WINAPI DlgDirListW(HWND dlg, LPWSTR path, int list, int status,
                       UINT type) {
  char a[MAX_PATH * 2];
  int r;
  if (path)
    ow_toa(path, -1, a, sizeof a);
  r = DlgDirListA(dlg, path ? a : NULL, list, status, type);
  if (r && path)
    ow_tow(a, -1, path, ow_wcslen(path) + 1);
  return r;
}

int WINAPI DlgDirListComboBoxW(HWND dlg, LPWSTR path, int combo, int status,
                               UINT type) {
  char a[MAX_PATH * 2];
  int r;
  if (path)
    ow_toa(path, -1, a, sizeof a);
  r = DlgDirListComboBoxA(dlg, path ? a : NULL, combo, status, type);
  if (r && path)
    ow_tow(a, -1, path, ow_wcslen(path) + 1);
  return r;
}

BOOL WINAPI DlgDirSelectExW(HWND dlg, LPWSTR buf, int cch, int list) {
  char a[MAX_PATH * 2];
  BOOL r = DlgDirSelectExA(dlg, a, sizeof a, list);
  ow_tow(a, -1, buf, cch);
  return r;
}

BOOL WINAPI DlgDirSelectComboBoxExW(HWND dlg, LPWSTR buf, int cch,
                                    int combo) {
  char a[MAX_PATH * 2];
  BOOL r = DlgDirSelectComboBoxExA(dlg, a, sizeof a, combo);
  ow_tow(a, -1, buf, cch);
  return r;
}

/* ---- properties ------------------------------------------------------- */

OW_REAL_DECL(HANDLE, GetPropA, (HWND, LPCSTR), 8);
OW_REAL_DECL(BOOL, SetPropA, (HWND, LPCSTR, HANDLE), 12);
OW_REAL_DECL(HANDLE, RemovePropA, (HWND, LPCSTR), 8);
OW_REAL_DECL(int, EnumPropsA, (HWND, PROPENUMPROCA), 8);
OW_REAL_DECL(int, EnumPropsExA, (HWND, PROPENUMPROCEXA, LPARAM), 12);

HANDLE WINAPI GetPropA(HWND h, LPCSTR s) { return REAL(GetPropA)(h, s); }
BOOL WINAPI SetPropA(HWND h, LPCSTR s, HANDLE v) {
  return REAL(SetPropA)(h, s, v);
}
HANDLE WINAPI RemovePropA(HWND h, LPCSTR s) { return REAL(RemovePropA)(h, s); }
int WINAPI EnumPropsA(HWND h, PROPENUMPROCA p) {
  return REAL(EnumPropsA)(h, p);
}
int WINAPI EnumPropsExA(HWND h, PROPENUMPROCEXA p, LPARAM lp) {
  return REAL(EnumPropsExA)(h, p, lp);
}

HANDLE WINAPI GetPropW(HWND h, LPCWSTR s) {
  LPSTR a = ow_ares(s);
  HANDLE r = REAL(GetPropA)(h, a);
  ow_free(a);
  return r;
}

BOOL WINAPI SetPropW(HWND h, LPCWSTR s, HANDLE v) {
  LPSTR a = ow_ares(s);
  BOOL r = REAL(SetPropA)(h, a, v);
  ow_free(a);
  return r;
}

HANDLE WINAPI RemovePropW(HWND h, LPCWSTR s) {
  LPSTR a = ow_ares(s);
  HANDLE r = REAL(RemovePropA)(h, a);
  ow_free(a);
  return r;
}

struct props_ctx {
  PROPENUMPROCEXW proc;
  PROPENUMPROCW old;
  LPARAM lp;
};

static BOOL CALLBACK props_cb(HWND h, LPSTR s, HANDLE v, ULONG_PTR lp) {
  struct props_ctx *c = (struct props_ctx *)lp;
  LPWSTR w = ow_wres(s);
  BOOL r = c->proc ? c->proc(h, w, v, c->lp) : c->old(h, w, v);
  ow_free(w);
  return r;
}

int WINAPI EnumPropsExW(HWND h, PROPENUMPROCEXW proc, LPARAM lp) {
  struct props_ctx c = {proc, NULL, lp};
  return REAL(EnumPropsExA)(h, props_cb, (LPARAM)&c);
}

int WINAPI EnumPropsW(HWND h, PROPENUMPROCW proc) {
  struct props_ctx c = {NULL, proc, 0};
  return REAL(EnumPropsExA)(h, props_cb, (LPARAM)&c);
}

/* ---- hooks ---------------------------------------------------------------- */

HHOOK WINAPI SetWindowsHookExW(int id, HOOKPROC proc, HINSTANCE inst,
                               DWORD tid) {
  return SetWindowsHookExA(id, proc, inst, tid);
}

HHOOK WINAPI SetWindowsHookW(int id, HOOKPROC proc) {
  return SetWindowsHookA(id, proc);
}
