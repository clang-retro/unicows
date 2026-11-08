/* shell32.dll: running and finding programs, icons, file operations, the
 * folder browser and the notification area.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"
#include <shellapi.h>
#include <shlobj.h>

UINT WINAPI DragQueryFileW(HDROP drop, UINT i, LPWSTR buf, UINT cch) {
  UINT n;
  LPSTR a;
  OW_FN(OW_SHELL32, DragQueryFileA, return 0);
  if (i == 0xffffffff)
    return pDragQueryFileA(drop, i, NULL, 0);
  n = pDragQueryFileA(drop, i, NULL, 0);
  if (!n)
    return 0;
  a = ow_alloc(n + 1);
  if (!a)
    return 0;
  pDragQueryFileA(drop, i, a, n + 1);
  if (!buf)
    n = ow_wlen_of(a, -1, CP_ACP);
  else
    n = cch ? ow_tow(a, -1, buf, cch) : 0;
  ow_free(a);
  return n;
}

UINT WINAPI ExtractIconExW(LPCWSTR file, int i, HICON *large, HICON *small,
                           UINT n) {
  LPSTR a = ow_a(file);
  UINT r;
  OW_FN(OW_SHELL32, ExtractIconExA, return 0);
  r = pExtractIconExA(a, i, large, small, n);
  ow_free(a);
  return r;
}

HICON WINAPI ExtractIconW(HINSTANCE inst, LPCWSTR file, UINT i) {
  LPSTR a = ow_a(file);
  HICON r;
  OW_FN(OW_SHELL32, ExtractIconA, return NULL);
  r = pExtractIconA(inst, a, i);
  ow_free(a);
  return r;
}

HINSTANCE WINAPI FindExecutableW(LPCWSTR file, LPCWSTR dir, LPWSTR result) {
  LPSTR a = ow_a(file), d = ow_a(dir);
  char res[MAX_PATH * 2];
  HINSTANCE r;
  OW_FN(OW_SHELL32, FindExecutableA, return (HINSTANCE)SE_ERR_OOM);
  res[0] = 0;
  r = pFindExecutableA(a, d, res);
  if (result)
    ow_tow(res, -1, result, MAX_PATH);
  ow_free(a);
  ow_free(d);
  return r;
}

/* ---- the folder browser ------------------------------------------------ */

struct bff {
  BFFCALLBACK cb;
  LPARAM lp;
  HWND h;
};

static int CALLBACK bff_cb(HWND h, UINT m, LPARAM lp, LPARAM data) {
  struct bff *c = (struct bff *)data;
  int r;
  if (m == BFFM_INITIALIZED && !c->h) {
    c->h = h;
    ow_cd_mark(h, OW_CD_BFF);
  }
  if (!c->cb)
    return 0;
  if (m == BFFM_VALIDATEFAILEDA) {
    LPWSTR w = ow_w((LPCSTR)lp);
    r = c->cb(h, BFFM_VALIDATEFAILEDW, (LPARAM)w, c->lp);
    ow_free(w);
    return r;
  }
  return c->cb(h, m, lp, c->lp);
}

PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderW(LPBROWSEINFOW w) {
  BROWSEINFOA a;
  struct bff c = {0};
  char name[MAX_PATH * 2];
  PIDLIST_ABSOLUTE r;
  OW_FN(OW_SHELL32, SHBrowseForFolderA, return NULL);
  if (!w)
    return pSHBrowseForFolderA(NULL);
  a.hwndOwner = w->hwndOwner;
  a.pidlRoot = w->pidlRoot;
  name[0] = 0;
  a.pszDisplayName = w->pszDisplayName ? name : NULL;
  a.lpszTitle = ow_a(w->lpszTitle);
  a.ulFlags = w->ulFlags;
  c.cb = w->lpfn;
  c.lp = w->lParam;
  a.lpfn = bff_cb;
  a.lParam = (LPARAM)&c;
  a.iImage = 0;
  r = pSHBrowseForFolderA(&a);
  if (c.h)
    ow_cd_unmark(c.h);
  if (w->pszDisplayName)
    ow_tow(name, -1, w->pszDisplayName, MAX_PATH);
  w->iImage = a.iImage;
  ow_free(a.lpszTitle);
  return r;
}

