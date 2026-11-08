/* kernel32: files and directories, processes, modules, environment,
 * profiles (.ini files), atoms, synchronization objects, pipes and
 * communication ports.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"

/* ---- helpers --------------------------------------------------------- */

/* Calls an A function that fills a buffer and returns the length or the
 * size it needs (GetCurrentDirectoryA and friends) through ow_get. */
struct get1 {
  DWORD(WINAPI *fn)(DWORD, LPSTR);
};
static DWORD call_get1(void *ctx, LPSTR buf, DWORD size) {
  return ((struct get1 *)ctx)->fn(size, buf);
}
struct getuint {
  UINT(WINAPI *fn)(LPSTR, UINT);
};
static DWORD call_getuint(void *ctx, LPSTR buf, DWORD size) {
  return ((struct getuint *)ctx)->fn(buf, size);
}

static DWORD ret_get1(DWORD(WINAPI *fn)(DWORD, LPSTR), LPWSTR buf,
                      DWORD cch, UINT cp) {
  struct get1 g = {fn};
  LPSTR a = ow_get(call_get1, &g);
  DWORD r = ow_ret_str(a, buf, cch, cp);
  ow_free(a);
  return r;
}

static UINT ret_getuint(UINT(WINAPI *fn)(LPSTR, UINT), LPWSTR buf, UINT cch,
                        UINT cp) {
  struct getuint g = {fn};
  LPSTR a = ow_get(call_getuint, &g);
  UINT r = ow_ret_str(a, buf, cch, cp);
  ow_free(a);
  return r;
}

/* Where the file part of a W path is, given where it is in the A one. */
static LPWSTR file_part(LPCSTR a, LPCSTR apart, LPWSTR w, UINT cp) {
  if (!apart)
    return NULL;
  return w + ow_wlen_of(a, (int)(apart - a), cp);
}

static void find_data_w(const WIN32_FIND_DATAA *a, LPWIN32_FIND_DATAW w) {
  UINT cp = ow_fcp();
  w->dwFileAttributes = a->dwFileAttributes;
  w->ftCreationTime = a->ftCreationTime;
  w->ftLastAccessTime = a->ftLastAccessTime;
  w->ftLastWriteTime = a->ftLastWriteTime;
  w->nFileSizeHigh = a->nFileSizeHigh;
  w->nFileSizeLow = a->nFileSizeLow;
  w->dwReserved0 = a->dwReserved0;
  w->dwReserved1 = a->dwReserved1;
  ow_towcp(a->cFileName, -1, w->cFileName, MAX_PATH, cp);
  ow_towcp(a->cAlternateFileName, -1, w->cAlternateFileName, 14, cp);
}

#define FA(var, w) LPSTR var = ow_acp(w, ow_fcp())

/* ---- files and directories ------------------------------------------- */

HANDLE WINAPI CreateFileW(LPCWSTR name, DWORD access, DWORD share,
                          LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags,
                          HANDLE tmpl) {
  FA(a, name);
  HANDLE h = CreateFileA(a, access, share, sa, disp, flags, tmpl);
  ow_free(a);
  return h;
}

BOOL WINAPI DeleteFileW(LPCWSTR name) {
  FA(a, name);
  BOOL r = DeleteFileA(a);
  ow_free(a);
  return r;
}

BOOL WINAPI CopyFileW(LPCWSTR from, LPCWSTR to, BOOL fail) {
  FA(a, from);
  FA(b, to);
  BOOL r = CopyFileA(a, b, fail);
  ow_free(a);
  ow_free(b);
  return r;
}

/* Windows 9x has no CopyFileEx: copy the file here, calling the progress
 * routine after each block. */
BOOL WINAPI CopyFileExW(LPCWSTR from, LPCWSTR to,
                        LPPROGRESS_ROUTINE progress, LPVOID data,
                        LPBOOL cancel, DWORD flags) {
  HANDLE in, out;
  LARGE_INTEGER total, done;
  BY_HANDLE_FILE_INFORMATION info;
  DWORD n, w;
  BYTE *buf;
  BOOL ok = FALSE;

  if (!progress && !cancel)
    return CopyFileW(from, to, (flags & COPY_FILE_FAIL_IF_EXISTS) != 0);
  in = CreateFileW(from, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                   FILE_FLAG_SEQUENTIAL_SCAN, NULL);
  if (in == INVALID_HANDLE_VALUE)
    return FALSE;
  if (!GetFileInformationByHandle(in, &info)) {
    CloseHandle(in);
    return FALSE;
  }
  out = CreateFileW(to, GENERIC_WRITE, 0, NULL,
                    (flags & COPY_FILE_FAIL_IF_EXISTS) ? CREATE_NEW
                                                       : CREATE_ALWAYS,
                    info.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY, NULL);
  if (out == INVALID_HANDLE_VALUE) {
    CloseHandle(in);
    return FALSE;
  }
  buf = ow_alloc(65536);
  if (!buf) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    goto done;
  }
  total.LowPart = info.nFileSizeLow;
  total.HighPart = (LONG)info.nFileSizeHigh;
  done.QuadPart = 0;
  if (progress) {
    DWORD r = progress(total, done, total, done, 1, CALLBACK_STREAM_SWITCH,
                       in, out, data);
    if (r == PROGRESS_CANCEL || r == PROGRESS_STOP)
      goto cancelled;
    if (r == PROGRESS_QUIET)
      progress = NULL;
  }
  for (;;) {
    if (cancel && *cancel)
      goto cancelled;
    if (!ReadFile(in, buf, 65536, &n, NULL))
      goto done;
    if (!n)
      break;
    if (!WriteFile(out, buf, n, &w, NULL) || w != n)
      goto done;
    done.QuadPart += n;
    if (progress) {
      DWORD r = progress(total, done, total, done, 1, CALLBACK_CHUNK_FINISHED,
                         in, out, data);
      if (r == PROGRESS_CANCEL || r == PROGRESS_STOP)
        goto cancelled;
      if (r == PROGRESS_QUIET)
        progress = NULL;
    }
  }
  SetFileTime(out, &info.ftCreationTime, &info.ftLastAccessTime,
              &info.ftLastWriteTime);
  ok = TRUE;
  goto done;
