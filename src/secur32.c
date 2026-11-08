/* secur32.dll (security.dll on some 9x systems): SSPI. Strings that the
 * W functions return are allocated here, so FreeContextBuffer tells them
 * from the system's.
 *
 * Copyright (C) clang-retro contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "ow.h"
#define _NO_KSECDD_IMPORT_
#include <security.h>

/* ---- buffers made here ------------------------------------------------ */

static void **mine;
static int nmine, maxmine;
static LONG lock;

static void enter(void) {
  while (InterlockedExchange(&lock, 1))
    Sleep(0);
}
static void leave(void) { InterlockedExchange(&lock, 0); }

static void *buffer(SIZE_T n) {
  void *p = ow_alloc(n);
  if (!p)
    return NULL;
  enter();
  if (nmine == maxmine) {
    int m = maxmine ? maxmine * 2 : 16;
    void **q = ow_realloc(mine, m * sizeof *q);
    if (!q) {
      leave();
      ow_free(p);
      return NULL;
    }
    mine = q;
    maxmine = m;
  }
  mine[nmine++] = p;
  leave();
  return p;
}

static BOOL free_mine(void *p) {
  int i;
  enter();
  for (i = 0; i < nmine; i++)
    if (mine[i] == p) {
      mine[i] = mine[--nmine];
      leave();
      ow_free(p);
      return TRUE;
    }
  leave();
  return FALSE;
}

/* ---- the A functions --------------------------------------------------- */

static PSecurityFunctionTableA tab(void) {
  static PSecurityFunctionTableA t;
  if (!t) {
    INIT_SECURITY_INTERFACE_A init = (INIT_SECURITY_INTERFACE_A)ow_proc(
        OW_SECUR32, "InitSecurityInterfaceA");
    if (init)
      t = init();
  }
  return t;
}

#define A(fn, fail)                                                         \
  PSecurityFunctionTableA t = tab();                                         \
  if (!t || !t->fn)                                                          \
    return fail;

SECURITY_STATUS SEC_ENTRY FreeContextBuffer(PVOID p) {
  if (!p)
    return SEC_E_OK;
  if (free_mine(p))
    return SEC_E_OK;
  {
    A(FreeContextBuffer, SEC_E_UNSUPPORTED_FUNCTION);
    return t->FreeContextBuffer(p);
  }
}

static SECURITY_STATUS free_a(void *p) {
  PSecurityFunctionTableA t = tab();
  if (p && t && t->FreeContextBuffer)
    t->FreeContextBuffer(p);
  return SEC_E_OK;
}

/* A W copy of a returned A string, freed with FreeContextBuffer. */
static SEC_WCHAR *string_w(SEC_CHAR *a) {
  int n;
  SEC_WCHAR *w;
  if (!a)
    return NULL;
  n = ow_wlen_of(a, -1, CP_ACP) + 1;
  w = buffer(n * sizeof(WCHAR));
  if (w)
    ow_tow(a, -1, w, n);
  return w;
}

/* W copies of count package descriptions, in one buffer. */
static PSecPkgInfoW packages_w(const SecPkgInfoA *a, ULONG count) {
  SIZE_T size = count * sizeof(SecPkgInfoW);
  ULONG i;
  BYTE *buf;
  WCHAR *s;
  for (i = 0; i < count; i++)
    size += (ow_wlen_of(a[i].Name ? a[i].Name : "", -1, CP_ACP) +
             ow_wlen_of(a[i].Comment ? a[i].Comment : "", -1, CP_ACP) + 2) *
            sizeof(WCHAR);
  buf = buffer(size ? size : 1);
  if (!buf)
    return NULL;
  s = (WCHAR *)(buf + count * sizeof(SecPkgInfoW));
  for (i = 0; i < count; i++) {
    PSecPkgInfoW w = (PSecPkgInfoW)buf + i;
    int n;
    w->fCapabilities = a[i].fCapabilities;
    w->wVersion = a[i].wVersion;
    w->wRPCID = a[i].wRPCID;
    w->cbMaxToken = a[i].cbMaxToken;
    n = ow_wlen_of(a[i].Name ? a[i].Name : "", -1, CP_ACP) + 1;
    ow_tow(a[i].Name ? a[i].Name : "", -1, s, n);
    w->Name = s;
    s += n;
    n = ow_wlen_of(a[i].Comment ? a[i].Comment : "", -1, CP_ACP) + 1;
    ow_tow(a[i].Comment ? a[i].Comment : "", -1, s, n);
    w->Comment = s;
    s += n;
  }
  return (PSecPkgInfoW)buf;
}

SECURITY_STATUS SEC_ENTRY EnumerateSecurityPackagesW(PULONG count,
                                                     PSecPkgInfoW *info) {
  PSecPkgInfoA a = NULL;
  SECURITY_STATUS r;
  A(EnumerateSecurityPackagesA, SEC_E_UNSUPPORTED_FUNCTION);
  r = t->EnumerateSecurityPackagesA(count, &a);
  if (r == SEC_E_OK && info) {
    *info = packages_w(a, *count);
    if (!*info)
      r = SEC_E_INSUFFICIENT_MEMORY;
  }
  free_a(a);
  return r;
}