BOOL WINAPI SHGetPathFromIDListW(PCIDLIST_ABSOLUTE pidl, LPWSTR path) {
  char a[MAX_PATH * 2];
  BOOL r;
  OW_FN(OW_SHELL32, SHGetPathFromIDListA, return FALSE);
  a[0] = 0;
  r = pSHGetPathFromIDListA(pidl, a);
  if (path)
    ow_tow(a, -1, path, MAX_PATH);
  return r;
}

/* Paths and printers in W form become A ones, which every shell knows. */
void WINAPI SHChangeNotify(LONG event, UINT flags, LPCVOID a1, LPCVOID a2) {
  UINT type = flags & SHCNF_TYPE;
  OW_FN(OW_SHELL32, SHChangeNotify, return);
  if (type == SHCNF_PATHW || type == SHCNF_PRINTERW) {
    LPSTR x = ow_a(a1), y = ow_a(a2);
    pSHChangeNotify(event,
                    (flags & ~SHCNF_TYPE) |
                        (type == SHCNF_PATHW ? SHCNF_PATHA : SHCNF_PRINTERA),
                    x, y);
    ow_free(x);
    ow_free(y);
    return;
  }
  pSHChangeNotify(event, flags, a1, a2);
}

int WINAPI SHFileOperationW(LPSHFILEOPSTRUCTW w) {
  SHFILEOPSTRUCTA a;
  int r;
  OW_FN(OW_SHELL32, SHFileOperationA, return ERROR_CALL_NOT_IMPLEMENTED);
  if (!w)
    return pSHFileOperationA(NULL);
  a.hwnd = w->hwnd;
  a.wFunc = w->wFunc;
  a.pFrom = ow_alist(w->pFrom);
  a.pTo = ow_alist(w->pTo);
  a.fFlags = w->fFlags;
  a.fAnyOperationsAborted = FALSE;
  a.hNameMappings = NULL;
  a.lpszProgressTitle = ow_a(w->lpszProgressTitle);
  if ((w->pFrom && !a.pFrom) || (w->pTo && !a.pTo))
    r = ERROR_NOT_ENOUGH_MEMORY;
  else
    r = pSHFileOperationA(&a);
  w->fAnyOperationsAborted = a.fAnyOperationsAborted;
  /* The mappings hold A names; they are freed with SHFreeNameMappings,
   * which takes either kind. */
  w->hNameMappings = a.hNameMappings;
  ow_free(a.pFrom);
  ow_free(a.pTo);
  ow_free(a.lpszProgressTitle);
  return r;
}

DWORD_PTR WINAPI SHGetFileInfoW(LPCWSTR path, DWORD attrs, SHFILEINFOW *w,
                                UINT cb, UINT flags) {
  SHFILEINFOA a;
  LPSTR p;
  DWORD_PTR r;
  OW_FN(OW_SHELL32, SHGetFileInfoA, return 0);
  p = (flags & SHGFI_PIDL) ? (LPSTR)path : ow_a(path);
  ow_memset(&a, 0, sizeof a);
  if (w && cb >= sizeof *w)
    a.dwAttributes = w->dwAttributes;
  r = pSHGetFileInfoA(p, attrs, w ? &a : NULL, w ? sizeof a : 0, flags);
  if (w && cb >= sizeof *w) {
    w->hIcon = a.hIcon;
    w->iIcon = a.iIcon;
    w->dwAttributes = a.dwAttributes;
    ow_tow(a.szDisplayName, -1, w->szDisplayName, MAX_PATH);
    ow_tow(a.szTypeName, -1, w->szTypeName, 80);
  }
  if (!(flags & SHGFI_PIDL))
    ow_free(p);
  return r;
}

BOOL WINAPI SHGetNewLinkInfoW(LPCWSTR to, LPCWSTR dir, LPWSTR name,
                              BOOL *copy, UINT flags) {
  LPSTR t, d = ow_a(dir);
  char n[MAX_PATH * 2];
  BOOL r;
  OW_FN(OW_SHELL32, SHGetNewLinkInfoA, return FALSE);
  t = (flags & SHGNLI_PIDL) ? (LPSTR)to : ow_a(to);
  n[0] = 0;
  r = pSHGetNewLinkInfoA(t, d, n, copy, flags);
  if (name)
    ow_tow(n, -1, name, MAX_PATH);
  if (!(flags & SHGNLI_PIDL))
    ow_free(t);
  ow_free(d);
  return r;
}