cancelled:
  CloseHandle(out);
  out = INVALID_HANDLE_VALUE;
  DeleteFileW(to);
  SetLastError(ERROR_REQUEST_ABORTED);
done:
  ow_free(buf);
  if (out != INVALID_HANDLE_VALUE) {
    CloseHandle(out);
    if (!ok)
      DeleteFileW(to);
  }
  CloseHandle(in);
  if (ok)
    SetFileAttributesW(to, info.dwFileAttributes);
  return ok;
}

BOOL WINAPI MoveFileW(LPCWSTR from, LPCWSTR to) {
  FA(a, from);
  FA(b, to);
  BOOL r = MoveFileA(a, b);
  ow_free(a);
  ow_free(b);
  return r;
}

BOOL WINAPI CreateDirectoryW(LPCWSTR name, LPSECURITY_ATTRIBUTES sa) {
  FA(a, name);
  BOOL r = CreateDirectoryA(a, sa);
  ow_free(a);
  return r;
}

BOOL WINAPI CreateDirectoryExW(LPCWSTR tmpl, LPCWSTR name,
                               LPSECURITY_ATTRIBUTES sa) {
  FA(a, tmpl);
  FA(b, name);
  BOOL r = CreateDirectoryExA(a, b, sa);
  ow_free(a);
  ow_free(b);
  return r;
}

BOOL WINAPI RemoveDirectoryW(LPCWSTR name) {
  FA(a, name);
  BOOL r = RemoveDirectoryA(a);
  ow_free(a);
  return r;
}

DWORD WINAPI GetFileAttributesW(LPCWSTR name) {
  FA(a, name);
  DWORD r = GetFileAttributesA(a);
  ow_free(a);
  return r;
}

BOOL WINAPI SetFileAttributesW(LPCWSTR name, DWORD attr) {
  FA(a, name);
  BOOL r = SetFileAttributesA(a, attr);
  ow_free(a);
  return r;
}

/* Windows 95 has no GetFileAttributesEx: FindFirstFile gives the same. */
BOOL WINAPI GetFileAttributesExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level,
                                 LPVOID info) {
  LPSTR a;
  BOOL r;
  WIN32_FIND_DATAA fd;
  HANDLE h;
  WIN32_FILE_ATTRIBUTE_DATA *d = info;
  OW_FN(OW_KERNEL32, GetFileAttributesExA, goto emulate);
  a = ow_acp(name, ow_fcp());
  r = pGetFileAttributesExA(a, level, info);
  ow_free(a);
  return r;
emulate:
  if (level != GetFileExInfoStandard || !d || !name) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  for (const WCHAR *p = name; *p; p++) {
    if (*p == '*' || *p == '?') {
      SetLastError(ERROR_INVALID_NAME);
      return FALSE;
    }
  }
  a = ow_acp(name, ow_fcp());
  h = FindFirstFileA(a, &fd);
  if (h == INVALID_HANDLE_VALUE) {
    /* A root directory has no entry of its own. */
    DWORD attr = GetFileAttributesA(a);
    ow_free(a);
    if (attr == INVALID_FILE_ATTRIBUTES)
      return FALSE;
    ow_memset(d, 0, sizeof *d);
    d->dwFileAttributes = attr;
    return TRUE;
  }
  ow_free(a);
  FindClose(h);
  d->dwFileAttributes = fd.dwFileAttributes;
  d->ftCreationTime = fd.ftCreationTime;
  d->ftLastAccessTime = fd.ftLastAccessTime;
  d->ftLastWriteTime = fd.ftLastWriteTime;
  d->nFileSizeHigh = fd.nFileSizeHigh;
  d->nFileSizeLow = fd.nFileSizeLow;
  return TRUE;
}

HANDLE WINAPI FindFirstFileW(LPCWSTR name, LPWIN32_FIND_DATAW w) {
  WIN32_FIND_DATAA fd;
  FA(a, name);
  HANDLE h = FindFirstFileA(a, &fd);
  ow_free(a);
  if (h != INVALID_HANDLE_VALUE)
    find_data_w(&fd, w);
  return h;
}

BOOL WINAPI FindNextFileW(HANDLE h, LPWIN32_FIND_DATAW w) {
  WIN32_FIND_DATAA fd;
  if (!FindNextFileA(h, &fd))
    return FALSE;
  find_data_w(&fd, w);
  return TRUE;
}

HANDLE WINAPI FindFirstChangeNotificationW(LPCWSTR name, BOOL sub,
                                           DWORD filter) {
  FA(a, name);
  HANDLE h = FindFirstChangeNotificationA(a, sub, filter);
  ow_free(a);
  return h;
}

DWORD WINAPI GetCurrentDirectoryW(DWORD cch, LPWSTR buf) {
  return ret_get1(GetCurrentDirectoryA, buf, cch, ow_fcp());
}

BOOL WINAPI SetCurrentDirectoryW(LPCWSTR name) {
  FA(a, name);
  BOOL r = SetCurrentDirectoryA(a);
  ow_free(a);
  return r;
}

DWORD WINAPI GetTempPathW(DWORD cch, LPWSTR buf) {
  return ret_get1(GetTempPathA, buf, cch, ow_fcp());
}

