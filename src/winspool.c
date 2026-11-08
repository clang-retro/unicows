/* winspool.drv: printers, jobs, drivers, ports, monitors and print
 * processors. Their information structures (PRINTER_INFO_2 and the like)
 * are arrays of fixed parts whose strings follow in the same buffer; each
 * kind is described by the offsets of its string, string list and DEVMODE
 * fields, and converted by the same code.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"
#include <winspool.h>

#define END OW_END
#define O OW_OFF


/* winspool.drv is loaded when a printer function is first called: each A
 * function is a jump through a slot that at first points to code which
 * finds the function (or one that fails) and puts it in the slot. */
void WINAPI ow_lazy_bind(void *fail, void **slot, const char *name) {
  void *p = (void *)ow_proc(OW_WINSPOOL, name);
  if (!p)
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  *slot = p ? p : fail;
}

#define LAZY(name, n)                                                       \
  __asm__(".text\n"                                                         \
          ".globl _" #name "@" #n "\n"                                      \
          "_" #name "@" #n ":\n"                                            \
          "  jmp *_lazy_" #name "\n"                                        \
          "1:\n"                                                            \
          "  pushl $2f\n"                                                   \
          "  pushl $_lazy_" #name "\n"                                      \
          "  pushl $3f\n"                                                   \
          "  call _ow_lazy_bind@12\n"                                       \
          "  jmp *_lazy_" #name "\n"                                        \
          "3:\n"                                                            \
          "  xorl %eax, %eax\n"                                             \
          "  ret $" #n "\n"                                                 \
          ".data\n"                                                         \
          "_lazy_" #name ": .long 1b\n"                                     \
          ".section .rdata,\"dr\"\n"                                       \
          "2: .asciz \"" #name "\"\n"                                       \
          ".text\n");

LAZY(AddJobA, 20)
LAZY(AddMonitorA, 12)
LAZY(AddPortA, 12)
LAZY(AddPrintProcessorA, 16)
LAZY(AddPrintProvidorA, 12)
LAZY(AddPrinterA, 12)
LAZY(AddPrinterDriverA, 12)
LAZY(ConfigurePortA, 12)
LAZY(DeleteMonitorA, 12)
LAZY(DeletePortA, 12)
LAZY(DeletePrintProcessorA, 12)
LAZY(DeletePrintProvidorA, 12)
LAZY(DeletePrinterDriverA, 12)
LAZY(DeviceCapabilitiesA, 20)
LAZY(DocumentPropertiesA, 24)
LAZY(EnumMonitorsA, 24)
LAZY(EnumPortsA, 24)
LAZY(EnumPrintProcessorDatatypesA, 28)
LAZY(EnumPrintProcessorsA, 28)
LAZY(EnumPrinterDriversA, 28)
LAZY(EnumPrintersA, 28)
LAZY(GetJobA, 24)
LAZY(GetPrintProcessorDirectoryA, 24)
LAZY(GetPrinterA, 20)
LAZY(GetPrinterDataA, 24)
LAZY(GetPrinterDriverA, 24)
LAZY(GetPrinterDriverDirectoryA, 24)
LAZY(OpenPrinterA, 12)
LAZY(ResetPrinterA, 8)
LAZY(SetJobA, 20)
LAZY(SetPrinterA, 16)
LAZY(SetPrinterDataA, 20)
LAZY(StartDocPrinterA, 12)

/* ---- the layouts ------------------------------------------------------- */

static const struct ow_layout printer1 = {
    sizeof(PRINTER_INFO_1W),
    {O(PRINTER_INFO_1W, pDescription), O(PRINTER_INFO_1W, pName),
     O(PRINTER_INFO_1W, pComment), END},
    {END},
    END,
    END};
static const struct ow_layout printer2 = {
    sizeof(PRINTER_INFO_2W),
    {O(PRINTER_INFO_2W, pServerName), O(PRINTER_INFO_2W, pPrinterName),
     O(PRINTER_INFO_2W, pShareName), O(PRINTER_INFO_2W, pPortName),
     O(PRINTER_INFO_2W, pDriverName), O(PRINTER_INFO_2W, pComment),
     O(PRINTER_INFO_2W, pLocation), O(PRINTER_INFO_2W, pSepFile),
     O(PRINTER_INFO_2W, pPrintProcessor), O(PRINTER_INFO_2W, pDatatype),
     O(PRINTER_INFO_2W, pParameters), END},
    {END},
    O(PRINTER_INFO_2W, pDevMode),
    O(PRINTER_INFO_2W, pSecurityDescriptor)};
static const struct ow_layout printer3 = {
    sizeof(PRINTER_INFO_3), {END}, {END}, END,
    O(PRINTER_INFO_3, pSecurityDescriptor)};
static const struct ow_layout printer4 = {
    sizeof(PRINTER_INFO_4W),
    {O(PRINTER_INFO_4W, pPrinterName), O(PRINTER_INFO_4W, pServerName), END},
    {END},
    END,
    END};
static const struct ow_layout printer5 = {
    sizeof(PRINTER_INFO_5W),
    {O(PRINTER_INFO_5W, pPrinterName), O(PRINTER_INFO_5W, pPortName), END},
    {END},
    END,
    END};
