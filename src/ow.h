/* Shared declarations for opencow, an implementation of the Microsoft Layer
 * for Unicode (unicows.dll) for Windows 95, 98 and ME.
 *
 * Each W function converts its strings to the ANSI (or OEM) code page,
 * calls the A function that Windows 9x has, and converts the results back.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef OPENCOW_OW_H
#define OPENCOW_OW_H

/* The DLL defines functions with the names that the Windows headers
 * declare, so nothing may be declared dllimport: calls to the system go
 * through the import thunks instead. */
#define DECLSPEC_IMPORT
#define _KERNEL32_
#define _USER32_
#define _GDI32_
#define _ADVAPI32_
#define _COMDLG32_
#define _SHELL32_
#define _WINMM_
#ifndef WINVER
#define WINVER 0x0500
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0500
#endif
#define NOMINMAX
#define SECURITY_WIN32
#include <windows.h>
#include <stdarg.h>
#include <stddef.h>

/* A system function that opencow also exports under the same name (an A
 * function that it changes, or a W function that Windows 9x implements):
 * REAL(name) calls the one in the system DLL, through its import slot. */
#define OW_REAL_DECL(ret, name, args, bytes)                                 \
  extern ret(WINAPI *const ow_real_##name) args __asm__("__imp__ow_" #name \
                                                         "@" #bytes)
#define REAL(name) (*ow_real_##name)

/* ---- memory -------------------------------------------------------- */

void *ow_alloc(SIZE_T n);   /* zeroed; NULL when out of memory */
void *ow_realloc(void *p, SIZE_T n);
void ow_free(const void *p); /* ignores NULL and MAKEINTRESOURCE values */

/* ---- code pages ---------------------------------------------------- */

UINT ow_fcp(void); /* the code page of the file functions (ANSI or OEM) */

int ow_mb2wc(UINT cp, DWORD flags, LPCSTR s, int n, LPWSTR w, int cch);
int ow_wc2mb(UINT cp, DWORD flags, LPCWSTR w, int n, LPSTR s, int cb);

/* ---- string conversion ---------------------------------------------- */

/* Heap copies in the other encoding. NULL gives NULL; _res variants pass
 * MAKEINTRESOURCE / MAKEINTATOM values through unchanged. Free them with
 * ow_free. The counted forms convert n characters (-1: up to the NUL) and
 * always add a NUL; *outn, when given, receives the length without it. */
LPSTR ow_a(LPCWSTR w);
LPSTR ow_acp(LPCWSTR w, UINT cp);
LPSTR ow_ares(LPCWSTR w);
LPSTR ow_an(LPCWSTR w, int n, int *outn);
LPSTR ow_ancp(LPCWSTR w, int n, int *outn, UINT cp);
LPWSTR ow_w(LPCSTR a);
LPWSTR ow_wcp(LPCSTR a, UINT cp);
LPWSTR ow_wres(LPCSTR a);
LPWSTR ow_wn(LPCSTR a, int n, int *outn);
LPWSTR ow_wncp(LPCSTR a, int n, int *outn, UINT cp);
/* Lists of NUL-terminated strings ending with an empty one. */
LPSTR ow_alist(LPCWSTR w);
LPWSTR ow_wlist(LPCSTR a);
int ow_alist_len(LPCSTR a);  /* bytes including the final NUL */
int ow_wlist_len(LPCWSTR w); /* WCHARs including the final NUL */

/* Converts the NUL-terminated (n = -1) or counted ANSI text a into the
 * buffer w of cch WCHARs, truncating it and always NUL-terminating when
 * cch > 0. Returns the WCHARs written, without the NUL. */
int ow_tow(LPCSTR a, int n, LPWSTR w, int cch);
int ow_towcp(LPCSTR a, int n, LPWSTR w, int cch, UINT cp);
/* The same into an ANSI buffer of cb bytes, not splitting DBCS pairs. */
int ow_toa(LPCWSTR w, int n, LPSTR a, int cb);
/* Length in WCHARs of ANSI text (n = -1: NUL-terminated), without NUL. */
int ow_wlen_of(LPCSTR a, int n, UINT cp);

/* For functions that return the length of their result without the NUL,
 * or the size they need with it when the buffer is too small (such as
 * GetCurrentDirectory): converts the ANSI result a into buf (cch WCHARs).
 * Returns 0 when a is NULL. */
DWORD ow_ret_str(LPCSTR a, LPWSTR buf, DWORD cch, UINT cp);
/* Returns the ANSI text in a heap buffer from fn, which takes a buffer and
 * its size in bytes and returns the length it wrote or the size it needs,
 * the way GetCurrentDirectoryA does. NULL on failure (the error is fn's). */
typedef DWORD (*ow_getter)(void *ctx, LPSTR buf, DWORD size);
LPSTR ow_get(ow_getter fn, void *ctx);

/* ---- wide string helpers (no C library) ------------------------------- */

int ow_wcslen(LPCWSTR s);
int ow_wcsnlen(LPCWSTR s, int max);
LPWSTR ow_wcscpy(LPWSTR d, LPCWSTR s);
int ow_wcscmp(LPCWSTR a, LPCWSTR b);
int ow_strlen(LPCSTR s);
int ow_wcsicmp_ascii(LPCWSTR a, LPCSTR b); /* case-blind, ASCII */
void *ow_memcpy(void *d, const void *s, SIZE_T n);
void *ow_memset(void *d, int c, SIZE_T n);
void ow_wcsncpyz(LPWSTR d, LPCWSTR s, int cch); /* truncates, adds NUL */

/* ---- DLLs that are loaded when first used ------------------------------ */

enum {
  OW_COMDLG32,
  OW_WINSPOOL,
  OW_SHELL32,
  OW_WINMM,
  OW_VERSION,
  OW_MPR,
  OW_OLEACC,
  OW_OLEDLG,
  OW_RASAPI32,
  OW_MSVFW32,
  OW_SENSAPI,
  OW_AVICAP32,
  OW_SECUR32,
  OW_OLE32,
  OW_KERNEL32,
  OW_USER32,
  OW_GDI32,
  OW_ADVAPI32,
  OW_IMM32,
  OW_NDLLS
};
FARPROC ow_proc(int dll, const char *name);

/* Declares p<name>, the function name of an optional DLL with the type of
 * its declaration, loading it on first use; runs fail (after setting
 * ERROR_CALL_NOT_IMPLEMENTED) when the DLL or function is missing. */
#define OW_FN(dll, name, fail)                                              \
  static __typeof__(&name) p##name;                                          \
  if (!p##name && !(p##name = (__typeof__(&name))ow_proc(dll, #name))) {     \
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);                                \
    fail;                                                                    \
  }