UINT WINAPI GetTempFileNameW(LPCWSTR path, LPCWSTR prefix, UINT unique,
                             LPWSTR out) {
  char buf[MAX_PATH * 2];
  UINT cp = ow_fcp();
  LPSTR a = ow_acp(path, cp), b = ow_acp(prefix, cp);
  UINT r = GetTempFileNameA(a, b, unique, buf);
  ow_free(a);
  ow_free(b);
  if (r && out)
    ow_towcp(buf, -1, out, MAX_PATH, cp);
  return r;
}

UINT WINAPI GetWindowsDirectoryW(LPWSTR buf, UINT cch) {
  return ret_getuint(GetWindowsDirectoryA, buf, cch, ow_fcp());
}

UINT WINAPI GetSystemWindowsDirectoryW(LPWSTR buf, UINT cch) {
  /* Only Terminal Server tells the two apart. */
  return ret_getuint(GetWindowsDirectoryA, buf, cch, ow_fcp());
}

UINT WINAPI GetSystemDirectoryW(LPWSTR buf, UINT cch) {
  return ret_getuint(GetSystemDirectoryA, buf, cch, ow_fcp());
}

DWORD WINAPI GetFullPathNameW(LPCWSTR name, DWORD cch, LPWSTR buf,
                              LPWSTR *part) {
  char out[MAX_PATH * 2];
  LPSTR apart = NULL;
  UINT cp = ow_fcp();
  LPSTR a = ow_acp(name, cp);
  DWORD r = GetFullPathNameA(a, sizeof out, out, &apart);
  ow_free(a);
  if (part)
    *part = NULL;
  if (!r)
    return 0;
  if (r >= sizeof out) {
    SetLastError(ERROR_FILENAME_EXCED_RANGE);
    return 0;
  }
  r = ow_ret_str(out, buf, cch, cp);
  if (r < cch && part)
    *part = file_part(out, apart, buf, cp);
  return r;
}

struct pathget {
  DWORD(WINAPI *fn)(LPCSTR, LPSTR, DWORD);
  LPCSTR in;
};
static DWORD call_pathget(void *ctx, LPSTR buf, DWORD size) {
  struct pathget *g = ctx;
  return g->fn(g->in, buf, size);
}

DWORD WINAPI GetShortPathNameW(LPCWSTR name, LPWSTR buf, DWORD cch) {
  UINT cp = ow_fcp();
  struct pathget g = {GetShortPathNameA, ow_acp(name, cp)};
  LPSTR a = ow_get(call_pathget, &g);
  DWORD r = ow_ret_str(a, buf, cch, cp);
  ow_free(a);
  ow_free(g.in);
  return r;
}

/* Windows 95 has no GetLongPathName: look up each part of the path. */
static DWORD WINAPI long_path_a(LPCSTR in, LPSTR out, DWORD size) {
  char res[MAX_PATH * 2];
  int n = 0, i = 0, len = ow_strlen(in);
  WIN32_FIND_DATAA fd;
  if (GetFileAttributesA(in) == INVALID_FILE_ATTRIBUTES)
    return 0;
  /* Keep the root: "C:\", "\\server\share\" or "\". */
  if (len >= 2 && in[1] == ':') {
    i = 2;
    if (in[2] == '\\' || in[2] == '/')
      i = 3;
  } else if (len >= 2 && (in[0] == '\\' || in[0] == '/') &&
             (in[1] == '\\' || in[1] == '/')) {
    int parts = 0;
    for (i = 2; i < len && parts < 2; i++)
      if (in[i] == '\\' || in[i] == '/')
        parts++;
  } else if (len && (in[0] == '\\' || in[0] == '/')) {
    i = 1;
  }
  ow_memcpy(res, in, i);
  n = i;
  while (i < len) {
    int start = i, j;
    HANDLE h;
    while (i < len && in[i] != '\\' && in[i] != '/') {
      if (IsDBCSLeadByte((BYTE)in[i]) && i + 1 < len)
        i++;
      i++;
    }
    if (n + (i - start) + 1 >= (int)sizeof res)
      return 0;
    ow_memcpy(res + n, in + start, i - start);
    res[n + (i - start)] = 0;
    if (!(i - start == 1 && in[start] == '.') &&
        !(i - start == 2 && in[start] == '.' && in[start + 1] == '.') &&
        (h = FindFirstFileA(res, &fd)) != INVALID_HANDLE_VALUE) {
      FindClose(h);
      j = ow_strlen(fd.cFileName);
      if (n + j + 2 >= (int)sizeof res)
        return 0;
      ow_memcpy(res + n, fd.cFileName, j);
      n += j;
    } else {
      n += i - start;
    }
    if (i < len) {
      res[n++] = in[i];
      i++;
    }
  }
  res[n] = 0;
  if ((DWORD)n + 1 > size)
    return n + 1;
  ow_memcpy(out, res, n + 1);
  return n;
}

DWORD WINAPI GetLongPathNameW(LPCWSTR name, LPWSTR buf, DWORD cch) {
  UINT cp = ow_fcp();
  struct pathget g = {long_path_a, ow_acp(name, cp)};
  LPSTR a;
  DWORD r;
  if (!name) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  {
    OW_FN(OW_KERNEL32, GetLongPathNameA, goto go);
    g.fn = pGetLongPathNameA;
  }
go:
  a = ow_get(call_pathget, &g);
  r = ow_ret_str(a, buf, cch, cp);
  ow_free(a);
  ow_free(g.in);
  return r;
}