static const struct ow_layout printer6 = {sizeof(PRINTER_INFO_6), {END}, {END},
                                       END, END};
static const struct ow_layout printer7 = {
    sizeof(PRINTER_INFO_7W), {O(PRINTER_INFO_7W, pszObjectGUID), END}, {END},
    END, END};
static const struct ow_layout printer8 = {sizeof(PRINTER_INFO_8W), {END}, {END},
                                       O(PRINTER_INFO_8W, pDevMode), END};

static const struct ow_layout driver1 = {
    sizeof(DRIVER_INFO_1W), {O(DRIVER_INFO_1W, pName), END}, {END}, END,
    END};
#define DRIVER_STRS(t)                                                       \
  O(t, pName), O(t, pEnvironment), O(t, pDriverPath), O(t, pDataFile),       \
      O(t, pConfigFile)
static const struct ow_layout driver2 = {
    sizeof(DRIVER_INFO_2W), {DRIVER_STRS(DRIVER_INFO_2W), END}, {END}, END,
    END};
static const struct ow_layout driver3 = {
    sizeof(DRIVER_INFO_3W),
    {DRIVER_STRS(DRIVER_INFO_3W), O(DRIVER_INFO_3W, pHelpFile),
     O(DRIVER_INFO_3W, pMonitorName), O(DRIVER_INFO_3W, pDefaultDataType),
     END},
    {O(DRIVER_INFO_3W, pDependentFiles), END},
    END,
    END};
static const struct ow_layout driver4 = {
    sizeof(DRIVER_INFO_4W),
    {DRIVER_STRS(DRIVER_INFO_4W), O(DRIVER_INFO_4W, pHelpFile),
     O(DRIVER_INFO_4W, pMonitorName), O(DRIVER_INFO_4W, pDefaultDataType),
     END},
    {O(DRIVER_INFO_4W, pDependentFiles), O(DRIVER_INFO_4W, pszzPreviousNames),
     END},
    END,
    END};
static const struct ow_layout driver5 = {
    sizeof(DRIVER_INFO_5W), {DRIVER_STRS(DRIVER_INFO_5W), END}, {END}, END,
    END};
static const struct ow_layout driver6 = {
    sizeof(DRIVER_INFO_6W),
    {DRIVER_STRS(DRIVER_INFO_6W), O(DRIVER_INFO_6W, pHelpFile),
     O(DRIVER_INFO_6W, pMonitorName), O(DRIVER_INFO_6W, pDefaultDataType),
     O(DRIVER_INFO_6W, pszMfgName), O(DRIVER_INFO_6W, pszOEMUrl),
     O(DRIVER_INFO_6W, pszHardwareID), O(DRIVER_INFO_6W, pszProvider), END},
    {O(DRIVER_INFO_6W, pDependentFiles), O(DRIVER_INFO_6W, pszzPreviousNames),
     END},
    END,
    END};

static const struct ow_layout job1 = {
    sizeof(JOB_INFO_1W),
    {O(JOB_INFO_1W, pPrinterName), O(JOB_INFO_1W, pMachineName),
     O(JOB_INFO_1W, pUserName), O(JOB_INFO_1W, pDocument),
     O(JOB_INFO_1W, pDatatype), O(JOB_INFO_1W, pStatus), END},
    {END},
    END,
    END};
static const struct ow_layout job2 = {
    sizeof(JOB_INFO_2W),
    {O(JOB_INFO_2W, pPrinterName), O(JOB_INFO_2W, pMachineName),
     O(JOB_INFO_2W, pUserName), O(JOB_INFO_2W, pDocument),
     O(JOB_INFO_2W, pNotifyName), O(JOB_INFO_2W, pDatatype),
     O(JOB_INFO_2W, pPrintProcessor), O(JOB_INFO_2W, pParameters),
     O(JOB_INFO_2W, pDriverName), O(JOB_INFO_2W, pStatus), END},
    {END},
    O(JOB_INFO_2W, pDevMode),
    O(JOB_INFO_2W, pSecurityDescriptor)};
static const struct ow_layout job3 = {sizeof(JOB_INFO_3), {END}, {END}, END,
                                   END};

static const struct ow_layout port1 = {
    sizeof(PORT_INFO_1W), {O(PORT_INFO_1W, pName), END}, {END}, END, END};
static const struct ow_layout port2 = {
    sizeof(PORT_INFO_2W),
    {O(PORT_INFO_2W, pPortName), O(PORT_INFO_2W, pMonitorName),
     O(PORT_INFO_2W, pDescription), END},
    {END},
    END,
    END};
static const struct ow_layout monitor1 = {
    sizeof(MONITOR_INFO_1W), {O(MONITOR_INFO_1W, pName), END}, {END}, END,
    END};
static const struct ow_layout monitor2 = {
    sizeof(MONITOR_INFO_2W),
    {O(MONITOR_INFO_2W, pName), O(MONITOR_INFO_2W, pEnvironment),
     O(MONITOR_INFO_2W, pDLLName), END},
    {END},
    END,
    END};
