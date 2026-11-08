/* gdi32: fonts and text, device contexts, metafiles, color management and
 * printing documents, and the DEVMODE and LOGFONT conversions that user32
 * and winspool share. Windows 9x draws and measures W text itself
 * (TextOutW, ExtTextOutW, GetTextExtentPoint32W and the like), so those
 * go to the system's W functions.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"

OW_REAL_DECL(BOOL, TextOutW, (HDC, int, int, LPCWSTR, int), 20);
OW_REAL_DECL(BOOL, ExtTextOutW,
             (HDC, int, int, UINT, const RECT *, LPCWSTR, UINT, const INT *),
             32);
OW_REAL_DECL(BOOL, GetTextExtentPoint32W, (HDC, LPCWSTR, int, LPSIZE), 16);
OW_REAL_DECL(BOOL, GetTextExtentPointW, (HDC, LPCWSTR, int, LPSIZE), 16);
OW_REAL_DECL(BOOL, GetTextExtentExPointW,
             (HDC, LPCWSTR, int, int, LPINT, LPINT, LPSIZE), 28);
OW_REAL_DECL(BOOL, GetCharWidthW, (HDC, UINT, UINT, LPINT), 16);

/* ---- conversions ------------------------------------------------------ */

void ow_logfont_a(const LOGFONTW *w, LOGFONTA *a) {
  ow_memcpy(a, w, offsetof(LOGFONTA, lfFaceName));
  ow_toa(w->lfFaceName, ow_wcsnlen(w->lfFaceName, LF_FACESIZE), a->lfFaceName,
         LF_FACESIZE);
}

void ow_logfont_w(const LOGFONTA *a, LOGFONTW *w) {
  int n = 0;
  while (n < LF_FACESIZE && a->lfFaceName[n])
    n++;
  ow_memcpy(w, a, offsetof(LOGFONTW, lfFaceName));
  ow_tow(a->lfFaceName, n, w->lfFaceName, LF_FACESIZE);
}

static WCHAR byte_w(BYTE c) {
  char b = (char)c;
  WCHAR w;
  return ow_mb2wc(CP_ACP, 0, &b, 1, &w, 1) ? w : c;
}

void ow_textmetric_w(const TEXTMETRICA *a, TEXTMETRICW *w) {
  w->tmHeight = a->tmHeight;
  w->tmAscent = a->tmAscent;
  w->tmDescent = a->tmDescent;
  w->tmInternalLeading = a->tmInternalLeading;
  w->tmExternalLeading = a->tmExternalLeading;
  w->tmAveCharWidth = a->tmAveCharWidth;
  w->tmMaxCharWidth = a->tmMaxCharWidth;
  w->tmWeight = a->tmWeight;
  w->tmOverhang = a->tmOverhang;
  w->tmDigitizedAspectX = a->tmDigitizedAspectX;
  w->tmDigitizedAspectY = a->tmDigitizedAspectY;
  w->tmFirstChar = byte_w(a->tmFirstChar);
  /* The last character of a font in a single-byte code page is the last
     byte; its Unicode value can be lower than others in the font. */
  w->tmLastChar = a->tmLastChar == 0xff ? 0xfffc : byte_w(a->tmLastChar);
  w->tmDefaultChar = byte_w(a->tmDefaultChar);
  w->tmBreakChar = byte_w(a->tmBreakChar);
  w->tmItalic = a->tmItalic;
  w->tmUnderlined = a->tmUnderlined;
  w->tmStruckOut = a->tmStruckOut;
  w->tmPitchAndFamily = a->tmPitchAndFamily;
  w->tmCharSet = a->tmCharSet;
}

static void ntm_w(const NEWTEXTMETRICA *a, NEWTEXTMETRICW *w) {
  ow_textmetric_w((const TEXTMETRICA *)a, (TEXTMETRICW *)w);
  w->ntmFlags = a->ntmFlags;
  w->ntmSizeEM = a->ntmSizeEM;
  w->ntmCellHeight = a->ntmCellHeight;
  w->ntmAvgWidth = a->ntmAvgWidth;
}

/* The fields between the two names and after the second are the same in
 * DEVMODEA and DEVMODEW. */
#define DM_MID_A offsetof(DEVMODEA, dmSpecVersion)
#define DM_MID_W offsetof(DEVMODEW, dmSpecVersion)
#define DM_MIDLEN (offsetof(DEVMODEA, dmFormName) - DM_MID_A)
#define DM_TAIL_A offsetof(DEVMODEA, dmLogPixels)
#define DM_TAIL_W offsetof(DEVMODEW, dmLogPixels)