DWORD WINAPI SearchPathW(LPCWSTR path, LPCWSTR name, LPCWSTR ext, DWORD cch,
                         LPWSTR buf, LPWSTR *part) {
  char out[MAX_PATH * 2];
  LPSTR apart = NULL;
  UINT cp = ow_fcp();
  LPSTR a = ow_acp(path, cp), b = ow_acp(name, cp), c = ow_acp(ext, cp);
  DWORD r = SearchPathA(a, b, c, sizeof out, out, &apart);
  ow_free(a);
  ow_free(b);
  ow_free(c);
  if (part)
    *part = NULL;
  if (!r)
    return 0;
  if (r >= sizeof out) {
    SetLastError(ERROR_FILENAME_EXCED_RANGE);
    return 0;
  }
  r = ow_ret_str(out, buf, cch, cp);
  if (r < cch && part)
    *part = file_part(out, apart, buf, cp);
  return r;
}

UINT WINAPI GetDriveTypeW(LPCWSTR root) {
  FA(a, root);
  UINT r = GetDriveTypeA(a);
  ow_free(a);
  return r;
}

BOOL WINAPI GetDiskFreeSpaceW(LPCWSTR root, LPDWORD spc, LPDWORD bps,
                              LPDWORD freec, LPDWORD total) {
  FA(a, root);
  BOOL r = GetDiskFreeSpaceA(a, spc, bps, freec, total);
  ow_free(a);
  return r;
}

/* The first Windows 95 has only GetDiskFreeSpace, which stops at 2 GB. */
BOOL WINAPI GetDiskFreeSpaceExW(LPCWSTR root, PULARGE_INTEGER avail,
                                PULARGE_INTEGER total,
                                PULARGE_INTEGER freeb) {
  LPSTR a = ow_acp(root, ow_fcp());
  BOOL r;
  OW_FN(OW_KERNEL32, GetDiskFreeSpaceExA, goto old);
  r = pGetDiskFreeSpaceExA(a, avail, total, freeb);
  ow_free(a);
  return r;
old: {
  DWORD spc, bps, fc, tc;
  r = GetDiskFreeSpaceA(a, &spc, &bps, &fc, &tc);
  ow_free(a);
  if (!r)
    return FALSE;
  if (avail)
    avail->QuadPart = (ULONGLONG)spc * bps * fc;
  if (total)
    total->QuadPart = (ULONGLONG)spc * bps * tc;
  if (freeb)
    freeb->QuadPart = (ULONGLONG)spc * bps * fc;
  return TRUE;
}
}

BOOL WINAPI GetVolumeInformationW(LPCWSTR root, LPWSTR name, DWORD ncch,
                                  LPDWORD serial, LPDWORD maxlen,
                                  LPDWORD flags, LPWSTR fs, DWORD fscch) {
  char an[MAX_PATH * 2], af[MAX_PATH * 2];
  UINT cp = ow_fcp();
  LPSTR a = ow_acp(root, cp);
  BOOL r = GetVolumeInformationA(a, name ? an : NULL, name ? sizeof an : 0,
                                 serial, maxlen, flags, fs ? af : NULL,
                                 fs ? sizeof af : 0);
  ow_free(a);
  if (!r)
    return FALSE;
  if (name) {
    if ((DWORD)ow_wlen_of(an, -1, cp) >= ncch) {
      SetLastError(ERROR_BAD_LENGTH);
      return FALSE;
    }
    ow_towcp(an, -1, name, ncch, cp);
  }
  if (fs) {
    if ((DWORD)ow_wlen_of(af, -1, cp) >= fscch) {
      SetLastError(ERROR_BAD_LENGTH);
      return FALSE;
    }
    ow_towcp(af, -1, fs, fscch, cp);
  }
  return TRUE;
}

BOOL WINAPI SetVolumeLabelW(LPCWSTR root, LPCWSTR label) {
  UINT cp = ow_fcp();
  LPSTR a = ow_acp(root, cp), b = ow_acp(label, cp);
  BOOL r = SetVolumeLabelA(a, b);
  ow_free(a);
  ow_free(b);
  return r;
}

DWORD WINAPI GetLogicalDriveStringsW(DWORD cch, LPWSTR buf) {
  char a[26 * 4 + 1];
  DWORD n = GetLogicalDriveStringsA(sizeof a, a), wl;
  if (!n || n >= sizeof a)
    return 0;
  wl = ow_wlen_of(a, n + 1, CP_ACP);
  if (wl > cch || !buf)
    return wl;
  ow_mb2wc(CP_ACP, 0, a, n + 1, buf, cch);
  return wl - 1;
}

DWORD WINAPI QueryDosDeviceW(LPCWSTR dev, LPWSTR buf, DWORD cch) {
  DWORD size = 4096, n, wl;
  LPSTR a = ow_acp(dev, ow_fcp()), out;
  for (;;) {
    out = ow_alloc(size);
    if (!out) {
      ow_free(a);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return 0;
    }
    n = QueryDosDeviceA(a, out, size);
    if (n || GetLastError() != ERROR_INSUFFICIENT_BUFFER || size > 1 << 20)
      break;
    ow_free(out);
    size *= 4;
  }
  ow_free(a);
  if (!n) {
    ow_free(out);
    return 0;
  }
  wl = ow_wlen_of(out, n, ow_fcp());
  if (wl > cch) {
    ow_free(out);
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  ow_mb2wc(ow_fcp(), 0, out, n, buf, cch);
  ow_free(out);
  return wl;
}

/* ---- modules and processes ----------------------------------------------- */

DWORD WINAPI GetModuleFileNameW(HMODULE mod, LPWSTR buf, DWORD cch) {
  char a[MAX_PATH * 2];
  DWORD n = GetModuleFileNameA(mod, a, sizeof a), wl;
  if (!n || !cch)
    return 0;
  a[n < sizeof a ? n : sizeof a - 1] = 0;
  wl = ow_wlen_of(a, -1, CP_ACP);
  ow_tow(a, -1, buf, cch);
  if (wl >= cch) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return cch;
  }
  return wl;
}

HMODULE WINAPI GetModuleHandleW(LPCWSTR name) {
  LPSTR a;
  HMODULE h;
  if (!name)
    return GetModuleHandleA(NULL);
  a = ow_a(name);
  h = GetModuleHandleA(a);
  ow_free(a);
  return h;
}