static const struct ow_layout procinfo1 = {
    sizeof(PRINTPROCESSOR_INFO_1W), {O(PRINTPROCESSOR_INFO_1W, pName), END},
    {END}, END, END};
static const struct ow_layout datatype1 = {
    sizeof(DATATYPES_INFO_1W), {O(DATATYPES_INFO_1W, pName), END}, {END},
    END, END};
static const struct ow_layout providor1 = {
    sizeof(PROVIDOR_INFO_1W),
    {O(PROVIDOR_INFO_1W, pName), O(PROVIDOR_INFO_1W, pEnvironment),
     O(PROVIDOR_INFO_1W, pDLLName), END},
    {END},
    END,
    END};
static const struct ow_layout doc1 = {
    sizeof(DOC_INFO_1W),
    {O(DOC_INFO_1W, pDocName), O(DOC_INFO_1W, pOutputFile),
     O(DOC_INFO_1W, pDatatype), END},
    {END},
    END,
    END};
static const struct ow_layout doc2 = {
    sizeof(DOC_INFO_2W),
    {O(DOC_INFO_2W, pDocName), O(DOC_INFO_2W, pOutputFile),
     O(DOC_INFO_2W, pDatatype), END},
    {END},
    END,
    END};
static const struct ow_layout addjob1 = {
    sizeof(ADDJOB_INFO_1W), {O(ADDJOB_INFO_1W, Path), END}, {END}, END, END};

static const struct ow_layout *printer_layout(DWORD level) {
  static const struct ow_layout *const l[] = {
      NULL,      &printer1, &printer2, &printer3, &printer4,
      &printer5, &printer6, &printer7, &printer8, &printer8};
  return level < 10 ? l[level] : NULL;
}

static const struct ow_layout *driver_layout(DWORD level) {
  static const struct ow_layout *const l[] = {NULL,     &driver1, &driver2,
                                           &driver3, &driver4, &driver5,
                                           &driver6};
  return level < 7 ? l[level] : NULL;
}

static const struct ow_layout *job_layout(DWORD level) {
  return level == 1 ? &job1 : level == 2 ? &job2 : level == 3 ? &job3 : NULL;
}

/* ---- conversion --------------------------------------------------------- */

static void **field(BYTE *s, int i) { return (void **)s + i; }

static DWORD up(DWORD n, DWORD a) { return (n + a - 1) / a * a; }

static DWORD devmode_w_size(const DEVMODEA *a) {
  return a->dmSize + (offsetof(DEVMODEW, dmLogPixels) -
                      offsetof(DEVMODEA, dmLogPixels)) +
         a->dmDriverExtra;
}

static DWORD devmode_a_size(const DEVMODEW *w) {
  return w->dmSize - (offsetof(DEVMODEW, dmLogPixels) -
                      offsetof(DEVMODEA, dmLogPixels)) +
         w->dmDriverExtra;
}

/* Converts count A structures into W ones in buf (cb bytes). Sets *need
 * to the bytes they take; FALSE when they do not fit. */
BOOL ow_info_w(const struct ow_layout *l, const BYTE *a, DWORD count,
                   BYTE *buf, DWORD cb, DWORD *need) {
  DWORD total = l->size * count, pos;
  for (DWORD k = 0; k < count; k++) {
    BYTE *s = (BYTE *)a + k * l->size;
    for (const signed char *f = l->str; *f != END; f++)
      if (*field(s, *f))
        total += (ow_wlen_of(*field(s, *f), -1, CP_ACP) + 1) * sizeof(WCHAR);
    for (const signed char *f = l->list; *f != END; f++)
      if (*field(s, *f))
        total += ow_wlen_of(*field(s, *f), ow_alist_len(*field(s, *f)),
                            CP_ACP) *
                 sizeof(WCHAR);
    if (l->dm != END && *field(s, l->dm))
      total = up(total, 4) + devmode_w_size(*field(s, l->dm));
  }
  total = up(total, 4);
  if (need)
    *need = total;
  if (!buf || cb < total)
    return FALSE;
  ow_memcpy(buf, a, l->size * count);
  pos = l->size * count;
  for (DWORD k = 0; k < count; k++) {
    BYTE *s = (BYTE *)a + k * l->size, *d = buf + k * l->size;
    for (const signed char *f = l->str; *f != END; f++) {
      LPCSTR str = *field(s, *f);
      if (str) {
        int n = ow_wlen_of(str, -1, CP_ACP) + 1;
        ow_tow(str, -1, (LPWSTR)(buf + pos), n);
        *field(d, *f) = buf + pos;
        pos += n * sizeof(WCHAR);
      }
    }
    for (const signed char *f = l->list; *f != END; f++) {
      LPCSTR str = *field(s, *f);
      if (str) {
        int an = ow_alist_len(str), n = ow_wlen_of(str, an, CP_ACP);
        ow_mb2wc(CP_ACP, 0, str, an, (LPWSTR)(buf + pos), n);
        *field(d, *f) = buf + pos;
        pos += n * sizeof(WCHAR);
      }
    }
    if (l->dm != END && *field(s, l->dm)) {
      const DEVMODEA *dm = *field(s, l->dm);
      DEVMODEW *w;
      pos = up(pos, 4);
      w = (DEVMODEW *)(buf + pos);
      w->dmSize = (WORD)(devmode_w_size(dm) - dm->dmDriverExtra);
      w->dmDriverExtra = dm->dmDriverExtra;
      ow_devmode_to_w(dm, w);
      *field(d, l->dm) = w;
      pos += devmode_w_size(dm);
    }
    if (l->sd != END)
      *field(d, l->sd) = NULL;
  }
  return TRUE;
}

