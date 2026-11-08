/* The small ones: oleacc.dll (accessibility), msvfw32.dll (the MCI window
 * and file dialogs with a preview), avicap32.dll (video capture) and
 * sensapi.dll (network reachability).
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"
#include <oleacc.h>
#include <vfw.h>
#include <sensapi.h>

/* ---- oleacc ------------------------------------------------------------ */

HRESULT WINAPI CreateStdAccessibleProxyW(HWND h, LPCWSTR cls, LONG id,
                                         REFIID iid, void **out) {
  LPSTR a = ow_a(cls);
  HRESULT r;
  OW_FN(OW_OLEACC, CreateStdAccessibleProxyA, ow_free(a); return E_NOTIMPL);
  r = pCreateStdAccessibleProxyA(h, a, id, iid, out);
  ow_free(a);
  return r;
}

typedef UINT(WINAPI *text_fn)(DWORD, LPSTR, UINT);

static UINT acc_text(const char *name, DWORD v, LPWSTR buf, UINT cch) {
  char a[256];
  text_fn fn = (text_fn)ow_proc(OW_OLEACC, name);
  if (!fn) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
  }
  a[0] = 0;
  if (!fn(v, a, sizeof a))
    return 0;
  if (!buf)
    return ow_wlen_of(a, -1, CP_ACP);
  return cch ? ow_tow(a, -1, buf, cch) : 0;
}

UINT WINAPI GetRoleTextW(DWORD role, LPWSTR buf, UINT cch) {
  return acc_text("GetRoleTextA", role, buf, cch);
}

UINT WINAPI GetStateTextW(DWORD state, LPWSTR buf, UINT cch) {
  return acc_text("GetStateTextA", state, buf, cch);
}

/* ---- msvfw32 ----------------------------------------------------------- */

BOOL WINAPI GetOpenFileNamePreviewW(LPOPENFILENAMEW w) {
  OW_FN(OW_MSVFW32, GetOpenFileNamePreviewA, return FALSE);
  return ow_ofn_call(w, pGetOpenFileNamePreviewA);
}

BOOL WINAPI GetSaveFileNamePreviewW(LPOPENFILENAMEW w) {
  OW_FN(OW_MSVFW32, GetSaveFileNamePreviewA, return FALSE);
  return ow_ofn_call(w, pGetSaveFileNamePreviewA);
}

HWND WINAPIV MCIWndCreateW(HWND parent, HINSTANCE inst, DWORD style,
                           LPCWSTR file) {
  LPSTR a = ow_a(file);
  HWND h;
  typedef HWND(WINAPIV * create_fn)(HWND, HINSTANCE, DWORD, LPCSTR);
  OW_FNT(OW_MSVFW32, create_fn, pcreate, "MCIWndCreateA", ow_free(a);
         return NULL);
  h = pcreate(parent, inst, style, a);
  ow_free(a);
  if (h)
    ow_cd_mark(h, OW_CD_MCIWND);
  return h;
}

/* ---- avicap32 ------------------------------------------------------------ */

HWND WINAPI capCreateCaptureWindowW(LPCWSTR name, DWORD style, int x, int y,
                                    int cx, int cy, HWND parent, int id) {
  LPSTR a = ow_a(name);
  HWND h;
  OW_FN(OW_AVICAP32, capCreateCaptureWindowA, ow_free(a); return NULL);
  h = pcapCreateCaptureWindowA(a, style, x, y, cx, cy, parent, id);
  ow_free(a);
  if (h)
    ow_cd_mark(h, OW_CD_CAP);
  return h;
}

BOOL WINAPI capGetDriverDescriptionW(UINT i, LPWSTR name, int cname,
                                     LPWSTR ver, int cver) {
  char n[256], v[256];
  BOOL r;
  OW_FN(OW_AVICAP32, capGetDriverDescriptionA, return FALSE);
  n[0] = v[0] = 0;
  r = pcapGetDriverDescriptionA(i, n, sizeof n, v, sizeof v);
  if (r) {
    if (name && cname > 0)
      ow_tow(n, -1, name, cname);
    if (ver && cver > 0)
      ow_tow(v, -1, ver, cver);
  }
  return r;
}

/* The capture window's W error and status callbacks: kept on the window,
 * and called from A ones. */
static const char prop_err[] = "ow.caperr", prop_status[] = "ow.capstat";

static LRESULT CALLBACK cap_cb(HWND h, int id, LPCSTR s, const char *prop) {
  CAPERRORCALLBACKW cb = (CAPERRORCALLBACKW)GetPropA(h, prop);
  LPWSTR w;
  LRESULT r;
  if (!cb)
    return 0;
  w = ow_w(s);
  r = cb(h, id, w);
  ow_free(w);
  return r;
}

static LRESULT CALLBACK cap_err(HWND h, int id, LPCSTR s) {
  return cap_cb(h, id, s, prop_err);
}