HMODULE WINAPI LoadLibraryW(LPCWSTR name) {
  FA(a, name);
  HMODULE h = LoadLibraryA(a);
  ow_free(a);
  return h;
}

HMODULE WINAPI LoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags) {
  FA(a, name);
  HMODULE h = LoadLibraryExA(a, file, flags);
  ow_free(a);
  return h;
}

/* A Unicode environment block in ANSI. */
static LPSTR env_a(LPCWSTR env) { return ow_alist(env); }

BOOL WINAPI CreateProcessW(LPCWSTR app, LPWSTR cmd,
                           LPSECURITY_ATTRIBUTES psa,
                           LPSECURITY_ATTRIBUTES tsa, BOOL inherit,
                           DWORD flags, LPVOID env, LPCWSTR dir,
                           LPSTARTUPINFOW wsi, LPPROCESS_INFORMATION pi) {
  STARTUPINFOA si;
  UINT cp = ow_fcp();
  LPSTR a = ow_acp(app, cp), c = ow_a(cmd), d = ow_acp(dir, cp);
  LPSTR e = NULL, desk = NULL, title = NULL;
  BOOL r;
  if (wsi) {
    ow_memset(&si, 0, sizeof si);
    ow_memcpy(&si, wsi, sizeof si < wsi->cb ? sizeof si : wsi->cb);
    si.cb = sizeof si;
    si.lpReserved = NULL;
    si.lpDesktop = desk = ow_a(wsi->lpDesktop);
    si.lpTitle = title = ow_a(wsi->lpTitle);
  }
  if (env && (flags & CREATE_UNICODE_ENVIRONMENT)) {
    e = env_a(env);
    env = e;
  }
  flags &= ~CREATE_UNICODE_ENVIRONMENT;
  r = CreateProcessA(a, c, psa, tsa, inherit, flags, env, d,
                     wsi ? &si : NULL, pi);
  ow_free(a);
  ow_free(c);
  ow_free(d);
  ow_free(e);
  ow_free(desk);
  ow_free(title);
  return r;
}

VOID WINAPI GetStartupInfoW(LPSTARTUPINFOW w) {
  /* The strings must stay valid: keep one converted copy. */
  static STARTUPINFOW cached;
  static LONG ready;
  if (!ready) {
    STARTUPINFOA a;
    GetStartupInfoA(&a);
    ow_memcpy(&cached, &a, sizeof cached);
    cached.cb = sizeof cached;
    cached.lpReserved = NULL;
    cached.lpDesktop = ow_w(a.lpDesktop);
    cached.lpTitle = ow_w(a.lpTitle);
    ready = 1;
  }
  if (w)
    *w = cached;
}

VOID WINAPI FatalAppExitW(UINT action, LPCWSTR msg) {
  LPSTR a = ow_a(msg);
  FatalAppExitA(action, a);
  ow_free(a);
}

VOID WINAPI OutputDebugStringW(LPCWSTR s) {
  LPSTR a = ow_a(s);
  OutputDebugStringA(a ? a : "");
  ow_free(a);
}

BOOL WINAPI IsBadStringPtrW(LPCWSTR s, UINT_PTR max) {
  UINT_PTR i;
  if (!s)
    return max != 0;
  for (i = 0; i < max; i++) {
    /* Check each page once. */
    if (i == 0 || ((UINT_PTR)(s + i) & 0xfff) < sizeof(WCHAR)) {
      if (IsBadReadPtr(s + i, sizeof(WCHAR)))
        return TRUE;
    }
    if (!s[i])
      return FALSE;
  }
  return FALSE;
}

/* ---- environment ------------------------------------------------------- */