LPDEVMODEA ow_devmode_a(const DEVMODEW *w) {
  LPDEVMODEA a;
  DWORD asize;
  if (!w)
    return NULL;
  asize = w->dmSize > DM_TAIL_W ? w->dmSize - (DM_TAIL_W - DM_TAIL_A)
                                : DM_TAIL_A;
  a = ow_alloc(asize + w->dmDriverExtra);
  if (!a)
    return NULL;
  ow_toa(w->dmDeviceName, ow_wcsnlen(w->dmDeviceName, CCHDEVICENAME),
         (LPSTR)a->dmDeviceName, CCHDEVICENAME);
  ow_memcpy((BYTE *)a + DM_MID_A, (const BYTE *)w + DM_MID_W, DM_MIDLEN);
  if (w->dmSize > offsetof(DEVMODEW, dmFormName))
    ow_toa(w->dmFormName, ow_wcsnlen(w->dmFormName, CCHFORMNAME),
           (LPSTR)a->dmFormName, CCHFORMNAME);
  if (w->dmSize > DM_TAIL_W)
    ow_memcpy((BYTE *)a + DM_TAIL_A, (const BYTE *)w + DM_TAIL_W,
              w->dmSize - DM_TAIL_W);
  a->dmSize = (WORD)asize;
  ow_memcpy((BYTE *)a + asize, (const BYTE *)w + w->dmSize, w->dmDriverExtra);
  return a;
}

static void devmode_w_into(const DEVMODEA *a, DEVMODEW *w, DWORD wsize,
                           DWORD extra) {
  int n = 0;
  while (n < CCHDEVICENAME && a->dmDeviceName[n])
    n++;
  ow_tow((LPCSTR)a->dmDeviceName, n, w->dmDeviceName, CCHDEVICENAME);
  ow_memcpy((BYTE *)w + DM_MID_W, (const BYTE *)a + DM_MID_A, DM_MIDLEN);
  if (wsize > offsetof(DEVMODEW, dmFormName)) {
    n = 0;
    while (n < CCHFORMNAME && a->dmFormName[n])
      n++;
    ow_tow((LPCSTR)a->dmFormName, n, w->dmFormName, CCHFORMNAME);
  }
  if (wsize > DM_TAIL_W && a->dmSize > DM_TAIL_A) {
    DWORD tail = a->dmSize - DM_TAIL_A;
    if (tail > wsize - DM_TAIL_W)
      tail = wsize - DM_TAIL_W;
    ow_memcpy((BYTE *)w + DM_TAIL_W, (const BYTE *)a + DM_TAIL_A, tail);
  }
  w->dmSize = (WORD)wsize;
  if (extra > a->dmDriverExtra)
    extra = a->dmDriverExtra;
  w->dmDriverExtra = (WORD)extra;
  ow_memcpy((BYTE *)w + wsize, (const BYTE *)a + a->dmSize, extra);
}

LPDEVMODEW ow_devmode_w(const DEVMODEA *a) {
  LPDEVMODEW w;
  DWORD wsize;
  if (!a)
    return NULL;
  wsize = a->dmSize > DM_TAIL_A ? a->dmSize + (DM_TAIL_W - DM_TAIL_A)
                                : DM_TAIL_W;
  w = ow_alloc(wsize + a->dmDriverExtra);
  if (w)
    devmode_w_into(a, w, wsize, a->dmDriverExtra);
  return w;
}

void ow_devmode_to_w(const DEVMODEA *a, DEVMODEW *w) {
  DWORD wsize = w->dmSize ? w->dmSize : sizeof *w;
  devmode_w_into(a, w, wsize, w->dmDriverExtra);
}

/* The code of W character c in the ANSI code page (one byte or a DBCS
 * pair), or -1. */
static int char_code(WCHAR c) {
  char b[4];
  BOOL used = FALSE;
  int n = WideCharToMultiByte(CP_ACP, 0, &c, 1, b, sizeof b, NULL, &used);
  if (used || n < 1 || n > 2)
    return -1;
  return n == 1 ? (BYTE)b[0] : ((BYTE)b[0] << 8) | (BYTE)b[1];
}

/* ---- fonts ------------------------------------------------------------- */

int WINAPI AddFontResourceW(LPCWSTR file) {
  LPSTR a = ow_acp(file, ow_fcp());
  int r = AddFontResourceA(a);
  ow_free(a);
  return r;
}

