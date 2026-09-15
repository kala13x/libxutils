/* libxutils tests: a throwaway TLS identity on disk.
 *
 * A P-256 self-signed certificate for localhost, its key, and the same pair
 * bundled as PKCS#12, all written into a private temporary directory. The
 * curve is chosen for speed: RSA generation would dominate the runtime of
 * every case that needs an identity.
 *
 * Nothing here reaches outside the machine and nothing depends on a system
 * trust store, so a client verifies by trusting this certificate directly.
 */

#ifndef XUTILS_TEST_TLS_FIXTURE_H
#define XUTILS_TEST_TLS_FIXTURE_H

#include "xstd.h"
#include <unistd.h>

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pkcs12.h>
#include <openssl/evp.h>

typedef struct {
    char sRoot[64];
    char sCert[128];
    char sKey[128];
    char sP12[128];
    int nCreated;
} tls_fixture_t;

/* Writes a self-signed localhost certificate and its key into a private
 * directory, plus the same pair bundled as PKCS#12. */
static int tls_fixture_begin(tls_fixture_t *pFixture)
{
    memset(pFixture, 0, sizeof(*pFixture));
    snprintf(pFixture->sRoot, sizeof(pFixture->sRoot), "/tmp/xutils-tls-XXXXXX");
    if (mkdtemp(pFixture->sRoot) == NULL) return XSTDERR;
    pFixture->nCreated = 1;

    snprintf(pFixture->sCert, sizeof(pFixture->sCert), "%s/cert.pem", pFixture->sRoot);
    snprintf(pFixture->sKey, sizeof(pFixture->sKey), "%s/key.pem", pFixture->sRoot);
    snprintf(pFixture->sP12, sizeof(pFixture->sP12), "%s/bundle.p12", pFixture->sRoot);

    /* A P-256 key keeps the fixture fast; RSA generation dominates otherwise. */
    EVP_PKEY *pKey = NULL;
    EVP_PKEY_CTX *pKeyCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (pKeyCtx == NULL) return XSTDERR;

    if (EVP_PKEY_keygen_init(pKeyCtx) != 1 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pKeyCtx, NID_X9_62_prime256v1) != 1 ||
        EVP_PKEY_keygen(pKeyCtx, &pKey) != 1)
    {
        EVP_PKEY_CTX_free(pKeyCtx);
        return XSTDERR;
    }
    EVP_PKEY_CTX_free(pKeyCtx);

    X509 *pCert = X509_new();
    if (pCert == NULL) { EVP_PKEY_free(pKey); return XSTDERR; }

    X509_set_version(pCert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(pCert), 1);
    X509_gmtime_adj(X509_getm_notBefore(pCert), -3600);
    X509_gmtime_adj(X509_getm_notAfter(pCert), 3600);
    X509_set_pubkey(pCert, pKey);

    X509_NAME *pName = X509_get_subject_name(pCert);
    X509_NAME_add_entry_by_txt(pName, "CN", MBSTRING_ASC, (const unsigned char*)"localhost", -1, -1, 0);
    X509_set_issuer_name(pCert, pName);

    /* A subject alternative name, so hostname verification has something
     * to match rather than falling back on the common name. */
    X509_EXTENSION *pExt = X509V3_EXT_conf_nid(NULL, NULL, NID_subject_alt_name,
        "DNS:localhost,IP:127.0.0.1");
    if (pExt != NULL) { X509_add_ext(pCert, pExt, -1); X509_EXTENSION_free(pExt); }

    if (X509_sign(pCert, pKey, EVP_sha256()) <= 0)
    {
        X509_free(pCert);
        EVP_PKEY_free(pKey);
        return XSTDERR;
    }

    int nStatus = XSTDOK;
    FILE *pFile = fopen(pFixture->sCert, "wb");
    if (pFile == NULL || PEM_write_X509(pFile, pCert) != 1) nStatus = XSTDERR;
    if (pFile != NULL) fclose(pFile);

    pFile = fopen(pFixture->sKey, "wb");
    if (pFile == NULL || PEM_write_PrivateKey(pFile, pKey, NULL, NULL, 0, NULL, NULL) != 1) nStatus = XSTDERR;
    if (pFile != NULL) fclose(pFile);

    /* The same pair as a PKCS#12 bundle, which the library can also load. */
    PKCS12 *pBundle = PKCS12_create("regression", "xutils", pKey, pCert, NULL, 0, 0, 0, 0, 0);
    if (pBundle != NULL)
    {
        pFile = fopen(pFixture->sP12, "wb");
        if (pFile != NULL) { i2d_PKCS12_fp(pFile, pBundle); fclose(pFile); }
        PKCS12_free(pBundle);
    }

    X509_free(pCert);
    EVP_PKEY_free(pKey);
    return nStatus;
}

static void tls_fixture_end(tls_fixture_t *pFixture)
{
    if (!pFixture->nCreated) return;
    unlink(pFixture->sCert);
    unlink(pFixture->sKey);
    unlink(pFixture->sP12);
    rmdir(pFixture->sRoot);
    pFixture->nCreated = 0;
}


#endif /* XUTILS_TEST_TLS_FIXTURE_H */