DWORD WINAPI GetEnvironmentVariableW(LPCWSTR name, LPWSTR buf, DWORD cch) {
  LPSTR a = ow_a(name), v;
  DWORD n, r;
  n = GetEnvironmentVariableA(a, NULL, 0);
  if (!n) {
    ow_free(a);
    return 0;
  }
  v = ow_alloc(n + 1);
  if (!v) {
    ow_free(a);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
  n = GetEnvironmentVariableA(a, v, n + 1);
  ow_free(a);
  r = ow_ret_str(v, buf, cch, CP_ACP);
  ow_free(v);
  return r;
}

BOOL WINAPI SetEnvironmentVariableW(LPCWSTR name, LPCWSTR value) {
  LPSTR a = ow_a(name), b = ow_a(value);
  BOOL r = SetEnvironmentVariableA(a, b);
  ow_free(a);
  ow_free(b);
  return r;
}

LPWSTR WINAPI GetEnvironmentStringsW(void) {
  LPSTR a = GetEnvironmentStringsA();
  LPWSTR w;
  if (!a)
    return NULL;
  w = ow_wlist(a);
  FreeEnvironmentStringsA(a);
  return w;
}

BOOL WINAPI FreeEnvironmentStringsW(LPWSTR env) {
  ow_free(env);
  return TRUE;
}

DWORD WINAPI ExpandEnvironmentStringsW(LPCWSTR src, LPWSTR dst, DWORD cch) {
  LPSTR a = ow_a(src), out;
  DWORD n, wl;
  n = ExpandEnvironmentStringsA(a, NULL, 0);
  if (!n) {
    ow_free(a);
    return 0;
  }
  out = ow_alloc(n + 1);
  if (!out) {
    ow_free(a);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
  ExpandEnvironmentStringsA(a, out, n + 1);
  ow_free(a);
  wl = ow_wlen_of(out, -1, CP_ACP) + 1;
  if (dst && cch)
    ow_tow(out, -1, dst, cch);
  ow_free(out);
  return wl;
}

BOOL WINAPI GetComputerNameW(LPWSTR buf, LPDWORD size) {
  char a[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD n = sizeof a, wl;
  if (!size) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!GetComputerNameA(a, &n))
    return FALSE;
  wl = ow_wlen_of(a, -1, CP_ACP);
  if (!buf || *size < wl + 1) {
    *size = wl + 1;
    SetLastError(ERROR_BUFFER_OVERFLOW);
    return FALSE;
  }
  ow_tow(a, -1, buf, *size);
  *size = wl;
  return TRUE;
}

BOOL WINAPI SetComputerNameW(LPCWSTR name) {
  LPSTR a = ow_a(name);
  BOOL r = SetComputerNameA(a);
  ow_free(a);
  return r;
}

BOOL WINAPI GetVersionExW(LPOSVERSIONINFOW w) {
  OSVERSIONINFOA a;
  if (!w || (w->dwOSVersionInfoSize != sizeof(OSVERSIONINFOW) &&
             w->dwOSVersionInfoSize != sizeof(OSVERSIONINFOEXW))) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  a.dwOSVersionInfoSize = sizeof a;
  if (!GetVersionExA(&a))
    return FALSE;
  w->dwMajorVersion = a.dwMajorVersion;
  w->dwMinorVersion = a.dwMinorVersion;
  w->dwBuildNumber = a.dwBuildNumber;
  w->dwPlatformId = a.dwPlatformId;
  ow_tow(a.szCSDVersion, -1, w->szCSDVersion, 128);
  if (w->dwOSVersionInfoSize == sizeof(OSVERSIONINFOEXW)) {
    LPOSVERSIONINFOEXW x = (LPOSVERSIONINFOEXW)w;
    x->wServicePackMajor = 0;
    x->wServicePackMinor = 0;
    x->wSuiteMask = 0;
    x->wProductType = VER_NT_WORKSTATION;
    x->wReserved = 0;
  }
  return TRUE;
}

/* ---- profiles ------------------------------------------------------------ */

/* Copies an A profile result of n bytes into a W buffer of cch WCHARs the
 * way the W functions return it: a string, or a list ending with two NULs
 * (list), truncated to fit. */
static DWORD profile_out(LPCSTR a, DWORD n, BOOL list, LPWSTR buf,
                         DWORD cch) {
  DWORD wl;
  LPWSTR w;
  if (!buf || !cch)
    return 0;
  if (!list) {
    return ow_tow(a, n, buf, cch);
  }
  /* n counts everything but the final NUL of the list. */
  w = ow_wn(a, n + 1, (int *)&wl);
  if (!w) {
    buf[0] = 0;
    return 0;
  }
  if (wl > cch) {
    if (cch < 2) {
      buf[0] = 0;
      ow_free(w);
      return 0;
    }
    ow_memcpy(buf, w, (cch - 2) * sizeof(WCHAR));
    buf[cch - 2] = 0;
    buf[cch - 1] = 0;
    ow_free(w);
    return cch - 2;
  }
  ow_memcpy(buf, w, wl * sizeof(WCHAR));
  ow_free(w);
  return wl ? wl - 1 : 0;
}

DWORD WINAPI GetPrivateProfileStringW(LPCWSTR app, LPCWSTR key, LPCWSTR def,
                                      LPWSTR buf, DWORD cch, LPCWSTR file) {
  LPSTR a = ow_a(app), k = ow_a(key), d = ow_a(def), f = ow_a(file), out;
  DWORD size = cch * 2 + 2, n, r = 0;
  out = ow_alloc(size);
  if (out) {
    n = GetPrivateProfileStringA(a, k, d, out, size, f);
    r = profile_out(out, n, !a || !k, buf, cch);
    ow_free(out);
  }
  ow_free(a);
  ow_free(k);
  ow_free(d);
  ow_free(f);
  return r;
}

DWORD WINAPI GetProfileStringW(LPCWSTR app, LPCWSTR key, LPCWSTR def,
                               LPWSTR buf, DWORD cch) {
  LPSTR a = ow_a(app), k = ow_a(key), d = ow_a(def), out;
  DWORD size = cch * 2 + 2, n, r = 0;
  out = ow_alloc(size);
  if (out) {
    n = GetProfileStringA(a, k, d, out, size);
    r = profile_out(out, n, !a || !k, buf, cch);
    ow_free(out);
  }
  ow_free(a);
  ow_free(k);
  ow_free(d);
  return r;
}

BOOL WINAPI WritePrivateProfileStringW(LPCWSTR app, LPCWSTR key,
                                       LPCWSTR value, LPCWSTR file) {
  LPSTR a = ow_a(app), k = ow_a(key), v = ow_a(value), f = ow_a(file);
  BOOL r = WritePrivateProfileStringA(a, k, v, f);
  ow_free(a);
  ow_free(k);
  ow_free(v);
  ow_free(f);
  return r;
}

BOOL WINAPI WriteProfileStringW(LPCWSTR app, LPCWSTR key, LPCWSTR value) {
  LPSTR a = ow_a(app), k = ow_a(key), v = ow_a(value);
  BOOL r = WriteProfileStringA(a, k, v);
  ow_free(a);
  ow_free(k);
  ow_free(v);
  return r;
}

UINT WINAPI GetPrivateProfileIntW(LPCWSTR app, LPCWSTR key, INT def,
                                  LPCWSTR file) {
  LPSTR a = ow_a(app), k = ow_a(key), f = ow_a(file);
  UINT r = GetPrivateProfileIntA(a, k, def, f);
  ow_free(a);
  ow_free(k);
  ow_free(f);
  return r;
}

UINT WINAPI GetProfileIntW(LPCWSTR app, LPCWSTR key, INT def) {
  LPSTR a = ow_a(app), k = ow_a(key);
  UINT r = GetProfileIntA(a, k, def);
  ow_free(a);
  ow_free(k);
  return r;
}

DWORD WINAPI GetPrivateProfileSectionW(LPCWSTR app, LPWSTR buf, DWORD cch,
                                       LPCWSTR file) {
  LPSTR a = ow_a(app), f = ow_a(file), out;
  DWORD size = cch * 2 + 2, n, r = 0;
  out = ow_alloc(size);
  if (out) {
    n = GetPrivateProfileSectionA(a, out, size, f);
    r = profile_out(out, n, TRUE, buf, cch);
    ow_free(out);
  }
  ow_free(a);
  ow_free(f);
  return r;
}

DWORD WINAPI GetProfileSectionW(LPCWSTR app, LPWSTR buf, DWORD cch) {
  LPSTR a = ow_a(app), out;
  DWORD size = cch * 2 + 2, n, r = 0;
  out = ow_alloc(size);
  if (out) {
    n = GetProfileSectionA(a, out, size);
    r = profile_out(out, n, TRUE, buf, cch);
    ow_free(out);
  }
  ow_free(a);
  return r;
}

DWORD WINAPI GetPrivateProfileSectionNamesW(LPWSTR buf, DWORD cch,
                                            LPCWSTR file) {
  LPSTR f = ow_a(file), out;
  DWORD size = cch * 2 + 2, n, r = 0;
  out = ow_alloc(size);
  if (out) {
    n = GetPrivateProfileSectionNamesA(out, size, f);
    r = profile_out(out, n, TRUE, buf, cch);
    ow_free(out);
  }
  ow_free(f);
  return r;
}

BOOL WINAPI WritePrivateProfileSectionW(LPCWSTR app, LPCWSTR data,
                                        LPCWSTR file) {
  LPSTR a = ow_a(app), d = ow_alist(data), f = ow_a(file);
  BOOL r = WritePrivateProfileSectionA(a, d, f);
  ow_free(a);
  ow_free(d);
  ow_free(f);
  return r;
}

BOOL WINAPI WriteProfileSectionW(LPCWSTR app, LPCWSTR data) {
  LPSTR a = ow_a(app), d = ow_alist(data);
  BOOL r = WriteProfileSectionA(a, d);
  ow_free(a);
  ow_free(d);
  return r;
}

BOOL WINAPI GetPrivateProfileStructW(LPCWSTR sect, LPCWSTR key, LPVOID data,
                                     UINT size, LPCWSTR file) {
  LPSTR a = ow_a(sect), k = ow_a(key), f = ow_a(file);
  BOOL r = GetPrivateProfileStructA(a, k, data, size, f);
  ow_free(a);
  ow_free(k);
  ow_free(f);
  return r;
}

BOOL WINAPI WritePrivateProfileStructW(LPCWSTR sect, LPCWSTR key,
                                       LPVOID data, UINT size, LPCWSTR file) {
  LPSTR a = ow_a(sect), k = ow_a(key), f = ow_a(file);
  BOOL r = WritePrivateProfileStructA(a, k, data, size, f);
  ow_free(a);
  ow_free(k);
  ow_free(f);
  return r;
}

/* ---- atoms ------------------------------------------------------------- */

ATOM WINAPI AddAtomW(LPCWSTR s) {
  LPSTR a = ow_ares(s);
  ATOM r = AddAtomA(a);
  ow_free(a);
  return r;
}

ATOM WINAPI FindAtomW(LPCWSTR s) {
  LPSTR a = ow_ares(s);
  ATOM r = FindAtomA(a);
  ow_free(a);
  return r;
}

ATOM WINAPI GlobalAddAtomW(LPCWSTR s) {
  LPSTR a = ow_ares(s);
  ATOM r = GlobalAddAtomA(a);
  ow_free(a);
  return r;
}

ATOM WINAPI GlobalFindAtomW(LPCWSTR s) {
  LPSTR a = ow_ares(s);
  ATOM r = GlobalFindAtomA(a);
  ow_free(a);
  return r;
}

UINT WINAPI GetAtomNameW(ATOM atom, LPWSTR buf, int cch) {
  char a[512];
  UINT n = GetAtomNameA(atom, a, sizeof a);
  if (!n)
    return 0;
  return ow_tow(a, n, buf, cch);
}

UINT WINAPI GlobalGetAtomNameW(ATOM atom, LPWSTR buf, int cch) {
  char a[512];
  UINT n = GlobalGetAtomNameA(atom, a, sizeof a);
  if (!n)
    return 0;
  return ow_tow(a, n, buf, cch);
}

/* ---- synchronization objects and file mappings ----------------------- */

HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL init,
                           LPCWSTR name) {
  LPSTR a = ow_a(name);
  HANDLE h = CreateEventA(sa, manual, init, a);
  ow_free(a);
  return h;
}

HANDLE WINAPI OpenEventW(DWORD access, BOOL inherit, LPCWSTR name) {
  LPSTR a = ow_a(name);
  HANDLE h = OpenEventA(access, inherit, a);
  ow_free(a);
  return h;
}

HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL owner,
                           LPCWSTR name) {
  LPSTR a = ow_a(name);
  HANDLE h = CreateMutexA(sa, owner, a);
  ow_free(a);
  return h;
}