BOOL WINAPI RemoveFontResourceW(LPCWSTR file) {
  LPSTR a = ow_acp(file, ow_fcp());
  BOOL r = RemoveFontResourceA(a);
  ow_free(a);
  return r;
}

BOOL WINAPI CreateScalableFontResourceW(DWORD hidden, LPCWSTR res,
                                        LPCWSTR file, LPCWSTR path) {
  UINT cp = ow_fcp();
  LPSTR a = ow_acp(res, cp), b = ow_acp(file, cp), c = ow_acp(path, cp);
  BOOL r = CreateScalableFontResourceA(hidden, a, b, c);
  ow_free(a);
  ow_free(b);
  ow_free(c);
  return r;
}

HFONT WINAPI CreateFontIndirectW(const LOGFONTW *w) {
  LOGFONTA a;
  if (!w) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  ow_logfont_a(w, &a);
  return CreateFontIndirectA(&a);
}

HFONT WINAPI CreateFontW(int h, int wd, int esc, int orient, int weight,
                         DWORD italic, DWORD under, DWORD strike, DWORD cs,
                         DWORD outp, DWORD clip, DWORD q, DWORD pitch,
                         LPCWSTR face) {
  LPSTR a = ow_a(face);
  HFONT r = CreateFontA(h, wd, esc, orient, weight, italic, under, strike, cs,
                        outp, clip, q, pitch, a);
  ow_free(a);
  return r;
}

struct font_ctx {
  void *proc;
  LPARAM lp;
};

static int CALLBACK font_ex_cb(const LOGFONTA *lf, const TEXTMETRICA *tm,
                               DWORD type, LPARAM lp) {
  struct font_ctx *c = (struct font_ctx *)lp;
  const ENUMLOGFONTEXA *a = (const ENUMLOGFONTEXA *)lf;
  ENUMLOGFONTEXW elf;
  NEWTEXTMETRICEXW ntm;
  ow_memset(&ntm, 0, sizeof ntm);
  ow_logfont_w(&a->elfLogFont, &elf.elfLogFont);
  ow_tow((LPCSTR)a->elfFullName, -1, elf.elfFullName, LF_FULLFACESIZE);
  ow_tow((LPCSTR)a->elfStyle, -1, elf.elfStyle, LF_FACESIZE);
  ow_tow((LPCSTR)a->elfScript, -1, elf.elfScript, LF_FACESIZE);
  if (type & TRUETYPE_FONTTYPE) {
    const NEWTEXTMETRICEXA *n = (const NEWTEXTMETRICEXA *)tm;
    ntm_w(&n->ntmTm, &ntm.ntmTm);
    ntm.ntmFontSig = n->ntmFontSig;
  } else {
    ow_textmetric_w(tm, (TEXTMETRICW *)&ntm.ntmTm);
  }
  return ((FONTENUMPROCW)c->proc)((const LOGFONTW *)&elf,
                                  (const TEXTMETRICW *)&ntm, type, c->lp);
}

int WINAPI EnumFontFamiliesExW(HDC dc, LPLOGFONTW w, FONTENUMPROCW proc,
                               LPARAM lp, DWORD flags) {
  LOGFONTA a;
  struct font_ctx c = {(void *)proc, lp};
  if (!w || !proc) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  ow_logfont_a(w, &a);
  return EnumFontFamiliesExA(dc, &a, font_ex_cb, (LPARAM)&c, flags);
}

static int CALLBACK font_fam_cb(const LOGFONTA *lf, const TEXTMETRICA *tm,
                                DWORD type, LPARAM lp) {
  struct font_ctx *c = (struct font_ctx *)lp;
  const ENUMLOGFONTA *a = (const ENUMLOGFONTA *)lf;
  ENUMLOGFONTW elf;
  NEWTEXTMETRICW ntm;
  ow_memset(&ntm, 0, sizeof ntm);
  ow_logfont_w(&a->elfLogFont, &elf.elfLogFont);
  ow_tow((LPCSTR)a->elfFullName, -1, elf.elfFullName, LF_FULLFACESIZE);
  ow_tow((LPCSTR)a->elfStyle, -1, elf.elfStyle, LF_FACESIZE);
  if (type & TRUETYPE_FONTTYPE)
    ntm_w((const NEWTEXTMETRICA *)tm, &ntm);
  else
    ow_textmetric_w(tm, (TEXTMETRICW *)&ntm);
  return ((FONTENUMPROCW)c->proc)((const LOGFONTW *)&elf,
                                  (const TEXTMETRICW *)&ntm, type, c->lp);
}