/* The same for a function that the headers do not declare. */
#define OW_FNT(dll, type, var, name, fail)                                  \
  static type var;                                                           \
  if (!var && !(var = (type)ow_proc(dll, name))) {                           \
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);                                \
    fail;                                                                    \
  }

/* ---- per-thread state ------------------------------------------------ */

/* The W callbacks of the Enum functions whose A callbacks take no
 * context, and the DBCS lead bytes of character messages. */
struct ow_thread {
  void *cb;        /* the caller's W callback */
  LPARAM lp;       /* its context */
  BOOL stop;       /* the callback returned FALSE */
  int extra;       /* what the enumeration asked for (calendar, flags) */
  BYTE lead_in;    /* lead byte of a WM_CHAR pair being received */
  BYTE lead_peek;  /* lead byte held back by PeekMessageW */
  MSG held;        /* a message GetMessageW combined, waiting to be read */
  BOOL have_held;
  void *dlg;       /* the common dialog being shown (comdlg32.c) */
};
struct ow_thread *ow_thread(void); /* never NULL */

/* ---- Unicode character data (unicode.c) ------------------------------- */

WCHAR ow_upper(WCHAR c);
WCHAR ow_lower(WCHAR c);
WORD ow_ctype1(WCHAR c);
WORD ow_ctype2(WCHAR c);
WORD ow_ctype3(WCHAR c);

/* ---- shared by the window functions ------------------------------------ */

extern HINSTANCE ow_self;
extern DWORD ow_tls;