HANDLE WINAPI OpenMutexW(DWORD access, BOOL inherit, LPCWSTR name) {
  LPSTR a = ow_a(name);
  HANDLE h = OpenMutexA(access, inherit, a);
  ow_free(a);
  return h;
}

HANDLE WINAPI CreateSemaphoreW(LPSECURITY_ATTRIBUTES sa, LONG init, LONG max,
                               LPCWSTR name) {
  LPSTR a = ow_a(name);
  HANDLE h = CreateSemaphoreA(sa, init, max, a);
  ow_free(a);
  return h;
}

HANDLE WINAPI OpenSemaphoreW(DWORD access, BOOL inherit, LPCWSTR name) {
  LPSTR a = ow_a(name);
  HANDLE h = OpenSemaphoreA(access, inherit, a);
  ow_free(a);
  return h;
}

/* Waitable timers are from Windows 98. */
HANDLE WINAPI CreateWaitableTimerW(LPSECURITY_ATTRIBUTES sa, BOOL manual,
                                   LPCWSTR name) {
  LPSTR a;
  HANDLE h;
  OW_FN(OW_KERNEL32, CreateWaitableTimerA, return NULL);
  a = ow_a(name);
  h = pCreateWaitableTimerA(sa, manual, a);
  ow_free(a);
  return h;
}

