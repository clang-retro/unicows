/* Unicode case mapping and character types, for the functions that must
 * handle characters the ANSI code page does not have (CharUpperW,
 * IsCharAlphaW, GetStringTypeW, LCMapStringW). It covers the alphabets
 * with case (Latin, Greek, Cyrillic, Armenian and the like) and classifies
 * the main scripts; it is not the full Unicode database.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"

/* Ranges of upper-case letters. step 1: lower = upper + delta for the
 * whole range; step 2: every other code point from lo is upper case and
 * the next one its lower case. */
static const struct {
  WCHAR lo, hi;
  short delta;
  BYTE step;
} ranges[] = {
    {0x0041, 0x005a, 32, 1},  {0x00c0, 0x00d6, 32, 1},
    {0x00d8, 0x00de, 32, 1},  {0x0100, 0x012e, 1, 2},
    {0x0132, 0x0136, 1, 2},   {0x0139, 0x0147, 1, 2},
    {0x014a, 0x0176, 1, 2},   {0x0179, 0x017d, 1, 2},
    {0x0182, 0x0184, 1, 2},   {0x0187, 0x0187, 1, 2},
    {0x018b, 0x018b, 1, 2},   {0x0191, 0x0191, 1, 2},
    {0x0198, 0x0198, 1, 2},   {0x01a0, 0x01a4, 1, 2},
    {0x01a7, 0x01a7, 1, 2},   {0x01ac, 0x01ac, 1, 2},
    {0x01af, 0x01af, 1, 2},   {0x01b3, 0x01b5, 1, 2},
    {0x01b8, 0x01b8, 1, 2},   {0x01bc, 0x01bc, 1, 2},
    {0x01cd, 0x01db, 1, 2},   {0x01de, 0x01ee, 1, 2},
    {0x01f4, 0x01f4, 1, 2},   {0x01f8, 0x021e, 1, 2},
    {0x0222, 0x0232, 1, 2},   {0x0246, 0x024e, 1, 2},
    {0x0391, 0x03a1, 32, 1},  {0x03a3, 0x03ab, 32, 1},
    {0x0388, 0x038a, 37, 1},  {0x038e, 0x038f, 63, 1},
    {0x03d8, 0x03ee, 1, 2},   {0x0400, 0x040f, 80, 1},
    {0x0410, 0x042f, 32, 1},  {0x0460, 0x0480, 1, 2},
    {0x048a, 0x04be, 1, 2},   {0x04c1, 0x04cd, 1, 2},
    {0x04d0, 0x052e, 1, 2},   {0x0531, 0x0556, 48, 1},
    {0x10a0, 0x10c5, 7264, 1}, {0x1e00, 0x1e94, 1, 2},
    {0x1ea0, 0x1efe, 1, 2},   {0x1f08, 0x1f0f, -8, 1},
    {0x1f18, 0x1f1d, -8, 1},  {0x1f28, 0x1f2f, -8, 1},
    {0x1f38, 0x1f3f, -8, 1},  {0x1f48, 0x1f4d, -8, 1},
    {0x1f68, 0x1f6f, -8, 1},  {0x2160, 0x216f, 16, 1},
    {0x24b6, 0x24cf, 26, 1},  {0x2c00, 0x2c2e, 48, 1},
    {0xff21, 0xff3a, 32, 1},
};

/* Pairs outside the ranges. */
static const WCHAR pairs[][2] = {
    {0x0178, 0x00ff}, {0x0181, 0x0253}, {0x0186, 0x0254}, {0x0189, 0x0256},
    {0x018a, 0x0257}, {0x018e, 0x01dd}, {0x018f, 0x0259}, {0x0190, 0x025b},
    {0x0193, 0x0260}, {0x0194, 0x0263}, {0x0196, 0x0269}, {0x0197, 0x0268},
    {0x019c, 0x026f}, {0x019d, 0x0272}, {0x019f, 0x0275}, {0x01a9, 0x0283},
    {0x01ae, 0x0288}, {0x01b1, 0x028a}, {0x01b2, 0x028b}, {0x01b7, 0x0292},
    {0x01c4, 0x01c6}, {0x01c7, 0x01c9}, {0x01ca, 0x01cc}, {0x01f1, 0x01f3},
    {0x01f6, 0x0195}, {0x01f7, 0x01bf}, {0x0220, 0x019e}, {0x0386, 0x03ac},
    {0x038c, 0x03cc}, {0x0130, 0x0069}, {0x1e9e, 0x00df},
};

