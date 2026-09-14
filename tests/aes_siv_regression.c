#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "xstd.h"
#include "aes.h"

#include "test.h"

#define SIV_TAG_SIZE 16

typedef struct siv_case_ {
    const char *pCipherName;
    size_t nHalfKeyBits;
} siv_case_t;

static void fill_range(uint8_t *pData, size_t nLen, uint8_t nStart)
{
    for (size_t i = 0; i < nLen; i++) pData[i] = (uint8_t)(nStart + i);
}

/* pNonce optional: when non-NULL it is fed as the single associated-data string,
   matching XAES_MODE_SIV_NONCE. When NULL, this is plain deterministic SIV. */
static uint8_t *openssl_siv_encrypt(const char *pCipherName, const uint8_t *pKey, const uint8_t *pNonce, size_t nNonceLen,
    const uint8_t *pPlain, size_t nPlainLen, size_t *pOutLen)
{
    if (pCipherName == NULL || pKey == NULL || pPlain == NULL || pOutLen == NULL || nPlainLen > INT_MAX) return NULL;

    EVP_CIPHER *pCipher = EVP_CIPHER_fetch(NULL, pCipherName, NULL);
    EVP_CIPHER_CTX *pCtx = EVP_CIPHER_CTX_new();
    uint8_t *pOutput = malloc(SIV_TAG_SIZE + nPlainLen);
    int nWritten = 0;
    int nFinal = 0;
    int nAad = 0;

    if (pCipher == NULL || pCtx == NULL || pOutput == NULL || EVP_EncryptInit_ex2(pCtx, pCipher, pKey, NULL, NULL) != 1 ||
        (pNonce != NULL && EVP_EncryptUpdate(pCtx, NULL, &nAad, pNonce, (int)nNonceLen) != 1) ||
        EVP_EncryptUpdate(pCtx, pOutput + SIV_TAG_SIZE, &nWritten, pPlain, (int)nPlainLen) != 1 ||
        EVP_EncryptFinal_ex(pCtx, pOutput + SIV_TAG_SIZE + nWritten, &nFinal) != 1 || (size_t)(nWritten + nFinal) != nPlainLen ||
        EVP_CIPHER_CTX_ctrl(pCtx, EVP_CTRL_AEAD_GET_TAG, SIV_TAG_SIZE, pOutput) != 1)
    {
        free(pOutput);
        pOutput = NULL;
    }
    else
    {
        *pOutLen = SIV_TAG_SIZE + nPlainLen;
    }

    EVP_CIPHER_CTX_free(pCtx);
    EVP_CIPHER_free(pCipher);
    return pOutput;
}