int WINAPI EnumFontFamiliesW(HDC dc, LPCWSTR family, FONTENUMPROCW proc,
                             LPARAM lp) {
  struct font_ctx c = {(void *)proc, lp};
  LPSTR a = ow_a(family);
  int r = EnumFontFamiliesA(dc, a, font_fam_cb, (LPARAM)&c);
  ow_free(a);
  return r;
}

static int CALLBACK fonts_cb(const LOGFONTA *lf, const TEXTMETRICA *tm,
                             DWORD type, LPARAM lp) {
  struct font_ctx *c = (struct font_ctx *)lp;
  LOGFONTW w;
  TEXTMETRICW t;
  ow_logfont_w(lf, &w);
  ow_textmetric_w(tm, &t);
  return ((FONTENUMPROCW)c->proc)(&w, &t, type, c->lp);
}

int WINAPI EnumFontsW(HDC dc, LPCWSTR face, FONTENUMPROCW proc, LPARAM lp) {
  struct font_ctx c = {(void *)proc, lp};
  LPSTR a = ow_a(face);
  int r = EnumFontsA(dc, a, fonts_cb, (LPARAM)&c);
  ow_free(a);
  return r;
}

int WINAPI GetTextFaceW(HDC dc, int cch, LPWSTR buf) {
  char a[LF_FACESIZE * 2 + 2];
  int n = GetTextFaceA(dc, sizeof a, a);
  if (!n)
    return 0;
  if (!buf)
    return ow_wlen_of(a, -1, CP_ACP) + 1;
  return ow_tow(a, -1, buf, cch) + 1;
}

BOOL WINAPI GetTextMetricsW(HDC dc, LPTEXTMETRICW w) {
  TEXTMETRICA a;
  if (!w || !GetTextMetricsA(dc, &a))
    return FALSE;
  ow_textmetric_w(&a, w);
  return TRUE;
}

UINT WINAPI GetOutlineTextMetricsW(HDC dc, UINT size,
                                   LPOUTLINETEXTMETRICW w) {
  UINT an = GetOutlineTextMetricsA(dc, 0, NULL), need, pos;
  LPOUTLINETEXTMETRICA a;
  OUTLINETEXTMETRICW tmp;
  LPWSTR strs[4];
  int lens[4];
  PSTR offs[4];
  BYTE *out;
  if (!an)
    return 0;
  a = ow_alloc(an);
  if (!a)
    return 0;
  if (!GetOutlineTextMetricsA(dc, an, a)) {
    ow_free(a);
    return 0;
  }
  offs[0] = a->otmpFamilyName;
  offs[1] = a->otmpFaceName;
  offs[2] = a->otmpStyleName;
  offs[3] = a->otmpFullName;
  need = sizeof tmp;
  for (int i = 0; i < 4; i++) {
    UINT_PTR o = (UINT_PTR)offs[i];
    strs[i] = o && o < an ? ow_wn((LPCSTR)a + o, -1, &lens[i]) : NULL;
    if (!strs[i])
      lens[i] = 0;
    need += (lens[i] + 1) * sizeof(WCHAR);
  }
  if (!w) {
    for (int i = 0; i < 4; i++)
      ow_free(strs[i]);
    ow_free(a);
    return need;
  }
  out = ow_alloc(need);
  if (!out) {
    for (int i = 0; i < 4; i++)
      ow_free(strs[i]);
    ow_free(a);
    return 0;
  }
  tmp.otmSize = need;
  ow_textmetric_w(&a->otmTextMetrics, &tmp.otmTextMetrics);
  ow_memcpy(&tmp.otmFiller, &a->otmFiller,
            offsetof(OUTLINETEXTMETRICA, otmpFamilyName) -
                offsetof(OUTLINETEXTMETRICA, otmFiller));
  pos = sizeof tmp;
  for (int i = 0; i < 4; i++) {
    PSTR *dst = i == 0   ? &tmp.otmpFamilyName
                : i == 1 ? &tmp.otmpFaceName
                : i == 2 ? &tmp.otmpStyleName
                         : &tmp.otmpFullName;
    *dst = (PSTR)(UINT_PTR)pos;
    if (strs[i])
      ow_memcpy(out + pos, strs[i], lens[i] * sizeof(WCHAR));
    pos += (lens[i] + 1) * sizeof(WCHAR);
    ow_free(strs[i]);
  }
  ow_memcpy(out, &tmp, sizeof tmp);
  ow_memcpy(w, out, size < need ? size : need);
  ow_free(out);
  ow_free(a);
  return size < need ? 0 : need;
}