/* Lower-case letters that map to an upper case that maps back to another
 * lower case: they are only converted upwards. */
static const WCHAR up_only[][2] = {
    {0x00b5, 0x039c}, {0x0131, 0x0049}, {0x017f, 0x0053}, {0x01c5, 0x01c4},
    {0x01c8, 0x01c7}, {0x01cb, 0x01ca}, {0x01f2, 0x01f1}, {0x03c2, 0x03a3},
    {0x03d0, 0x0392}, {0x03d1, 0x0398}, {0x03d5, 0x03a6}, {0x03d6, 0x03a0},
    {0x03f0, 0x039a}, {0x03f1, 0x03a1},
};

WCHAR ow_lower(WCHAR c) {
  unsigned i;
  if (c < 0x80)
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
  for (i = 0; i < sizeof ranges / sizeof *ranges; i++) {
    if (c >= ranges[i].lo && c <= ranges[i].hi) {
      if (ranges[i].step == 1)
        return (WCHAR)(c + ranges[i].delta);
      if ((c - ranges[i].lo) % 2 == 0)
        return c + 1;
    }
  }
  for (i = 0; i < sizeof pairs / sizeof *pairs; i++)
    if (pairs[i][0] == c)
      return pairs[i][1];
  return c;
}

WCHAR ow_upper(WCHAR c) {
  unsigned i;
  if (c < 0x80)
    return c >= 'a' && c <= 'z' ? c - 32 : c;
  for (i = 0; i < sizeof ranges / sizeof *ranges; i++) {
    WCHAR u;
    if (ranges[i].step == 1) {
      u = (WCHAR)(c - ranges[i].delta);
      if (u >= ranges[i].lo && u <= ranges[i].hi)
        return u;
    } else {
      u = c - 1;
      if (u >= ranges[i].lo && u <= ranges[i].hi &&
          (u - ranges[i].lo) % 2 == 0)
        return u;
    }
  }
  for (i = 0; i < sizeof pairs / sizeof *pairs; i++)
    if (pairs[i][1] == c && pairs[i][0] != 0x0130 && pairs[i][0] != 0x1e9e)
      return pairs[i][0];
  for (i = 0; i < sizeof up_only / sizeof *up_only; i++)
    if (up_only[i][0] == c)
      return up_only[i][1];
  return c;
}

static int in(WCHAR c, WCHAR lo, WCHAR hi) { return c >= lo && c <= hi; }