/* One W structure as A, in a single heap block. */
BYTE *ow_info_a(const struct ow_layout *l, const BYTE *w) {
  DWORD total = l->size, pos;
  BYTE *a;
  if (!w)
    return NULL;
  for (const signed char *f = l->str; *f != END; f++)
    if (*field((BYTE *)w, *f))
      total += ow_wc2mb(CP_ACP, 0, *field((BYTE *)w, *f), -1, NULL, 0);
  for (const signed char *f = l->list; *f != END; f++)
    if (*field((BYTE *)w, *f)) {
      LPCWSTR s = *field((BYTE *)w, *f);
      total += ow_wc2mb(CP_ACP, 0, s, ow_wlist_len(s), NULL, 0);
    }
  if (l->dm != END && *field((BYTE *)w, l->dm))
    total = up(total, 4) + devmode_a_size(*field((BYTE *)w, l->dm));
  a = ow_alloc(total);
  if (!a)
    return NULL;
  ow_memcpy(a, w, l->size);
  pos = l->size;
  for (const signed char *f = l->str; *f != END; f++) {
    LPCWSTR s = *field((BYTE *)w, *f);
    if (s) {
      int n = ow_wc2mb(CP_ACP, 0, s, -1, (LPSTR)(a + pos), total - pos);
      *field(a, *f) = a + pos;
      pos += n;
    }
  }
  for (const signed char *f = l->list; *f != END; f++) {
    LPCWSTR s = *field((BYTE *)w, *f);
    if (s) {
      int n = ow_wc2mb(CP_ACP, 0, s, ow_wlist_len(s), (LPSTR)(a + pos),
                       total - pos);
      *field(a, *f) = a + pos;
      pos += n;
    }
  }
  if (l->dm != END && *field((BYTE *)w, l->dm)) {
    LPDEVMODEA dm = ow_devmode_a(*field((BYTE *)w, l->dm));
    pos = up(pos, 4);
    if (dm) {
      ow_memcpy(a + pos, dm, dm->dmSize + dm->dmDriverExtra);
      *field(a, l->dm) = a + pos;
      ow_free(dm);
    } else {
      *field(a, l->dm) = NULL;
    }
  }
  return a;
}

/* Runs an A Enum or Get function into a heap buffer, then returns the W
 * structures the way the W function does. */
typedef BOOL (*info_call)(void *ctx, BYTE *buf, DWORD cb, DWORD *need,
                          DWORD *count);