SECURITY_STATUS SEC_ENTRY QuerySecurityPackageInfoW(SEC_WCHAR *name,
                                                    PSecPkgInfoW *info) {
  PSecPkgInfoA a = NULL;
  LPSTR n = ow_a(name);
  SECURITY_STATUS r;
  A(QuerySecurityPackageInfoA, SEC_E_UNSUPPORTED_FUNCTION);
  r = t->QuerySecurityPackageInfoA(n, &a);
  ow_free(n);
  if (r == SEC_E_OK && info) {
    *info = packages_w(a, 1);
    if (!*info)
      r = SEC_E_INSUFFICIENT_MEMORY;
  }
  free_a(a);
  return r;
}

/* The identity that NTLM, Kerberos and Negotiate take, in W form. */
static BOOL takes_identity(LPCWSTR package) {
  return package && (!ow_wcsicmp_ascii(package, "NTLM") ||
                     !ow_wcsicmp_ascii(package, "Kerberos") ||
                     !ow_wcsicmp_ascii(package, "Negotiate"));
}

SECURITY_STATUS SEC_ENTRY AcquireCredentialsHandleW(
    SEC_WCHAR *principal, SEC_WCHAR *package, ULONG use, PVOID logon,
    PVOID auth, SEC_GET_KEY_FN getkey, PVOID arg, PCredHandle cred,
    PTimeStamp expiry) {
  LPSTR p = ow_a(principal), k = ow_a(package);
  SEC_WINNT_AUTH_IDENTITY_A id;
  SEC_WINNT_AUTH_IDENTITY_W *w = auth;
  SECURITY_STATUS r;
  BOOL conv = w && takes_identity(package) &&
              w->Flags == SEC_WINNT_AUTH_IDENTITY_UNICODE;
  PSecurityFunctionTableA t = tab();
  if (!t || !t->AcquireCredentialsHandleA) {
    ow_free(p);
    ow_free(k);
    return SEC_E_UNSUPPORTED_FUNCTION;
  }
  if (conv) {
    int n;
    id.User = (unsigned char *)ow_an(w->User, w->UserLength, &n);
    id.UserLength = n;
    id.Domain = (unsigned char *)ow_an(w->Domain, w->DomainLength, &n);
    id.DomainLength = n;
    id.Password = (unsigned char *)ow_an(w->Password, w->PasswordLength, &n);
    id.PasswordLength = n;
    id.Flags = SEC_WINNT_AUTH_IDENTITY_ANSI;
    if (!w->User)
      ow_free(id.User), id.User = NULL, id.UserLength = 0;
    if (!w->Domain)
      ow_free(id.Domain), id.Domain = NULL, id.DomainLength = 0;
    if (!w->Password)
      ow_free(id.Password), id.Password = NULL, id.PasswordLength = 0;
    auth = &id;
  }
  r = t->AcquireCredentialsHandleA(p, k, use, logon, auth, getkey, arg, cred,
                                   expiry);
  if (conv) {
    if (id.Password)
      ow_memset(id.Password, 0, id.PasswordLength);
    ow_free(id.User);
    ow_free(id.Domain);
    ow_free(id.Password);
  }
  ow_free(p);
  ow_free(k);
  return r;
}

SECURITY_STATUS SEC_ENTRY InitializeSecurityContextW(
    PCredHandle cred, PCtxtHandle ctx, SEC_WCHAR *target, ULONG req,
    ULONG r1, ULONG rep, PSecBufferDesc in, ULONG r2, PCtxtHandle out_ctx,
    PSecBufferDesc out, PULONG attrs, PTimeStamp expiry) {
  LPSTR a = ow_a(target);
  SECURITY_STATUS r;
  PSecurityFunctionTableA t = tab();
  if (!t || !t->InitializeSecurityContextA) {
    ow_free(a);
    return SEC_E_UNSUPPORTED_FUNCTION;
  }
  r = t->InitializeSecurityContextA(cred, ctx, a, req, r1, rep, in, r2,
                                    out_ctx, out, attrs, expiry);
  ow_free(a);
  return r;
}