int WINAPI GetObjectW(HANDLE h, int size, LPVOID buf) {
  if (GetObjectType(h) == OBJ_FONT) {
    LOGFONTA a;
    LOGFONTW w;
    if (!buf)
      return sizeof(LOGFONTW);
    if (!GetObjectA(h, sizeof a, &a))
      return 0;
    ow_logfont_w(&a, &w);
    if (size > (int)sizeof w)
      size = sizeof w;
    ow_memcpy(buf, &w, size);
    return size;
  }
  return GetObjectA(h, size, buf);
}

/* ---- text ------------------------------------------------------------- */

BOOL WINAPI TextOutW(HDC dc, int x, int y, LPCWSTR s, int n) {
  return REAL(TextOutW)(dc, x, y, s, n);
}

BOOL WINAPI ExtTextOutW(HDC dc, int x, int y, UINT opts, const RECT *rc,
                        LPCWSTR s, UINT n, const INT *dx) {
  return REAL(ExtTextOutW)(dc, x, y, opts, rc, s, n, dx);
}

BOOL WINAPI PolyTextOutW(HDC dc, const POLYTEXTW *pt, int n) {
  for (int i = 0; i < n; i++) {
    if (!REAL(ExtTextOutW)(dc, pt[i].x, pt[i].y, pt[i].uiFlags, &pt[i].rcl,
                           pt[i].lpstr, pt[i].n, pt[i].pdx))
      return FALSE;
  }
  return TRUE;
}

BOOL WINAPI GetTextExtentPoint32W(HDC dc, LPCWSTR s, int n, LPSIZE sz) {
  return REAL(GetTextExtentPoint32W)(dc, s, n, sz);
}

BOOL WINAPI GetTextExtentPointW(HDC dc, LPCWSTR s, int n, LPSIZE sz) {
  return REAL(GetTextExtentPointW)(dc, s, n, sz);
}

BOOL WINAPI GetTextExtentExPointW(HDC dc, LPCWSTR s, int n, int max,
                                  LPINT fit, LPINT dx, LPSIZE sz) {
  return REAL(GetTextExtentExPointW)(dc, s, n, max, fit, dx, sz);
}

BOOL WINAPI GetCharWidthW(HDC dc, UINT first, UINT last, LPINT w) {
  return REAL(GetCharWidthW)(dc, first, last, w);
}

BOOL WINAPI GetCharWidth32W(HDC dc, UINT first, UINT last, LPINT w) {
  return REAL(GetCharWidthW)(dc, first, last, w);
}

BOOL WINAPI GetCharWidthFloatW(HDC dc, UINT first, UINT last, PFLOAT w) {
  OW_FN(OW_GDI32, GetCharWidthFloatA, return FALSE);
  if (!w || last < first) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  for (UINT c = first; c <= last; c++) {
    int code = char_code((WCHAR)c), iw;
    FLOAT f;
    if (code >= 0 && pGetCharWidthFloatA(dc, code, code, &f)) {
      w[c - first] = f;
    } else if (REAL(GetCharWidthW)(dc, c, c, &iw)) {
      w[c - first] = (FLOAT)iw;
    } else {
      return FALSE;
    }
  }
  return TRUE;
}

BOOL WINAPI GetCharABCWidthsW(HDC dc, UINT first, UINT last, LPABC abc) {
  if (!abc || last < first) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  for (UINT c = first; c <= last; c++) {
    int code = char_code((WCHAR)c), iw;
    ABC *x = &abc[c - first];
    if (code >= 0 && GetCharABCWidthsA(dc, code, code, x))
      continue;
    /* Not a TrueType font, or not in the code page: no overhangs. */
    if (!REAL(GetCharWidthW)(dc, c, c, &iw))
      return FALSE;
    x->abcA = 0;
    x->abcB = iw;
    x->abcC = 0;
  }
  return TRUE;
}

BOOL WINAPI GetCharABCWidthsFloatW(HDC dc, UINT first, UINT last,
                                   LPABCFLOAT abc) {
  OW_FN(OW_GDI32, GetCharABCWidthsFloatA, return FALSE);
  if (!abc || last < first) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  for (UINT c = first; c <= last; c++) {
    int code = char_code((WCHAR)c), iw;
    ABCFLOAT *x = &abc[c - first];
    if (code >= 0 && pGetCharABCWidthsFloatA(dc, code, code, x))
      continue;
    if (!REAL(GetCharWidthW)(dc, c, c, &iw))
      return FALSE;
    x->abcfA = 0;
    x->abcfB = (FLOAT)iw;
    x->abcfC = 0;
  }
  return TRUE;
}

