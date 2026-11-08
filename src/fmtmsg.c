/* FormatMessageW. Windows 9x's FormatMessageA cannot take W strings as
 * inserts, so the message is formatted here: the template comes from the
 * caller, from the module's message table, or from the system through
 * FormatMessageA with FORMAT_MESSAGE_IGNORE_INSERTS.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"

struct buf {
  LPWSTR p;
  int n, cap;
  BOOL fail;
};

static void put(struct buf *b, LPCWSTR s, int n) {
  if (b->fail)
    return;
  if (b->n + n + 1 > b->cap) {
    int cap = (b->n + n + 1) * 2;
    LPWSTR p = ow_realloc(b->p, cap * sizeof(WCHAR));
    if (!p) {
      b->fail = TRUE;
      return;
    }
    b->p = p;
    b->cap = cap;
  }
  ow_memcpy(b->p + b->n, s, n * sizeof(WCHAR));
  b->n += n;
  b->p[b->n] = 0;
}

static void putc_(struct buf *b, WCHAR c) { put(b, &c, 1); }

/* Finds message id in a module's message table, in W. */
static LPWSTR from_table(HMODULE mod, DWORD id, LANGID lang) {
  LANGID langs[6];
  int nl = 0, i;
  HRSRC res = NULL;
  const MESSAGE_RESOURCE_DATA *data;
  if (lang) {
    langs[nl++] = lang;
  } else {
    langs[nl++] = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    langs[nl++] = LANGIDFROMLCID(GetThreadLocale());
    langs[nl++] = GetUserDefaultLangID();
    langs[nl++] = GetSystemDefaultLangID();
    langs[nl++] = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
  }
  for (i = 0; i < nl && !res; i++)
    res = FindResourceExA(mod, (LPCSTR)RT_MESSAGETABLE, MAKEINTRESOURCEA(1),
                          langs[i]);
  if (!res && !lang)
    res = FindResourceA(mod, MAKEINTRESOURCEA(1), (LPCSTR)RT_MESSAGETABLE);
  if (!res)
    return NULL;
  data = LockResource(LoadResource(mod, res));
  if (!data)
    return NULL;
  for (DWORD b = 0; b < data->NumberOfBlocks; b++) {
    const MESSAGE_RESOURCE_BLOCK *blk = &data->Blocks[b];
    const BYTE *e;
    if (id < blk->LowId || id > blk->HighId)
      continue;
    e = (const BYTE *)data + blk->OffsetToEntries;
    for (DWORD k = blk->LowId; k < id; k++)
      e += ((const MESSAGE_RESOURCE_ENTRY *)e)->Length;
    {
      const MESSAGE_RESOURCE_ENTRY *m = (const MESSAGE_RESOURCE_ENTRY *)e;
      int len = m->Length - 4;
      if (m->Flags & 1) {
        LPWSTR w = ow_alloc(len + sizeof(WCHAR));
        if (w)
          ow_memcpy(w, m->Text, len);
        return w;
      }
      return ow_wn((LPCSTR)m->Text, ow_strlen((LPCSTR)m->Text) < len
                                        ? ow_strlen((LPCSTR)m->Text)
                                        : len,
                   NULL);
    }
  }
  return NULL;
}

static LPWSTR from_system(DWORD id, DWORD lang) {
  LPSTR a = NULL;
  LPWSTR w;
  if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                          FORMAT_MESSAGE_IGNORE_INSERTS |
                          FORMAT_MESSAGE_ALLOCATE_BUFFER,
                      NULL, id, lang, (LPSTR)&a, 0, NULL))
    return NULL;
  w = ow_w(a);
  LocalFree(a);
  return w;
}

/* Arguments: from the array, or read from the va_list into one. */
struct args {
  BOOL array;
  const DWORD_PTR *arr;
  va_list *ap;
  DWORD_PTR vals[100];
  int got;
};

static DWORD_PTR arg(struct args *a, int i) {
  if (a->array)
    return a->arr ? a->arr[i - 1] : 0;
  while (a->got < i && a->got < 99 && a->ap)
    a->vals[++a->got] = va_arg(*a->ap, DWORD_PTR);
  return i <= a->got ? a->vals[i] : 0;
}