/* Attributes that hold strings; the rest are the same in both forms. */
SECURITY_STATUS SEC_ENTRY QueryContextAttributesW(PCtxtHandle ctx,
                                                  ULONG attr, PVOID buf) {
  union {
    SecPkgContext_NamesA names;
    SecPkgContext_AuthorityA authority;
    SecPkgContext_KeyInfoA key;
    SecPkgContext_PackageInfoA package;
    SecPkgContext_NegotiationInfoA negotiation;
    SecPkgContext_NativeNamesA native;
    BYTE pad[64];
  } a;
  SECURITY_STATUS r;
  A(QueryContextAttributesA, SEC_E_UNSUPPORTED_FUNCTION);
  switch (attr) {
  case SECPKG_ATTR_NAMES:
  case SECPKG_ATTR_AUTHORITY:
  case SECPKG_ATTR_KEY_INFO:
  case SECPKG_ATTR_PACKAGE_INFO:
  case SECPKG_ATTR_NEGOTIATION_INFO:
  case SECPKG_ATTR_NATIVE_NAMES:
    break;
  default:
    return t->QueryContextAttributesA(ctx, attr, buf);
  }
  if (!buf)
    return SEC_E_INVALID_TOKEN;
  ow_memset(&a, 0, sizeof a);
  r = t->QueryContextAttributesA(ctx, attr, &a);
  if (r != SEC_E_OK)
    return r;
  switch (attr) {
  case SECPKG_ATTR_NAMES: {
    SecPkgContext_NamesW *w = buf;
    w->sUserName = string_w(a.names.sUserName);
    free_a(a.names.sUserName);
    break;
  }
  case SECPKG_ATTR_AUTHORITY: {
    SecPkgContext_AuthorityW *w = buf;
    w->sAuthorityName = string_w(a.authority.sAuthorityName);
    free_a(a.authority.sAuthorityName);
    break;
  }
  case SECPKG_ATTR_KEY_INFO: {
    SecPkgContext_KeyInfoW *w = buf;
    w->sSignatureAlgorithmName = string_w(a.key.sSignatureAlgorithmName);
    w->sEncryptAlgorithmName = string_w(a.key.sEncryptAlgorithmName);
    w->KeySize = a.key.KeySize;
    w->SignatureAlgorithm = a.key.SignatureAlgorithm;
    w->EncryptAlgorithm = a.key.EncryptAlgorithm;
    free_a(a.key.sSignatureAlgorithmName);
    free_a(a.key.sEncryptAlgorithmName);
    break;
  }
  case SECPKG_ATTR_PACKAGE_INFO: {
    SecPkgContext_PackageInfoW *w = buf;
    w->PackageInfo =
        a.package.PackageInfo ? packages_w(a.package.PackageInfo, 1) : NULL;
    free_a(a.package.PackageInfo);
    break;
  }
  case SECPKG_ATTR_NEGOTIATION_INFO: {
    SecPkgContext_NegotiationInfoW *w = buf;
    w->PackageInfo = a.negotiation.PackageInfo
                         ? packages_w(a.negotiation.PackageInfo, 1)
                         : NULL;
    w->NegotiationState = a.negotiation.NegotiationState;
    free_a(a.negotiation.PackageInfo);
    break;
  }
  case SECPKG_ATTR_NATIVE_NAMES: {
    SecPkgContext_NativeNamesW *w = buf;
    w->sClientName = string_w(a.native.sClientName);
    w->sServerName = string_w(a.native.sServerName);
    free_a(a.native.sClientName);
    free_a(a.native.sServerName);
    break;
  }
  }
  return r;
}

SECURITY_STATUS SEC_ENTRY QueryCredentialsAttributesW(PCredHandle cred,
                                                      ULONG attr,
                                                      PVOID buf) {
  SecPkgCredentials_NamesA a;
  SECURITY_STATUS r;
  A(QueryCredentialsAttributesA, SEC_E_UNSUPPORTED_FUNCTION);
  if (attr != SECPKG_CRED_ATTR_NAMES)
    return t->QueryCredentialsAttributesA(cred, attr, buf);
  if (!buf)
    return SEC_E_INVALID_TOKEN;
  a.sUserName = NULL;
  r = t->QueryCredentialsAttributesA(cred, attr, &a);
  if (r == SEC_E_OK) {
    ((SecPkgCredentials_NamesW *)buf)->sUserName = string_w(a.sUserName);
    free_a(a.sUserName);
  }
  return r;
}

/* The W table: the A one with the functions that take strings replaced,
 * and those this layer does not convert left out. */
PSecurityFunctionTableW SEC_ENTRY InitSecurityInterfaceW(void) {
  static SecurityFunctionTableW w;
  PSecurityFunctionTableA t = tab();
  SIZE_T n;
  if (!t)
    return NULL;
  if (w.dwVersion)
    return &w;
  if (t->dwVersion <= 1)
    n = offsetof(SecurityFunctionTableA, ExportSecurityContext);
  else if (t->dwVersion == 2)
    n = offsetof(SecurityFunctionTableA, QuerySecurityContextToken);
  else if (t->dwVersion == 3)
    n = offsetof(SecurityFunctionTableA, SetCredentialsAttributesA);
  else
    n = sizeof w;
  ow_memcpy(&w, t, n);
  w.EnumerateSecurityPackagesW = EnumerateSecurityPackagesW;
  w.QueryCredentialsAttributesW = QueryCredentialsAttributesW;
  w.AcquireCredentialsHandleW = AcquireCredentialsHandleW;
  w.InitializeSecurityContextW = InitializeSecurityContextW;
  w.QueryContextAttributesW = QueryContextAttributesW;
  w.FreeContextBuffer = FreeContextBuffer;
  w.QuerySecurityPackageInfoW = QuerySecurityPackageInfoW;
  w.ImportSecurityContextW = NULL;
  w.AddCredentialsW = NULL;
  w.SetContextAttributesW = NULL;
  w.SetCredentialsAttributesW = NULL;
  w.ChangeAccountPasswordW = NULL;
  w.dwVersion = t->dwVersion ? t->dwVersion : 1;
  return &w;
}