DWORD WINAPI GetGlyphOutlineW(HDC dc, UINT c, UINT fmt, LPGLYPHMETRICS gm,
                              DWORD size, LPVOID buf, const MAT2 *mat) {
  int code;
  if (fmt & GGO_GLYPH_INDEX)
    return GetGlyphOutlineA(dc, c, fmt, gm, size, buf, mat);
  code = char_code((WCHAR)c);
  if (code < 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return GDI_ERROR;
  }
  return GetGlyphOutlineA(dc, code, fmt, gm, size, buf, mat);
}

static WORD code_w(WORD code) {
  char b[2];
  WCHAR w;
  int n;
  if (code > 0xff) {
    b[0] = (char)(code >> 8);
    b[1] = (char)code;
    n = 2;
  } else {
    b[0] = (char)code;
    n = 1;
  }
  return ow_mb2wc(CP_ACP, 0, b, n, &w, 1) ? w : code;
}

DWORD WINAPI GetKerningPairsW(HDC dc, DWORD n, LPKERNINGPAIR pairs) {
  DWORD r = GetKerningPairsA(dc, n, pairs);
  if (pairs)
    for (DWORD i = 0; i < r && i < n; i++) {
      pairs[i].wFirst = code_w(pairs[i].wFirst);
      pairs[i].wSecond = code_w(pairs[i].wSecond);
    }
  return r;
}

