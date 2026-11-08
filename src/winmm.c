/* winmm.dll: sounds, device capabilities and error texts, MCI and MMIO.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"
#include <mmsystem.h>
#include <digitalv.h>

#ifndef MCI_SOUND
#define MCI_SOUND 0x0812
#endif
#ifndef MCI_SOUND_NAME
#define MCI_SOUND_NAME 0x00000100L
#endif
#ifndef MCI_DGV_WINDOW_TEXT
#define MCI_DGV_WINDOW_TEXT 0x00080000L
#endif

/* ---- sounds ------------------------------------------------------------ */

BOOL WINAPI PlaySoundW(LPCWSTR sound, HMODULE mod, DWORD flags) {
  LPSTR a;
  BOOL r;
  OW_FN(OW_WINMM, PlaySoundA, return FALSE);
  if (!sound || (flags & SND_MEMORY) ||
      (flags & SND_ALIAS_ID) == SND_ALIAS_ID)
    return pPlaySoundA((LPCSTR)sound, mod, flags);
  a = (flags & SND_RESOURCE) == SND_RESOURCE ? ow_ares(sound) : ow_a(sound);
  r = pPlaySoundA(a, mod, flags);
  ow_free(a);
  return r;
}

BOOL WINAPI sndPlaySoundW(LPCWSTR sound, UINT flags) {
  LPSTR a;
  BOOL r;
  OW_FN(OW_WINMM, sndPlaySoundA, return FALSE);
  if (!sound || (flags & SND_MEMORY))
    return psndPlaySoundA((LPCSTR)sound, flags);
  a = ow_a(sound);
  r = psndPlaySoundA(a, flags);
  ow_free(a);
  return r;
}

/* ---- device capabilities -------------------------------------------------
 * The structures start with wMid, wPid, vDriverVersion and szPname, and
 * the rest is the same in both forms. */

#define CAPS_HEAD 8
#define CAPS_TAIL_A (CAPS_HEAD + MAXPNAMELEN)
#define CAPS_TAIL_W (CAPS_HEAD + MAXPNAMELEN * 2)

typedef MMRESULT(WINAPI *caps_fn)(UINT_PTR, void *, UINT);

static MMRESULT caps(const char *name, UINT_PTR id, void *w, UINT cb,
                     UINT asize) {
  BYTE a[256];
  BYTE wbuf[512];
  UINT wsize = asize + (CAPS_TAIL_W - CAPS_TAIL_A);
  MMRESULT r;
  caps_fn fn = (caps_fn)ow_proc(OW_WINMM, name);
  if (!fn)
    return MMSYSERR_NOTSUPPORTED;
  if (!w)
    return MMSYSERR_INVALPARAM;
  ow_memset(a, 0, sizeof a);
  r = fn(id, a, asize);
  if (r != MMSYSERR_NOERROR)
    return r;
  ow_memset(wbuf, 0, sizeof wbuf);
  ow_memcpy(wbuf, a, CAPS_HEAD);
  a[CAPS_TAIL_A - 1] = 0; /* a name that fills the field */
  ow_tow((LPCSTR)a + CAPS_HEAD, -1, (LPWSTR)(wbuf + CAPS_HEAD), MAXPNAMELEN);
  ow_memcpy(wbuf + CAPS_TAIL_W, a + CAPS_TAIL_A, asize - CAPS_TAIL_A);
  ow_memcpy(w, wbuf, cb < wsize ? cb : wsize);
  return r;
}

MMRESULT WINAPI auxGetDevCapsW(UINT_PTR id, LPAUXCAPSW w, UINT cb) {
  return caps("auxGetDevCapsA", id, w, cb, sizeof(AUXCAPSA));
}
MMRESULT WINAPI midiInGetDevCapsW(UINT_PTR id, LPMIDIINCAPSW w, UINT cb) {
  return caps("midiInGetDevCapsA", id, w, cb, sizeof(MIDIINCAPSA));
}
MMRESULT WINAPI midiOutGetDevCapsW(UINT_PTR id, LPMIDIOUTCAPSW w, UINT cb) {
  return caps("midiOutGetDevCapsA", id, w, cb, sizeof(MIDIOUTCAPSA));
}
MMRESULT WINAPI mixerGetDevCapsW(UINT_PTR id, LPMIXERCAPSW w, UINT cb) {
  return caps("mixerGetDevCapsA", id, w, cb, sizeof(MIXERCAPSA));
}
MMRESULT WINAPI waveInGetDevCapsW(UINT_PTR id, LPWAVEINCAPSW w, UINT cb) {
  return caps("waveInGetDevCapsA", id, w, cb, sizeof(WAVEINCAPSA));
}
MMRESULT WINAPI waveOutGetDevCapsW(UINT_PTR id, LPWAVEOUTCAPSW w, UINT cb) {
  return caps("waveOutGetDevCapsA", id, w, cb, sizeof(WAVEOUTCAPSA));
}

