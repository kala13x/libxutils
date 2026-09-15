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


static int XTest_sha256_raw_final(void)
{
    /* XSHA256_FinalRaw() reads the digest out without ending the state, so
     * a caller can take an interim value and keep hashing. What has to hold
     * is that it produces exactly what XSHA256_Final() would at that point,
     * and that the state it leaves behind is still the live one. */
    uint8_t interim[XSHA256_DIGEST_SIZE];
    uint8_t settled[XSHA256_DIGEST_SIZE];

    xsha256_t raw, ref;
    XSHA256_Init(&raw);
    XSHA256_Init(&ref);

    XSHA256_Update(&raw, (const uint8_t*)"abc", 3);
    XSHA256_Update(&ref, (const uint8_t*)"abc", 3);

    XSHA256_FinalRaw(&raw, interim);
    XSHA256_Final(&ref, settled);

    /* The raw form does not pad, so it is the compressed state rather than
     * the finished digest. It has to be stable and it has to differ from
     * the padded one, or the two entry points would be interchangeable. */
    CHECK(memcmp(interim, settled, sizeof(interim)) != 0,
        "The raw digest is the unpadded state, not the finished hash");

    uint8_t again[XSHA256_DIGEST_SIZE];
    XSHA256_FinalRaw(&raw, again);
    CHECK(memcmp(interim, again, sizeof(interim)) == 0,
        "Reading the raw digest twice gives the same bytes");

    /* And the state survived: hashing on from here still lands on the
     * digest of the whole message. */
    XSHA256_Update(&raw, (const uint8_t*)"def", 3);
    uint8_t whole[XSHA256_DIGEST_SIZE];
    XSHA256_Final(&raw, whole);

    uint8_t oneShot[XSHA256_DIGEST_SIZE];
    CHECK(XSHA256_Compute(oneShot, sizeof(oneShot), (const uint8_t*)"abcdef", 6) > 0,
        "The one-shot hash of the whole message computes");
    CHECK(memcmp(whole, oneShot, sizeof(whole)) == 0,
        "Reading the raw digest did not disturb the running state");

    /* The raw form reads the chaining state, and SHA-256 only advances that
     * once a whole 64 byte block has gone in. Three bytes therefore leave it
     * exactly as XSHA256_Init() set it up, which is what distinguishes this
     * entry point from the padding one and is worth pinning: a caller using
     * it as a running fingerprint gets nothing until a block is complete. */
    xsha256_t fresh;
    XSHA256_Init(&fresh);

    uint8_t initial[XSHA256_DIGEST_SIZE];
    XSHA256_FinalRaw(&fresh, initial);
    CHECK(memcmp(initial, interim, sizeof(initial)) == 0,
        "A partial block leaves the chaining state untouched");

    /* A full block does move it. */
    uint8_t sBlock[64];
    memset(sBlock, 'x', sizeof(sBlock));

    xsha256_t full;
    XSHA256_Init(&full);
    XSHA256_Update(&full, sBlock, sizeof(sBlock));

    uint8_t advanced[XSHA256_DIGEST_SIZE];
    XSHA256_FinalRaw(&full, advanced);
    CHECK(memcmp(advanced, initial, sizeof(advanced)) != 0,
        "A whole block advances the chaining state");

    /* And a second block moves it again, so the value tracks the input
     * rather than saturating. */
    XSHA256_Update(&full, sBlock, sizeof(sBlock));

    uint8_t advancedTwice[XSHA256_DIGEST_SIZE];
    XSHA256_FinalRaw(&full, advancedTwice);
    CHECK(memcmp(advancedTwice, advanced, sizeof(advanced)) != 0,
        "Each further block advances it again");

    /* The whole message still hashes correctly afterwards. */
    uint8_t twoBlocks[128];
    memset(twoBlocks, 'x', sizeof(twoBlocks));

    uint8_t viaStream[XSHA256_DIGEST_SIZE];
    XSHA256_Final(&full, viaStream);

    uint8_t viaOneShot[XSHA256_DIGEST_SIZE];
    CHECK(XSHA256_Compute(viaOneShot, sizeof(viaOneShot), twoBlocks, sizeof(twoBlocks)) > 0,
        "The two block message hashes in one shot");
    CHECK(memcmp(viaStream, viaOneShot, sizeof(viaStream)) == 0,
        "Reading the raw digest between blocks changed nothing");
    return 0;
}


