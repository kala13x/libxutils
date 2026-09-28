/* libxutils: digest and HMAC known answers that hold on every build.
 *
 * The published vectors of FIPS 180, RFC 1321, RFC 2202 and RFC 4231 pin the
 * algorithms; the streaming cases pin the bookkeeping around them: a message
 * fed in pieces of any size, at any alignment, gives the digest of the whole.
 */

#include "test.h"
#include "sha256.h"
#include "sha1.h"
#include "md5.h"
#include "crc32.h"
#include "hmac.h"

static int sha_hex_equals(const uint8_t *pDigest, size_t nSize, const char *pHex)
{
    char sHex[129];
    if (nSize * 2 >= sizeof(sHex) || strlen(pHex) != nSize * 2) return 0;

    for (size_t i = 0; i < nSize; i++) snprintf(sHex + i * 2, 3, "%02x", pDigest[i]);
    return strcmp(sHex, pHex) == 0;
}

static const char g_sMsg896[] = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                                "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

static int XTest_sha256_vectors(void)
{
    uint8_t digest[XSHA256_DIGEST_SIZE];

    CHECK(XSHA256_Compute(digest, sizeof(digest), (const uint8_t*)g_sMsg896, strlen(g_sMsg896)) == XSTDOK,
        "The 896 bit message digests");
    CHECK(sha_hex_equals(digest, sizeof(digest), "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"),
        "It matches the FIPS 180 vector");

    /* A million times 'a', fed in uneven pieces */
    uint8_t sChunk[997];
    memset(sChunk, 'a', sizeof(sChunk));

    xsha256_t sha;
    XSHA256_Init(&sha);
    size_t nDone = 0;
    while (nDone < 1000000)
    {
        size_t nPart = XSTD_MIN(sizeof(sChunk), 1000000 - nDone);
        XSHA256_Update(&sha, sChunk, nPart);
        nDone += nPart;
    }

    XSHA256_Final(&sha, digest);
    CHECK(sha_hex_equals(digest, sizeof(digest), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
        "A million 'a' fed in pieces matches the FIPS 180 vector");

    char sSum[XSHA256_LENGTH + 1];
    CHECK(XSHA256_ComputeSum(sSum, sizeof(sSum), (const uint8_t*)"abc", 3) == XSTDOK, "The hex form computes");
    CHECK(strcmp(sSum, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0, "The hex form is lower case");
    CHECK(XSHA256_ComputeSum(sSum, XSHA256_LENGTH, (const uint8_t*)"abc", 3) == XSTDERR, "A short hex buffer is refused");
    CHECK(XSHA256_Compute(digest, XSHA256_DIGEST_SIZE - 1, (const uint8_t*)"abc", 3) == XSTDERR, "A short buffer is refused");
    CHECK(XSHA256_Compute(NULL, XSHA256_DIGEST_SIZE, (const uint8_t*)"abc", 3) == XSTDERR, "A missing buffer is refused");

    char *pSum = XSHA256_Sum((const uint8_t*)"abc", 3);
    CHECK(pSum != NULL && strcmp(pSum, sSum) == 0, "The allocating hex form agrees");
    free(pSum);

    uint8_t *pRaw = XSHA256_Encrypt((const uint8_t*)"abc", 3);
    CHECK(pRaw != NULL && sha_hex_equals(pRaw, XSHA256_DIGEST_SIZE, sSum), "The allocating raw form agrees");
    free(pRaw);
    return 0;
}

static int XTest_sha256_streaming(void)
{
    /* Every way of cutting a message into two or three updates, including
       empty ones, gives the digest of the whole message. The lengths cover
       every position of the padding relative to the 64 byte blocks. */
    uint8_t sMessage[300];
    for (size_t i = 0; i < sizeof(sMessage); i++) sMessage[i] = (uint8_t)(i * 31 + 7);

    const size_t nLengths[] = { 0, 1, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 129, 191, 192, 255, 256, 300 };
    for (size_t l = 0; l < sizeof(nLengths) / sizeof(*nLengths); l++)
    {
        size_t nLength = nLengths[l];
        uint8_t whole[XSHA256_DIGEST_SIZE];
        CHECK(XSHA256_Compute(whole, sizeof(whole), sMessage, nLength) == XSTDOK, "Digest the whole message");

        for (size_t nFirst = 0; nFirst <= nLength; nFirst++)
        {
            size_t nSecond = (nLength - nFirst) / 2;
            uint8_t parts[XSHA256_DIGEST_SIZE];

            xsha256_t sha;
            XSHA256_Init(&sha);
            XSHA256_Update(&sha, sMessage, nFirst);
            XSHA256_Update(&sha, sMessage + nFirst, 0);
            XSHA256_Update(&sha, sMessage + nFirst, nSecond);
            XSHA256_Update(&sha, sMessage + nFirst + nSecond, nLength - nFirst - nSecond);
            XSHA256_Final(&sha, parts);

            CHECK(memcmp(whole, parts, sizeof(whole)) == 0, "A message in pieces digests like the whole");
        }
    }

    /* The input is read byte by byte, so its alignment does not matter */
    uint8_t sAligned[XSHA256_DIGEST_SIZE];
    CHECK(XSHA256_Compute(sAligned, sizeof(sAligned), sMessage, 200) == XSTDOK, "Digest an aligned copy");

    uint8_t *pStorage = (uint8_t*)malloc(sizeof(sMessage) + 16);
    CHECK(pStorage != NULL, "Allocate room for unaligned copies");

    for (size_t nShift = 1; nShift < 16; nShift++)
    {
        memcpy(pStorage + nShift, sMessage, 200);
        uint8_t sShifted[XSHA256_DIGEST_SIZE];
        CHECK(XSHA256_Compute(sShifted, sizeof(sShifted), pStorage + nShift, 200) == XSTDOK, "Digest a shifted copy");
        CHECK(memcmp(sAligned, sShifted, sizeof(sAligned)) == 0, "Every alignment gives the same digest");
    }

    free(pStorage);

    /* A block handed to the compression function directly advances the state
       exactly like an update of those 64 bytes */
    xsha256_t direct, streamed;
    XSHA256_Init(&direct);
    XSHA256_Init(&streamed);
    memcpy(direct.uBlock.block, sMessage, XSHA256_BLOCK_SIZE);
    XSHA256_ProcessBlock(&direct);
    XSHA256_Update(&streamed, sMessage, XSHA256_BLOCK_SIZE);
    CHECK(memcmp(direct.uDigest.hBytes, streamed.uDigest.hBytes, sizeof(direct.uDigest.hBytes)) == 0,
        "Processing a block directly matches streaming it");
    return 0;
}

static int XTest_sha1_vectors(void)
{
    uint8_t digest[XSHA1_DIGEST_SIZE];
    CHECK(XSHA1_Compute(digest, sizeof(digest), (const uint8_t*)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56)
        == XSTDOK, "The 448 bit message digests");
    CHECK(sha_hex_equals(digest, sizeof(digest), "84983e441c3bd26ebaae4aa1f95129e5e54670f1"), "It matches the FIPS 180 vector");

    CHECK(XSHA1_Compute(digest, sizeof(digest), (const uint8_t*)g_sMsg896, strlen(g_sMsg896)) == XSTDOK,
        "The 896 bit message digests");
    CHECK(sha_hex_equals(digest, sizeof(digest), "a49b2446a02c645bf419f995b67091253a04a259"), "It matches too");

    uint8_t sChunk[1000];
    memset(sChunk, 'a', sizeof(sChunk));

    xsha1_ctx_t ctx;
    XSHA1_Init(&ctx);
    for (int i = 0; i < 1000; i++) XSHA1_Update(&ctx, sChunk, sizeof(sChunk));
    XSHA1_Final(&ctx, digest);
    CHECK(sha_hex_equals(digest, sizeof(digest), "34aa973cd4c4daa4f61eeb2bdbad27316534016f"),
        "A million 'a' matches the FIPS 180 vector");

    char sSum[XSHA1_LENGTH + 1];
    CHECK(XSHA1_ComputeSum(sSum, sizeof(sSum), (const uint8_t*)"abc", 3) == XSTDOK, "The hex form computes");
    CHECK(strcmp(sSum, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0, "The hex form matches");
    return 0;
}

static int XTest_md5_vectors(void)
{
    /* RFC 1321 appendix A.5, all seven */
    struct { const char *pInput; const char *pDigest; } vectors[] = {
        { "", "d41d8cd98f00b204e9800998ecf8427e" },
        { "a", "0cc175b9c0f1b6a831c399e269772661" },
        { "abc", "900150983cd24fb0d6963f7d28e17f72" },
        { "message digest", "f96b697d7cb7938d525a2f31aaf161d0" },
        { "abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b" },
        { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f" },
        { "12345678901234567890123456789012345678901234567890123456789012345678901234567890",
          "57edf4a22be3c955ac49da2e2107b67a" }
    };

    for (size_t i = 0; i < sizeof(vectors) / sizeof(*vectors); i++)
    {
        uint8_t digest[XMD5_DIGEST_SIZE];
        CHECK(XMD5_Compute(digest, sizeof(digest), (const uint8_t*)vectors[i].pInput, strlen(vectors[i].pInput)) == XSTDOK,
            "The vector digests");
        CHECK(sha_hex_equals(digest, sizeof(digest), vectors[i].pDigest), "It matches RFC 1321");

        char *pSum = XMD5_Sum((const uint8_t*)vectors[i].pInput, strlen(vectors[i].pInput));
        CHECK(pSum != NULL && strcmp(pSum, vectors[i].pDigest) == 0, "The hex form matches as well");
        free(pSum);
    }

    return 0;
}

static int XTest_crc32_vectors(void)
{
    /* The table form is CRC-32 without the initial and final inversion, so
       its check value is its own rather than the published 0xcbf43926 */
    CHECK(XCRC32_Compute((const uint8_t*)"123456789", 9) == 0x2dfd2d88, "The table form keeps its check value");
    CHECK(XCRC32_ComputeB((const uint8_t*)"123456789", 9) == 0xcb0b0c3f, "The bitwise form keeps its check value");
    CHECK(XCRC32_Compute((const uint8_t*)"", 0) == 0 && XCRC32_ComputeB((const uint8_t*)"", 0) == 0,
        "An empty input has a zero CRC");
    CHECK(XCRC32_Compute(NULL, 9) == 0 && XCRC32_ComputeB(NULL, 9) == 0, "A missing input has a zero CRC");

    /* One changed bit anywhere changes the CRC */
    uint8_t sData[64];
    for (size_t i = 0; i < sizeof(sData); i++) sData[i] = (uint8_t)i;
    uint32_t nBase = XCRC32_Compute(sData, sizeof(sData));

    for (size_t nBit = 0; nBit < sizeof(sData) * 8; nBit++)
    {
        sData[nBit / 8] ^= (uint8_t)(1 << (nBit % 8));
        CHECK(XCRC32_Compute(sData, sizeof(sData)) != nBase, "Every single bit error is detected");
        sData[nBit / 8] ^= (uint8_t)(1 << (nBit % 8));
    }

    return 0;
}

static int XTest_hmac_vectors(void)
{
    /* RFC 4231 cases 3, 4, 6 and 7: keys shorter than, equal to and longer
       than the block, and data longer than the block */
    uint8_t sKey3[20], sData3[50], sKey4[25], sData4[50], sKey6[131];
    memset(sKey3, 0xaa, sizeof(sKey3));
    memset(sData3, 0xdd, sizeof(sData3));
    for (size_t i = 0; i < sizeof(sKey4); i++) sKey4[i] = (uint8_t)(i + 1);
    memset(sData4, 0xcd, sizeof(sData4));
    memset(sKey6, 0xaa, sizeof(sKey6));

    const char *pData6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    const char *pData7 = "This is a test using a larger than block-size key and a larger than block-size data. "
                         "The key needs to be hashed before being used by the HMAC algorithm.";

    struct { const uint8_t *pKey; size_t nKey; const uint8_t *pData; size_t nData; const char *pMac; } vectors[] = {
        { sKey3, sizeof(sKey3), sData3, sizeof(sData3), "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe" },
        { sKey4, sizeof(sKey4), sData4, sizeof(sData4), "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b" },
        { sKey6, sizeof(sKey6), (const uint8_t*)pData6, strlen(pData6),
          "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54" },
        { sKey6, sizeof(sKey6), (const uint8_t*)pData7, strlen(pData7),
          "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2" }
    };

    for (size_t i = 0; i < sizeof(vectors) / sizeof(*vectors); i++)
    {
        uint8_t mac[XSHA256_DIGEST_SIZE];
        CHECK(XHMAC_SHA256(mac, sizeof(mac), vectors[i].pData, vectors[i].nData, vectors[i].pKey, vectors[i].nKey) == XSTDOK,
            "The HMAC computes");
        CHECK(sha_hex_equals(mac, sizeof(mac), vectors[i].pMac), "It matches RFC 4231");

        char sHex[XSHA256_LENGTH + 1];
        CHECK(XHMAC_SHA256_HEX(sHex, sizeof(sHex), vectors[i].pData, vectors[i].nData, vectors[i].pKey, vectors[i].nKey)
            == XSTDOK && strcmp(sHex, vectors[i].pMac) == 0, "The hex form matches it");

        char *pNew = XHMAC_SHA256_NEW(vectors[i].pData, vectors[i].nData, vectors[i].pKey, vectors[i].nKey);
        CHECK(pNew != NULL && strcmp(pNew, vectors[i].pMac) == 0, "The allocating form matches it");
        free(pNew);
    }

    /* RFC 2202 HMAC-MD5 cases 3, 6 and 7 */
    uint8_t sMd5Key3[16], sMd5Key6[80];
    memset(sMd5Key3, 0xaa, sizeof(sMd5Key3));
    memset(sMd5Key6, 0xaa, sizeof(sMd5Key6));

    const char *pMd5Data7 = "Test Using Larger Than Block-Size Key and Larger Than One Block-Size Data";
    struct { const uint8_t *pKey; size_t nKey; const uint8_t *pData; size_t nData; const char *pMac; } md5[] = {
        { sMd5Key3, sizeof(sMd5Key3), sData3, sizeof(sData3), "56be34521d144c88dbb8c733f0e8b3f6" },
        { sMd5Key6, sizeof(sMd5Key6), (const uint8_t*)pData6, strlen(pData6), "6b1ab7fe4bd7bf8f0b62e6ce61b9d0cd" },
        { sMd5Key6, sizeof(sMd5Key6), (const uint8_t*)pMd5Data7, strlen(pMd5Data7), "6f630fad67cda0ee1fb1f562db3aa53e" }
    };

    for (size_t i = 0; i < sizeof(md5) / sizeof(*md5); i++)
    {
        char sMac[XMD5_LENGTH + 1];
        CHECK(XHMAC_MD5(sMac, sizeof(sMac), md5[i].pData, md5[i].nData, md5[i].pKey, md5[i].nKey) == XSTDOK, "It computes");
        CHECK(strcmp(sMac, md5[i].pMac) == 0, "It matches RFC 2202");
    }

    /* Tags for a key and data that differ in one bit differ */
    uint8_t first[XSHA256_DIGEST_SIZE], second[XSHA256_DIGEST_SIZE];
    CHECK(XHMAC_SHA256(first, sizeof(first), sData3, sizeof(sData3), sKey3, sizeof(sKey3)) == XSTDOK, "Tag one");
    sKey3[19] ^= 1;
    CHECK(XHMAC_SHA256(second, sizeof(second), sData3, sizeof(sData3), sKey3, sizeof(sKey3)) == XSTDOK, "Tag two");
    CHECK(memcmp(first, second, sizeof(first)) != 0, "A different key gives a different tag");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(sha256_vectors),
    XTEST_CASE(sha256_streaming),
    XTEST_CASE(sha1_vectors),
    XTEST_CASE(md5_vectors),
    XTEST_CASE(crc32_vectors),
    XTEST_CASE(hmac_vectors)
)