MMRESULT WINAPI joyGetDevCapsW(UINT_PTR id, LPJOYCAPSW w, UINT cb) {
  JOYCAPSA a;
  JOYCAPSW t;
  MMRESULT r;
  OW_FN(OW_WINMM, joyGetDevCapsA, return MMSYSERR_NOTSUPPORTED);
  if (!w)
    return MMSYSERR_INVALPARAM;
  ow_memset(&a, 0, sizeof a);
  r = pjoyGetDevCapsA(id, &a, sizeof a);
  if (r != JOYERR_NOERROR)
    return r;
  ow_memset(&t, 0, sizeof t);
  t.wMid = a.wMid;
  t.wPid = a.wPid;
  ow_tow(a.szPname, -1, t.szPname, MAXPNAMELEN);
  ow_memcpy(&t.wXmin, &a.wXmin,
            offsetof(JOYCAPSA, szRegKey) - offsetof(JOYCAPSA, wXmin));
  ow_tow(a.szRegKey, -1, t.szRegKey, MAXPNAMELEN);
  ow_tow(a.szOEMVxD, -1, t.szOEMVxD, MAX_JOYSTICKOEMVXDNAME);
  ow_memcpy(w, &t, cb < sizeof t ? cb : sizeof t);
  return r;
}

/* ---- error texts ---------------------------------------------------------- */

typedef MMRESULT(WINAPI *text_fn)(MMRESULT, LPSTR, UINT);

static MMRESULT error_text(const char *name, MMRESULT e, LPWSTR buf,
                           UINT cch) {
  char a[MAXERRORLENGTH * 2];
  MMRESULT r;
  text_fn fn = (text_fn)ow_proc(OW_WINMM, name);
  if (!fn)
    return MMSYSERR_NOTSUPPORTED;
  if (!buf || !cch)
    return fn(e, NULL, 0);
  a[0] = 0;
  r = fn(e, a, sizeof a);
  if (r == MMSYSERR_NOERROR)
    ow_tow(a, -1, buf, cch);
  return r;
}

MMRESULT WINAPI midiInGetErrorTextW(MMRESULT e, LPWSTR buf, UINT cch) {
  return error_text("midiInGetErrorTextA", e, buf, cch);
}
MMRESULT WINAPI midiOutGetErrorTextW(MMRESULT e, LPWSTR buf, UINT cch) {
  return error_text("midiOutGetErrorTextA", e, buf, cch);
}
MMRESULT WINAPI waveInGetErrorTextW(MMRESULT e, LPWSTR buf, UINT cch) {
  return error_text("waveInGetErrorTextA", e, buf, cch);
}
MMRESULT WINAPI waveOutGetErrorTextW(MMRESULT e, LPWSTR buf, UINT cch) {
  return error_text("waveOutGetErrorTextA", e, buf, cch);
}

BOOL WINAPI mciGetErrorStringW(MCIERROR e, LPWSTR buf, UINT cch) {
  char a[MAXERRORLENGTH * 2];
  BOOL r;
  OW_FN(OW_WINMM, mciGetErrorStringA, return FALSE);
  if (!buf || !cch)
    return pmciGetErrorStringA(e, NULL, 0);
  a[0] = 0;
  r = pmciGetErrorStringA(e, a, sizeof a);
  ow_tow(a, -1, buf, cch);
  return r;
}

/* ---- mixers --------------------------------------------------------------- */

static void line_a(const MIXERLINEW *w, MIXERLINEA *a) {
  ow_memset(a, 0, sizeof *a);
  ow_memcpy(a, w, offsetof(MIXERLINEA, szShortName));
  a->cbStruct = sizeof *a;
  ow_toa(w->szShortName, ow_wcsnlen(w->szShortName, MIXER_SHORT_NAME_CHARS),
         a->szShortName, MIXER_SHORT_NAME_CHARS);
  ow_toa(w->szName, ow_wcsnlen(w->szName, MIXER_LONG_NAME_CHARS), a->szName,
         MIXER_LONG_NAME_CHARS);
  a->Target.dwType = w->Target.dwType;
  a->Target.dwDeviceID = w->Target.dwDeviceID;
  a->Target.wMid = w->Target.wMid;
  a->Target.wPid = w->Target.wPid;
  a->Target.vDriverVersion = w->Target.vDriverVersion;
  ow_toa(w->Target.szPname, ow_wcsnlen(w->Target.szPname, MAXPNAMELEN),
         a->Target.szPname, MAXPNAMELEN);
}

