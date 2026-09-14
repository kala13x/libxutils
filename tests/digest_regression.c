/* Known-answer tests for the bundled hash implementations.
 *
 * SHA-256, SHA-1, MD5 and HMAC-SHA256 are checked against the official
 * NIST / RFC vectors: these primitives guard file-transfer integrity and
 * the E2E key schedule, so they must match the standards bit for bit.
 * CRC32 is locked to the current (implementation-specific, non-IEEE)
 * output instead, because the hash map seeding depends on it staying
 * stable rather than on it matching zlib. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include "sha256.h"
#include "sha1.h"
#include "md5.h"
#include "crc32.h"
#include "hmac.h"

#include "test.h"

static int hex_equals(const uint8_t *pDigest, size_t nLen, const char *pHex)
{
    char sHex[160];
    for (size_t i = 0; i < nLen && i * 2 + 2 < sizeof(sHex); i++) snprintf(&sHex[i * 2], 3, "%02x", pDigest[i]);
    return strcmp(sHex, pHex) == 0;
}

static int XTest_boundaries(void)
{
    uint8_t key[131], input[257], expected[32], actual[32];
    memset(key, 0xaa, sizeof(key));
    for (size_t i = 0; i < sizeof(input); i++) input[i] = (uint8_t)i;
    const size_t keyLengths[] = {0, 1, 64, 65, 131};
    for (size_t n = 0; n < sizeof(input); n++)
    {
        unsigned int length;
        CHECK(EVP_Digest(input, n, expected, &length, EVP_md5(), NULL) == 1, "MD5 reference");
        CHECK(XMD5_Compute(actual, sizeof(actual), input, n) == XSTDOK && !memcmp(actual, expected, 16),
            "MD5 padding boundaries match OpenSSL");
        for (size_t k = 0; k < sizeof(keyLengths) / sizeof(*keyLengths); k++)
        {
            CHECK(HMAC(EVP_sha256(), key, (int)keyLengths[k], input, n, expected, &length), "HMAC reference");
            CHECK(XHMAC_SHA256(actual, sizeof(actual), input, n, key, keyLengths[k]) == XSTDOK && !memcmp(actual, expected, 32),
                "HMAC-SHA256 empty/long keys match OpenSSL");
            char hex[33];
            CHECK(HMAC(EVP_md5(), key, (int)keyLengths[k], input, n, expected, &length), "HMAC MD5 reference");
            CHECK(XHMAC_MD5(hex, sizeof(hex), input, n, key, keyLengths[k]) == XSTDOK && hex_equals(expected, 16, hex),
                "HMAC-MD5 empty/long keys match OpenSSL");
        }
    }
    char badHex[65];
    size_t badLen = 123;
    CHECK(XHMAC_SHA256_HEX(badHex, sizeof(badHex), NULL, 1, key, 1) != XSTDOK && !badHex[0], "HMAC hex rejects invalid input");
    CHECK(!XHMAC_SHA256_NEW(NULL, 1, key, 1) && !XHMAC_MD5_NEW(NULL, 1, key, 1), "HMAC alloc helpers propagate failure");
    CHECK(!XHMAC_SHA256_B64(NULL, 1, key, 1, &badLen) && !badLen, "HMAC base64 propagates failure");
    CHECK(XMD5_Compute(actual, sizeof(actual), input, SIZE_MAX) != XSTDOK && !XMD5_Sum(input, SIZE_MAX) &&
        !XMD5_Encrypt(input, SIZE_MAX), "MD5 rejects size overflow");

    /* ---- SHA-256 (FIPS 180-4 / NIST CAVS vectors) ---- */
    uint8_t digest[XSHA256_DIGEST_SIZE];

    CHECK(XSHA256_Compute(digest, sizeof(digest), (const uint8_t*)"abc", 3) > 0, "sha256 compute");
    CHECK(hex_equals(digest, sizeof(digest), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
        "sha256(abc) NIST vector");

    CHECK(XSHA256_Compute(digest, sizeof(digest), (const uint8_t*)"", 0) > 0, "sha256 empty compute");
    CHECK(hex_equals(digest, sizeof(digest), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
        "sha256(empty) NIST vector");

    /* Two-block message: exercises the block-boundary path */
    CHECK(XSHA256_Compute(
              digest, sizeof(digest), (const uint8_t*)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56) > 0,
        "sha256 two-block compute");
    CHECK(hex_equals(digest, sizeof(digest), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
        "sha256(two-block) NIST vector");

    /* Streaming (Init/Update/Final) must agree with the one-shot API */
    xsha256_t sha;
    XSHA256_Init(&sha);
    XSHA256_Update(&sha, (const uint8_t*)"ab", 2);
    XSHA256_Update(&sha, (const uint8_t*)"c", 1);
    XSHA256_Final(&sha, digest);
    CHECK(hex_equals(digest, sizeof(digest), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
        "sha256 streaming matches one-shot");

    /* Hex helper used for transfer checksums */
    char sSum[XSHA256_LENGTH + 1];
    CHECK(XSHA256_ComputeSum(sSum, sizeof(sSum), (const uint8_t*)"abc", 3) > 0, "sha256 hex sum");
    CHECK(strcmp(sSum, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0, "sha256 hex sum value");

    /* ---- SHA-1 (FIPS 180-1 vector) ---- */
    uint8_t sha1Digest[XSHA1_DIGEST_SIZE];
    CHECK(XSHA1_Compute(sha1Digest, sizeof(sha1Digest), (const uint8_t*)"abc", 3) > 0, "sha1 compute");
    CHECK(hex_equals(sha1Digest, sizeof(sha1Digest), "a9993e364706816aba3e25717850c26c9cd0d89d"), "sha1(abc) vector");

    /* ---- MD5 (RFC 1321 appendix vectors) ---- */
    uint8_t md5Digest[16];
    CHECK(XMD5_Compute(md5Digest, sizeof(md5Digest), (const uint8_t*)"abc", 3) > 0, "md5 compute");
    CHECK(hex_equals(md5Digest, sizeof(md5Digest), "900150983cd24fb0d6963f7d28e17f72"), "md5(abc) vector");

    CHECK(XMD5_Compute(md5Digest, sizeof(md5Digest), (const uint8_t*)"", 0) > 0, "md5 empty compute");
    CHECK(hex_equals(md5Digest, sizeof(md5Digest), "d41d8cd98f00b204e9800998ecf8427e"), "md5(empty) vector");

    /* ---- HMAC-SHA256 (RFC 4231 test case 1 and 2) ---- */
    uint8_t mac[32];
    uint8_t key1[20];
    memset(key1, 0x0b, sizeof(key1));

    CHECK(XHMAC_SHA256(mac, sizeof(mac), (const uint8_t*)"Hi There", 8, key1, sizeof(key1)) > 0, "hmac case 1 compute");
    CHECK(hex_equals(mac, sizeof(mac), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"),
        "hmac-sha256 RFC4231 case 1");

    CHECK(XHMAC_SHA256(mac, sizeof(mac), (const uint8_t*)"what do ya want for nothing?", 28, (const uint8_t*)"Jefe", 4) > 0,
        "hmac case 2 compute");
    CHECK(hex_equals(mac, sizeof(mac), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"),
        "hmac-sha256 RFC4231 case 2");

    /* ---- CRC32: lock the current implementation-defined output ---- */
    CHECK(XCRC32_Compute((const uint8_t*)"123456789", 9) == 0x2dfd2d88U, "crc32 stable output");
    CHECK(XCRC32_ComputeB((const uint8_t*)"123456789", 9) == 0xcb0b0c3fU, "crc32b stable output");
    CHECK(XCRC32_Compute((const uint8_t*)"", 0) == XCRC32_Compute((const uint8_t*)"", 0), "crc32 deterministic on empty input");

    puts("digest_regression: OK");
    return 0;
}

XTEST_MAIN(XTEST_CASE(boundaries))
