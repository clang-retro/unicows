/* Tests for opencow, linked straight to its unicows.dll so that the W
 * functions it calls are opencow's (libunicows would use the system's on
 * NT, and Wine is NT). Each check prints a line when it fails; the exit
 * status is the number of failures.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int failures, checks;

static void check(int ok, const char *what, int line) {
  checks++;
  if (!ok) {
    failures++;
    printf("FAIL line %d: %s\n", line, what);
  }
}
#define CHECK(c) check(!!(c), #c, __LINE__)
#define SAME(a, b) check(!wcscmp((a), (b)), #a " == " #b, __LINE__)

/* ---- conversions and strings ------------------------------------------- */

static void test_strings(void) {
  static const char utf8[] = "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80";
  WCHAR w[32];
  char a[32];
  int n;

  n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 32);
  CHECK(n == 6);
  CHECK(w[0] == 'a' && w[1] == 0xe9 && w[2] == 0x20ac);
  CHECK(w[3] == 0xd83d && w[4] == 0xde00 && w[5] == 0);
  n = WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof a, NULL, NULL);
  CHECK(n == (int)sizeof utf8 && !strcmp(a, utf8));
  CHECK(MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0) == 6);

  CHECK(lstrlenW(L"hello") == 5);
  lstrcpyW(w, L"abc");
  lstrcatW(w, L"def");
  SAME(w, L"abcdef");
  CHECK(lstrcmpiW(L"ABCdef", L"abcDEF") == 0);
  CHECK(lstrcmpW(L"abc", L"abd") < 0);
  lstrcpynW(w, L"truncate", 4);
  SAME(w, L"tru");

  wsprintfW(w, L"%d-%s-%x-%c-%5.2s|", 42, L"wide", 255, L'Z', L"abc");
  SAME(w, L"42-wide-ff-Z-   ab|");
  wsprintfW(w, L"%S", "narrow");
  SAME(w, L"narrow");

  lstrcpyW(w, L"stra\xdf" L"e \xe9t\xe9");
  CharUpperW(w);
  CHECK(w[0] == 'S' && w[4] == 0xdf && w[7] == 0xc9 && w[9] == 0xc9);
  CharLowerW(w);
  CHECK(w[0] == 's' && w[7] == 0xe9);
  CHECK(IsCharAlphaW(0xe9) && !IsCharAlphaW('1') && IsCharUpperW(0xc9));
  CHECK((DWORD_PTR)CharUpperW((LPWSTR)(DWORD_PTR)0xe9) == 0xc9);
  CHECK(CharNextW(L"ab") != NULL);

  CHECK(CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, L"Apple", -1,
                       L"apple", -1) == CSTR_EQUAL);
  CHECK(CompareStringW(LOCALE_USER_DEFAULT, 0, L"a", -1, L"b", -1) ==
        CSTR_LESS_THAN);
  n = LCMapStringW(LOCALE_USER_DEFAULT, LCMAP_UPPERCASE, L"abc\xe9", -1, w,
                   32);
  CHECK(n == 5);
  SAME(w, L"ABC\xc9");
  {
    WORD t[3];
    CHECK(GetStringTypeW(CT_CTYPE1, L"a1 ", 3, t));
    CHECK((t[0] & C1_ALPHA) && (t[1] & C1_DIGIT) && (t[2] & C1_SPACE));
  }
}

/* ---- the kernel ------------------------------------------------------- */