static void line_w(const MIXERLINEA *a, MIXERLINEW *w) {
  DWORD cb = w->cbStruct;
  ow_memcpy(w, a, offsetof(MIXERLINEA, szShortName));
  w->cbStruct = cb;
  ow_tow(a->szShortName, -1, w->szShortName, MIXER_SHORT_NAME_CHARS);
  ow_tow(a->szName, -1, w->szName, MIXER_LONG_NAME_CHARS);
  w->Target.dwType = a->Target.dwType;
  w->Target.dwDeviceID = a->Target.dwDeviceID;
  w->Target.wMid = a->Target.wMid;
  w->Target.wPid = a->Target.wPid;
  w->Target.vDriverVersion = a->Target.vDriverVersion;
  ow_tow(a->Target.szPname, -1, w->Target.szPname, MAXPNAMELEN);
}

MMRESULT WINAPI mixerGetLineInfoW(HMIXEROBJ h, LPMIXERLINEW w, DWORD flags) {
  MIXERLINEA a;
  MMRESULT r;
  OW_FN(OW_WINMM, mixerGetLineInfoA, return MMSYSERR_NOTSUPPORTED);
  if (!w || w->cbStruct < sizeof *w)
    return MMSYSERR_INVALPARAM;
  line_a(w, &a);
  r = pmixerGetLineInfoA(h, &a, flags);
  if (r == MMSYSERR_NOERROR)
    line_w(&a, w);
  return r;
}

MMRESULT WINAPI mixerGetLineControlsW(HMIXEROBJ h, LPMIXERLINECONTROLSW w,
                                      DWORD flags) {
  MIXERLINECONTROLSA a;
  MIXERCONTROLA *ac;
  DWORD n, i;
  MMRESULT r;
  OW_FN(OW_WINMM, mixerGetLineControlsA, return MMSYSERR_NOTSUPPORTED);
  if (!w || w->cbStruct < sizeof *w || w->cbmxctrl < sizeof(MIXERCONTROLW))
    return MMSYSERR_INVALPARAM;
  switch (flags & MIXER_GETLINECONTROLSF_QUERYMASK) {
  case MIXER_GETLINECONTROLSF_ONEBYID:
  case MIXER_GETLINECONTROLSF_ONEBYTYPE:
    n = 1;
    break;
  default:
    n = w->cControls;
  }
  ac = ow_alloc((n ? n : 1) * sizeof *ac);
  if (!ac)
    return MMSYSERR_NOMEM;
  a.cbStruct = sizeof a;
  a.dwLineID = w->dwLineID;
  a.dwControlID = w->dwControlID; /* or dwControlType: the same field */
  a.cControls = w->cControls;
  a.cbmxctrl = sizeof *ac;
  a.pamxctrl = ac;
  r = pmixerGetLineControlsA(h, &a, flags);
  if (r == MMSYSERR_NOERROR) {
    for (i = 0; i < n; i++) {
      MIXERCONTROLW *c =
          (MIXERCONTROLW *)((BYTE *)w->pamxctrl + i * w->cbmxctrl);
      c->cbStruct = sizeof *c;
      c->dwControlID = ac[i].dwControlID;
      c->dwControlType = ac[i].dwControlType;
      c->fdwControl = ac[i].fdwControl;
      c->cMultipleItems = ac[i].cMultipleItems;
      ow_tow(ac[i].szShortName, -1, c->szShortName, MIXER_SHORT_NAME_CHARS);
      ow_tow(ac[i].szName, -1, c->szName, MIXER_LONG_NAME_CHARS);
      ow_memcpy(&c->Bounds, &ac[i].Bounds, sizeof c->Bounds);
      ow_memcpy(&c->Metrics, &ac[i].Metrics, sizeof c->Metrics);
    }
    w->dwLineID = a.dwLineID;
  }
  ow_free(ac);
  return r;
}

