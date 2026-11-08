/* oledlg.dll: the OLE dialogs. The structures have the same layout in both
 * forms but for their strings, so each is copied and its strings
 * converted; hooks and dialog procedures go through the A-to-W window
 * procedures of window.c, and the interfaces that the caller provides are
 * wrapped in A ones.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"
#include <oledlg.h>
#include <prsht.h>

/* ---- the OLE task allocator, which the dialogs use for strings --------- */

static void *task_alloc(SIZE_T n) {
  OW_FN(OW_OLE32, CoTaskMemAlloc, return NULL);
  return pCoTaskMemAlloc(n);
}

static void task_free(void *p) {
  OW_FN(OW_OLE32, CoTaskMemFree, return);
  if (p)
    pCoTaskMemFree(p);
}

static LPSTR task_a(LPCWSTR w) {
  int n;
  LPSTR a;
  if (!w)
    return NULL;
  n = ow_wc2mb(CP_ACP, 0, w, -1, NULL, 0);
  a = task_alloc(n ? n : 1);
  if (a) {
    a[0] = 0;
    ow_wc2mb(CP_ACP, 0, w, -1, a, n);
  }
  return a;
}

static LPWSTR task_w(LPCSTR a) {
  int n;
  LPWSTR w;
  if (!a)
    return NULL;
  n = ow_mb2wc(CP_ACP, 0, a, -1, NULL, 0);
  w = task_alloc((n ? n : 1) * sizeof(WCHAR));
  if (w) {
    w[0] = 0;
    ow_mb2wc(CP_ACP, 0, a, -1, w, n);
  }
  return w;
}

/* Replaces a task-allocated W string by its A form, freeing it. */
static void swap_a(LPWSTR w, LPSTR *a) {
  *a = task_a(w);
  task_free(w);
}

static LPFNOLEUIHOOK hook_a(LPFNOLEUIHOOK w) {
  return w ? (LPFNOLEUIHOOK)ow_wndproc_a((WNDPROC)w) : NULL;
}

/* The fields that start every structure. */
#define HEAD_IN(a, w)                                                       \
  do {                                                                       \
    (a).cbStruct = sizeof(a);                                                \
    (a).lpszCaption = ow_a((w)->lpszCaption);                                \
    (a).lpszTemplate = ow_ares((w)->lpszTemplate);                           \
    (a).lpfnHook = hook_a((w)->lpfnHook);                                    \
  } while (0)
#define HEAD_FREE(a)                                                        \
  do {                                                                       \
    ow_free((void *)(a).lpszCaption);                                        \
    ow_free((void *)(a).lpszTemplate);                                       \
  } while (0)

/* ---- interfaces ------------------------------------------------------------
 * IOleUILinkInfoA (which is IOleUILinkContainerA with one more method) and
 * IOleUIObjInfoA, calling the caller's W ones. */

struct link_a {
  IOleUILinkInfoAVtbl *vtbl;
  LONG ref;
  IOleUILinkInfoW *w;
};

struct obj_a {
  IOleUIObjInfoAVtbl *vtbl;
  LONG ref;
  IOleUIObjInfoW *w;
};

#define LINK(t) (((struct link_a *)(t))->w)
#define OBJ(t) (((struct obj_a *)(t))->w)