static LRESULT CALLBACK cap_status(HWND h, int id, LPCSTR s) {
  return cap_cb(h, id, s, prop_status);
}

/* The messages: A forms of W ones that take a string, or return one into
 * a buffer of wParam characters. */
struct vfw_msg {
  UINT w, a;
  BYTE out;
};

static const struct vfw_msg mci_msgs[] = {
    {MCIWNDM_SENDSTRINGW, MCIWNDM_SENDSTRINGA, 0},
    {MCIWNDM_GETPOSITIONW, MCIWNDM_GETPOSITIONA, 1},
    {MCIWNDM_GETMODEW, MCIWNDM_GETMODEA, 1},
    {MCIWNDM_SETTIMEFORMATW, MCIWNDM_SETTIMEFORMATA, 0},
    {MCIWNDM_GETTIMEFORMATW, MCIWNDM_GETTIMEFORMATA, 1},
    {MCIWNDM_GETFILENAMEW, MCIWNDM_GETFILENAMEA, 1},
    {MCIWNDM_GETDEVICEW, MCIWNDM_GETDEVICEA, 1},
    {MCIWNDM_GETERRORW, MCIWNDM_GETERRORA, 1},
    {MCIWNDM_NEWW, MCIWNDM_NEWA, 0},
    {MCIWNDM_RETURNSTRINGW, MCIWNDM_RETURNSTRINGA, 1},
    {MCIWNDM_OPENW, MCIWNDM_OPENA, 0},
    {0, 0, 0}};

static const struct vfw_msg cap_msgs[] = {
    {WM_CAP_DRIVER_GET_NAMEW, WM_CAP_DRIVER_GET_NAMEA, 1},
    {WM_CAP_DRIVER_GET_VERSIONW, WM_CAP_DRIVER_GET_VERSIONA, 1},
    {WM_CAP_FILE_SET_CAPTURE_FILEW, WM_CAP_FILE_SET_CAPTURE_FILEA, 0},
    {WM_CAP_FILE_GET_CAPTURE_FILEW, WM_CAP_FILE_GET_CAPTURE_FILEA, 1},
    {WM_CAP_FILE_SAVEASW, WM_CAP_FILE_SAVEASA, 0},
    {WM_CAP_FILE_SAVEDIBW, WM_CAP_FILE_SAVEDIBA, 0},
    {WM_CAP_SET_MCI_DEVICEW, WM_CAP_SET_MCI_DEVICEA, 0},
    {WM_CAP_GET_MCI_DEVICEW, WM_CAP_GET_MCI_DEVICEA, 1},
    {WM_CAP_PAL_OPENW, WM_CAP_PAL_OPENA, 0},
    {WM_CAP_PAL_SAVEW, WM_CAP_PAL_SAVEA, 0},
    {0, 0, 0}};

BOOL ow_vfw_msg(int kind, ow_sender send, void *ctx, HWND h, UINT m,
                WPARAM wp, LPARAM lp, LRESULT *r) {
  const struct vfw_msg *t = kind == OW_CD_MCIWND ? mci_msgs : cap_msgs;
  if (kind == OW_CD_CAP &&
      (m == WM_CAP_SET_CALLBACK_ERRORW || m == WM_CAP_SET_CALLBACK_STATUSW)) {
    BOOL err = m == WM_CAP_SET_CALLBACK_ERRORW;
    const char *prop = err ? prop_err : prop_status;
    if (lp)
      SetPropA(h, prop, (HANDLE)lp);
    else
      RemovePropA(h, prop);
    *r = send(ctx, h,
              err ? WM_CAP_SET_CALLBACK_ERRORA : WM_CAP_SET_CALLBACK_STATUSA,
              wp,
              lp ? (LPARAM)(err ? (void *)cap_err : (void *)cap_status) : 0);
    return TRUE;
  }
  for (; t->w; t++)
    if (t->w == m)
      break;
  if (!t->w)
    return FALSE;
  if (!t->out) {
    LPSTR a = ow_a((LPCWSTR)lp);
    *r = send(ctx, h, t->a, wp, (LPARAM)a);
    ow_free(a);
  } else if (!lp || !wp) {
    *r = send(ctx, h, t->a, wp, lp);
  } else {
    LPSTR a = ow_alloc(wp * 2 + 1);
    if (!a) {
      *r = 0;
      return TRUE;
    }
    *r = send(ctx, h, t->a, wp * 2, (LPARAM)a);
    ow_tow(a, -1, (LPWSTR)lp, (int)wp);
    ow_free(a);
  }
  return TRUE;
}

/* ---- sensapi --------------------------------------------------------------- */

BOOL WINAPI IsDestinationReachableW(LPCWSTR dest, LPQOCINFO info) {
  LPSTR a = ow_a(dest);
  BOOL r;
  OW_FN(OW_SENSAPI, IsDestinationReachableA, ow_free(a); return FALSE);
  r = pIsDestinationReachableA(a, info);
  ow_free(a);
  return r;
}