static void test_kernel(void) {
  WCHAR dir[MAX_PATH], path[MAX_PATH], buf[MAX_PATH];
  HANDLE h;
  DWORD n;
  WIN32_FIND_DATAW fd;
  OSVERSIONINFOW v;

  CHECK(GetTempPathW(MAX_PATH, dir) > 0);
  CHECK(dir[lstrlenW(dir) - 1] == '\\');
  CHECK(GetTempFileNameW(dir, L"ow", 0, path));
  h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  CHECK(h != INVALID_HANDLE_VALUE);
  CHECK(WriteFile(h, "data", 4, &n, NULL) && n == 4);
  CloseHandle(h);
  CHECK(GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES);
  {
    WIN32_FILE_ATTRIBUTE_DATA ad;
    CHECK(GetFileAttributesExW(path, GetFileExInfoStandard, &ad));
    CHECK(ad.nFileSizeLow == 4);
  }
  h = FindFirstFileW(path, &fd);
  CHECK(h != INVALID_HANDLE_VALUE);
  if (h != INVALID_HANDLE_VALUE) {
    CHECK(fd.nFileSizeLow == 4);
    CHECK(fd.cFileName[0] == 'o' || fd.cFileName[0] == 'O');
    FindClose(h);
  }
  lstrcpyW(buf, path);
  lstrcatW(buf, L".moved");
  CHECK(MoveFileW(path, buf));
  CHECK(CopyFileW(buf, path, FALSE));
  CHECK(DeleteFileW(buf));
  CHECK(DeleteFileW(path));
  CHECK(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES);
  CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);

  lstrcpyW(buf, dir);
  lstrcatW(buf, L"opencow-dir");
  CHECK(CreateDirectoryW(buf, NULL));
  CHECK(GetFileAttributesW(buf) & FILE_ATTRIBUTE_DIRECTORY);
  CHECK(RemoveDirectoryW(buf));

  n = GetCurrentDirectoryW(MAX_PATH, buf);
  CHECK(n > 0 && n == (DWORD)lstrlenW(buf));
  CHECK(GetCurrentDirectoryW(0, NULL) == n + 1);
  CHECK(GetFullPathNameW(L"x.txt", MAX_PATH, path, NULL) > n);
  CHECK(GetModuleFileNameW(NULL, buf, MAX_PATH) > 0);
  CHECK(wcsstr(buf, L".exe") || wcsstr(buf, L".EXE"));
  CHECK(GetModuleHandleW(L"kernel32.dll") != NULL);
  CHECK(GetWindowsDirectoryW(buf, MAX_PATH) > 0);
  CHECK(GetSystemDirectoryW(buf, MAX_PATH) > 0);

  CHECK(SetEnvironmentVariableW(L"OPENCOW_TEST", L"v\xe9"));
  CHECK(GetEnvironmentVariableW(L"OPENCOW_TEST", buf, MAX_PATH) == 2);
  SAME(buf, L"v\xe9");
  CHECK(ExpandEnvironmentStringsW(L"<%OPENCOW_TEST%>", buf, MAX_PATH) == 5);
  SAME(buf, L"<v\xe9>");
  CHECK(GetEnvironmentVariableW(L"OPENCOW_TEST", buf, 1) == 3);

  v.dwOSVersionInfoSize = sizeof v;
  CHECK(GetVersionExW(&v) && v.dwMajorVersion >= 4);

  /* GetProcAddress gives opencow's W functions for system DLLs. */
  {
    FARPROC p = GetProcAddress(GetModuleHandleA("kernel32.dll"),
                               "CreateFileW");
    FARPROC mine = GetProcAddress(GetModuleHandleA("unicows.dll"),
                                  "CreateFileW");
    CHECK(p && p == mine);
  }

  /* Profiles. */
  GetTempPathW(MAX_PATH, path);
  lstrcatW(path, L"opencow.ini");
  CHECK(WritePrivateProfileStringW(L"sec", L"key", L"val\xe9", path));
  CHECK(GetPrivateProfileStringW(L"sec", L"key", L"", buf, MAX_PATH, path) ==
        4);
  SAME(buf, L"val\xe9");
  CHECK(GetPrivateProfileIntW(L"sec", L"none", 7, path) == 7);
  DeleteFileW(path);

  /* Atoms. */
  {
    ATOM a = AddAtomW(L"opencow-atom");
    CHECK(a && FindAtomW(L"opencow-atom") == a);
    CHECK(GetAtomNameW(a, buf, MAX_PATH) == 12);
    SAME(buf, L"opencow-atom");
  }

  /* Named objects. */
  h = CreateEventW(NULL, TRUE, FALSE, L"opencow-event");
  CHECK(h != NULL);
  {
    HANDLE o = OpenEventW(EVENT_ALL_ACCESS, FALSE, L"opencow-event");
    CHECK(o != NULL);
    CloseHandle(o);
  }
  CloseHandle(h);
}

/* ---- locales and messages -------------------------------------------- */

