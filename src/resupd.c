/* BeginUpdateResource, UpdateResource and EndUpdateResource, which Windows
 * 9x lacks in both forms: the resources of a PE file are read into a list,
 * changed there, and written back as a new resource section.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"

/* A resource type or name: a number, or a string (heap, W). */
struct id {
  WORD num;
  LPWSTR str;
};

struct res {
  struct id type, name;
  WORD lang;
  DWORD size, codepage;
  BYTE *data;
  struct res *next;
};

struct update {
  LPSTR path;
  struct res *list;
};

static BOOL id_set(struct id *id, LPCWSTR w) {
  if (IS_INTRESOURCE(w)) {
    id->num = LOWORD((UINT_PTR)w);
    id->str = NULL;
    return TRUE;
  }
  if (w[0] == '#') {
    WORD v = 0;
    for (LPCWSTR p = w + 1; *p >= '0' && *p <= '9'; p++)
      v = (WORD)(v * 10 + (*p - '0'));
    id->num = v;
    id->str = NULL;
    return TRUE;
  }
  id->num = 0;
  id->str = ow_alloc((ow_wcslen(w) + 1) * sizeof(WCHAR));
  if (!id->str)
    return FALSE;
  ow_wcscpy(id->str, w);
  return TRUE;
}

/* The order of a resource directory: names (by their code points, case
 * blind) before numbers. */
static int id_cmp(const struct id *a, const struct id *b) {
  if (a->str && b->str) {
    LPCWSTR x = a->str, y = b->str;
    for (;; x++, y++) {
      WCHAR c = ow_upper(*x), d = ow_upper(*y);
      if (c != d)
        return c < d ? -1 : 1;
      if (!c)
        return 0;
    }
  }
  if (a->str)
    return -1;
  if (b->str)
    return 1;
  return a->num < b->num ? -1 : a->num > b->num;
}

static void res_free(struct res *r) {
  ow_free(r->type.str);
  ow_free(r->name.str);
  ow_free(r->data);
  ow_free(r);
}

static void update_free(struct update *u) {
  while (u->list) {
    struct res *n = u->list->next;
    res_free(u->list);
    u->list = n;
  }
  ow_free(u->path);
  ow_free(u);
}

/* Adds r in sorted order, replacing a resource with the same ids. */
static void insert(struct update *u, struct res *r) {
  struct res **p = &u->list;
  while (*p) {
    int c = id_cmp(&(*p)->type, &r->type);
    if (!c)
      c = id_cmp(&(*p)->name, &r->name);
    if (!c)
      c = (*p)->lang < r->lang ? -1 : (*p)->lang > r->lang;
    if (c == 0) {
      struct res *old = *p;
      r->next = old->next;
      *p = r;
      res_free(old);
      return;
    }
    if (c > 0)
      break;
    p = &(*p)->next;
  }
  r->next = *p;
  *p = r;
}

/* ---- reading a PE file ---------------------------------------------- */

struct pe {
  BYTE *file;
  DWORD size;
  DWORD nt;         /* offset of the NT headers */
  DWORD sections;   /* offset of the section table */
  WORD nsections;
  DWORD dirs;       /* offset of the data directories */
  DWORD ndirs;
  DWORD falign, salign;
};

static IMAGE_SECTION_HEADER *section(struct pe *pe, int i) {
  return (IMAGE_SECTION_HEADER *)(pe->file + pe->sections) + i;
}

static IMAGE_DATA_DIRECTORY *dir(struct pe *pe, int i) {
  return (IMAGE_DATA_DIRECTORY *)(pe->file + pe->dirs) + i;
}