DWORD WINAPI FormatMessageW(DWORD flags, LPCVOID src, DWORD id, DWORD lang,
                            LPWSTR out, DWORD size, va_list *ap) {
  LPWSTR tmpl = NULL, own = NULL;
  struct buf b = {0};
  struct args args;
  DWORD width = flags & FORMAT_MESSAGE_MAX_WIDTH_MASK;
  LPCWSTR p;

  if (out && (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER))
    *(LPWSTR *)out = NULL;
  if (flags & FORMAT_MESSAGE_FROM_STRING) {
    tmpl = (LPWSTR)src;
  } else {
    if (flags & FORMAT_MESSAGE_FROM_HMODULE)
      tmpl = own = from_table(src ? (HMODULE)src : GetModuleHandleA(NULL),
                              id, (LANGID)lang);
    if (!tmpl && (flags & FORMAT_MESSAGE_FROM_SYSTEM))
      tmpl = own = from_system(id, lang);
  }
  if (!tmpl) {
    SetLastError(lang ? ERROR_RESOURCE_LANG_NOT_FOUND
                      : ERROR_MR_MID_NOT_FOUND);
    return 0;
  }
  args.array = (flags & FORMAT_MESSAGE_ARGUMENT_ARRAY) != 0;
  args.arr = (const DWORD_PTR *)ap;
  args.ap = args.array ? NULL : ap;
  args.got = 0;

  for (p = tmpl; *p;) {
    if (*p == '\r' || *p == '\n') {
      /* Ordinary line breaks: kept, or spaces with a maximum width. */
      if (*p == '\r' && p[1] == '\n')
        p++;
      p++;
      if (width)
        putc_(&b, ' ');
      else
        put(&b, L"\r\n", 2);
      continue;
    }
    if (*p != '%') {
      putc_(&b, *p++);
      continue;
    }
    p++;
    if ((flags & FORMAT_MESSAGE_IGNORE_INSERTS) && *p >= '1' && *p <= '9') {
      /* Inserts stay as they are. */
      putc_(&b, '%');
      continue;
    }
    if (*p >= '1' && *p <= '9') {
      int n = *p++ - '0';
      WCHAR spec[32];
      int sl = 0;
      if (*p >= '0' && *p <= '9')
        n = n * 10 + (*p++ - '0');
      spec[sl++] = '%';
      if (*p == '!') {
        p++;
        while (*p && *p != '!' && sl < 30)
          spec[sl++] = *p++;
        if (*p == '!')
          p++;
      } else {
        spec[sl++] = 's';
      }
      spec[sl] = 0;
      if (!args.array && !ap) {
        SetLastError(ERROR_INVALID_PARAMETER);
        ow_free(own);
        ow_free(b.p);
        return 0;
      }
      {
        /* "*" takes the width or precision from the next argument. */
        WCHAR fmt[64], res[1100];
        int fl = 0, next = n + 1;
        DWORD_PTR v;
        for (int i = 0; spec[i] && fl < 60; i++) {
          if (spec[i] == '*') {
            WCHAR num[16];
            int k = wsprintfW(num, L"%d", (int)arg(&args, next++));
            for (int j = 0; j < k && fl < 60; j++)
              fmt[fl++] = num[j];
          } else {
            fmt[fl++] = spec[i];
          }
        }
        fmt[fl] = 0;
        v = arg(&args, n);
        {
          WCHAR t = fl ? fmt[fl - 1] : 's';
          if (t == 's' || t == 'S') {
            int wide = t == 's';
            for (int i = 1; i < fl - 1; i++) {
              if (fmt[i] == 'h')
                wide = 0;
              else if (fmt[i] == 'l' || fmt[i] == 'w')
                wide = 1;
            }
            /* Long strings: no width or precision, so put them as they
               are instead of through wsprintf's 1024 limit. */
            if (fl == 2 || (fl == 3 && (fmt[1] == 'l' || fmt[1] == 'h' ||
                                        fmt[1] == 'w'))) {
              if (wide) {
                LPCWSTR s = v ? (LPCWSTR)v : L"(null)";
                put(&b, s, ow_wcslen(s));
              } else {
                LPWSTR s = ow_w(v ? (LPCSTR)v : "(null)");
                if (s)
                  put(&b, s, ow_wcslen(s));
                ow_free(s);
              }
              continue;
            }
          }
        }
        {
          int k = wsprintfW(res, fmt, v);
          put(&b, res, k);
        }
      }
      continue;
    }
    switch (*p) {
    case '0':
      /* End of the message, without a line break. */
      goto done;
    case 'n':
      put(&b, L"\r\n", 2);
      break;
    case 'r':
      putc_(&b, '\r');
      break;
    case 't':
      putc_(&b, '\t');
      break;
    case 'b':
      putc_(&b, ' ');
      break;
    case 0:
      putc_(&b, '%');
      goto done;
    default:
      putc_(&b, *p);
      break;
    }
    p++;
  }
done:
  ow_free(own);
  if (b.fail) {
    ow_free(b.p);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
  if (!b.p) {
    b.p = ow_alloc(sizeof(WCHAR));
    if (!b.p) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return 0;
    }
  }
  if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
    DWORD n = (DWORD)b.n + 1 > size ? (DWORD)b.n + 1 : size;
    LPWSTR r = LocalAlloc(LMEM_FIXED, n * sizeof(WCHAR));
    if (!r) {
      ow_free(b.p);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return 0;
    }
    ow_memcpy(r, b.p, (b.n + 1) * sizeof(WCHAR));
    *(LPWSTR *)out = r;
  } else {
    if (!out || (DWORD)b.n + 1 > size) {
      ow_free(b.p);
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return 0;
    }
    ow_memcpy(out, b.p, (b.n + 1) * sizeof(WCHAR));
  }
  ow_free(b.p);
  return b.n;
}