DWORD WINAPI GetCharacterPlacementW(HDC dc, LPCWSTR s, int n, int max,
                                    LPGCP_RESULTSW res, DWORD flags) {
  GCP_RESULTSA a;
  int al;
  LPSTR str;
  DWORD r;
  int *map = NULL; /* the byte where each W character starts */
  if (!s || n < 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  str = ow_an(s, n, &al);
  if (!str)
    return 0;
  if (!res) {
    r = GetCharacterPlacementA(dc, str, al, max, NULL, flags);
    ow_free(str);
    return r;
  }
  ow_memset(&a, 0, sizeof a);
  a.lStructSize = sizeof a;
  a.nGlyphs = res->nGlyphs;
  a.nMaxFit = res->nMaxFit;
  if (al == n) {
    /* One byte per character: the arrays line up. */
    a.lpOutString = res->lpOutString ? ow_alloc(n + 1) : NULL;
    a.lpOrder = res->lpOrder;
    a.lpDx = res->lpDx;
    a.lpCaretPos = res->lpCaretPos;
    a.lpClass = res->lpClass;
    a.lpGlyphs = res->lpGlyphs;
    r = GetCharacterPlacementA(dc, str, al, max, &a, flags);
    if (r && a.lpOutString)
      ow_tow(a.lpOutString, n, res->lpOutString, n + 1);
    ow_free(a.lpOutString);
    res->nGlyphs = a.nGlyphs;
    res->nMaxFit = a.nMaxFit;
    ow_free(str);
    return r;
  }
  /* DBCS: per-byte results, gathered per character. */
  map = ow_alloc((n + 1) * sizeof *map);
  a.lpOrder = res->lpOrder ? ow_alloc(al * sizeof(UINT)) : NULL;
  a.lpDx = res->lpDx ? ow_alloc(al * sizeof(int)) : NULL;
  a.lpCaretPos = res->lpCaretPos ? ow_alloc(al * sizeof(int)) : NULL;
  a.lpClass = res->lpClass ? ow_alloc(al) : NULL;
  a.lpGlyphs = res->lpGlyphs ? ow_alloc(al * sizeof(WCHAR)) : NULL;
  a.nGlyphs = al;
  if (!map) {
    r = 0;
    goto done;
  }
  for (int i = 0, b = 0; i <= n; i++) {
    map[i] = b;
    if (i < n)
      b += ow_wc2mb(CP_ACP, 0, s + i, 1, NULL, 0);
  }
  r = GetCharacterPlacementA(dc, str, al, max, &a, flags);
  if (r) {
    for (int i = 0; i < n; i++) {
      int b = map[i], e = map[i + 1];
      if (res->lpDx) {
        int sum = 0;
        for (int k = b; k < e && k < al; k++)
          sum += a.lpDx[k];
        res->lpDx[i] = sum;
      }
      if (res->lpCaretPos)
        res->lpCaretPos[i] = b < al ? a.lpCaretPos[b] : 0;
      if (res->lpClass)
        res->lpClass[i] = b < al ? a.lpClass[b] : 0;
      if (res->lpOrder)
        res->lpOrder[i] = i;
      if (res->lpGlyphs && i < (int)res->nGlyphs)
        res->lpGlyphs[i] = b < al ? a.lpGlyphs[b] : 0;
    }
    if (res->lpOutString)
      ow_wcsncpyz(res->lpOutString, s, n + 1);
    res->nGlyphs = n;
    {
      int fit = 0;
      while (fit < n && map[fit + 1] <= a.nMaxFit)
        fit++;
      res->nMaxFit = fit;
    }
  }
done:
  ow_free(map);
  ow_free(a.lpOrder);
  ow_free(a.lpDx);
  ow_free(a.lpCaretPos);
  ow_free(a.lpClass);
  ow_free(a.lpGlyphs);
  ow_free(str);
  return r;
}

/* ---- device contexts ---------------------------------------------------- */

HDC WINAPI CreateDCW(LPCWSTR driver, LPCWSTR device, LPCWSTR output,
                     const DEVMODEW *dm) {
  LPSTR a = ow_a(driver), b = ow_a(device), c = ow_a(output);
  LPDEVMODEA d = ow_devmode_a(dm);
  HDC r = CreateDCA(a, b, c, d);
  ow_free(a);
  ow_free(b);
  ow_free(c);
  ow_free(d);
  return r;
}

HDC WINAPI CreateICW(LPCWSTR driver, LPCWSTR device, LPCWSTR output,
                     const DEVMODEW *dm) {
  LPSTR a = ow_a(driver), b = ow_a(device), c = ow_a(output);
  LPDEVMODEA d = ow_devmode_a(dm);
  HDC r = CreateICA(a, b, c, d);
  ow_free(a);
  ow_free(b);
  ow_free(c);
  ow_free(d);
  return r;
}

HDC WINAPI ResetDCW(HDC dc, const DEVMODEW *dm) {
  LPDEVMODEA d = ow_devmode_a(dm);
  HDC r = ResetDCA(dc, d);
  ow_free(d);
  return r;
}

int WINAPI StartDocW(HDC dc, const DOCINFOW *w) {
  DOCINFOA a;
  int r;
  if (!w) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  a.cbSize = sizeof a;
  a.lpszDocName = ow_a(w->lpszDocName);
  a.lpszOutput = ow_acp(w->lpszOutput, ow_fcp());
  a.lpszDatatype = w->cbSize > (int)offsetof(DOCINFOW, lpszDatatype)
                       ? ow_a(w->lpszDatatype)
                       : NULL;
  a.fwType = w->cbSize > (int)offsetof(DOCINFOW, fwType) ? w->fwType : 0;
  r = StartDocA(dc, &a);
  ow_free(a.lpszDocName);
  ow_free(a.lpszOutput);
  ow_free(a.lpszDatatype);
  return r;
}

/* ---- metafiles ----------------------------------------------------------- */

HENHMETAFILE WINAPI CopyEnhMetaFileW(HENHMETAFILE h, LPCWSTR file) {
  LPSTR a = ow_acp(file, ow_fcp());
  HENHMETAFILE r = CopyEnhMetaFileA(h, a);
  ow_free(a);
  return r;
}

HMETAFILE WINAPI CopyMetaFileW(HMETAFILE h, LPCWSTR file) {
  LPSTR a = ow_acp(file, ow_fcp());
  HMETAFILE r = CopyMetaFileA(h, a);
  ow_free(a);
  return r;
}

HDC WINAPI CreateEnhMetaFileW(HDC dc, LPCWSTR file, const RECT *rc,
                              LPCWSTR desc) {
  LPSTR a = ow_acp(file, ow_fcp()), d = ow_alist(desc);
  HDC r = CreateEnhMetaFileA(dc, a, rc, d);
  ow_free(a);
  ow_free(d);
  return r;
}

HDC WINAPI CreateMetaFileW(LPCWSTR file) {
  LPSTR a = ow_acp(file, ow_fcp());
  HDC r = CreateMetaFileA(a);
  ow_free(a);
  return r;
}

HENHMETAFILE WINAPI GetEnhMetaFileW(LPCWSTR file) {
  LPSTR a = ow_acp(file, ow_fcp());
  HENHMETAFILE r = GetEnhMetaFileA(a);
  ow_free(a);
  return r;
}

HMETAFILE WINAPI GetMetaFileW(LPCWSTR file) {
  LPSTR a = ow_acp(file, ow_fcp());
  HMETAFILE r = GetMetaFileA(a);
  ow_free(a);
  return r;
}

UINT WINAPI GetEnhMetaFileDescriptionW(HENHMETAFILE h, UINT cch, LPWSTR buf) {
  UINT n = GetEnhMetaFileDescriptionA(h, 0, NULL), wl;
  LPSTR a;
  LPWSTR w;
  if (n == GDI_ERROR || n == 0)
    return n;
  a = ow_alloc(n + 2);
  if (!a)
    return GDI_ERROR;
  n = GetEnhMetaFileDescriptionA(h, n, a);
  /* Two strings, each with its NUL: count up to the second. */
  w = ow_wn(a, n, (int *)&wl);
  ow_free(a);
  if (!w)
    return GDI_ERROR;
  if (buf) {
    if (wl > cch)
      wl = cch;
    ow_memcpy(buf, w, wl * sizeof(WCHAR));
  }
  ow_free(w);
  return wl;
}

/* ---- color management (from Windows 95 on, loaded when first used) ----- */

static void lcs_a(const LOGCOLORSPACEW *w, LOGCOLORSPACEA *a) {
  ow_memcpy(a, w, offsetof(LOGCOLORSPACEA, lcsFilename));
  ow_toa(w->lcsFilename, ow_wcsnlen(w->lcsFilename, MAX_PATH), a->lcsFilename,
         MAX_PATH);
  a->lcsSize = sizeof *a;
}

HCOLORSPACE WINAPI CreateColorSpaceW(LPLOGCOLORSPACEW w) {
  LOGCOLORSPACEA a;
  OW_FN(OW_GDI32, CreateColorSpaceA, return NULL);
  if (!w) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  lcs_a(w, &a);
  return pCreateColorSpaceA(&a);
}

BOOL WINAPI GetLogColorSpaceW(HCOLORSPACE h, LPLOGCOLORSPACEW w, DWORD size) {
  LOGCOLORSPACEA a;
  OW_FN(OW_GDI32, GetLogColorSpaceA, return FALSE);
  if (!w || size < sizeof *w) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!pGetLogColorSpaceA(h, &a, sizeof a))
    return FALSE;
  ow_memcpy(w, &a, offsetof(LOGCOLORSPACEA, lcsFilename));
  ow_tow(a.lcsFilename, -1, w->lcsFilename, MAX_PATH);
  w->lcsSize = sizeof *w;
  return TRUE;
}