HANDLE WINAPI OpenWaitableTimerW(DWORD access, BOOL inherit, LPCWSTR name) {
  LPSTR a;
  HANDLE h;
  OW_FN(OW_KERNEL32, OpenWaitableTimerA, return NULL);
  a = ow_a(name);
  h = pOpenWaitableTimerA(access, inherit, a);
  ow_free(a);
  return h;
}

HANDLE WINAPI CreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES sa,
                                 DWORD prot, DWORD hi, DWORD lo,
                                 LPCWSTR name) {
  LPSTR a = ow_a(name);
  HANDLE h = CreateFileMappingA(file, sa, prot, hi, lo, a);
  ow_free(a);
  return h;
}

HANDLE WINAPI OpenFileMappingW(DWORD access, BOOL inherit, LPCWSTR name) {
  LPSTR a = ow_a(name);
  HANDLE h = OpenFileMappingA(access, inherit, a);
  ow_free(a);
  return h;
}

/* ---- pipes and mailslots --------------------------------------------- */

HANDLE WINAPI CreateMailslotW(LPCWSTR name, DWORD max, DWORD timeout,
                              LPSECURITY_ATTRIBUTES sa) {
  LPSTR a = ow_a(name);
  HANDLE h = CreateMailslotA(a, max, timeout, sa);
  ow_free(a);
  return h;
}

HANDLE WINAPI CreateNamedPipeW(LPCWSTR name, DWORD open, DWORD mode,
                               DWORD max, DWORD out, DWORD in, DWORD timeout,
                               LPSECURITY_ATTRIBUTES sa) {
  LPSTR a = ow_a(name);
  HANDLE h = CreateNamedPipeA(a, open, mode, max, out, in, timeout, sa);
  ow_free(a);
  return h;
}

BOOL WINAPI CallNamedPipeW(LPCWSTR name, LPVOID in, DWORD insize, LPVOID out,
                           DWORD outsize, LPDWORD read, DWORD timeout) {
  LPSTR a = ow_a(name);
  BOOL r = CallNamedPipeA(a, in, insize, out, outsize, read, timeout);
  ow_free(a);
  return r;
}

BOOL WINAPI WaitNamedPipeW(LPCWSTR name, DWORD timeout) {
  LPSTR a = ow_a(name);
  BOOL r = WaitNamedPipeA(a, timeout);
  ow_free(a);
  return r;
}

BOOL WINAPI GetNamedPipeHandleStateW(HANDLE h, LPDWORD state, LPDWORD inst,
                                     LPDWORD count, LPDWORD timeout,
                                     LPWSTR user, DWORD cch) {
  char a[256];
  BOOL r = GetNamedPipeHandleStateA(h, state, inst, count, timeout,
                                    user ? a : NULL, user ? sizeof a : 0);
  if (r && user)
    ow_tow(a, -1, user, cch);
  return r;
}

/* ---- communication ports ------------------------------------------------- */

BOOL WINAPI BuildCommDCBW(LPCWSTR def, LPDCB dcb) {
  LPSTR a = ow_a(def);
  BOOL r = BuildCommDCBA(a, dcb);
  ow_free(a);
  return r;
}

BOOL WINAPI BuildCommDCBAndTimeoutsW(LPCWSTR def, LPDCB dcb,
                                     LPCOMMTIMEOUTS to) {
  LPSTR a = ow_a(def);
  BOOL r = BuildCommDCBAndTimeoutsA(a, dcb, to);
  ow_free(a);
  return r;
}

BOOL WINAPI CommConfigDialogW(LPCWSTR name, HWND wnd, LPCOMMCONFIG cc) {
  LPSTR a = ow_a(name);
  BOOL r = CommConfigDialogA(a, wnd, cc);
  ow_free(a);
  return r;
}

BOOL WINAPI GetDefaultCommConfigW(LPCWSTR name, LPCOMMCONFIG cc,
                                  LPDWORD size) {
  LPSTR a = ow_a(name);
  BOOL r = GetDefaultCommConfigA(a, cc, size);
  ow_free(a);
  return r;
}

BOOL WINAPI SetDefaultCommConfigW(LPCWSTR name, LPCOMMCONFIG cc, DWORD size) {
  LPSTR a = ow_a(name);
  BOOL r = SetDefaultCommConfigA(a, cc, size);
  ow_free(a);
  return r;
}

/* ---- resources ------------------------------------------------------ */

HRSRC WINAPI FindResourceW(HMODULE mod, LPCWSTR name, LPCWSTR type) {
  LPSTR a = ow_ares(name), b = ow_ares(type);
  HRSRC r = FindResourceA(mod, a, b);
  ow_free(a);
  ow_free(b);
  return r;
}

HRSRC WINAPI FindResourceExW(HMODULE mod, LPCWSTR type, LPCWSTR name,
                             WORD lang) {
  LPSTR a = ow_ares(type), b = ow_ares(name);
  HRSRC r = FindResourceExA(mod, a, b, lang);
  ow_free(a);
  ow_free(b);
  return r;
}