MMRESULT WINAPI mixerGetControlDetailsW(HMIXEROBJ h,
                                        LPMIXERCONTROLDETAILS d,
                                        DWORD flags) {
  MIXERCONTROLDETAILS a;
  MIXERCONTROLDETAILS_LISTTEXTA *at;
  DWORD n, i;
  MMRESULT r;
  OW_FN(OW_WINMM, mixerGetControlDetailsA, return MMSYSERR_NOTSUPPORTED);
  if ((flags & MIXER_GETCONTROLDETAILSF_QUERYMASK) !=
          MIXER_GETCONTROLDETAILSF_LISTTEXT ||
      !d)
    return pmixerGetControlDetailsA(h, d, flags);
  n = (d->cChannels ? d->cChannels : 1) *
      (d->cMultipleItems ? d->cMultipleItems : 1);
  at = ow_alloc(n * sizeof *at);
  if (!at)
    return MMSYSERR_NOMEM;
  a = *d;
  a.cbDetails = sizeof *at;
  a.paDetails = at;
  r = pmixerGetControlDetailsA(h, &a, flags);
  if (r == MMSYSERR_NOERROR)
    for (i = 0; i < n; i++) {
      MIXERCONTROLDETAILS_LISTTEXTW *t =
          (MIXERCONTROLDETAILS_LISTTEXTW *)((BYTE *)d->paDetails +
                                            i * d->cbDetails);
      t->dwParam1 = at[i].dwParam1;
      t->dwParam2 = at[i].dwParam2;
      ow_tow(at[i].szName, -1, t->szName, MIXER_LONG_NAME_CHARS);
    }
  ow_free(at);
  return r;
}

/* ---- MCI -------------------------------------------------------------- */

MCIDEVICEID WINAPI mciGetDeviceIDW(LPCWSTR dev) {
  LPSTR a = ow_a(dev);
  MCIDEVICEID r;
  OW_FN(OW_WINMM, mciGetDeviceIDA, return 0);
  r = pmciGetDeviceIDA(a);
  ow_free(a);
  return r;
}

MCIERROR WINAPI mciSendStringW(LPCWSTR cmd, LPWSTR ret, UINT cch,
                               HWND callback) {
  LPSTR a = ow_a(cmd), r = NULL;
  MCIERROR e;
  OW_FN(OW_WINMM, mciSendStringA, return MCIERR_OUT_OF_MEMORY);
  if (ret && cch) {
    r = ow_alloc(cch * 2);
    if (!r) {
      ow_free(a);
      return MCIERR_OUT_OF_MEMORY;
    }
  }
  e = pmciSendStringA(a, r, r ? cch * 2 : 0, callback);
  if (r)
    ow_tow(r, -1, ret, cch);
  ow_free(a);
  ow_free(r);
  return e;
}

/* The parameter blocks of the commands that carry strings, as slots the
 * size of a pointer: which slots are strings coming in (when a flag is
 * given), which one returns a string and which holds its size, and which
 * slots come back. */
struct mci_cmd {
  UINT msg;
  BYTE n;        /* slots */
  BYTE next;     /* slots with ext_flag */
  DWORD ext_flag;
  BYTE in[2];    /* string slots, or 0 */
  DWORD in_flag[2];
  BYTE out, size;   /* returned string and its size in characters, or 0 */
  DWORD out_unless; /* flags with which nothing is returned */
  WORD back;        /* bit i: slot i comes back */
};