int WINAPI ShellAboutW(HWND h, LPCWSTR app, LPCWSTR other, HICON icon) {
  LPSTR a = ow_a(app), o = ow_a(other);
  int r;
  OW_FN(OW_SHELL32, ShellAboutA, return 0);
  r = pShellAboutA(h, a, o, icon);
  ow_free(a);
  ow_free(o);
  return r;
}

BOOL WINAPI ShellExecuteExW(SHELLEXECUTEINFOW *w) {
  SHELLEXECUTEINFOA a;
  BOOL r;
  OW_FN(OW_SHELL32, ShellExecuteExA, return FALSE);
  if (!w || w->cbSize != sizeof *w) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  ow_memcpy(&a, w, sizeof a); /* the same layout, with A strings */
  a.cbSize = sizeof a;
  a.lpVerb = ow_a(w->lpVerb);
  a.lpFile = ow_a(w->lpFile);
  a.lpParameters = ow_a(w->lpParameters);
  a.lpDirectory = ow_a(w->lpDirectory);
  a.lpClass = (w->fMask & SEE_MASK_CLASSNAME) ? ow_a(w->lpClass) : NULL;
  r = pShellExecuteExA(&a);
  w->hInstApp = a.hInstApp;
  w->hProcess = a.hProcess;
  ow_free(a.lpVerb);
  ow_free(a.lpFile);
  ow_free(a.lpParameters);
  ow_free(a.lpDirectory);
  ow_free(a.lpClass);
  return r;
}

HINSTANCE WINAPI ShellExecuteW(HWND h, LPCWSTR verb, LPCWSTR file,
                               LPCWSTR params, LPCWSTR dir, INT show) {
  LPSTR v = ow_a(verb), f = ow_a(file), p = ow_a(params), d = ow_a(dir);
  HINSTANCE r;
  OW_FN(OW_SHELL32, ShellExecuteA, return (HINSTANCE)SE_ERR_OOM);
  r = pShellExecuteA(h, v, f, p, d, show);
  ow_free(v);
  ow_free(f);
  ow_free(p);
  ow_free(d);
  return r;
}

/* The notification area: shells before 5.0 take only the first form of
 * NOTIFYICONDATA, with a 64-character tip. */
BOOL WINAPI Shell_NotifyIconW(DWORD msg, PNOTIFYICONDATAW w) {
  NOTIFYICONDATAA a;
  BOOL r;
  OW_FN(OW_SHELL32, Shell_NotifyIconA, return FALSE);
  if (!w)
    return FALSE;
  ow_memset(&a, 0, sizeof a);
  a.hWnd = w->hWnd;
  a.uID = w->uID;
  a.uFlags = w->uFlags;
  a.uCallbackMessage = w->uCallbackMessage;
  a.hIcon = w->hIcon;
  if (w->cbSize <= NOTIFYICONDATAW_V1_SIZE) {
    a.cbSize = NOTIFYICONDATAA_V1_SIZE;
    if (w->uFlags & NIF_TIP)
      ow_toa(w->szTip, ow_wcsnlen(w->szTip, 64), a.szTip, 64);
    return pShell_NotifyIconA(msg, &a);
  }
  a.cbSize = w->cbSize >= sizeof *w ? sizeof a : NOTIFYICONDATAA_V2_SIZE;
  if (w->uFlags & NIF_TIP)
    ow_toa(w->szTip, ow_wcsnlen(w->szTip, 128), a.szTip, 128);
  a.dwState = w->dwState;
  a.dwStateMask = w->dwStateMask;
  if (w->uFlags & NIF_INFO) {
    ow_toa(w->szInfo, ow_wcsnlen(w->szInfo, 256), a.szInfo, 256);
    ow_toa(w->szInfoTitle, ow_wcsnlen(w->szInfoTitle, 64), a.szInfoTitle,
           64);
  }
  a.uTimeout = w->uTimeout;
  a.dwInfoFlags = w->dwInfoFlags;
  if (w->cbSize >= sizeof *w)
    a.guidItem = w->guidItem;
  r = pShell_NotifyIconA(msg, &a);
  if (!r && msg != NIM_SETVERSION) {
    /* An older shell: the first form, without the balloon. */
    a.cbSize = NOTIFYICONDATAA_V1_SIZE;
    a.uFlags &= NIF_MESSAGE | NIF_ICON | NIF_TIP;
    a.szTip[63] = 0;
    if (a.uFlags || msg == NIM_DELETE)
      r = pShell_NotifyIconA(msg, &a);
  }
  return r;
}