static void test_nls(void) {
  WCHAR buf[256];
  SYSTEMTIME st = {2001, 2, 0, 3, 4, 5, 6, 0};
  LPWSTR p = NULL;
  DWORD_PTR args[2] = {(DWORD_PTR)L"one", 2};

  CHECK(GetLocaleInfoW(0x409, LOCALE_SENGLANGUAGE, buf, 256) > 0);
  SAME(buf, L"English");
  {
    DWORD n = 0;
    CHECK(GetLocaleInfoW(0x409, LOCALE_IDEFAULTANSICODEPAGE |
                                    LOCALE_RETURN_NUMBER,
                         (LPWSTR)&n, 2) == 2);
    CHECK(n == 1252);
  }
  CHECK(GetDateFormatW(0x409, 0, &st, L"yyyy-MM-dd", buf, 256) == 11);
  SAME(buf, L"2001-02-03");
  CHECK(GetTimeFormatW(0x409, 0, &st, L"HH:mm:ss", buf, 256) == 9);
  SAME(buf, L"04:05:06");
  CHECK(GetNumberFormatW(0x409, 0, L"1234.5", NULL, buf, 256) > 0);
  SAME(buf, L"1,234.50");

  CHECK(FormatMessageW(FORMAT_MESSAGE_FROM_STRING |
                           FORMAT_MESSAGE_ARGUMENT_ARRAY,
                       L"%1 and %2!d!%n", 0, 0, buf, 256,
                       (va_list *)args) == 11);
  SAME(buf, L"one and 2\r\n");
  CHECK(FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_ALLOCATE_BUFFER |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, ERROR_FILE_NOT_FOUND, 0, (LPWSTR)&p, 0,
                       NULL) > 0);
  CHECK(p && p[0]);
  LocalFree(p);
}

/* ---- the registry ---------------------------------------------------- */

static void test_registry(void) {
  HKEY k;
  WCHAR buf[64], name[64];
  DWORD type, cb, cn, i, n = 0;
  LONG r;

  r = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\opencow-test", 0, NULL,
                      0, KEY_ALL_ACCESS, NULL, &k, NULL);
  CHECK(r == ERROR_SUCCESS);
  if (r)
    return;
  CHECK(RegSetValueExW(k, L"s", 0, REG_SZ, (const BYTE *)L"v\xe9",
                       3 * sizeof(WCHAR)) == 0);
  CHECK(RegSetValueExW(k, L"m", 0, REG_MULTI_SZ, (const BYTE *)L"a\0bc\0",
                       6 * sizeof(WCHAR)) == 0);
  i = 0x12345678;
  CHECK(RegSetValueExW(k, L"d", 0, REG_DWORD, (BYTE *)&i, 4) == 0);
  cb = sizeof buf;
  CHECK(RegQueryValueExW(k, L"s", NULL, &type, (BYTE *)buf, &cb) == 0);
  CHECK(type == REG_SZ && cb == 3 * sizeof(WCHAR));
  SAME(buf, L"v\xe9");
  cb = 2;
  CHECK(RegQueryValueExW(k, L"s", NULL, NULL, (BYTE *)buf, &cb) ==
        ERROR_MORE_DATA);
  CHECK(cb == 3 * sizeof(WCHAR));
  cb = sizeof buf;
  CHECK(RegQueryValueExW(k, L"m", NULL, &type, (BYTE *)buf, &cb) == 0);
  CHECK(type == REG_MULTI_SZ && cb == 6 * sizeof(WCHAR));
  CHECK(!memcmp(buf, L"a\0bc\0", 6 * sizeof(WCHAR)));
  cb = sizeof i;
  CHECK(RegQueryValueExW(k, L"d", NULL, &type, (BYTE *)&i, &cb) == 0);
  CHECK(type == REG_DWORD && i == 0x12345678);
  for (i = 0;; i++) {
    cn = 64;
    cb = sizeof buf;
    if (RegEnumValueW(k, i, name, &cn, NULL, &type, (BYTE *)buf, &cb))
      break;
    n++;
    CHECK(cn == 1);
  }
  CHECK(n == 3);
  CHECK(RegDeleteValueW(k, L"s") == 0);
  RegCloseKey(k);
  CHECK(RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\opencow-test") == 0);
}