static HRESULT STDMETHODCALLTYPE la_qi(IOleUILinkInfoA *t, REFIID iid,
                                       void **out) {
  static const IID unk = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
  if (!out)
    return E_POINTER;
  if (IsEqualIID(iid, &unk)) {
    *out = t;
    t->lpVtbl->AddRef(t);
    return S_OK;
  }
  *out = NULL;
  return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE la_addref(IOleUILinkInfoA *t) {
  return InterlockedIncrement(&((struct link_a *)t)->ref);
}

static ULONG STDMETHODCALLTYPE la_release(IOleUILinkInfoA *t) {
  LONG r = InterlockedDecrement(&((struct link_a *)t)->ref);
  if (!r) {
    LINK(t)->lpVtbl->Release(LINK(t));
    ow_free(t);
  }
  return r;
}

static DWORD STDMETHODCALLTYPE la_next(IOleUILinkInfoA *t, DWORD l) {
  return LINK(t)->lpVtbl->GetNextLink(LINK(t), l);
}

static HRESULT STDMETHODCALLTYPE la_setopt(IOleUILinkInfoA *t, DWORD l,
                                           DWORD o) {
  return LINK(t)->lpVtbl->SetLinkUpdateOptions(LINK(t), l, o);
}

static HRESULT STDMETHODCALLTYPE la_getopt(IOleUILinkInfoA *t, DWORD l,
                                           DWORD *o) {
  return LINK(t)->lpVtbl->GetLinkUpdateOptions(LINK(t), l, o);
}

/* A length in characters of the start of a string, in the other form. */
static ULONG len_w(LPCSTR a, ULONG n) {
  return a ? (ULONG)ow_wlen_of(a, (int)n, CP_ACP) : 0;
}

static ULONG len_a(LPCWSTR w, ULONG n) {
  return w && n ? (ULONG)ow_wc2mb(CP_ACP, 0, w, (int)n, NULL, 0) : 0;
}

static HRESULT STDMETHODCALLTYPE la_setsrc(IOleUILinkInfoA *t, DWORD l,
                                           LPSTR name, ULONG flen,
                                           ULONG *eaten, BOOL validate) {
  LPWSTR w = ow_w(name);
  ULONG e = 0;
  HRESULT r = LINK(t)->lpVtbl->SetLinkSource(LINK(t), l, w, len_w(name, flen),
                                             &e, validate);
  if (eaten)
    *eaten = len_a(w, e);
  ow_free(w);
  return r;
}

static HRESULT STDMETHODCALLTYPE la_getsrc(IOleUILinkInfoA *t, DWORD l,
                                           LPSTR *name, ULONG *flen,
                                           LPSTR *full, LPSTR *shrt,
                                           BOOL *avail, BOOL *sel) {
  LPWSTR wn = NULL, wf = NULL, ws = NULL;
  ULONG n = 0;
  HRESULT r = LINK(t)->lpVtbl->GetLinkSource(
      LINK(t), l, name ? &wn : NULL, &n, full ? &wf : NULL,
      shrt ? &ws : NULL, avail, sel);
  if (flen)
    *flen = len_a(wn, n);
  if (name)
    swap_a(wn, name);
  if (full)
    swap_a(wf, full);
  if (shrt)
    swap_a(ws, shrt);
  return r;
}

static HRESULT STDMETHODCALLTYPE la_open(IOleUILinkInfoA *t, DWORD l) {
  return LINK(t)->lpVtbl->OpenLinkSource(LINK(t), l);
}

static HRESULT STDMETHODCALLTYPE la_update(IOleUILinkInfoA *t, DWORD l,
                                           BOOL msg, BOOL res) {
  return LINK(t)->lpVtbl->UpdateLink(LINK(t), l, msg, res);
}

static HRESULT STDMETHODCALLTYPE la_cancel(IOleUILinkInfoA *t, DWORD l) {
  return LINK(t)->lpVtbl->CancelLink(LINK(t), l);
}

static HRESULT STDMETHODCALLTYPE la_last(IOleUILinkInfoA *t, DWORD l,
                                         FILETIME *ft) {
  return LINK(t)->lpVtbl->GetLastUpdate(LINK(t), l, ft);
}

static IOleUILinkInfoAVtbl link_vtbl = {
    la_qi,     la_addref, la_release, la_next,   la_setopt, la_getopt,
    la_setsrc, la_getsrc, la_open,    la_update, la_cancel, la_last};

/* Wraps a W link container (or link info, when info is set: only then is
 * GetLastUpdate called). */
static IOleUILinkInfoA *link_wrap(void *w) {
  struct link_a *a;
  if (!w)
    return NULL;
  a = ow_alloc(sizeof *a);
  if (!a)
    return NULL;
  a->vtbl = &link_vtbl;
  a->ref = 1;
  a->w = w;
  a->w->lpVtbl->AddRef(a->w);
  return (IOleUILinkInfoA *)a;
}

static void link_release(void *a) {
  if (a)
    ((IOleUILinkInfoA *)a)->lpVtbl->Release(a);
}

static HRESULT STDMETHODCALLTYPE oa_qi(IOleUIObjInfoA *t, REFIID iid,
                                       void **out) {
  return la_qi((IOleUILinkInfoA *)t, iid, out);
}

static ULONG STDMETHODCALLTYPE oa_addref(IOleUIObjInfoA *t) {
  return InterlockedIncrement(&((struct obj_a *)t)->ref);
}

static ULONG STDMETHODCALLTYPE oa_release(IOleUIObjInfoA *t) {
  LONG r = InterlockedDecrement(&((struct obj_a *)t)->ref);
  if (!r) {
    OBJ(t)->lpVtbl->Release(OBJ(t));
    ow_free(t);
  }
  return r;
}

static HRESULT STDMETHODCALLTYPE oa_info(IOleUIObjInfoA *t, DWORD o,
                                         DWORD *size, LPSTR *label,
                                         LPSTR *type, LPSTR *shrt,
                                         LPSTR *loc) {
  LPWSTR wl = NULL, wt = NULL, ws = NULL, wo = NULL;
  HRESULT r = OBJ(t)->lpVtbl->GetObjectInfo(
      OBJ(t), o, size, label ? &wl : NULL, type ? &wt : NULL,
      shrt ? &ws : NULL, loc ? &wo : NULL);
  if (label)
    swap_a(wl, label);
  if (type)
    swap_a(wt, type);
  if (shrt)
    swap_a(ws, shrt);
  if (loc)
    swap_a(wo, loc);
  return r;
}

static HRESULT STDMETHODCALLTYPE oa_conv(IOleUIObjInfoA *t, DWORD o,
                                         CLSID *c, WORD *f, CLSID *d,
                                         LPCLSID *x, UINT *n) {
  return OBJ(t)->lpVtbl->GetConvertInfo(OBJ(t), o, c, f, d, x, n);
}

static HRESULT STDMETHODCALLTYPE oa_convert(IOleUIObjInfoA *t, DWORD o,
                                            REFCLSID c) {
  return OBJ(t)->lpVtbl->ConvertObject(OBJ(t), o, c);
}

static HRESULT STDMETHODCALLTYPE oa_getview(IOleUIObjInfoA *t, DWORD o,
                                            HGLOBAL *m, DWORD *asp,
                                            int *scale) {
  return OBJ(t)->lpVtbl->GetViewInfo(OBJ(t), o, m, asp, scale);
}

static HRESULT STDMETHODCALLTYPE oa_setview(IOleUIObjInfoA *t, DWORD o,
                                            HGLOBAL m, DWORD asp, int scale,
                                            BOOL rel) {
  return OBJ(t)->lpVtbl->SetViewInfo(OBJ(t), o, m, asp, scale, rel);
}

static IOleUIObjInfoAVtbl obj_vtbl = {oa_qi,    oa_addref,  oa_release,
                                      oa_info,  oa_conv,    oa_convert,
                                      oa_getview, oa_setview};

static IOleUIObjInfoA *obj_wrap(IOleUIObjInfoW *w) {
  struct obj_a *a;
  if (!w)
    return NULL;
  a = ow_alloc(sizeof *a);
  if (!a)
    return NULL;
  a->vtbl = &obj_vtbl;
  a->ref = 1;
  a->w = w;
  w->lpVtbl->AddRef(w);
  return (IOleUIObjInfoA *)a;
}

/* ---- the dialogs ---------------------------------------------------------- */

#define OLEDLG(name, fail) OW_FN(OW_OLEDLG, name, return fail)

BOOL WINAPI OleUIAddVerbMenuW(LPOLEOBJECT obj, LPCWSTR type, HMENU menu,
                              UINT pos, UINT idmin, UINT idmax, BOOL conv,
                              UINT idconv, HMENU *popup) {
  LPSTR a = ow_a(type);
  BOOL r;
  OLEDLG(OleUIAddVerbMenuA, FALSE);
  r = pOleUIAddVerbMenuA(obj, a, menu, pos, idmin, idmax, conv, idconv,
                         popup);
  ow_free(a);
  return r;
}

UINT WINAPI OleUIBusyW(LPOLEUIBUSYW w) {
  OLEUIBUSYA a;
  UINT r;
  OLEDLG(OleUIBusyA, OLEUI_ERR_STRUCTUREINVALID);
  if (!w)
    return OLEUI_ERR_STRUCTURENULL;
  ow_memcpy(&a, w, sizeof a);
  HEAD_IN(a, w);
  r = pOleUIBusyA(&a);
  w->dwFlags = a.dwFlags;
  HEAD_FREE(a);
  return r;
}

UINT WINAPI OleUIChangeIconW(LPOLEUICHANGEICONW w) {
  OLEUICHANGEICONA a;
  UINT r;
  OLEDLG(OleUIChangeIconA, OLEUI_ERR_STRUCTUREINVALID);
  if (!w)
    return OLEUI_ERR_STRUCTURENULL;
  ow_memcpy(&a, w, offsetof(OLEUICHANGEICONA, szIconExe));
  HEAD_IN(a, w);
  ow_toa(w->szIconExe, ow_wcsnlen(w->szIconExe, MAX_PATH), a.szIconExe,
         MAX_PATH);
  a.cchIconExe = MAX_PATH;
  r = pOleUIChangeIconA(&a);
  w->dwFlags = a.dwFlags;
  w->hMetaPict = a.hMetaPict;
  w->clsid = a.clsid;
  HEAD_FREE(a);
  return r;
}

UINT WINAPI OleUIConvertW(LPOLEUICONVERTW w) {
  OLEUICONVERTA a;
  UINT r;
  OLEDLG(OleUIConvertA, OLEUI_ERR_STRUCTUREINVALID);
  if (!w)
    return OLEUI_ERR_STRUCTURENULL;
  ow_memcpy(&a, w, sizeof a);
  HEAD_IN(a, w);
  a.lpszUserType = ow_a(w->lpszUserType);
  a.lpszDefLabel = ow_a(w->lpszDefLabel);
  r = pOleUIConvertA(&a);
  w->dwFlags = a.dwFlags;
  w->clsidNew = a.clsidNew;
  w->dvAspect = a.dvAspect;
  w->hMetaPict = a.hMetaPict;
  w->fObjectsIconChanged = a.fObjectsIconChanged;
  ow_free(a.lpszUserType);
  ow_free(a.lpszDefLabel);
  HEAD_FREE(a);
  return r;
}

UINT WINAPI OleUIEditLinksW(LPOLEUIEDITLINKSW w) {
  OLEUIEDITLINKSA a;
  UINT r;
  OLEDLG(OleUIEditLinksA, OLEUI_ERR_STRUCTUREINVALID);
  if (!w)
    return OLEUI_ERR_STRUCTURENULL;
  ow_memcpy(&a, w, sizeof a);
  HEAD_IN(a, w);
  a.lpOleUILinkContainer =
      (LPOLEUILINKCONTAINERA)link_wrap(w->lpOleUILinkContainer);
  r = pOleUIEditLinksA(&a);
  w->dwFlags = a.dwFlags;
  link_release(a.lpOleUILinkContainer);
  HEAD_FREE(a);
  return r;
}

UINT WINAPI OleUIInsertObjectW(LPOLEUIINSERTOBJECTW w) {
  OLEUIINSERTOBJECTA a;
  UINT r;
  OLEDLG(OleUIInsertObjectA, OLEUI_ERR_STRUCTUREINVALID);
  if (!w)
    return OLEUI_ERR_STRUCTURENULL;
  ow_memcpy(&a, w, sizeof a);
  HEAD_IN(a, w);
  a.lpszFile = NULL;
  if (w->lpszFile && w->cchFile) {
    a.cchFile = w->cchFile * 2;
    a.lpszFile = ow_alloc(a.cchFile);
    if (!a.lpszFile) {
      HEAD_FREE(a);
      return OLEUI_ERR_GLOBALMEMALLOC;
    }
    ow_toa(w->lpszFile, -1, a.lpszFile, a.cchFile);
  }
  r = pOleUIInsertObjectA(&a);
  w->dwFlags = a.dwFlags;
  w->clsid = a.clsid;
  w->sc = a.sc;
  w->hMetaPict = a.hMetaPict;
  if (a.lpszFile)
    ow_tow(a.lpszFile, -1, w->lpszFile, w->cchFile);
  ow_free(a.lpszFile);
  HEAD_FREE(a);
  return r;
}

UINT WINAPI OleUIPasteSpecialW(LPOLEUIPASTESPECIALW w) {
  OLEUIPASTESPECIALA a;
  OLEUIPASTEENTRYA *e = NULL;
  UINT r;
  int i;
  OLEDLG(OleUIPasteSpecialA, OLEUI_ERR_STRUCTUREINVALID);
  if (!w)
    return OLEUI_ERR_STRUCTURENULL;
  ow_memcpy(&a, w, sizeof a);
  HEAD_IN(a, w);
  if (w->arrPasteEntries && w->cPasteEntries > 0) {
    e = ow_alloc(w->cPasteEntries * sizeof *e);
    if (!e) {
      HEAD_FREE(a);
      return OLEUI_ERR_GLOBALMEMALLOC;
    }
    for (i = 0; i < w->cPasteEntries; i++) {
      e[i].fmtetc = w->arrPasteEntries[i].fmtetc;
      e[i].lpstrFormatName = ow_a(w->arrPasteEntries[i].lpstrFormatName);
      e[i].lpstrResultText = ow_a(w->arrPasteEntries[i].lpstrResultText);
      e[i].dwFlags = w->arrPasteEntries[i].dwFlags;
      e[i].dwScratchSpace = w->arrPasteEntries[i].dwScratchSpace;
    }
    a.arrPasteEntries = e;
  }
  r = pOleUIPasteSpecialA(&a);
  w->dwFlags = a.dwFlags;
  w->nSelectedIndex = a.nSelectedIndex;
  w->fLink = a.fLink;
  w->hMetaPict = a.hMetaPict;
  w->sizel = a.sizel;
  if (e) {
    for (i = 0; i < w->cPasteEntries; i++) {
      w->arrPasteEntries[i].dwScratchSpace = e[i].dwScratchSpace;
      ow_free(e[i].lpstrFormatName);
      ow_free(e[i].lpstrResultText);
    }
    ow_free(e);
  }
  HEAD_FREE(a);
  return r;
}

/* The OPENFILENAME that the change source dialog browses with: only its
 * inputs, as the dialog keeps the result itself. */
static OPENFILENAMEA *ofn_a(const OPENFILENAMEW *w) {
  OPENFILENAMEA *a;
  if (!w)
    return NULL;
  a = ow_alloc(sizeof *a);
  if (!a)
    return NULL;
  a->lStructSize = OPENFILENAME_SIZE_VERSION_400A;
  a->hwndOwner = w->hwndOwner;
  a->hInstance = w->hInstance;
  a->lpstrFilter = ow_alist(w->lpstrFilter);
  a->nFilterIndex = w->nFilterIndex;
  a->lpstrInitialDir = ow_a(w->lpstrInitialDir);
  a->lpstrTitle = ow_a(w->lpstrTitle);
  a->Flags = w->Flags & ~(OFN_ENABLEHOOK | OFN_ENABLETEMPLATE |
                          OFN_ENABLETEMPLATEHANDLE);
  a->lpstrDefExt = ow_a(w->lpstrDefExt);
  a->lCustData = w->lCustData;
  return a;
}

static void ofn_free(OPENFILENAMEA *a) {
  if (!a)
    return;
  ow_free(a->lpstrFilter);
  ow_free(a->lpstrInitialDir);
  ow_free(a->lpstrTitle);
  ow_free(a->lpstrDefExt);
  ow_free(a);
}

UINT WINAPI OleUIChangeSourceW(LPOLEUICHANGESOURCEW w) {
  OLEUICHANGESOURCEA a;
  LPSTR name;
  UINT r;
  OLEDLG(OleUIChangeSourceA, OLEUI_ERR_STRUCTUREINVALID);
  if (!w)
    return OLEUI_ERR_STRUCTURENULL;
  ow_memcpy(&a, w, sizeof a);
  HEAD_IN(a, w);
  a.lpOFN = ofn_a(w->lpOFN);
  a.lpOleUILinkContainer =
      (LPOLEUILINKCONTAINERA)link_wrap(w->lpOleUILinkContainer);
  a.lpszDisplayName = name = task_a(w->lpszDisplayName);
  a.nFileLength = w->lpszDisplayName
                      ? len_a(w->lpszDisplayName, w->nFileLength)
                      : 0;
  a.lpszFrom = a.lpszTo = NULL;
  r = pOleUIChangeSourceA(&a);
  w->dwFlags = a.dwFlags;
  if (a.lpszDisplayName != name) {
    /* The dialog freed the name it was given and made a new one. */
    task_free(w->lpszDisplayName);
    w->lpszDisplayName = task_w(a.lpszDisplayName);
    w->nFileLength = len_w(a.lpszDisplayName, a.nFileLength);
    task_free(a.lpszDisplayName);
  } else {
    task_free(name);
  }
  w->lpszFrom = task_w(a.lpszFrom);
  w->lpszTo = task_w(a.lpszTo);
  task_free(a.lpszFrom);
  task_free(a.lpszTo);
  link_release(a.lpOleUILinkContainer);
  ofn_free(a.lpOFN);
  HEAD_FREE(a);
  return r;
}

/* ---- object properties: a property sheet of A pages -------------------- */

static PROPSHEETPAGEA *pages_a(const PROPSHEETHEADERW *w) {
  const BYTE *src = (const BYTE *)w->ppsp;
  BYTE *dst, *d;
  UINT i;
  SIZE_T total = 0;
  if (!(w->dwFlags & PSH_PROPSHEETPAGE) || !src || !w->nPages)
    return NULL;
  for (i = 0; i < w->nPages; i++)
    total += ((const PROPSHEETPAGEW *)(src + total))->dwSize;
  dst = ow_alloc(total);
  if (!dst)
    return NULL;
  ow_memcpy(dst, src, total);
  for (d = dst, i = 0; i < w->nPages; i++) {
    PROPSHEETPAGEA *p = (PROPSHEETPAGEA *)d;
    const PROPSHEETPAGEW *s = (const PROPSHEETPAGEW *)(src + (d - dst));
    if (!(s->dwFlags & PSP_DLGINDIRECT))
      p->pszTemplate = ow_ares(s->pszTemplate);
    if (s->dwFlags & PSP_USEICONID)
      p->pszIcon = ow_ares(s->pszIcon);
    p->pszTitle = (s->dwFlags & PSP_USETITLE) ? ow_ares(s->pszTitle) : NULL;
    if (s->dwSize > offsetof(PROPSHEETPAGEW, pszHeaderTitle)) {
      if (s->dwFlags & PSP_USEHEADERTITLE)
        p->pszHeaderTitle = ow_ares(s->pszHeaderTitle);
      if (s->dwFlags & PSP_USEHEADERSUBTITLE)
        p->pszHeaderSubTitle = ow_ares(s->pszHeaderSubTitle);
    }
    if (s->pfnDlgProc)
      p->pfnDlgProc = (DLGPROC)ow_wndproc_a((WNDPROC)s->pfnDlgProc);
    d += s->dwSize;
  }
  return (PROPSHEETPAGEA *)dst;
}

static void pages_free(PROPSHEETPAGEA *pages, const PROPSHEETHEADERW *w) {
  BYTE *d = (BYTE *)pages;
  UINT i;
  if (!pages)
    return;
  for (i = 0; i < w->nPages; i++) {
    PROPSHEETPAGEA *p = (PROPSHEETPAGEA *)d;
    if (!(p->dwFlags & PSP_DLGINDIRECT))
      ow_free(p->pszTemplate);
    if (p->dwFlags & PSP_USEICONID)
      ow_free(p->pszIcon);
    ow_free(p->pszTitle);
    if (p->dwSize > offsetof(PROPSHEETPAGEA, pszHeaderTitle)) {
      if (p->dwFlags & PSP_USEHEADERTITLE)
        ow_free(p->pszHeaderTitle);
      if (p->dwFlags & PSP_USEHEADERSUBTITLE)
        ow_free(p->pszHeaderSubTitle);
    }
    d += p->dwSize;
  }
  ow_free(pages);
}

static PROPSHEETHEADERA *sheet_a(const PROPSHEETHEADERW *w) {
  PROPSHEETHEADERA *a;
  DWORD size;
  if (!w)
    return NULL;
  size = w->dwSize < sizeof *a ? w->dwSize : sizeof *a;
  a = ow_alloc(sizeof *a);
  if (!a)
    return NULL;
  ow_memcpy(a, w, size);
  a->pszCaption = ow_ares(w->pszCaption);
  if (w->dwFlags & PSH_USEICONID)
    a->pszIcon = ow_ares(w->pszIcon);
  if (w->dwFlags & PSH_USEPSTARTPAGE)
    a->pStartPage = ow_ares(w->pStartPage);
  if (w->dwFlags & PSH_PROPSHEETPAGE)
    a->ppsp = pages_a(w);
  if (w->pfnCallback)
    a->pfnCallback = w->pfnCallback; /* takes no strings */
  return a;
}

static void sheet_free(PROPSHEETHEADERA *a, const PROPSHEETHEADERW *w) {
  if (!a)
    return;
  ow_free(a->pszCaption);
  if (w->dwFlags & PSH_USEICONID)
    ow_free(a->pszIcon);
  if (w->dwFlags & PSH_USEPSTARTPAGE)
    ow_free(a->pStartPage);
  if (w->dwFlags & PSH_PROPSHEETPAGE)
    pages_free((PROPSHEETPAGEA *)a->ppsp, w);
  ow_free(a);
}

UINT WINAPI OleUIObjectPropertiesW(LPOLEUIOBJECTPROPSW w) {
  OLEUIOBJECTPROPSA a;
  OLEUIGNRLPROPSA gp;
  OLEUIVIEWPROPSA vp;
  OLEUILINKPROPSA lp;
  UINT r;
  OLEDLG(OleUIObjectPropertiesA, OLEUI_ERR_STRUCTUREINVALID);
  if (!w)
    return OLEUI_ERR_STRUCTURENULL;
  ow_memcpy(&a, w, sizeof a);
  a.cbStruct = sizeof a;
  a.lpPS = sheet_a((const PROPSHEETHEADERW *)w->lpPS);
  a.lpObjInfo = obj_wrap(w->lpObjInfo);
  a.lpLinkInfo = link_wrap(w->lpLinkInfo);
  a.lpGP = NULL;
  a.lpVP = NULL;
  a.lpLP = NULL;
  if (w->lpGP) {
    ow_memcpy(&gp, w->lpGP, sizeof gp);
    gp.cbStruct = sizeof gp;
    gp.lpfnHook = hook_a(w->lpGP->lpfnHook);
    gp.lpOP = &a;
    a.lpGP = &gp;
  }
  if (w->lpVP) {
    ow_memcpy(&vp, w->lpVP, sizeof vp);
    vp.cbStruct = sizeof vp;
    vp.lpfnHook = hook_a(w->lpVP->lpfnHook);
    vp.lpOP = &a;
    a.lpVP = &vp;
  }
  if (w->lpLP) {
    ow_memcpy(&lp, w->lpLP, sizeof lp);
    lp.cbStruct = sizeof lp;
    lp.lpfnHook = hook_a(w->lpLP->lpfnHook);
    lp.lpOP = &a;
    a.lpLP = &lp;
  }
  r = pOleUIObjectPropertiesA(&a);
  w->dwFlags = a.dwFlags;
  if (w->lpGP)
    w->lpGP->dwFlags = gp.dwFlags;
  if (w->lpVP)
    w->lpVP->dwFlags = vp.dwFlags;
  if (w->lpLP)
    w->lpLP->dwFlags = lp.dwFlags;
  if (a.lpObjInfo)
    a.lpObjInfo->lpVtbl->Release(a.lpObjInfo);
  link_release(a.lpLinkInfo);
  sheet_free(a.lpPS, (const PROPSHEETHEADERW *)w->lpPS);
  return r;
}

BOOL WINAPI OleUIUpdateLinksW(LPOLEUILINKCONTAINERW c, HWND parent,
                              LPWSTR caption, int n) {
  IOleUILinkInfoA *a = link_wrap(c);
  LPSTR t = ow_a(caption);
  BOOL r;
  OW_FN(OW_OLEDLG, OleUIUpdateLinksA, r = FALSE; goto out);
  r = pOleUIUpdateLinksA((LPOLEUILINKCONTAINERA)a, parent, t, n);
out:
  link_release(a);
  ow_free(t);
  return r;
}

/* The templates that take a string; the others take none. */
int __cdecl OleUIPromptUserW(int tmpl, HWND parent, ...) {
  va_list ap;
  LPSTR s = NULL;
  int r;
  typedef int(__cdecl * prompt_fn)(int, HWND, ...);
  OW_FNT(OW_OLEDLG, prompt_fn, pprompt, "OleUIPromptUserA", return -1);
  va_start(ap, parent);
  switch (tmpl) {
  case IDD_SERVERNOTREGW:
    tmpl = IDD_SERVERNOTREGA;
    s = ow_a(va_arg(ap, LPCWSTR));
    break;
  case IDD_LINKTYPECHANGEDW:
    tmpl = IDD_LINKTYPECHANGEDA;
    s = ow_a(va_arg(ap, LPCWSTR));
    break;
  case IDD_SERVERNOTREGA:
  case IDD_LINKTYPECHANGEDA:
  case IDD_SERVERNOTFOUND:
  case IDD_UPDATELINKS:
    s = ow_a(va_arg(ap, LPCWSTR));
    break;
  }
  va_end(ap);
  r = s ? pprompt(tmpl, parent, s) : pprompt(tmpl, parent);
  ow_free(s);
  return r;
}