/* Letters without case and other alphabetic characters. */
static int is_letter(WCHAR c) {
  if (c < 0x80)
    return in(c, 'A', 'Z') || in(c, 'a', 'z');
  if (c < 0x100)
    return c == 0xaa || c == 0xb5 || c == 0xba ||
           (c >= 0xc0 && c != 0xd7 && c != 0xf7);
  return in(c, 0x0100, 0x02af) || in(c, 0x02b0, 0x02c1) ||
         in(c, 0x0370, 0x0373) || in(c, 0x0376, 0x037d) || c == 0x0386 ||
         in(c, 0x0388, 0x03ff) || in(c, 0x0400, 0x0481) ||
         in(c, 0x048a, 0x052f) || in(c, 0x0531, 0x0556) ||
         in(c, 0x0561, 0x0587) || in(c, 0x05d0, 0x05ea) ||
         in(c, 0x05f0, 0x05f2) || in(c, 0x0620, 0x064a) ||
         in(c, 0x066e, 0x06d3) || in(c, 0x06fa, 0x06fc) ||
         in(c, 0x0904, 0x0939) || in(c, 0x0958, 0x0961) ||
         in(c, 0x0985, 0x09b9) || in(c, 0x0a05, 0x0a39) ||
         in(c, 0x0a85, 0x0ab9) || in(c, 0x0b05, 0x0b39) ||
         in(c, 0x0b85, 0x0bb9) || in(c, 0x0c05, 0x0c39) ||
         in(c, 0x0c85, 0x0cb9) || in(c, 0x0d05, 0x0d39) ||
         in(c, 0x0e01, 0x0e30) || in(c, 0x0e40, 0x0e46) ||
         in(c, 0x0e81, 0x0eb0) || in(c, 0x10a0, 0x10fa) ||
         in(c, 0x1100, 0x11ff) || in(c, 0x1e00, 0x1fbc) ||
         in(c, 0x1fc2, 0x1ffc) || in(c, 0x2c00, 0x2c5e) ||
         in(c, 0x3041, 0x3096) || in(c, 0x309d, 0x309f) ||
         in(c, 0x30a1, 0x30fa) || in(c, 0x30fc, 0x30ff) ||
         in(c, 0x3105, 0x312f) || in(c, 0x3131, 0x318e) ||
         in(c, 0x3400, 0x4dbf) || in(c, 0x4e00, 0x9fff) ||
         in(c, 0xa000, 0xa48c) || in(c, 0xac00, 0xd7a3) ||
         in(c, 0xf900, 0xfaff) || in(c, 0xfb00, 0xfb06) ||
         in(c, 0xfb1d, 0xfdfb) || in(c, 0xfe70, 0xfefc) ||
         in(c, 0xff21, 0xff3a) || in(c, 0xff41, 0xff5a) ||
         in(c, 0xff66, 0xffdc);
}

static int is_space(WCHAR c) {
  return in(c, 0x09, 0x0d) || c == 0x20 || c == 0x85 || c == 0xa0 ||
         c == 0x1680 || in(c, 0x2000, 0x200b) || c == 0x2028 ||
         c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 ||
         c == 0xfeff;
}

static int is_digit(WCHAR c) {
  return in(c, '0', '9') || in(c, 0xff10, 0xff19) || in(c, 0x0660, 0x0669) ||
         in(c, 0x06f0, 0x06f9) || in(c, 0x0966, 0x096f) || c == 0xb2 ||
         c == 0xb3 || c == 0xb9;
}

static int is_cntrl(WCHAR c) { return c < 0x20 || in(c, 0x7f, 0x9f); }

static int is_mark(WCHAR c) {
  return in(c, 0x0300, 0x036f) || in(c, 0x0483, 0x0489) ||
         in(c, 0x0591, 0x05c7) || in(c, 0x0610, 0x061a) ||
         in(c, 0x064b, 0x065f) || c == 0x0670 || in(c, 0x06d6, 0x06ed) ||
         in(c, 0x0900, 0x0903) || in(c, 0x093a, 0x094f) ||
         in(c, 0x0e31, 0x0e3a) || in(c, 0x0e47, 0x0e4e) ||
         in(c, 0x20d0, 0x20ff) || in(c, 0x3099, 0x309a) ||
         in(c, 0xfe20, 0xfe2f);
}

WORD ow_ctype1(WCHAR c) {
  WORD t = C1_DEFINED;
  if (in(c, 0xd800, 0xdfff) || in(c, 0xe000, 0xf8ff))
    return 0;
  if (is_cntrl(c))
    t |= C1_CNTRL;
  if (is_space(c))
    t |= C1_SPACE;
  if (c == ' ' || c == '\t' || c == 0xa0 || c == 0x3000)
    t |= C1_BLANK;
  if (is_digit(c))
    t |= C1_DIGIT;
  if (in(c, '0', '9') || in(c, 'A', 'F') || in(c, 'a', 'f') ||
      in(c, 0xff10, 0xff19) || in(c, 0xff21, 0xff26) ||
      in(c, 0xff41, 0xff46))
    t |= C1_XDIGIT;
  if (is_letter(c)) {
    t |= C1_ALPHA;
    if (ow_lower(c) != c)
      t |= C1_UPPER;
    else if (ow_upper(c) != c || c == 0xdf || c == 0xaa || c == 0xba ||
             c == 0x0138 || c == 0x0149)
      t |= C1_LOWER;
  } else if (!(t & (C1_CNTRL | C1_SPACE | C1_DIGIT)) && !is_mark(c)) {
    t |= C1_PUNCT;
  }
  if (in(c, 0x2160, 0x217f) || in(c, 0x24b6, 0x24e9)) {
    t |= C1_ALPHA;
    t &= ~C1_PUNCT;
    t |= ow_lower(c) != c ? C1_UPPER : C1_LOWER;
  }
  return t;
}

