#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base64.h"

#include "test.h"

static int check_roundtrip(const uint8_t *pData, size_t nDataLen, const char *pLabel)
{
    size_t nEncLen = nDataLen;
    char *pEncoded = XBase64_Encrypt(pData, &nEncLen);
    CHECK(pEncoded != NULL, "base64 encode failed");

    size_t nDecLen = nEncLen;
    char *pDecoded = XBase64_Decrypt((const uint8_t*)pEncoded, &nDecLen);
    CHECK(pDecoded != NULL, "base64 decode failed");
    if (nDecLen != nDataLen)
    {
        fprintf(
            stderr, "base64_regression: %s length mismatch: got(%zu), want(%zu), enc(%s)\n", pLabel, nDecLen, nDataLen, pEncoded);
        return 1;
    }
    if (memcmp(pDecoded, pData, nDataLen) != 0)
    {
        fprintf(stderr, "base64_regression: %s bytes mismatch, enc(%s)\n", pLabel, pEncoded);
        return 1;
    }

    free(pDecoded);
    free(pEncoded);
    return 0;
}

static int check_url_roundtrip(const uint8_t *pData, size_t nDataLen, const char *pLabel)
{
    size_t nEncLen = nDataLen;
    char *pEncoded = XBase64_UrlEncrypt(pData, &nEncLen);
    CHECK(pEncoded != NULL, "base64url encode failed");

    size_t nDecLen = nEncLen;
    char *pDecoded = XBase64_UrlDecrypt((const uint8_t*)pEncoded, &nDecLen);
    CHECK(pDecoded != NULL, "base64url decode failed");
    if (nDecLen != nDataLen)
    {
        fprintf(
            stderr, "base64_regression: %s length mismatch: got(%zu), want(%zu), enc(%s)\n", pLabel, nDecLen, nDataLen, pEncoded);
        return 1;
    }
    if (memcmp(pDecoded, pData, nDataLen) != 0)
    {
        fprintf(stderr, "base64_regression: %s bytes mismatch, enc(%s)\n", pLabel, pEncoded);
        return 1;
    }

    free(pDecoded);
    free(pEncoded);
    return 0;
}

static int check_vector(const char *pPlain, const char *pExpected)
{
    size_t nLength = strlen(pPlain);
    char *pEncoded = XBase64_Encrypt((const uint8_t*)pPlain, &nLength);
    CHECK(pEncoded != NULL, "vector encode");
    CHECK(strcmp(pEncoded, pExpected) == 0, "vector encoded bytes");
    CHECK(nLength == strlen(pExpected), "vector encoded length");

    char *pDecoded = XBase64_Decrypt((const uint8_t*)pEncoded, &nLength);
    CHECK(pDecoded != NULL, "vector decode");
    CHECK(nLength == strlen(pPlain) && memcmp(pDecoded, pPlain, nLength) == 0, "vector decoded bytes");
    free(pDecoded);
    free(pEncoded);
    return 0;
}

static int reject_decode(const char *pEncoded)
{
    size_t nLength = strlen(pEncoded);
    char *pDecoded = XBase64_Decrypt((const uint8_t*)pEncoded, &nLength);
    if (pDecoded != NULL) free(pDecoded);
    return pDecoded == NULL;
}

static int XTest_boundaries(void)
{
    uint8_t key[32];
    uint8_t sig[64];

    for (size_t i = 0; i < sizeof(key); i++) key[i] = (uint8_t)(i + 1);
    key[sizeof(key) - 1] = 0;

    for (size_t i = 0; i < sizeof(sig); i++) sig[i] = (uint8_t)(0xa0 + i);
    sig[sizeof(sig) - 1] = 0;

    CHECK(check_roundtrip(key, sizeof(key), "32-byte trailing-zero roundtrip") == 0, "32-byte trailing-zero roundtrip");
    CHECK(check_roundtrip(sig, sizeof(sig), "64-byte trailing-zero roundtrip") == 0, "64-byte trailing-zero roundtrip");
    CHECK(check_url_roundtrip(sig, sizeof(sig), "url trailing-zero roundtrip") == 0, "url trailing-zero roundtrip");

    CHECK(check_vector("f", "Zg==") == 0, "RFC4648 f");
    CHECK(check_vector("fo", "Zm8=") == 0, "RFC4648 fo");
    CHECK(check_vector("foo", "Zm9v") == 0, "RFC4648 foo");
    CHECK(check_vector("foob", "Zm9vYg==") == 0, "RFC4648 foob");
    CHECK(check_vector("fooba", "Zm9vYmE=") == 0, "RFC4648 fooba");
    CHECK(check_vector("foobar", "Zm9vYmFy") == 0, "RFC4648 foobar");

    size_t nZero = 0;
    CHECK(XBase64_Encrypt((const uint8_t*)"x", &nZero) == NULL, "reject zero encode");
    CHECK(XBase64_Decrypt((const uint8_t*)"x", &nZero) == NULL, "reject zero decode");
    CHECK(reject_decode("A"), "reject remainder one");
    CHECK(reject_decode("===="), "reject excessive padding");
    CHECK(reject_decode("Z==="), "reject three padding bytes");
    CHECK(reject_decode("Zg="), "reject padded non-multiple of four");
    CHECK(reject_decode("Z=g="), "reject interior padding");
    CHECK(reject_decode("Zm$v"), "reject invalid alphabet");
    CHECK(reject_decode("Zm9v\n"), "reject whitespace");

    size_t nUrlLen = 3;
    const uint8_t sUrlBytes[] = {0xfb, 0xff, 0xef};
    char *pUrl = XBase64_UrlEncrypt(sUrlBytes, &nUrlLen);
    CHECK(pUrl != NULL && strcmp(pUrl, "-__v") == 0 && nUrlLen == 4, "base64url alphabet");
    free(pUrl);

    printf("base64_regression: OK\n");
    return 0;
}

XTEST_MAIN(XTEST_CASE(boundaries))