static BOOL pe_parse(struct pe *pe) {
  IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)pe->file;
  IMAGE_FILE_HEADER *fh;
  WORD magic;
  DWORD opt;
  if (pe->size < sizeof *dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
    return FALSE;
  pe->nt = dos->e_lfanew;
  if (pe->nt > pe->size - 24 ||
      *(DWORD *)(pe->file + pe->nt) != IMAGE_NT_SIGNATURE)
    return FALSE;
  fh = (IMAGE_FILE_HEADER *)(pe->file + pe->nt + 4);
  opt = pe->nt + 24;
  if (opt + fh->SizeOfOptionalHeader > pe->size || fh->SizeOfOptionalHeader < 2)
    return FALSE;
  magic = *(WORD *)(pe->file + opt);
  if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    IMAGE_OPTIONAL_HEADER32 *o = (IMAGE_OPTIONAL_HEADER32 *)(pe->file + opt);
    pe->dirs = opt + offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory);
    pe->ndirs = o->NumberOfRvaAndSizes;
    pe->falign = o->FileAlignment;
    pe->salign = o->SectionAlignment;
  } else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    IMAGE_OPTIONAL_HEADER64 *o = (IMAGE_OPTIONAL_HEADER64 *)(pe->file + opt);
    pe->dirs = opt + offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory);
    pe->ndirs = o->NumberOfRvaAndSizes;
    pe->falign = o->FileAlignment;
    pe->salign = o->SectionAlignment;
  } else {
    return FALSE;
  }
  if (!pe->falign || !pe->salign)
    return FALSE;
  pe->sections = opt + fh->SizeOfOptionalHeader;
  pe->nsections = fh->NumberOfSections;
  if (pe->sections + pe->nsections * sizeof(IMAGE_SECTION_HEADER) > pe->size)
    return FALSE;
  return TRUE;
}

/* The file offset of rva, or 0. */
static DWORD rva_off(struct pe *pe, DWORD rva, DWORD len) {
  for (int i = 0; i < pe->nsections; i++) {
    IMAGE_SECTION_HEADER *s = section(pe, i);
    DWORD vs = s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData;
    if (rva >= s->VirtualAddress && rva + len <= s->VirtualAddress + vs &&
        rva - s->VirtualAddress + len <= s->SizeOfRawData) {
      DWORD off = s->PointerToRawData + (rva - s->VirtualAddress);
      return off + len <= pe->size ? off : 0;
    }
  }
  return 0;
}

static BOOL read_id(struct pe *pe, DWORD base, DWORD limit,
                    const IMAGE_RESOURCE_DIRECTORY_ENTRY *e, struct id *id) {
  if (e->NameIsString) {
    DWORD off = base + e->NameOffset;
    WORD len;
    if (off + 2 > limit)
      return FALSE;
    len = *(WORD *)(pe->file + off);
    if (off + 2 + len * 2 > limit)
      return FALSE;
    id->num = 0;
    id->str = ow_alloc((len + 1) * sizeof(WCHAR));
    if (!id->str)
      return FALSE;
    ow_memcpy(id->str, pe->file + off + 2, len * 2);
    return TRUE;
  }
  id->num = e->Id;
  id->str = NULL;
  return TRUE;
}