/* Converts a DEVMODEW to a DEVMODEA and back (heap; NULL gives NULL). */
LPDEVMODEA ow_devmode_a(const DEVMODEW *w);
LPDEVMODEW ow_devmode_w(const DEVMODEA *a);
void ow_devmode_to_w(const DEVMODEA *a, DEVMODEW *w); /* in place */
void ow_logfont_a(const LOGFONTW *w, LOGFONTA *a);
void ow_logfont_w(const LOGFONTA *a, LOGFONTW *w);
void ow_textmetric_w(const TEXTMETRICA *a, TEXTMETRICW *w);

/* Window procedures (window.c). */
WNDPROC ow_wndproc_a(WNDPROC wproc);  /* an A procedure that calls wproc */
WNDPROC ow_wndproc_w(WNDPROC aproc);  /* the W procedure behind a thunk */
BOOL ow_is_thunk(WNDPROC p);
LRESULT ow_call_w(WNDPROC wproc, HWND h, UINT m, WPARAM wp, LPARAM lp);
void ow_msg_a2w(MSG *m); /* WM_CHAR and the like in a message, in place */
void ow_msg_w2a(const MSG *w, MSG *a);

/* Sends a W message to something that takes A messages: send() is called
 * with the converted message and ctx. */
typedef LRESULT (*ow_sender)(void *ctx, HWND, UINT, WPARAM, LPARAM);
LRESULT ow_send_w(ow_sender send, void *ctx, HWND h, UINT m, WPARAM wp,
                  LPARAM lp);
/* The same for a message that may be handled after the call returns
 * (PostMessage and the like): only values, not pointers, are converted. */
LRESULT ow_post_w(ow_sender send, void *ctx, HWND h, UINT m, WPARAM wp,
                  LPARAM lp);
void ow_window_init(void);
/* A heap W copy of an A string that stays until the DLL is unloaded, for
 * results that must outlive the call (class menu names). */
LPCWSTR ow_intern_w(LPCSTR a);

/* Common dialogs (comdlg32.c). ow_find_msg is the FINDMSGSTRING message
 * once a find dialog has been made; ow_find_w gives the W FINDREPLACE for
 * the A one in its lParam. ow_comdlg_open counts the dialogs that take
 * messages with structures (CDM_GETFILEPATH, WM_CHOOSEFONT_GETLOGFONT);
 * ow_comdlg_msg sends a W one of those to h, returning FALSE when h is not
 * such a dialog. */
extern UINT ow_find_msg;
LPARAM ow_find_w(LPARAM lp);
extern LONG ow_comdlg_open;
void ow_comdlg_init(void);
enum { OW_CD_OFN = 1, OW_CD_FONT, OW_CD_BFF, OW_CD_MCIWND, OW_CD_CAP };
void ow_cd_mark(HWND h, int kind);
void ow_cd_unmark(HWND h);
/* The W messages of MCI and capture windows (misc.c). */
BOOL ow_vfw_msg(int kind, ow_sender send, void *ctx, HWND h, UINT m,
                WPARAM wp, LPARAM lp, LRESULT *r);
/* Shows an open or save dialog of fn (GetOpenFileNameA and the like) for
 * a W OPENFILENAME. */
typedef BOOL(WINAPI *ow_ofn_fn)(LPOPENFILENAMEA);
BOOL ow_ofn_call(LPOPENFILENAMEW w, ow_ofn_fn fn);
BOOL ow_comdlg_msg(ow_sender send, void *ctx, HWND h, UINT m, WPARAM wp,
                   LPARAM lp, LRESULT *r);

/* Structures whose strings follow them in the same buffer (winspool.c):
 * the offsets of their string, string list and DEVMODE fields, counted in
 * pointers. ow_info_w converts count A structures into buf (cb bytes),
 * giving the size needed in *need; ow_info_a makes a heap A copy of one W
 * structure. */
#define OW_END -1
#define OW_OFF(t, f) ((signed char)(offsetof(t, f) / sizeof(void *)))

struct ow_layout {
  DWORD size;
  signed char str[16];  /* string fields, ending with OW_END */
  signed char list[3];  /* lists of strings ending with an empty one */
  signed char dm;       /* a DEVMODE, or OW_END */
  signed char sd;       /* a security descriptor (not kept), or OW_END */
};

BOOL ow_info_w(const struct ow_layout *l, const BYTE *a, DWORD count,
               BYTE *buf, DWORD cb, DWORD *need);
BYTE *ow_info_a(const struct ow_layout *l, const BYTE *w);

#endif