static const struct mci_cmd mci_cmds[] = {
    {MCI_OPEN, 5, 7, 0xffff0000, {2, 3},
     {MCI_OPEN_TYPE, MCI_OPEN_ELEMENT}, 0, 0, 0, 1 << 1},
    {MCI_INFO, 3, 4, MCI_DGV_INFO_ITEM, {0, 0}, {0, 0}, 1, 2, 0, 0},
    {MCI_SYSINFO, 5, 5, 0, {0, 0}, {0, 0}, 1, 2, MCI_SYSINFO_QUANTITY, 0},
    {MCI_LOAD, 2, 6, 0xffff0000, {1, 0}, {MCI_LOAD_FILE, 0}, 0, 0, 0, 0},
    {MCI_SAVE, 2, 6, 0xffff0000, {1, 0}, {MCI_SAVE_FILE, 0}, 0, 0, 0, 0},
    {MCI_SOUND, 2, 2, 0, {1, 0}, {MCI_SOUND_NAME, 0}, 0, 0, 0, 0},
    {MCI_ESCAPE, 2, 2, 0, {1, 0}, {MCI_VD_ESCAPE_STRING, 0}, 0, 0, 0, 0},
    {MCI_CAPTURE, 6, 6, 0, {1, 0}, {MCI_DGV_CAPTURE_AS, 0}, 0, 0, 0, 0},
    {MCI_RESERVE, 3, 3, 0, {1, 0}, {MCI_DGV_RESERVE_IN, 0}, 0, 0, 0, 0},
    {MCI_RESTORE, 6, 6, 0, {1, 0}, {MCI_DGV_RESTORE_FROM, 0}, 0, 0, 0, 0},
    {MCI_SETAUDIO, 6, 6, 0, {4, 5},
     {MCI_DGV_SETAUDIO_ALG, MCI_DGV_SETAUDIO_QUALITY}, 0, 0, 0, 0},
    {MCI_SETVIDEO, 7, 7, 0, {4, 5},
     {MCI_DGV_SETVIDEO_ALG, MCI_DGV_SETVIDEO_QUALITY}, 0, 0, 0, 0},
    {MCI_QUALITY, 5, 5, 0, {2, 0}, {MCI_QUALITY_NAME, 0}, 0, 0, 0, 1 << 4},
    {MCI_LIST, 6, 6, 0, {5, 0}, {MCI_DGV_LIST_ALG, 0}, 1, 2,
     MCI_DGV_LIST_COUNT, 1 << 3},
    {MCI_STATUS, 6, 6, 0, {4, 0}, {MCI_DGV_STATUS_DISKSPACE, 0}, 0, 0, 0,
     1 << 1},
    {MCI_WINDOW, 4, 4, 0, {3, 0}, {MCI_DGV_WINDOW_TEXT, 0}, 0, 0, 0, 0},
};

MCIERROR WINAPI mciSendCommandW(MCIDEVICEID id, UINT msg, DWORD_PTR flags,
                                DWORD_PTR parm) {
  const struct mci_cmd *c = NULL;
  DWORD_PTR *w = (DWORD_PTR *)parm, a[8];
  LPSTR str[3] = {NULL, NULL, NULL};
  BOOL any = FALSE, out;
  MCIERROR e;
  int i, n;
  OW_FN(OW_WINMM, mciSendCommandA, return MCIERR_OUT_OF_MEMORY);
  for (i = 0; i < (int)(sizeof mci_cmds / sizeof *mci_cmds); i++)
    if (mci_cmds[i].msg == msg)
      c = &mci_cmds[i];
  if (!c || !w)
    return pmciSendCommandA(id, msg, flags, parm);
  out = c->out && !(flags & c->out_unless) && w[c->out] && w[c->size];
  for (i = 0; i < 2; i++)
    if (c->in[i] && (flags & c->in_flag[i]))
      any = TRUE;
  if (msg == MCI_OPEN && (flags & MCI_OPEN_ALIAS))
    any = TRUE;
  if (!any && !out)
    return pmciSendCommandA(id, msg, flags, parm);
  n = (flags & c->ext_flag) ? c->next : c->n;
  ow_memcpy(a, w, n * sizeof *a);
  for (i = 0; i < 2; i++)
    if (c->in[i] && (flags & c->in_flag[i]) &&
        !(msg == MCI_OPEN && i == 0 && (flags & MCI_OPEN_TYPE_ID)) &&
        !(msg == MCI_OPEN && i == 1 && (flags & MCI_OPEN_ELEMENT_ID)))
      a[c->in[i]] = (DWORD_PTR)(str[i] = ow_a((LPCWSTR)w[c->in[i]]));
  if (msg == MCI_OPEN && (flags & MCI_OPEN_ALIAS))
    a[4] = (DWORD_PTR)(str[2] = ow_a((LPCWSTR)w[4]));
  if (out) {
    a[c->size] = w[c->size] * 2;
    a[c->out] = (DWORD_PTR)(str[2] = ow_alloc(a[c->size]));
    if (!str[2]) {
      e = MCIERR_OUT_OF_MEMORY;
      goto done;
    }
  }
  e = pmciSendCommandA(id, msg, flags, (DWORD_PTR)a);
  for (i = 0; i < n; i++)
    if (c->back & (1 << i))
      w[i] = a[i];
  if (out && !e)
    ow_tow(str[2], -1, (LPWSTR)w[c->out], (int)w[c->size]);
done:
  for (i = 0; i < 3; i++)
    ow_free(str[i]);
  return e;
}