/* ---- windows ---------------------------------------------------------- */

static WCHAR got_text[64];
static WPARAM got_char;
static int got_create;

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
  case WM_CREATE: {
    CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
    got_create = cs->lpszName && !wcscmp(cs->lpszName, L"title\xe9");
    break;
  }
  case WM_SETTEXT:
    lstrcpynW(got_text, (LPCWSTR)lp, 64);
    break;
  case WM_CHAR:
    got_char = wp;
    return 0;
  case WM_USER + 1:
    return IsWindowUnicode(h);
  }
  return DefWindowProcW(h, m, wp, lp);
}

static void test_windows(void) {
  WNDCLASSW wc = {0};
  WCHAR buf[64];
  HWND h, e;
  MSG msg;

  wc.lpfnWndProc = proc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.lpszClassName = L"opencow-test";
  CHECK(RegisterClassW(&wc));
  h = CreateWindowExW(0, L"opencow-test", L"title\xe9", WS_OVERLAPPEDWINDOW,
                      0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
  CHECK(h != NULL);
  if (!h)
    return;
  CHECK(got_create);
  CHECK(IsWindowUnicode(h));
  CHECK((WNDPROC)GetWindowLongW(h, GWL_WNDPROC) == proc);
  CHECK(GetWindowTextW(h, buf, 64) == 6);
  SAME(buf, L"title\xe9");
  CHECK(GetWindowTextLengthW(h) >= 6);
  CHECK(SetWindowTextW(h, L"new \xe9"));
  SAME(got_text, L"new \xe9");
  CHECK(SendMessageW(h, WM_GETTEXT, 64, (LPARAM)buf) == 5);
  SAME(buf, L"new \xe9");
  CHECK(SendMessageW(h, WM_USER + 1, 0, 0));
  CHECK(GetClassNameW(h, buf, 64) == 12);
  SAME(buf, L"opencow-test");
  CHECK(FindWindowW(L"opencow-test", NULL) == h);

  /* A character through the message queue. */
  CHECK(PostMessageW(h, WM_CHAR, 0xe9, 0));
  while (PeekMessageW(&msg, h, 0, 0, PM_REMOVE))
    DispatchMessageW(&msg);
  CHECK(got_char == 0xe9);
  got_char = 0;
  CHECK(SendMessageW(h, WM_CHAR, 0x20ac, 0) == 0);
  CHECK(got_char == 0x20ac);

  /* A system control. */
  e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD, 0, 0, 50, 20, h, NULL,
                      wc.hInstance, NULL);
  CHECK(e != NULL);
  if (e) {
    CHECK(SetWindowTextW(e, L"ed\xe9t"));
    CHECK(GetWindowTextLengthW(e) == 4);
    CHECK(GetWindowTextW(e, buf, 64) == 4);
    SAME(buf, L"ed\xe9t");
    SendMessageW(e, EM_SETSEL, 0, -1);
    SendMessageW(e, EM_REPLACESEL, FALSE, (LPARAM)L"x\xe9y");
    CHECK(GetWindowTextW(e, buf, 64) == 3);
    SAME(buf, L"x\xe9y");
  }
  {
    HWND lb = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | LBS_HASSTRINGS,
                              0, 0, 50, 50, h, NULL, wc.hInstance, NULL);
    CHECK(lb != NULL);
    if (lb) {
      CHECK(SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)L"\xe9l\xe9ment") == 0);
      CHECK(SendMessageW(lb, LB_GETTEXTLEN, 0, 0) == 7);
      CHECK(SendMessageW(lb, LB_GETTEXT, 0, (LPARAM)buf) == 7);
      SAME(buf, L"\xe9l\xe9ment");
      CHECK(SendMessageW(lb, LB_FINDSTRINGEXACT, -1,
                         (LPARAM)L"\xe9l\xe9ment") == 0);
    }
  }
  DestroyWindow(h);
  CHECK(UnregisterClassW(L"opencow-test", wc.hInstance));

  /* Menus. */
  {
    HMENU m = CreatePopupMenu();
    MENUITEMINFOW mi = {sizeof mi};
    CHECK(AppendMenuW(m, MF_STRING, 1, L"&Open \xe9"));
    CHECK(GetMenuStringW(m, 1, buf, 64, MF_BYCOMMAND) == 7);
    SAME(buf, L"&Open \xe9");
    mi.fMask = MIIM_TYPE;
    mi.dwTypeData = buf;
    mi.cch = 64;
    CHECK(GetMenuItemInfoW(m, 1, FALSE, &mi));
    CHECK(mi.cch == 7);
    DestroyMenu(m);
  }

  /* The clipboard gives text in W form. */
  if (OpenClipboard(NULL)) {
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 4);
    memcpy(GlobalLock(g), "abc", 4);
    GlobalUnlock(g);
    EmptyClipboard();
    SetClipboardData(CF_TEXT, g);
    CHECK(IsClipboardFormatAvailable(CF_UNICODETEXT));
    g = GetClipboardData(CF_UNICODETEXT);
    CHECK(g && !wcscmp(GlobalLock(g), L"abc"));
    if (g)
      GlobalUnlock(g);
    CloseClipboard();
  }
  CHECK(RegisterWindowMessageW(L"opencow-msg") ==
        RegisterWindowMessageA("opencow-msg"));
}