WORD ow_ctype2(WCHAR c) {
  if (in(c, 0x0590, 0x05ff) || in(c, 0xfb1d, 0xfb4f))
    return C2_RIGHTTOLEFT;
  if (in(c, 0x0600, 0x06ff) || in(c, 0xfb50, 0xfdff) ||
      in(c, 0xfe70, 0xfeff)) {
    if (in(c, 0x0660, 0x0669) || in(c, 0x06f0, 0x06f9))
      return C2_ARABICNUMBER;
    return is_letter(c) ? C2_RIGHTTOLEFT : C2_OTHERNEUTRAL;
  }
  if (in(c, '0', '9') || in(c, 0xff10, 0xff19) || c == 0xb2 || c == 0xb3 ||
      c == 0xb9)
    return C2_EUROPENUMBER;
  if (c == '+' || c == '-')
    return C2_EUROPESEPARATOR;
  if (c == '#' || c == '$' || c == '%' || c == 0xa2 || c == 0xa3 ||
      c == 0xa5 || c == 0xb0 || c == 0xb1)
    return C2_EUROPETERMINATOR;
  if (c == ',' || c == '.' || c == ':' || c == '/' || c == 0xa0)
    return C2_COMMONSEPARATOR;
  if (c == 0x0a || c == 0x0d || c == 0x1c || c == 0x1d || c == 0x1e ||
      c == 0x85 || c == 0x2029)
    return C2_BLOCKSEPARATOR;
  if (c == 0x09 || c == 0x0b || c == 0x1f)
    return C2_SEGMENTSEPARATOR;
  if (is_space(c))
    return C2_WHITESPACE;
  if (is_letter(c) || is_mark(c))
    return C2_LEFTTORIGHT;
  if (is_cntrl(c))
    return C2_NOTAPPLICABLE;
  return C2_OTHERNEUTRAL;
}

WORD ow_ctype3(WCHAR c) {
  WORD t = 0;
  if (is_mark(c))
    t |= C3_NONSPACING;
  if (in(c, 0x0300, 0x036f))
    t |= C3_DIACRITIC;
  if (in(c, 0x064b, 0x065f))
    t |= C3_VOWELMARK;
  if (in(c, 0x3041, 0x309f))
    t |= C3_HIRAGANA | C3_FULLWIDTH;
  if (in(c, 0x30a0, 0x30ff))
    t |= C3_KATAKANA | C3_FULLWIDTH;
  if (in(c, 0xff66, 0xff9f))
    t |= C3_KATAKANA | C3_HALFWIDTH;
  if (in(c, 0x3400, 0x4dbf) || in(c, 0x4e00, 0x9fff) ||
      in(c, 0xf900, 0xfaff))
    t |= C3_IDEOGRAPH | C3_FULLWIDTH;
  if (in(c, 0xac00, 0xd7a3) || in(c, 0x3131, 0x318e))
    t |= C3_FULLWIDTH | C3_ALPHA;
  if (in(c, 0xff01, 0xff5e) || c == 0x3000)
    t |= C3_FULLWIDTH;
  if (in(c, 0xffa0, 0xffdc))
    t |= C3_HALFWIDTH;
  if (is_letter(c))
    t |= C3_ALPHA;
  if (!(ow_ctype1(c) & (C1_ALPHA | C1_DIGIT | C1_SPACE | C1_CNTRL)) &&
      !is_mark(c) && c >= 0x20)
    t |= C3_SYMBOL;
  if (c == 0x0640)
    t |= C3_KASHIDA;
  if (in(c, 0x0020, 0x007e) || in(c, 0xff61, 0xff9f))
    t |= C3_HALFWIDTH;
  return t;
}