/* ---- MMIO ------------------------------------------------------------------
 * A W I/O procedure gets the names of MMIOM_OPEN and MMIOM_RENAME in W;
 * winmm of 9x passes them in A, so it is called through mmio_proc, which
 * finds it by the FOURCC in the MMIOINFO. */

#define MAX_PROCS 32
static struct {
  FOURCC fcc;
  LPMMIOPROC proc;
} procs[MAX_PROCS];

static LPMMIOPROC find_proc(FOURCC fcc) {
  int i;
  for (i = 0; i < MAX_PROCS; i++)
    if (procs[i].proc && procs[i].fcc == fcc)
      return procs[i].proc;
  return NULL;
}

static LRESULT CALLBACK mmio_proc(LPSTR info, UINT m, LPARAM l1, LPARAM l2) {
  LPMMIOINFO mi = (LPMMIOINFO)info;
  LPMMIOPROC p = mi ? find_proc(mi->fccIOProc) : NULL;
  LRESULT r;
  if (!p)
    return MMIOERR_CANNOTOPEN;
  if (m == MMIOM_OPEN || m == MMIOM_RENAME) {
    LPWSTR a = ow_w((LPCSTR)l1);
    LPWSTR b = m == MMIOM_RENAME ? ow_w((LPCSTR)l2) : NULL;
    r = p(info, m, (LPARAM)a, m == MMIOM_RENAME ? (LPARAM)b : l2);
    ow_free(a);
    ow_free(b);
    return r;
  }
  return p(info, m, l1, l2);
}

LPMMIOPROC WINAPI mmioInstallIOProcW(FOURCC fcc, LPMMIOPROC proc,
                                     DWORD flags) {
  int i;
  LPMMIOPROC r;
  OW_FN(OW_WINMM, mmioInstallIOProcA, return NULL);
  switch (flags & (MMIO_INSTALLPROC | MMIO_REMOVEPROC | MMIO_FINDPROC)) {
  case MMIO_INSTALLPROC:
    for (i = 0; i < MAX_PROCS; i++)
      if (!procs[i].proc)
        break;
    if (i == MAX_PROCS || !proc)
      return NULL;
    r = pmmioInstallIOProcA(fcc, mmio_proc, flags);
    if (r) {
      procs[i].fcc = fcc;
      procs[i].proc = proc;
      return proc;
    }
    return NULL;
  case MMIO_REMOVEPROC:
    for (i = 0; i < MAX_PROCS; i++)
      if (procs[i].proc && procs[i].fcc == fcc &&
          (!proc || procs[i].proc == proc)) {
        if (pmmioInstallIOProcA(fcc, mmio_proc, flags)) {
          r = procs[i].proc;
          procs[i].proc = NULL;
          return r;
        }
        return NULL;
      }
    return pmmioInstallIOProcA(fcc, proc, flags);
  case MMIO_FINDPROC:
    r = find_proc(fcc);
    return r ? r : pmmioInstallIOProcA(fcc, proc, flags);
  }
  return pmmioInstallIOProcA(fcc, proc, flags);
}

HMMIO WINAPI mmioOpenW(LPWSTR name, LPMMIOINFO info, DWORD flags) {
  char a[MAX_PATH * 2];
  HMMIO r;
  OW_FN(OW_WINMM, mmioOpenA, return NULL);
  if (!name)
    return pmmioOpenA(NULL, info, flags);
  /* A file in memory, or a name for an I/O procedure */
  ow_toa(name, -1, a, sizeof a);
  r = pmmioOpenA(a, info, flags);
  /* These return the full name of the file in the buffer, which holds
   * 128 characters. */
  if (flags & (MMIO_PARSE | MMIO_GETTEMP))
    ow_tow(a, -1, name, 128);
  return r;
}

MMRESULT WINAPI mmioRenameW(LPCWSTR from, LPCWSTR to, LPCMMIOINFO info,
                            DWORD flags) {
  LPSTR a = ow_a(from), b = ow_a(to);
  MMRESULT r;
  OW_FN(OW_WINMM, mmioRenameA, return MMSYSERR_NOTSUPPORTED);
  r = pmmioRenameA(a, b, info, flags);
  ow_free(a);
  ow_free(b);
  return r;
}

FOURCC WINAPI mmioStringToFOURCCW(LPCWSTR s, UINT flags) {
  LPSTR a = ow_a(s);
  FOURCC r;
  OW_FN(OW_WINMM, mmioStringToFOURCCA, return 0);
  r = pmmioStringToFOURCCA(a, flags);
  ow_free(a);
  return r;
}