static uint8_t *openssl_siv_decrypt(const char *pCipherName, const uint8_t *pKey, const uint8_t *pNonce, size_t nNonceLen,
    const uint8_t *pInput, size_t nInputLen, size_t *pOutLen)
{
    if (pCipherName == NULL || pKey == NULL || pInput == NULL || pOutLen == NULL || nInputLen <= SIV_TAG_SIZE ||
        nInputLen - SIV_TAG_SIZE > INT_MAX)
        return NULL;

    size_t nCipherLen = nInputLen - SIV_TAG_SIZE;
    EVP_CIPHER *pCipher = EVP_CIPHER_fetch(NULL, pCipherName, NULL);
    EVP_CIPHER_CTX *pCtx = EVP_CIPHER_CTX_new();
    uint8_t *pOutput = malloc(nCipherLen);
    int nWritten = 0;
    int nFinal = 0;
    int nAad = 0;

    if (pCipher == NULL || pCtx == NULL || pOutput == NULL || EVP_DecryptInit_ex2(pCtx, pCipher, pKey, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(pCtx, EVP_CTRL_AEAD_SET_TAG, SIV_TAG_SIZE, (void*)pInput) != 1 ||
        (pNonce != NULL && EVP_DecryptUpdate(pCtx, NULL, &nAad, pNonce, (int)nNonceLen) != 1) ||
        EVP_DecryptUpdate(pCtx, pOutput, &nWritten, pInput + SIV_TAG_SIZE, (int)nCipherLen) != 1 ||
        EVP_DecryptFinal_ex(pCtx, pOutput + nWritten, &nFinal) != 1 || (size_t)(nWritten + nFinal) != nCipherLen)
    {
        free(pOutput);
        pOutput = NULL;
    }
    else
    {
        *pOutLen = nCipherLen;
    }

    EVP_CIPHER_CTX_free(pCtx);
    EVP_CIPHER_free(pCipher);
    return pOutput;
}

/* Deterministic SIV (XAES_MODE_SIV, no associated data) must stay byte-identical
   to OpenSSL with no AAD. This guards the legacy mode other libxutils users rely
   on against accidental drift. */
static int run_case_siv(const siv_case_t *pCase, size_t nPlainLen)
{
    size_t nHalfKeyLen = pCase->nHalfKeyBits / 8;
    size_t nFullKeyLen = nHalfKeyLen * 2;
    uint8_t fullKey[64];
    uint8_t plaintext[65];

    fill_range(fullKey, nFullKeyLen, (uint8_t)(0x10 + nPlainLen));
    fill_range(plaintext, nPlainLen, (uint8_t)(0x80 + nHalfKeyLen));

    xaes_key_t key;
    xaes_t ctx;
    XAES_InitSIVKey(&key, fullKey, fullKey + nHalfKeyLen, pCase->nHalfKeyBits);
    CHECK(XAES_Init(&ctx, &key, XAES_MODE_SIV) == XSTDOK, "initialize bundled AES-SIV");

    size_t nBundledLen = nPlainLen;
    uint8_t *pBundled = XAES_Encrypt(&ctx, plaintext, &nBundledLen);
    CHECK(pBundled != NULL, "bundled AES-SIV encrypt");

    size_t nOpenSSLLen = 0;
    uint8_t *pOpenSSL = openssl_siv_encrypt(pCase->pCipherName, fullKey, NULL, 0, plaintext, nPlainLen, &nOpenSSLLen);
    CHECK(pOpenSSL != NULL, "OpenSSL AES-SIV encrypt");
    CHECK(nBundledLen == nOpenSSLLen && memcmp(pBundled, pOpenSSL, nBundledLen) == 0, "bundled AES-SIV differs from OpenSSL");

    size_t nBundledPlainLen = nOpenSSLLen;
    uint8_t *pBundledPlain = XAES_Decrypt(&ctx, pOpenSSL, &nBundledPlainLen);
    CHECK(pBundledPlain != NULL && nBundledPlainLen == nPlainLen && memcmp(pBundledPlain, plaintext, nPlainLen) == 0,
        "bundled AES-SIV cannot decrypt OpenSSL output");

    size_t nOpenSSLPlainLen = 0;
    uint8_t *pOpenSSLPlain = openssl_siv_decrypt(pCase->pCipherName, fullKey, NULL, 0, pBundled, nBundledLen, &nOpenSSLPlainLen);
    CHECK(pOpenSSLPlain != NULL && nOpenSSLPlainLen == nPlainLen && memcmp(pOpenSSLPlain, plaintext, nPlainLen) == 0,
        "OpenSSL cannot decrypt bundled AES-SIV output");

    pBundled[0] ^= 0x01;
    size_t nTamperedLen = nBundledLen;
    CHECK(XAES_Decrypt(&ctx, pBundled, &nTamperedLen) == NULL, "bundled AES-SIV accepted a tampered tag");

    free(pOpenSSLPlain);
    free(pBundledPlain);
    free(pOpenSSL);
    free(pBundled);
    return 0;
}

/* Nonce-based SIV (XAES_MODE_SIV_NONCE) is what DirectGate ships. For a fixed nonce
   the bundled S2V-with-associated-data must match OpenSSL fed the same nonce as
   AAD, byte-for-byte, in both directions. */
static int run_case_sivn(const siv_case_t *pCase, size_t nPlainLen)
{
    size_t nHalfKeyLen = pCase->nHalfKeyBits / 8;
    size_t nFullKeyLen = nHalfKeyLen * 2;
    uint8_t fullKey[64];
    uint8_t plaintext[65];
    uint8_t nonce[16];

    fill_range(fullKey, nFullKeyLen, (uint8_t)(0x10 + nPlainLen));
    fill_range(plaintext, nPlainLen, (uint8_t)(0x80 + nHalfKeyLen));
    fill_range(nonce, sizeof(nonce), (uint8_t)(0x40 + nPlainLen));

    xaes_key_t key;
    xaes_t ctx;
    XAES_InitSIVKey(&key, fullKey, fullKey + nHalfKeyLen, pCase->nHalfKeyBits);
    CHECK(XAES_Init(&ctx, &key, XAES_MODE_SIV_NONCE) == XSTDOK, "initialize bundled AES-SIV (nonce)");
    XAES_SetSIVNonce(&ctx, nonce, sizeof(nonce));

    size_t nBundledLen = nPlainLen;
    uint8_t *pBundled = XAES_Encrypt(&ctx, plaintext, &nBundledLen);
    CHECK(pBundled != NULL, "bundled AES-SIVN encrypt");

    size_t nOpenSSLLen = 0;
    uint8_t *pOpenSSL =
        openssl_siv_encrypt(pCase->pCipherName, fullKey, nonce, sizeof(nonce), plaintext, nPlainLen, &nOpenSSLLen);
    CHECK(pOpenSSL != NULL, "OpenSSL AES-SIV encrypt (AAD)");
    CHECK(
        nBundledLen == nOpenSSLLen && memcmp(pBundled, pOpenSSL, nBundledLen) == 0, "bundled AES-SIVN differs from OpenSSL+AAD");

    /* Cross-decrypt each backend's output with the other. */
    size_t nBundledPlainLen = nOpenSSLLen;
    uint8_t *pBundledPlain = XAES_Decrypt(&ctx, pOpenSSL, &nBundledPlainLen);
    CHECK(pBundledPlain != NULL && nBundledPlainLen == nPlainLen && memcmp(pBundledPlain, plaintext, nPlainLen) == 0,
        "bundled AES-SIVN cannot decrypt OpenSSL+AAD output");

    size_t nOpenSSLPlainLen = 0;
    uint8_t *pOpenSSLPlain =
        openssl_siv_decrypt(pCase->pCipherName, fullKey, nonce, sizeof(nonce), pBundled, nBundledLen, &nOpenSSLPlainLen);
    CHECK(pOpenSSLPlain != NULL && nOpenSSLPlainLen == nPlainLen && memcmp(pOpenSSLPlain, plaintext, nPlainLen) == 0,
        "OpenSSL+AAD cannot decrypt bundled AES-SIVN output");

    /* A different nonce must change the synthetic IV (non-determinism). */
    uint8_t nonce2[16];
    fill_range(nonce2, sizeof(nonce2), (uint8_t)(0x41 + nPlainLen));
    XAES_SetSIVNonce(&ctx, nonce2, sizeof(nonce2));
    size_t nBundled2Len = nPlainLen;
    uint8_t *pBundled2 = XAES_Encrypt(&ctx, plaintext, &nBundled2Len);
    CHECK(pBundled2 != NULL, "bundled AES-SIVN encrypt (nonce2)");
    CHECK(nBundled2Len == nBundledLen && memcmp(pBundled2, pBundled, SIV_TAG_SIZE) != 0,
        "AES-SIVN produced identical tag for different nonces");

    /* Tamper detection. */
    XAES_SetSIVNonce(&ctx, nonce, sizeof(nonce));
    pBundled[0] ^= 0x01;
    size_t nTamperedLen = nBundledLen;
    CHECK(XAES_Decrypt(&ctx, pBundled, &nTamperedLen) == NULL, "bundled AES-SIVN accepted a tampered tag");

    free(pBundled2);
    free(pOpenSSLPlain);
    free(pBundledPlain);
    free(pOpenSSL);
    free(pBundled);
    return 0;
}

static int run_case(const siv_case_t *pCase, size_t nPlainLen)
{
    if (run_case_siv(pCase, nPlainLen) != 0) return 1;
    if (run_case_sivn(pCase, nPlainLen) != 0) return 1;
    return 0;
}

static int XTest_openssl_vectors(void)
{
    /* Covers both the legacy deterministic SIV (no AD) and the nonce-based SIVN
       that DirectGate ships. These lengths exercise both S2V final-block
       branches and CTR spanning. */
    const siv_case_t cases[] = {{"AES-128-SIV", 128}, {"AES-192-SIV", 192}, {"AES-256-SIV", 256}};
    const size_t lengths[] = {1, 15, 16, 17, 31, 32, 65};

    EVP_CIPHER *pProbe = EVP_CIPHER_fetch(NULL, "AES-256-SIV", NULL);
    if (pProbe == NULL)
    {
        puts("aes_siv_regression: SKIP (AES-SIV provider unavailable)");
        return 77;
    }
    EVP_CIPHER_free(pProbe);

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        for (size_t j = 0; j < sizeof(lengths) / sizeof(lengths[0]); j++)
            CHECK(run_case(&cases[i], lengths[j]) == 0, "AES-SIV differential case");
    }

    puts("aes_siv_regression: OK");
    return 0;
}

XTEST_MAIN(XTEST_CASE(openssl_vectors))