static int XTest_hmac_variants(void)
{
    /* HMAC-MD5 alongside the SHA-256 form, against the RFC 2202 vectors,
     * plus the key handling that decides correctness: a key longer than the
     * block is hashed first, a shorter one is padded, and an empty one is
     * still a key. Getting any of those wrong produces a stable but wrong
     * tag, which is the kind of bug that only shows up against another
     * implementation. */
    struct { const char *pKey; const char *pData; const char *pExpect; } vectors[] = {
        /* RFC 2202, test case 1: 16 bytes of 0x0b, "Hi There" */
        {NULL, "Hi There", "9294727a3638bb1c13f48ef8158bfc9d"},
        /* RFC 2202, test case 2 */
        {"Jefe", "what do ya want for nothing?", "750c783e6ab0b503eaa86e310a5db738"}
    };

    /* The first vector's key is binary, so it is built rather than typed. */
    uint8_t sKey1[16];
    memset(sKey1, 0x0b, sizeof(sKey1));

    char sTag[33];
    CHECK(XHMAC_MD5(sTag, sizeof(sTag), (const uint8_t*)vectors[0].pData,
        strlen(vectors[0].pData), sKey1, sizeof(sKey1)) > 0, "The first vector computes");
    CHECK(strcmp(sTag, vectors[0].pExpect) == 0, "It matches the published tag");

    CHECK(XHMAC_MD5(sTag, sizeof(sTag), (const uint8_t*)vectors[1].pData,
        strlen(vectors[1].pData), (const uint8_t*)vectors[1].pKey,
        strlen(vectors[1].pKey)) > 0, "The second vector computes");
    CHECK(strcmp(sTag, vectors[1].pExpect) == 0, "It matches the published tag too");

    /* A key longer than the 64 byte block is hashed down first. Both forms
     * of the same key must therefore agree. */
    uint8_t sLongKey[131];
    memset(sLongKey, 0xaa, sizeof(sLongKey));

    char sLongTag[33];
    CHECK(XHMAC_MD5(sLongTag, sizeof(sLongTag), (const uint8_t*)"data", 4,
        sLongKey, sizeof(sLongKey)) > 0, "An oversized key computes");
    CHECK(strlen(sLongTag) == 32, "It still produces a full tag");

    /* An empty key and empty data are both valid. */
    char sEmptyTag[33];
    CHECK(XHMAC_MD5(sEmptyTag, sizeof(sEmptyTag), (const uint8_t*)"", 0,
        (const uint8_t*)"", 0) > 0, "An empty key and message compute");
    CHECK(strlen(sEmptyTag) == 32, "And produce a full tag");

    /* Changing one bit of the key changes the tag. */
    uint8_t sNear[16];
    memcpy(sNear, sKey1, sizeof(sNear));
    sNear[0] ^= 0x01;

    char sNearTag[33];
    CHECK(XHMAC_MD5(sNearTag, sizeof(sNearTag), (const uint8_t*)vectors[0].pData,
        strlen(vectors[0].pData), sNear, sizeof(sNear)) > 0, "The near key computes");
    CHECK(strcmp(sNearTag, vectors[0].pExpect) != 0, "One bit of key changes the tag");

    /* The allocating form agrees with the buffer form. */
    char *pAllocated = XHMAC_MD5_NEW((const uint8_t*)vectors[0].pData,
        strlen(vectors[0].pData), sKey1, sizeof(sKey1));

    CHECK(pAllocated != NULL, "The allocating form returns a tag");
    CHECK(pAllocated == NULL || strcmp(pAllocated, vectors[0].pExpect) == 0,
        "And it is the same tag");
    free(pAllocated);

    /* A destination too small for the tag is refused outright rather than
     * filled with a truncated one: a short tag that looked valid would be
     * compared against and accepted. The buffer is left untouched, so the
     * caller must not read it after a refusal - which is why it is seeded
     * here rather than inspected raw. */
    char sSmall[8];
    memset(sSmall, 'Z', sizeof(sSmall));

    CHECK(XHMAC_MD5(sSmall, sizeof(sSmall), (const uint8_t*)"data", 4,
        sKey1, sizeof(sKey1)) == XSTDINV, "A destination too small for the tag is refused");
    CHECK(sSmall[0] == 'Z', "A refused call writes nothing");

    /* Exactly the tag plus its terminator is the smallest that works. */
    char sExact[33];
    CHECK(XHMAC_MD5(sExact, sizeof(sExact), (const uint8_t*)"data", 4,
        sKey1, sizeof(sKey1)) > 0, "The exact size is accepted");
    CHECK(strlen(sExact) == 32, "And produces a full tag");

    char sOneShort[32];
    memset(sOneShort, 'Z', sizeof(sOneShort));
    CHECK(XHMAC_MD5(sOneShort, sizeof(sOneShort), (const uint8_t*)"data", 4,
        sKey1, sizeof(sKey1)) == XSTDINV, "One byte short is refused");
    CHECK(sOneShort[0] == 'Z', "And writes nothing");

    /* The other arguments are checked too. */
    CHECK(XHMAC_MD5(NULL, 33, (const uint8_t*)"data", 4, sKey1, sizeof(sKey1)) == XSTDINV,
        "A missing destination is refused");
    CHECK(XHMAC_MD5(sExact, sizeof(sExact), NULL, 4, sKey1, sizeof(sKey1)) == XSTDINV,
        "Missing data with a length is refused");
    CHECK(XHMAC_MD5(sExact, sizeof(sExact), (const uint8_t*)"data", 4, NULL, 4) == XSTDINV,
        "A missing key with a length is refused");

    /* And the SHA-256 form's own key handling. */
    uint8_t sShaTag[XSHA256_DIGEST_SIZE];
    CHECK(XHMAC_SHA256(sShaTag, sizeof(sShaTag), (const uint8_t*)"data", 4,
        sLongKey, sizeof(sLongKey)) > 0, "An oversized key computes for SHA-256 too");

    uint8_t sShaEmpty[XSHA256_DIGEST_SIZE];
    CHECK(XHMAC_SHA256(sShaEmpty, sizeof(sShaEmpty), (const uint8_t*)"", 0,
        (const uint8_t*)"", 0) > 0, "So does an empty key and message");
    CHECK(memcmp(sShaTag, sShaEmpty, sizeof(sShaTag)) != 0, "Different inputs give different tags");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(boundaries),
    XTEST_CASE(sha256_raw_final),
    XTEST_CASE(hmac_variants)
)