static BOOL read_dir(struct pe *pe, struct update *u, DWORD base,
                     DWORD limit, DWORD off, int level, struct res *proto) {
  IMAGE_RESOURCE_DIRECTORY *d;
  IMAGE_RESOURCE_DIRECTORY_ENTRY *e;
  int n;
  if (off + sizeof *d > limit || level > 2)
    return FALSE;
  d = (IMAGE_RESOURCE_DIRECTORY *)(pe->file + off);
  n = d->NumberOfNamedEntries + d->NumberOfIdEntries;
  e = (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(d + 1);
  if (off + sizeof *d + n * sizeof *e > limit)
    return FALSE;
  for (int i = 0; i < n; i++, e++) {
    struct res r = *proto;
    struct id id;
    if (level < 2) {
      if (!read_id(pe, base, limit, e, &id))
        return FALSE;
      if (level == 0)
        r.type = id;
      else
        r.name = id;
    } else {
      r.lang = e->Id;
    }
    if (e->DataIsDirectory) {
      BOOL ok = level < 2 &&
                read_dir(pe, u, base, limit, base + e->OffsetToDirectory,
                         level + 1, &r);
      if (level < 2)
        ow_free(id.str);
      if (!ok)
        return FALSE;
    } else if (level == 2) {
      IMAGE_RESOURCE_DATA_ENTRY *de;
      struct res *nr;
      DWORD doff;
      if (base + e->OffsetToData + sizeof *de > limit)
        return FALSE;
      de = (IMAGE_RESOURCE_DATA_ENTRY *)(pe->file + base + e->OffsetToData);
      doff = de->Size ? rva_off(pe, de->OffsetToData, de->Size) : 0;
      if (de->Size && !doff)
        return FALSE;
      nr = ow_alloc(sizeof *nr);
      if (!nr)
        return FALSE;
      *nr = r;
      nr->size = de->Size;
      nr->codepage = de->CodePage;
      nr->data = ow_alloc(de->Size);
      if (!nr->data || !id_set(&nr->type, (LPCWSTR)(r.type.str
                                                         ? r.type.str
                                                         : MAKEINTRESOURCEW(
                                                               r.type.num))) ||
          !id_set(&nr->name, (LPCWSTR)(r.name.str ? r.name.str
                                                  : MAKEINTRESOURCEW(
                                                        r.name.num)))) {
        ow_free(nr->data);
        ow_free(nr);
        return FALSE;
      }
      ow_memcpy(nr->data, pe->file + doff, de->Size);
      nr->next = NULL;
      insert(u, nr);
    } else {
      if (level < 2)
        ow_free(id.str);
      return FALSE;
    }
  }
  return TRUE;
}

static BOOL read_file(LPCSTR path, struct pe *pe) {
  HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, 0, NULL);
  DWORD got;
  if (h == INVALID_HANDLE_VALUE)
    return FALSE;
  pe->size = GetFileSize(h, NULL);
  pe->file = pe->size != INVALID_FILE_SIZE ? ow_alloc(pe->size) : NULL;
  if (!pe->file || !ReadFile(h, pe->file, pe->size, &got, NULL) ||
      got != pe->size) {
    ow_free(pe->file);
    pe->file = NULL;
    CloseHandle(h);
    if (GetLastError() == 0)
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  CloseHandle(h);
  if (!pe_parse(pe)) {
    ow_free(pe->file);
    pe->file = NULL;
    SetLastError(ERROR_BAD_EXE_FORMAT);
    return FALSE;
  }
  return TRUE;
}

static HANDLE begin(LPSTR path, BOOL drop) {
  struct update *u;
  struct pe pe;
  if (!path) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (!read_file(path, &pe)) {
    ow_free(path);
    return NULL;
  }
  u = ow_alloc(sizeof *u);
  if (!u) {
    ow_free(pe.file);
    ow_free(path);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  u->path = path;
  if (!drop && pe.ndirs > IMAGE_DIRECTORY_ENTRY_RESOURCE &&
      dir(&pe, IMAGE_DIRECTORY_ENTRY_RESOURCE)->Size) {
    IMAGE_DATA_DIRECTORY *rd = dir(&pe, IMAGE_DIRECTORY_ENTRY_RESOURCE);
    DWORD base = rva_off(&pe, rd->VirtualAddress, rd->Size);
    struct res proto = {0};
    if (!base ||
        !read_dir(&pe, u, base, base + rd->Size, base, 0, &proto)) {
      ow_free(pe.file);
      update_free(u);
      SetLastError(ERROR_BAD_EXE_FORMAT);
      return NULL;
    }
  }
  ow_free(pe.file);
  return u;
}

HANDLE WINAPI BeginUpdateResourceW(LPCWSTR file, BOOL drop) {
  return begin(ow_acp(file, ow_fcp()), drop);
}

HANDLE WINAPI BeginUpdateResourceA(LPCSTR file, BOOL drop) {
  LPSTR p;
  if (!file) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  p = ow_alloc(ow_strlen(file) + 1);
  if (!p) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  lstrcpyA(p, file);
  return begin(p, drop);
}

BOOL WINAPI UpdateResourceW(HANDLE h, LPCWSTR type, LPCWSTR name, WORD lang,
                            LPVOID data, DWORD size) {
  struct update *u = h;
  struct res *r;
  if (!u || !type || !name) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  r = ow_alloc(sizeof *r);
  if (!r || !id_set(&r->type, type) || !id_set(&r->name, name)) {
    if (r)
      res_free(r);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  r->lang = lang;
  if (!data && !size) {
    /* Delete it. */
    struct res **p = &u->list;
    while (*p) {
      if (!id_cmp(&(*p)->type, &r->type) && !id_cmp(&(*p)->name, &r->name) &&
          (*p)->lang == lang) {
        struct res *old = *p;
        *p = old->next;
        res_free(old);
        break;
      }
      p = &(*p)->next;
    }
    res_free(r);
    return TRUE;
  }
  if (!data) {
    res_free(r);
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  r->size = size;
  r->data = ow_alloc(size);
  if (!r->data) {
    res_free(r);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  ow_memcpy(r->data, data, size);
  insert(u, r);
  return TRUE;
}

BOOL WINAPI UpdateResourceA(HANDLE h, LPCSTR type, LPCSTR name, WORD lang,
                            LPVOID data, DWORD size) {
  LPWSTR t = ow_wres(type), n = ow_wres(name);
  BOOL r = UpdateResourceW(h, t, n, lang, data, size);
  ow_free(t);
  ow_free(n);
  return r;
}

/* ---- writing the new resource section ------------------------------- */

static DWORD align(DWORD v, DWORD a) { return (v + a - 1) / a * a; }

struct layout {
  BYTE *p;       /* the section, or NULL while measuring */
  DWORD dirs;    /* where the next directory goes */
  DWORD strs;    /* where the next string goes */
  DWORD entries; /* where the next data entry goes */
  DWORD data;    /* where the next data goes */
  DWORD rva;     /* the section's address */
};

static int same_type(const struct res *a, const struct res *b) {
  return !id_cmp(&a->type, &b->type);
}
static int same_name(const struct res *a, const struct res *b) {
  return same_type(a, b) && !id_cmp(&a->name, &b->name);
}

/* The first resource after r's group at a level. */
static struct res *group_end(struct res *r, int level) {
  struct res *n = r->next;
  if (level == 0)
    while (n && same_type(n, r))
      n = n->next;
  else if (level == 1)
    while (n && same_name(n, r))
      n = n->next;
  return n;
}

static DWORD put_string(struct layout *l, LPCWSTR s) {
  DWORD off = l->strs, n = ow_wcslen(s);
  if (l->p) {
    *(WORD *)(l->p + off) = (WORD)n;
    ow_memcpy(l->p + off + 2, s, n * 2);
  }
  l->strs += 2 + n * 2;
  return off;
}

/* Writes the directory of the resources [first, end) at level (0 types,
 * 1 names, 2 languages); returns its offset. */
static DWORD put_dir(struct layout *l, struct res *first, struct res *end,
                     int level) {
  DWORD off = l->dirs;
  IMAGE_RESOURCE_DIRECTORY_ENTRY *e;
  struct res *r;
  int named = 0, n = 0;
  for (r = first; r != end; r = group_end(r, level)) {
    const struct id *id = level == 0 ? &r->type : &r->name;
    if (level < 2 && id->str)
      named++;
    n++;
  }
  l->dirs += sizeof(IMAGE_RESOURCE_DIRECTORY) + n * sizeof *e;
  if (l->p) {
    IMAGE_RESOURCE_DIRECTORY *d = (void *)(l->p + off);
    ow_memset(d, 0, sizeof *d);
    d->NumberOfNamedEntries = (WORD)named;
    d->NumberOfIdEntries = (WORD)(n - named);
  }
  e = (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(l->p + off +
                                         sizeof(IMAGE_RESOURCE_DIRECTORY));
  for (r = first; r != end; r = group_end(r, level), e++) {
    DWORD name, to;
    if (level < 2) {
      const struct id *id = level == 0 ? &r->type : &r->name;
      name = id->str ? 0x80000000u | put_string(l, id->str) : id->num;
      to = 0x80000000u | put_dir(l, r, group_end(r, level), level + 1);
    } else {
      name = r->lang;
      to = l->entries;
      l->entries += sizeof(IMAGE_RESOURCE_DATA_ENTRY);
      l->data = align(l->data, 8);
      if (l->p) {
        IMAGE_RESOURCE_DATA_ENTRY *x = (void *)(l->p + to);
        x->OffsetToData = l->rva + l->data;
        x->Size = r->size;
        x->CodePage = r->codepage;
        x->Reserved = 0;
        ow_memcpy(l->p + l->data, r->data, r->size);
      }
      l->data += r->size;
    }
    if (l->p) {
      e->Name = name;
      e->OffsetToData = to;
    }
  }
  return off;
}

/* Lays the section out at rva into p (NULL: only measure, filling
 * sizes). Returns its size. */
static DWORD build(struct update *u, BYTE *p, DWORD rva, DWORD sizes[3]) {
  struct layout l = {0};
  if (!p) {
    l.strs = 0;
    l.entries = 0;
    l.data = 0;
    put_dir(&l, u->list, NULL, 0);
    sizes[0] = l.dirs;
    sizes[1] = align(l.strs, 8);
    sizes[2] = l.entries;
  }
  l.p = p;
  l.rva = rva;
  l.dirs = 0;
  l.strs = sizes[0];
  l.entries = sizes[0] + sizes[1];
  l.data = sizes[0] + sizes[1] + sizes[2];
  put_dir(&l, u->list, NULL, 0);
  return l.data;
}

static DWORD checksum(const BYTE *f, DWORD size, DWORD skip) {
  unsigned long long sum = 0;
  DWORD i;
  for (i = 0; i + 1 < size; i += 2) {
    if (i == skip || i == skip + 2)
      continue;
    sum += *(const WORD *)(f + i);
    sum = (sum & 0xffff) + (sum >> 16);
  }
  if (size & 1) {
    sum += f[size - 1];
    sum = (sum & 0xffff) + (sum >> 16);
  }
  sum = (sum & 0xffff) + (sum >> 16);
  return (DWORD)sum + size;
}

static BOOL write_file(struct update *u) {
  struct pe pe;
  DWORD sizes[3], len, rva, raw, old_raw = 0, tail_off, tail_len;
  int rs = -1, last = -1;
  IMAGE_SECTION_HEADER *s;
  BYTE *out;
  DWORD outsize, written, opt = 0, hdrsize, sizeofimage_off, csum_off;
  HANDLE h;
  BOOL ok;

  if (!read_file(u->path, &pe))
    return FALSE;
  opt = pe.nt + 24;
  /* The section that holds the resources, and the last one in the file. */
  for (int i = 0; i < pe.nsections; i++) {
    s = section(&pe, i);
    if (pe.ndirs > IMAGE_DIRECTORY_ENTRY_RESOURCE &&
        dir(&pe, IMAGE_DIRECTORY_ENTRY_RESOURCE)->VirtualAddress ==
            s->VirtualAddress &&
        s->VirtualAddress)
      rs = i;
    if (last < 0 || s->VirtualAddress > section(&pe, last)->VirtualAddress)
      last = i;
  }
  if (*(WORD *)(pe.file + opt) == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    sizeofimage_off = opt + offsetof(IMAGE_OPTIONAL_HEADER32, SizeOfImage);
    csum_off = opt + offsetof(IMAGE_OPTIONAL_HEADER32, CheckSum);
    hdrsize = ((IMAGE_OPTIONAL_HEADER32 *)(pe.file + opt))->SizeOfHeaders;
  } else {
    sizeofimage_off = opt + offsetof(IMAGE_OPTIONAL_HEADER64, SizeOfImage);
    csum_off = opt + offsetof(IMAGE_OPTIONAL_HEADER64, CheckSum);
    hdrsize = ((IMAGE_OPTIONAL_HEADER64 *)(pe.file + opt))->SizeOfHeaders;
  }
  if (pe.ndirs <= IMAGE_DIRECTORY_ENTRY_RESOURCE) {
    ow_free(pe.file);
    SetLastError(ERROR_BAD_EXE_FORMAT);
    return FALSE;
  }

  len = build(u, NULL, 0, sizes);

  if (rs >= 0 && rs != last) {
    /* In the middle: rewrite it in place if the new one fits. */
    s = section(&pe, rs);
    if (len <= s->SizeOfRawData &&
        len <= section(&pe, rs + 1 < pe.nsections ? rs + 1 : rs)
                       ->VirtualAddress -
                   s->VirtualAddress) {
      BYTE *sec = ow_alloc(s->SizeOfRawData);
      if (!sec)
        goto nomem;
      build(u, sec, s->VirtualAddress, sizes);
      ow_memcpy(pe.file + s->PointerToRawData, sec, s->SizeOfRawData);
      ow_free(sec);
      s->Misc.VirtualSize = len;
      dir(&pe, IMAGE_DIRECTORY_ENTRY_RESOURCE)->Size = len;
      out = pe.file;
      outsize = pe.size;
      goto write;
    }
    /* Otherwise leave the old data and add a section at the end. */
    ow_memcpy(s->Name, ".orsrc\0\0", 8);
    rs = -1;
  }
  if (rs < 0) {
    /* A new section after the last: its header must fit. */
    DWORD end = pe.sections + (pe.nsections + 1) * sizeof *s;
    DWORD first = pe.size;
    for (int i = 0; i < pe.nsections; i++)
      if (section(&pe, i)->PointerToRawData &&
          section(&pe, i)->PointerToRawData < first)
        first = section(&pe, i)->PointerToRawData;
    if (end > hdrsize || end > first) {
      ow_free(pe.file);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }
    s = section(&pe, pe.nsections);
    ow_memset(s, 0, sizeof *s);
    ow_memcpy(s->Name, ".rsrc\0\0\0", 8);
    s->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
    if (last >= 0) {
      IMAGE_SECTION_HEADER *ls = section(&pe, last);
      DWORD vs = ls->Misc.VirtualSize ? ls->Misc.VirtualSize
                                      : ls->SizeOfRawData;
      s->VirtualAddress = align(ls->VirtualAddress + vs, pe.salign);
      s->PointerToRawData =
          align(ls->PointerToRawData + ls->SizeOfRawData, pe.falign);
    } else {
      s->VirtualAddress = align(hdrsize, pe.salign);
      s->PointerToRawData = align(hdrsize, pe.falign);
    }
    if (s->PointerToRawData > pe.size)
      s->PointerToRawData = align(pe.size, pe.falign);
    /* Data after the sections (an overlay) moves behind the new one. */
    tail_off = s->PointerToRawData;
    ((IMAGE_FILE_HEADER *)(pe.file + pe.nt + 4))->NumberOfSections++;
    pe.nsections++;
    rs = pe.nsections - 1;
  } else {
    s = section(&pe, rs);
    old_raw = s->SizeOfRawData;
    tail_off = s->PointerToRawData + old_raw;
  }
  tail_len = tail_off < pe.size ? pe.size - tail_off : 0;
  rva = s->VirtualAddress;
  raw = align(len, pe.falign);
  outsize = s->PointerToRawData + raw + tail_len;
  out = ow_alloc(outsize);
  if (!out)
    goto nomem;
  ow_memcpy(out, pe.file, s->PointerToRawData < pe.size ? s->PointerToRawData
                                                         : pe.size);
  build(u, out + s->PointerToRawData, rva, sizes);
  if (tail_len)
    ow_memcpy(out + s->PointerToRawData + raw, pe.file + tail_off, tail_len);
  s = (IMAGE_SECTION_HEADER *)(out + pe.sections) + rs;
  s->SizeOfRawData = raw;
  s->Misc.VirtualSize = len;
  ((IMAGE_DATA_DIRECTORY *)(out + pe.dirs))[IMAGE_DIRECTORY_ENTRY_RESOURCE]
      .VirtualAddress = rva;
  ((IMAGE_DATA_DIRECTORY *)(out + pe.dirs))[IMAGE_DIRECTORY_ENTRY_RESOURCE]
      .Size = len;
  {
    DWORD img = 0;
    for (int i = 0; i < pe.nsections; i++) {
      IMAGE_SECTION_HEADER *x = (IMAGE_SECTION_HEADER *)(out + pe.sections) + i;
      DWORD vs = x->Misc.VirtualSize ? x->Misc.VirtualSize : x->SizeOfRawData;
      if (align(x->VirtualAddress + vs, pe.salign) > img)
        img = align(x->VirtualAddress + vs, pe.salign);
    }
    *(DWORD *)(out + sizeofimage_off) = img;
  }
  ow_free(pe.file);
  pe.file = out;
write:
  if (*(DWORD *)(out + csum_off))
    *(DWORD *)(out + csum_off) = checksum(out, outsize, csum_off);
  h = CreateFileA(u->path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    ow_free(out);
    return FALSE;
  }
  ok = WriteFile(h, out, outsize, &written, NULL) && written == outsize;
  CloseHandle(h);
  ow_free(out);
  return ok;
nomem:
  ow_free(pe.file);
  SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  return FALSE;
}

BOOL WINAPI EndUpdateResourceW(HANDLE h, BOOL discard) {
  struct update *u = h;
  BOOL r = TRUE;
  if (!u) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (!discard)
    r = write_file(u);
  update_free(u);
  return r;
}

BOOL WINAPI EndUpdateResourceA(HANDLE h, BOOL discard) {
  return EndUpdateResourceW(h, discard);
}