/* ---- dialogs ------------------------------------------------------------ */

static INT_PTR CALLBACK dlg_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  if (m == WM_INITDIALOG) {
    WCHAR buf[32];
    SetWindowTextW(h, L"dialog \xe9");
    GetWindowTextW(h, buf, 32);
    EndDialog(h, lp == 1234 && !wcscmp(buf, L"dialog \xe9") &&
                         IsWindowUnicode(h)
                     ? 42
                     : 1);
    return TRUE;
  }
  return FALSE;
}

static UINT_PTR CALLBACK ofn_hook(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  if (m == WM_INITDIALOG) {
    OPENFILENAMEW *o = (OPENFILENAMEW *)lp;
    /* The hook gets the caller's structure; close the dialog. */
    if (o->lCustData == 77 && !wcscmp(o->lpstrTitle, L"T\xeftle"))
      o->lCustData = 78;
    PostMessageW(GetParent(h), WM_COMMAND, IDCANCEL, 0);
  }
  return 0;
}

static void test_dialogs(void) {
  static const WORD tmpl[] = {
      /* style */ LOWORD(DS_MODALFRAME | WS_POPUP), HIWORD(WS_POPUP),
      0, 0, /* extended style */
      0,    /* controls */
      0, 0, 100, 50,
      0, 0, 0 /* menu, class, title */};
  WCHAR file[MAX_PATH] = L"";
  OPENFILENAMEW o = {sizeof o};

  CHECK(DialogBoxIndirectParamW(GetModuleHandleW(NULL),
                                (LPCDLGTEMPLATEW)tmpl, NULL, dlg_proc,
                                1234) == 42);

  o.lpstrFile = file;
  o.nMaxFile = MAX_PATH;
  o.lpstrFilter = L"Text\0*.txt\0All\0*.*\0";
  o.lpstrTitle = L"T\xeftle";
  o.Flags = OFN_EXPLORER | OFN_ENABLEHOOK;
  o.lpfnHook = ofn_hook;
  o.lCustData = 77;
  CHECK(!GetOpenFileNameW(&o));
  CHECK(o.lCustData == 78);
}

/* ---- GDI ---------------------------------------------------------------- */

static int CALLBACK font_cb(const LOGFONTW *lf, const TEXTMETRICW *tm,
                            DWORD type, LPARAM lp) {
  (*(int *)lp)++;
  return lf->lfFaceName[0] != 0;
}

static void test_gdi(void) {
  HDC dc = GetDC(NULL);
  LOGFONTW lf = {0};
  HFONT f;
  WCHAR face[LF_FACESIZE];
  TEXTMETRICW tm;
  SIZE sz;
  int n = 0;

  lf.lfHeight = 16;
  lstrcpyW(lf.lfFaceName, L"Tahoma");
  f = CreateFontIndirectW(&lf);
  CHECK(f != NULL);
  SelectObject(dc, f);
  CHECK(GetTextFaceW(dc, LF_FACESIZE, face) > 0);
  CHECK(GetTextMetricsW(dc, &tm) && tm.tmHeight > 0);
  CHECK(GetTextExtentPoint32W(dc, L"abc", 3, &sz) && sz.cx > 0);
  {
    LOGFONTW back;
    CHECK(GetObjectW(f, sizeof back, &back) == sizeof back);
    SAME(back.lfFaceName, L"Tahoma");
  }
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfFaceName[0] = 0;
  EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)font_cb, (LPARAM)&n, 0);
  CHECK(n > 0);
  DeleteObject(f);
  ReleaseDC(NULL, dc);
}