BOOL WINAPI GetICMProfileW(HDC dc, LPDWORD size, LPWSTR buf) {
  char a[MAX_PATH * 2];
  DWORD n = sizeof a, wl;
  OW_FN(OW_GDI32, GetICMProfileA, return FALSE);
  if (!size) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!pGetICMProfileA(dc, &n, a))
    return FALSE;
  wl = ow_wlen_of(a, -1, ow_fcp()) + 1;
  if (!buf || *size < wl) {
    *size = wl;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  ow_towcp(a, -1, buf, *size, ow_fcp());
  *size = wl;
  return TRUE;
}

BOOL WINAPI SetICMProfileW(HDC dc, LPWSTR file) {
  LPSTR a;
  BOOL r;
  OW_FN(OW_GDI32, SetICMProfileA, return FALSE);
  a = ow_acp(file, ow_fcp());
  r = pSetICMProfileA(dc, a);
  ow_free(a);
  return r;
}

struct icm_ctx {
  ICMENUMPROCW proc;
  LPARAM lp;
};
static int CALLBACK icm_cb(LPSTR file, LPARAM lp) {
  struct icm_ctx *c = (struct icm_ctx *)lp;
  LPWSTR w = ow_wcp(file, ow_fcp());
  int r = c->proc(w ? w : (LPWSTR)L"", c->lp);
  ow_free(w);
  return r;
}

int WINAPI EnumICMProfilesW(HDC dc, ICMENUMPROCW proc, LPARAM lp) {
  struct icm_ctx c = {proc, lp};
  OW_FN(OW_GDI32, EnumICMProfilesA, return -1);
  return pEnumICMProfilesA(dc, icm_cb, (LPARAM)&c);
}

BOOL WINAPI UpdateICMRegKeyW(DWORD reserved, LPWSTR cmid, LPWSTR file,
                             UINT cmd) {
  LPSTR a, b;
  BOOL r;
  OW_FN(OW_GDI32, UpdateICMRegKeyA, return FALSE);
  a = ow_a(cmid);
  b = ow_acp(file, ow_fcp());
  r = pUpdateICMRegKeyA(reserved, a, b, cmd);
  ow_free(a);
  ow_free(b);
  return r;
}