static BOOL get_info(const struct ow_layout *l, info_call call, void *ctx,
                     BYTE *buf, DWORD cb, DWORD *need, DWORD *count) {
  DWORD an = 0, n = 0, wn;
  BYTE *a;
  BOOL r;
  if (!l) {
    SetLastError(ERROR_INVALID_LEVEL);
    return FALSE;
  }
  if (!call(ctx, NULL, 0, &an, &n) &&
      GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return FALSE;
  a = ow_alloc(an ? an : 1);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  n = 0;
  if (an && !call(ctx, a, an, &an, &n)) {
    ow_free(a);
    return FALSE;
  }
  if (!count)
    n = an ? 1 : 0; /* a Get function: one structure */
  r = ow_info_w(l, a, n, buf, cb, &wn);
  ow_free(a);
  if (need)
    *need = wn;
  if (count)
    *count = r ? n : 0;
  if (!r)
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
  return r;
}

/* ---- enumerations ----------------------------------------------------- */

struct enum_ctx {
  DWORD flags, level;
  LPSTR a, b;
};

static BOOL call_printers(void *c, BYTE *buf, DWORD cb, DWORD *need,
                          DWORD *count) {
  struct enum_ctx *e = c;
  return EnumPrintersA(e->flags, e->a, e->level, buf, cb, need, count);
}

BOOL WINAPI EnumPrintersW(DWORD flags, LPWSTR name, DWORD level, LPBYTE buf,
                          DWORD cb, LPDWORD need, LPDWORD count) {
  struct enum_ctx e = {flags, level, ow_a(name), NULL};
  BOOL r = get_info(printer_layout(level), call_printers, &e, buf, cb, need,
                    count);
  ow_free(e.a);
  return r;
}

static BOOL call_ports(void *c, BYTE *buf, DWORD cb, DWORD *need,
                       DWORD *count) {
  struct enum_ctx *e = c;
  return EnumPortsA(e->a, e->level, buf, cb, need, count);
}

BOOL WINAPI EnumPortsW(LPWSTR name, DWORD level, LPBYTE buf, DWORD cb,
                       LPDWORD need, LPDWORD count) {
  struct enum_ctx e = {0, level, ow_a(name), NULL};
  BOOL r = get_info(level == 1 ? &port1 : level == 2 ? &port2 : NULL,
                    call_ports, &e, buf, cb, need, count);
  ow_free(e.a);
  return r;
}

static BOOL call_monitors(void *c, BYTE *buf, DWORD cb, DWORD *need,
                          DWORD *count) {
  struct enum_ctx *e = c;
  return EnumMonitorsA(e->a, e->level, buf, cb, need, count);
}

BOOL WINAPI EnumMonitorsW(LPWSTR name, DWORD level, LPBYTE buf, DWORD cb,
                          LPDWORD need, LPDWORD count) {
  struct enum_ctx e = {0, level, ow_a(name), NULL};
  BOOL r = get_info(level == 1 ? &monitor1 : level == 2 ? &monitor2 : NULL,
                    call_monitors, &e, buf, cb, need, count);
  ow_free(e.a);
  return r;
}

static BOOL call_procs(void *c, BYTE *buf, DWORD cb, DWORD *need,
                       DWORD *count) {
  struct enum_ctx *e = c;
  return EnumPrintProcessorsA(e->a, e->b, e->level, buf, cb, need, count);
}

BOOL WINAPI EnumPrintProcessorsW(LPWSTR name, LPWSTR env, DWORD level,
                                 LPBYTE buf, DWORD cb, LPDWORD need,
                                 LPDWORD count) {
  struct enum_ctx e = {0, level, ow_a(name), ow_a(env)};
  BOOL r = get_info(level == 1 ? &procinfo1 : NULL, call_procs, &e, buf, cb,
                    need, count);
  ow_free(e.a);
  ow_free(e.b);
  return r;
}

static BOOL call_datatypes(void *c, BYTE *buf, DWORD cb, DWORD *need,
                           DWORD *count) {
  struct enum_ctx *e = c;
  return EnumPrintProcessorDatatypesA(e->a, e->b, e->level, buf, cb, need,
                                      count);
}

BOOL WINAPI EnumPrintProcessorDatatypesW(LPWSTR name, LPWSTR proc,
                                         DWORD level, LPBYTE buf, DWORD cb,
                                         LPDWORD need, LPDWORD count) {
  struct enum_ctx e = {0, level, ow_a(name), ow_a(proc)};
  BOOL r = get_info(level == 1 ? &datatype1 : NULL, call_datatypes, &e, buf,
                    cb, need, count);
  ow_free(e.a);
  ow_free(e.b);
  return r;
}

static BOOL call_drivers(void *c, BYTE *buf, DWORD cb, DWORD *need,
                         DWORD *count) {
  struct enum_ctx *e = c;
  return EnumPrinterDriversA(e->a, e->b, e->level, buf, cb, need, count);
}

BOOL WINAPI EnumPrinterDriversW(LPWSTR name, LPWSTR env, DWORD level,
                                LPBYTE buf, DWORD cb, LPDWORD need,
                                LPDWORD count) {
  struct enum_ctx e = {0, level, ow_a(name), ow_a(env)};
  BOOL r = get_info(driver_layout(level), call_drivers, &e, buf, cb, need,
                    count);
  ow_free(e.a);
  ow_free(e.b);
  return r;
}

/* ---- printers ----------------------------------------------------------- */

static PRINTER_DEFAULTSA *defaults_a(const PRINTER_DEFAULTSW *w,
                                     PRINTER_DEFAULTSA *a) {
  if (!w)
    return NULL;
  a->pDatatype = ow_a(w->pDatatype);
  a->pDevMode = ow_devmode_a(w->pDevMode);
  a->DesiredAccess = w->DesiredAccess;
  return a;
}

static void defaults_free(PRINTER_DEFAULTSA *a) {
  if (a) {
    ow_free(a->pDatatype);
    ow_free(a->pDevMode);
  }
}

BOOL WINAPI OpenPrinterW(LPWSTR name, LPHANDLE h, LPPRINTER_DEFAULTSW def) {
  PRINTER_DEFAULTSA da, *d = defaults_a(def, &da);
  LPSTR n = ow_a(name);
  BOOL r = OpenPrinterA(n, h, d);
  ow_free(n);
  defaults_free(d);
  return r;
}

BOOL WINAPI ResetPrinterW(HANDLE h, LPPRINTER_DEFAULTSW def) {
  PRINTER_DEFAULTSA da, *d = defaults_a(def, &da);
  BOOL r = ResetPrinterA(h, d);
  defaults_free(d);
  return r;
}

struct get_ctx {
  HANDLE h;
  DWORD level, id;
  LPSTR a;
};

static BOOL call_getprinter(void *c, BYTE *buf, DWORD cb, DWORD *need,
                            DWORD *count) {
  struct get_ctx *g = c;
  (void)count;
  return GetPrinterA(g->h, g->level, buf, cb, need);
}

BOOL WINAPI GetPrinterW(HANDLE h, DWORD level, LPBYTE buf, DWORD cb,
                        LPDWORD need) {
  struct get_ctx g = {h, level, 0, NULL};
  return get_info(printer_layout(level), call_getprinter, &g, buf, cb, need,
                  NULL);
}

BOOL WINAPI SetPrinterW(HANDLE h, DWORD level, LPBYTE buf, DWORD cmd) {
  const struct ow_layout *l = printer_layout(level);
  BYTE *a;
  BOOL r;
  if (!buf || !l)
    return SetPrinterA(h, level, buf, cmd);
  a = ow_info_a(l, buf);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  r = SetPrinterA(h, level, a, cmd);
  ow_free(a);
  return r;
}

HANDLE WINAPI AddPrinterW(LPWSTR name, DWORD level, LPBYTE buf) {
  const struct ow_layout *l = printer_layout(level);
  LPSTR n;
  BYTE *a;
  HANDLE r;
  if (!l || !buf) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  a = ow_info_a(l, buf);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  n = ow_a(name);
  r = AddPrinterA(n, level, a);
  ow_free(n);
  ow_free(a);
  return r;
}

DWORD WINAPI GetPrinterDataW(HANDLE h, LPWSTR name, LPDWORD type, LPBYTE data,
                             DWORD cb, LPDWORD need) {
  LPSTR n = ow_a(name);
  DWORD t = 0, an = 0, r;
  BYTE *a;
  r = GetPrinterDataA(h, n, &t, NULL, 0, &an);
  if (r != ERROR_SUCCESS && r != ERROR_MORE_DATA) {
    ow_free(n);
    return r;
  }
  a = ow_alloc(an + 2);
  if (!a) {
    ow_free(n);
    return ERROR_NOT_ENOUGH_MEMORY;
  }
  r = GetPrinterDataA(h, n, &t, a, an, &an);
  ow_free(n);
  if (r == ERROR_SUCCESS) {
    DWORD wcb = an;
    if (t == REG_SZ || t == REG_EXPAND_SZ || t == REG_MULTI_SZ) {
      wcb = ow_wlen_of((LPCSTR)a, an, CP_ACP) * sizeof(WCHAR);
      if (data && cb >= wcb)
        ow_mb2wc(CP_ACP, 0, (LPCSTR)a, an, (LPWSTR)data, cb / 2);
    } else if (data && cb >= wcb) {
      ow_memcpy(data, a, an);
    }
    if (type)
      *type = t;
    if (need)
      *need = wcb;
    if (!data || cb < wcb)
      r = ERROR_MORE_DATA;
  }
  ow_free(a);
  return r;
}

DWORD WINAPI SetPrinterDataW(HANDLE h, LPWSTR name, DWORD type, LPBYTE data,
                             DWORD cb) {
  LPSTR n = ow_a(name);
  DWORD r;
  if (type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ) {
    int al;
    LPSTR a = ow_an((LPCWSTR)data, cb / 2, &al);
    r = SetPrinterDataA(h, n, type, (LPBYTE)a, al);
    ow_free(a);
  } else {
    r = SetPrinterDataA(h, n, type, data, cb);
  }
  ow_free(n);
  return r;
}

/* ---- jobs and documents ------------------------------------------------ */

static BOOL call_getjob(void *c, BYTE *buf, DWORD cb, DWORD *need,
                        DWORD *count) {
  struct get_ctx *g = c;
  (void)count;
  return GetJobA(g->h, g->id, g->level, buf, cb, need);
}

BOOL WINAPI GetJobW(HANDLE h, DWORD id, DWORD level, LPBYTE buf, DWORD cb,
                    LPDWORD need) {
  struct get_ctx g = {h, level, id, NULL};
  return get_info(job_layout(level), call_getjob, &g, buf, cb, need, NULL);
}

BOOL WINAPI SetJobW(HANDLE h, DWORD id, DWORD level, LPBYTE buf, DWORD cmd) {
  const struct ow_layout *l = job_layout(level);
  BYTE *a;
  BOOL r;
  if (!buf || !l)
    return SetJobA(h, id, level, buf, cmd);
  a = ow_info_a(l, buf);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  r = SetJobA(h, id, level, a, cmd);
  ow_free(a);
  return r;
}

static BOOL call_addjob(void *c, BYTE *buf, DWORD cb, DWORD *need,
                        DWORD *count) {
  struct get_ctx *g = c;
  (void)count;
  return AddJobA(g->h, g->level, buf, cb, need);
}

BOOL WINAPI AddJobW(HANDLE h, DWORD level, LPBYTE buf, DWORD cb,
                    LPDWORD need) {
  struct get_ctx g = {h, level, 0, NULL};
  return get_info(level == 1 ? &addjob1 : NULL, call_addjob, &g, buf, cb,
                  need, NULL);
}

DWORD WINAPI StartDocPrinterW(HANDLE h, DWORD level, LPBYTE buf) {
  BYTE *a;
  DWORD r;
  if (!buf || (level != 1 && level != 2))
    return StartDocPrinterA(h, level, buf);
  a = ow_info_a(level == 1 ? &doc1 : &doc2, buf);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
  r = StartDocPrinterA(h, level, a);
  ow_free(a);
  return r;
}

/* ---- drivers ----------------------------------------------------------- */

struct driver_ctx {
  HANDLE h;
  DWORD level;
  LPSTR env;
};

static BOOL call_getdriver(void *c, BYTE *buf, DWORD cb, DWORD *need,
                           DWORD *count) {
  struct driver_ctx *d = c;
  (void)count;
  return GetPrinterDriverA(d->h, d->env, d->level, buf, cb, need);
}

BOOL WINAPI GetPrinterDriverW(HANDLE h, LPWSTR env, DWORD level, LPBYTE buf,
                              DWORD cb, LPDWORD need) {
  struct driver_ctx d = {h, level, ow_a(env)};
  BOOL r = get_info(driver_layout(level), call_getdriver, &d, buf, cb, need,
                    NULL);
  ow_free(d.env);
  return r;
}

BOOL WINAPI AddPrinterDriverW(LPWSTR name, DWORD level, LPBYTE buf) {
  const struct ow_layout *l = driver_layout(level);
  LPSTR n;
  BYTE *a;
  BOOL r;
  if (!l || !buf) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  a = ow_info_a(l, buf);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  n = ow_a(name);
  r = AddPrinterDriverA(n, level, a);
  ow_free(n);
  ow_free(a);
  return r;
}

BOOL WINAPI DeletePrinterDriverW(LPWSTR name, LPWSTR env, LPWSTR driver) {
  LPSTR a = ow_a(name), b = ow_a(env), c = ow_a(driver);
  BOOL r = DeletePrinterDriverA(a, b, c);
  ow_free(a);
  ow_free(b);
  ow_free(c);
  return r;
}

/* A directory from GetPrinterDriverDirectory or
 * GetPrintProcessorDirectory: a string, its size in bytes. */
static BOOL ret_dir(BOOL ok, LPCSTR a, LPBYTE buf, DWORD cb, LPDWORD need) {
  DWORD wcb;
  if (!ok)
    return FALSE;
  wcb = (ow_wlen_of(a, -1, CP_ACP) + 1) * sizeof(WCHAR);
  if (need)
    *need = wcb;
  if (!buf || cb < wcb) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  ow_tow(a, -1, (LPWSTR)buf, cb / 2);
  return TRUE;
}

BOOL WINAPI GetPrinterDriverDirectoryW(LPWSTR name, LPWSTR env, DWORD level,
                                       LPBYTE buf, DWORD cb, LPDWORD need) {
  char d[MAX_PATH * 2];
  DWORD n = sizeof d;
  LPSTR a = ow_a(name), b = ow_a(env);
  BOOL r = GetPrinterDriverDirectoryA(a, b, level, (LPBYTE)d, n, &n);
  ow_free(a);
  ow_free(b);
  return ret_dir(r, d, buf, cb, need);
}

BOOL WINAPI GetPrintProcessorDirectoryW(LPWSTR name, LPWSTR env, DWORD level,
                                        LPBYTE buf, DWORD cb, LPDWORD need) {
  char d[MAX_PATH * 2];
  DWORD n = sizeof d;
  LPSTR a = ow_a(name), b = ow_a(env);
  BOOL r = GetPrintProcessorDirectoryA(a, b, level, (LPBYTE)d, n, &n);
  ow_free(a);
  ow_free(b);
  return ret_dir(r, d, buf, cb, need);
}

/* ---- ports, monitors, print processors and providers ------------------- */

BOOL WINAPI AddPortW(LPWSTR name, HWND h, LPWSTR monitor) {
  LPSTR a = ow_a(name), b = ow_a(monitor);
  BOOL r = AddPortA(a, h, b);
  ow_free(a);
  ow_free(b);
  return r;
}

BOOL WINAPI ConfigurePortW(LPWSTR name, HWND h, LPWSTR port) {
  LPSTR a = ow_a(name), b = ow_a(port);
  BOOL r = ConfigurePortA(a, h, b);
  ow_free(a);
  ow_free(b);
  return r;
}

BOOL WINAPI DeletePortW(LPWSTR name, HWND h, LPWSTR port) {
  LPSTR a = ow_a(name), b = ow_a(port);
  BOOL r = DeletePortA(a, h, b);
  ow_free(a);
  ow_free(b);
  return r;
}

BOOL WINAPI AddMonitorW(LPWSTR name, DWORD level, LPBYTE buf) {
  LPSTR n;
  BYTE *a;
  BOOL r;
  if (level != 2 || !buf) {
    SetLastError(ERROR_INVALID_LEVEL);
    return FALSE;
  }
  a = ow_info_a(&monitor2, buf);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  n = ow_a(name);
  r = AddMonitorA(n, level, a);
  ow_free(n);
  ow_free(a);
  return r;
}

BOOL WINAPI DeleteMonitorW(LPWSTR name, LPWSTR env, LPWSTR monitor) {
  LPSTR a = ow_a(name), b = ow_a(env), c = ow_a(monitor);
  BOOL r = DeleteMonitorA(a, b, c);
  ow_free(a);
  ow_free(b);
  ow_free(c);
  return r;
}

BOOL WINAPI AddPrintProcessorW(LPWSTR name, LPWSTR env, LPWSTR path,
                               LPWSTR proc) {
  LPSTR a = ow_a(name), b = ow_a(env), c = ow_a(path), d = ow_a(proc);
  BOOL r = AddPrintProcessorA(a, b, c, d);
  ow_free(a);
  ow_free(b);
  ow_free(c);
  ow_free(d);
  return r;
}

BOOL WINAPI DeletePrintProcessorW(LPWSTR name, LPWSTR env, LPWSTR proc) {
  LPSTR a = ow_a(name), b = ow_a(env), c = ow_a(proc);
  BOOL r = DeletePrintProcessorA(a, b, c);
  ow_free(a);
  ow_free(b);
  ow_free(c);
  return r;
}

BOOL WINAPI AddPrintProvidorW(LPWSTR name, DWORD level, LPBYTE buf) {
  LPSTR n;
  BYTE *a;
  BOOL r;
  if (level != 1 || !buf) {
    SetLastError(ERROR_INVALID_LEVEL);
    return FALSE;
  }
  a = ow_info_a(&providor1, buf);
  if (!a) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  n = ow_a(name);
  r = AddPrintProvidorA(n, level, a);
  ow_free(n);
  ow_free(a);
  return r;
}

BOOL WINAPI DeletePrintProvidorW(LPWSTR name, LPWSTR env, LPWSTR prov) {
  LPSTR a = ow_a(name), b = ow_a(env), c = ow_a(prov);
  BOOL r = DeletePrintProvidorA(a, b, c);
  ow_free(a);
  ow_free(b);
  ow_free(c);
  return r;
}

/* ---- device capabilities and document properties ---------------------- */

/* The string results of DeviceCapabilities: arrays of fixed-size names. */
static int cap_name_size(WORD cap) {
  switch (cap) {
  case DC_BINNAMES:
    return 24;
  case DC_PAPERNAMES:
  case DC_FILEDEPENDENCIES:
  case DC_MEDIATYPENAMES:
  case DC_MEDIAREADY:
    return 64;
  case DC_PERSONALITY:
    return 32;
  }
  return 0;
}

int WINAPI DeviceCapabilitiesW(LPCWSTR device, LPCWSTR port, WORD cap,
                               LPWSTR out, const DEVMODEW *dm) {
  LPSTR d = ow_a(device), p = ow_a(port), a = NULL;
  LPDEVMODEA m = ow_devmode_a(dm);
  int size = cap_name_size(cap), r;
  if (size && out) {
    r = DeviceCapabilitiesA(d, p, cap, NULL, m);
    if (r > 0) {
      a = ow_alloc(r * size + 1);
      if (a) {
        r = DeviceCapabilitiesA(d, p, cap, a, m);
        for (int i = 0; i < r; i++) {
          LPCSTR s = a + i * size;
          int n = 0;
          while (n < size && s[n])
            n++;
          ow_memset(out + i * size, 0, size * sizeof(WCHAR));
          ow_tow(s, n, out + i * size, size);
        }
        ow_free(a);
      } else {
        r = -1;
      }
    }
  } else {
    r = DeviceCapabilitiesA(d, p, cap, (LPSTR)out, m);
  }
  ow_free(d);
  ow_free(p);
  ow_free(m);
  return r;
}

LONG WINAPI DocumentPropertiesW(HWND h, HANDLE printer, LPWSTR device,
                                PDEVMODEW out, PDEVMODEW in, DWORD mode) {
  LPSTR d = ow_a(device);
  LPDEVMODEA ain = (mode & DM_IN_BUFFER) ? ow_devmode_a(in) : NULL, aout = NULL;
  LONG r;
  if (!mode || !(mode & DM_OUT_BUFFER)) {
    r = DocumentPropertiesA(h, printer, d, NULL, ain, mode);
    if (!mode && r > 0)
      r += offsetof(DEVMODEW, dmLogPixels) - offsetof(DEVMODEA, dmLogPixels);
  } else {
    LONG size = DocumentPropertiesA(h, printer, d, NULL, NULL, 0);
    aout = size > 0 ? ow_alloc(size) : NULL;
    if (!aout) {
      r = -1;
    } else {
      r = DocumentPropertiesA(h, printer, d, aout, ain, mode);
      if (r >= 0 && out) {
        LPDEVMODEW w = ow_devmode_w(aout);
        if (w) {
          ow_memcpy(out, w, w->dmSize + w->dmDriverExtra);
          ow_free(w);
        }
      }
    }
  }
  ow_free(d);
  ow_free(ain);
  ow_free(aout);
  return r;
}

LONG WINAPI AdvancedDocumentPropertiesW(HWND h, HANDLE printer, LPWSTR device,
                                        PDEVMODEW out, PDEVMODEW in) {
  return DocumentPropertiesW(h, printer, device, out, in,
                             DM_IN_BUFFER | DM_OUT_BUFFER | DM_IN_PROMPT) ==
                 IDOK
             ? 1
             : 0;
}