/* ---- other DLLs ------------------------------------------------------------ */

static void test_other(void) {
  WCHAR buf[MAX_PATH], sys[MAX_PATH];
  DWORD size, h;
  void *ver;

  /* A version resource of a 32-bit DLL. */
  GetSystemDirectoryW(sys, MAX_PATH);
  lstrcatW(sys, L"\\kernel32.dll");
  size = GetFileVersionInfoSizeW(sys, &h);
  CHECK(size > 0);
  ver = HeapAlloc(GetProcessHeap(), 0, size);
  if (ver && GetFileVersionInfoW(sys, 0, size, ver)) {
    VS_FIXEDFILEINFO *fi;
    WORD *tr;
    LPWSTR s;
    UINT n;
    CHECK(VerQueryValueW(ver, L"\\", (void **)&fi, &n));
    CHECK(n == sizeof *fi && fi->dwSignature == 0xfeef04bd);
    CHECK(VerQueryValueW(ver, L"\\VarFileInfo\\Translation", (void **)&tr,
                         &n) &&
          n >= 4);
    wsprintfW(buf, L"\\StringFileInfo\\%04x%04x\\FileVersion", tr[0], tr[1]);
    CHECK(VerQueryValueW(ver, buf, (void **)&s, &n) && n > 1 && s[0]);
  } else {
    CHECK(!"GetFileVersionInfoW");
  }
  HeapFree(GetProcessHeap(), 0, ver);

  {
    SHFILEINFOW fi;
    CHECK(SHGetFileInfoW(sys, 0, &fi, sizeof fi, SHGFI_DISPLAYNAME));
    CHECK(fi.szDisplayName[0] != 0);
  }
  CHECK(mmioStringToFOURCCW(L"wave", MMIO_TOUPPER) ==
        mmioFOURCC('W', 'A', 'V', 'E'));
}

/* ---- updating resources ------------------------------------------------ */

static void test_resources(void) {
  WCHAR exe[MAX_PATH], copy[MAX_PATH], buf[64];
  HANDLE u;
  HMODULE m;
  /* String block 1 holds strings 16 to 31; string 17 is "w\xe9rld". */
  static const WCHAR block[] = {0, 6, 'w', 0xe9, 'r', 'l', 'd', '!',
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  GetModuleFileNameW(NULL, exe, MAX_PATH);
  GetTempPathW(MAX_PATH, copy);
  lstrcatW(copy, L"opencow-res.exe");
  CHECK(CopyFileW(exe, copy, FALSE));
  u = BeginUpdateResourceW(copy, FALSE);
  CHECK(u != NULL);
  if (!u)
    return;
  CHECK(UpdateResourceW(u, (LPCWSTR)RT_STRING, MAKEINTRESOURCEW(2),
                        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                        (void *)block, sizeof block));
  CHECK(UpdateResourceW(u, L"OWDATA", L"BLOB", 0, "payload", 7));
  CHECK(EndUpdateResourceW(u, FALSE));
  m = LoadLibraryExW(copy, NULL, LOAD_LIBRARY_AS_DATAFILE);
  CHECK(m != NULL);
  if (m) {
    HRSRC r;
    CHECK(LoadStringW(m, 17, buf, 64) == 6);
    SAME(buf, L"w\xe9rld!");
    r = FindResourceW(m, L"BLOB", L"OWDATA");
    CHECK(r && SizeofResource(m, r) == 7);
    FreeLibrary(m);
  }
  DeleteFileW(copy);
}

int main(void) {
  test_strings();
  test_kernel();
  test_nls();
  test_registry();
  test_windows();
  test_dialogs();
  test_gdi();
  test_other();
  test_resources();
  printf("%d checks, %d failed\n", checks, failures);
  return failures;
}
