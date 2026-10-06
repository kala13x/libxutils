/* libxutils: binary-safe utility encodings and dispatcher consistency. */
#include "test.h"
#include "crypt.h"
#include "rsa.h"
#include <limits.h>

static int XTest_hex(void)
{
    const uint8_t data[] = {0, 1, 0x1a, 0xff, 0x80};
    for (int nLower = 0; nLower < 2; nLower++)
    {
        size_t nLength = sizeof(data);
        uint8_t *pHex = XCrypt_HEX(data, &nLength, NULL, 0, nLower);
        CHECK(pHex && nLength == 10, "Hex encoding doubles the byte length");
        CHECK(strcmp((char*)pHex, nLower ? "00011aff80" : "00011AFF80") == 0, "Hex alphabet matches requested case");
        uint8_t *pPlain = XDecrypt_HEX(pHex, &nLength, nLower);
        CHECK(pPlain && nLength == sizeof(data) && memcmp(pPlain, data, nLength) == 0, "Hex decoding retains NUL and high bytes");
        free(pPlain);
        free(pHex);
    }
    return 0;
}

static int XTest_transforms(void)
{
    const uint8_t data[] = {0, 1, 2, 0, 0xff}, key[] = {0xa1, 0x57};
    uint8_t *pEncoded = XCrypt_XOR(data, sizeof(data), key, sizeof(key));
    CHECK(pEncoded != NULL, "XOR accepts binary input");
    for (size_t i = 0; i < sizeof(data); i++) CHECK(pEncoded[i] == (uint8_t)(data[i] ^ key[i % 2]), "XOR cycles key bytes");
    uint8_t *pPlain = XCrypt_XOR(pEncoded, sizeof(data), key, sizeof(key));
    CHECK(pPlain && memcmp(pPlain, data, sizeof(data)) == 0, "XOR inversion retains binary length");
    free(pPlain);
    free(pEncoded);
    char *pReverse = XCrypt_Reverse((const char*)data, sizeof(data));
    CHECK(pReverse != NULL, "Reverse accepts a bounded binary slice");
    for (size_t i = 0; i < sizeof(data); i++)
        CHECK((uint8_t)pReverse[i] == data[sizeof(data) - i - 1], "Reverse uses length, not strlen");
    free(pReverse);
    CHECK(XCrypt_XOR(data, sizeof(data), key, 0) == NULL, "Empty XOR key cannot divide by zero");
    return 0;
}

static int XTest_dispatch(void)
{
    CHECK(XCrypt_GetCipher("base64") == XC_BASE64 && XCrypt_GetCipher("unknown") == XC_INVALID,
        "Resolve named cipher or reject it");
    xcrypt_ctx_t context;
    XCrypt_Init(&context, XFALSE, NULL, NULL, NULL);
    size_t nLength = 3;
    uint8_t *pEncoded = XCrypt_Single(&context, XC_BASE64, (const uint8_t*)"abc", &nLength);
    CHECK(pEncoded && nLength == 4 && strcmp((char*)pEncoded, "YWJj") == 0, "Dispatcher selects base64 correctly");
    uint8_t *pPlain = XDecrypt_Single(&context, XC_BASE64, pEncoded, &nLength);
    CHECK(pPlain && nLength == 3 && memcmp(pPlain, "abc", 3) == 0, "Inverse dispatcher recovers plaintext");
    free(pPlain);
    free(pEncoded);
    return 0;
}


/* Supplies keys and records errors for the dispatcher cases below. */
typedef struct {
    const char *pKey;
    const char *pIV;
    int nKeyCalls;
    int nIVCalls;
    int nErrors;
    xbool_t bRefuseKey;
    xbool_t bRefuseIV;
    char sLastError[256];
} crypt_ctx_t;

static xbool_t crypt_callback(xcrypt_cb_type_t eType, void *pData, void *pUser)
{
    crypt_ctx_t *pTest = (crypt_ctx_t*)pUser;

    if (eType == XCB_ERROR)
    {
        pTest->nErrors++;
        xstrncpy(pTest->sLastError, sizeof(pTest->sLastError), (const char*)pData);
        return XTRUE;
    }

    xcrypt_key_t *pKey = (xcrypt_key_t*)pData;
    if (eType == XCB_KEY)
    {
        pTest->nKeyCalls++;
        if (pTest->bRefuseKey) return XFALSE;
        pKey->nLength = xstrncpy(pKey->sKey, sizeof(pKey->sKey), pTest->pKey);
        return XTRUE;
    }

    if (eType == XCB_IV)
    {
        pTest->nIVCalls++;
        if (pTest->bRefuseIV) return XFALSE;
        xstrncpy(pKey->sIV, sizeof(pKey->sIV), pTest->pIV);
        return XTRUE;
    }

    return XTRUE;
}

static void crypt_init(crypt_ctx_t *pTest, xcrypt_ctx_t *pCtx, xbool_t bDecrypt, char *pCiphers)
{
    memset(pTest, 0, sizeof(*pTest));
    pTest->pKey = "0123456789abcdef";
    pTest->pIV = "fedcba9876543210";
    XCrypt_Init(pCtx, bDecrypt, pCiphers, crypt_callback, pTest);
}

static int XTest_cipher_names(void)
{
    /* Every cipher the dispatcher can name must parse back to itself.
     * A shorter cipher name that prefixes a longer one would otherwise
     * shadow it and silently select the wrong algorithm. */
    for (int i = 0; i < XC_INVALID; i++)
    {
        xcrypt_chipher_t eCipher = (xcrypt_chipher_t)i;
        const char *pName = XCrypt_GetCipherStr(eCipher);
        CHECK(pName != NULL, "Every cipher has a name");

        if (eCipher == XC_MULTY)
        {
            CHECK(strcmp(pName, "multy") == 0, "The multi-cipher pseudo cipher is named");
            continue;
        }

        CHECK(strcmp(pName, "invalid") != 0, "Every real cipher has a real name");
        CHECK(XCrypt_GetCipher(pName) == eCipher, "Every cipher name parses back to its own cipher");
    }

    CHECK(strcmp(XCrypt_GetCipherStr(XC_INVALID), "invalid") == 0, "The invalid cipher is named");
    CHECK(strcmp(XCrypt_GetCipherStr((xcrypt_chipher_t)200), "invalid") == 0, "An out of range cipher is named invalid");
    CHECK(XCrypt_GetCipher("") == XC_INVALID, "An empty name is rejected");
    CHECK(XCrypt_GetCipher("nonsense") == XC_INVALID, "An unknown name is rejected");

    /* The two checksum ciphers are distinct algorithms, so the longer name
     * must not be swallowed by the shorter one. */
    CHECK(XCrypt_GetCipher("crc32") == XC_CRC32, "The short checksum name selects the short checksum");
    CHECK(XCrypt_GetCipher("crc32b") == XC_CRC32B, "The long checksum name selects the long checksum");

    const uint8_t data[] = "checksum me";
    size_t nA = sizeof(data) - 1, nB = sizeof(data) - 1;
    xcrypt_ctx_t ctx;
    crypt_ctx_t test;
    crypt_init(&test, &ctx, XFALSE, NULL);

    uint8_t *pPlain = XCrypt_Single(&ctx, XC_CRC32, data, &nA);
    uint8_t *pReflected = XCrypt_Single(&ctx, XC_CRC32B, data, &nB);
    CHECK(pPlain != NULL && pReflected != NULL, "Both checksums compute");
    CHECK(strcmp((char*)pPlain, (char*)pReflected) != 0, "The two checksums are genuinely different algorithms");
    free(pPlain);
    free(pReflected);
    return 0;
}

static int XTest_keyless_roundtrip(void)
{
    /* Ciphers that need no key must survive an encrypt/decrypt round trip
     * on binary input, including embedded NUL bytes. */
    const uint8_t data[] = {'r', 'o', 'u', 'n', 'd', 0x00, 0xff, 't', 'r', 'i', 'p'};
    const xcrypt_chipher_t ciphers[] = {XC_HEX, XC_BASE64, XC_B64URL, XC_REVERSE};

    for (size_t i = 0; i < sizeof(ciphers) / sizeof(*ciphers); i++)
    {
        xcrypt_ctx_t ctx;
        crypt_ctx_t test;
        crypt_init(&test, &ctx, XFALSE, NULL);

        size_t nLength = sizeof(data);
        uint8_t *pCrypted = XCrypt_Single(&ctx, ciphers[i], data, &nLength);
        CHECK(pCrypted != NULL, "A keyless cipher encrypts");

        uint8_t *pPlain = XDecrypt_Single(&ctx, ciphers[i], pCrypted, &nLength);
        CHECK(pPlain != NULL, "A keyless cipher decrypts");
        CHECK(nLength == sizeof(data), "The round trip restores the original length");
        CHECK(memcmp(pPlain, data, sizeof(data)) == 0, "The round trip restores every byte");
        CHECK(test.nKeyCalls == 0, "A keyless cipher never asks for a key");
        CHECK(test.nErrors == 0, "A successful round trip reports no error");

        free(pCrypted);
        free(pPlain);
    }
    return 0;
}

static int XTest_keyed_roundtrip(void)
{
    const uint8_t data[] = {'s', 'e', 'c', 'r', 'e', 't', 0x00, 0x01, 'z'};

    /* XOR asks for a key but not an initialisation vector. */
    xcrypt_ctx_t ctx;
    crypt_ctx_t test;
    crypt_init(&test, &ctx, XFALSE, NULL);

    size_t nLength = sizeof(data);
    uint8_t *pCrypted = XCrypt_Single(&ctx, XC_XOR, data, &nLength);
    CHECK(pCrypted != NULL, "The keyed cipher encrypts");
    CHECK(test.nKeyCalls == 1, "The keyed cipher asked for a key");
    CHECK(test.nIVCalls == 0, "A cipher with no initialisation vector does not ask for one");
    CHECK(memcmp(pCrypted, data, sizeof(data)) != 0, "The output differs from the input");

    uint8_t *pPlain = XDecrypt_Single(&ctx, XC_XOR, pCrypted, &nLength);
    CHECK(pPlain != NULL && memcmp(pPlain, data, sizeof(data)) == 0, "The keyed round trip restores the input");
    free(pCrypted);
    free(pPlain);

    /* AES asks for both a key and an initialisation vector. */
    crypt_init(&test, &ctx, XFALSE, NULL);
    nLength = sizeof(data);
    pCrypted = XCrypt_Single(&ctx, XC_AES, data, &nLength);
    CHECK(pCrypted != NULL, "The block cipher encrypts");
    CHECK(test.nKeyCalls == 1 && test.nIVCalls == 1, "The block cipher asked for both a key and an IV");

    size_t nCryptedLen = nLength;
    pPlain = XDecrypt_Single(&ctx, XC_AES, pCrypted, &nLength);
    CHECK(pPlain != NULL, "The block cipher decrypts");
    CHECK(nCryptedLen >= sizeof(data), "The block cipher output is padded up to a block boundary");
    CHECK(memcmp(pPlain, data, sizeof(data)) == 0, "The block cipher round trip restores every byte");
    free(pCrypted);
    free(pPlain);

    /* A refused key aborts before the cipher runs. */
    crypt_init(&test, &ctx, XFALSE, NULL);
    test.bRefuseKey = XTRUE;
    nLength = sizeof(data);
    CHECK(XCrypt_Single(&ctx, XC_XOR, data, &nLength) == NULL, "A refused key aborts the encryption");
    CHECK(test.nKeyCalls == 1 && test.nIVCalls == 0, "A refused key is not followed by an IV request");

    crypt_init(&test, &ctx, XTRUE, NULL);
    test.bRefuseKey = XTRUE;
    nLength = sizeof(data);
    CHECK(XDecrypt_Single(&ctx, XC_XOR, data, &nLength) == NULL, "A refused key aborts the decryption");

    /* A refused IV aborts a cipher that needs one. */
    crypt_init(&test, &ctx, XFALSE, NULL);
    test.bRefuseIV = XTRUE;
    nLength = sizeof(data);
    CHECK(XCrypt_Single(&ctx, XC_AES, data, &nLength) == NULL, "A refused IV aborts the encryption");
    CHECK(test.nKeyCalls == 1 && test.nIVCalls == 1, "The IV was requested after the key");
    return 0;
}

static int XTest_digests(void)
{
    /* The digest ciphers are one way: each produces its documented length
     * and has no decrypt path. */
    const uint8_t data[] = "digest me";
    const struct { xcrypt_chipher_t eCipher; size_t nLength; } digests[] = {
        {XC_MD5, 16}, {XC_SHA1, 20}, {XC_SHA256, 32},
        {XC_MD5_SUM, 32}, {XC_SHA1_SUM, 40}, {XC_SHA256_SUM, 64}
    };

    for (size_t i = 0; i < sizeof(digests) / sizeof(*digests); i++)
    {
        xcrypt_ctx_t ctx;
        crypt_ctx_t test;
        crypt_init(&test, &ctx, XFALSE, NULL);

        size_t nLength = sizeof(data) - 1;
        uint8_t *pDigest = XCrypt_Single(&ctx, digests[i].eCipher, data, &nLength);
        CHECK(pDigest != NULL, "The digest computes");
        CHECK(nLength == digests[i].nLength, "The digest has its documented length");
        free(pDigest);

        /* Decrypting a digest is not defined and must fail loudly. */
        crypt_init(&test, &ctx, XTRUE, NULL);
        nLength = sizeof(data) - 1;
        CHECK(XDecrypt_Single(&ctx, digests[i].eCipher, data, &nLength) == NULL, "A digest cannot be decrypted");
        CHECK(test.nErrors == 1, "The failed decryption is reported through the callback");
        CHECK(strstr(test.sLastError, "decrypt") != NULL, "The error names the failed direction");
    }

    /* The keyed digests take a key and produce a hex string. */
    const xcrypt_chipher_t keyed[] = {XC_HS256, XC_MD5_HMAC};
    const size_t lengths[] = {64, 32};
    for (size_t i = 0; i < 2; i++)
    {
        xcrypt_ctx_t ctx;
        crypt_ctx_t test;
        crypt_init(&test, &ctx, XFALSE, NULL);

        size_t nLength = sizeof(data) - 1;
        uint8_t *pDigest = XCrypt_Single(&ctx, keyed[i], data, &nLength);
        CHECK(pDigest != NULL, "The keyed digest computes");
        CHECK(nLength == lengths[i], "The keyed digest has its documented length");
        CHECK(test.nKeyCalls == 1, "The keyed digest asked for a key");
        free(pDigest);
    }
    return 0;
}

static int XTest_casear(void)
{
    /* The shift cipher only moves letters, and only within their own case. */
    const char plain[] = "Attack at Dawn, 123!";
    char *pShifted = XCrypt_Casear(plain, strlen(plain), 3);
    CHECK(pShifted != NULL, "The shift cipher encrypts");
    CHECK(strcmp(pShifted, "Dwwdfn dw Gdzq, 123!") == 0, "Letters shift and everything else is left alone");

    char *pPlain = XDecrypt_Casear(pShifted, strlen(pShifted), 3);
    CHECK(pPlain != NULL && strcmp(pPlain, plain) == 0, "The shift is reversible");
    free(pShifted);
    free(pPlain);

    /* A shift of a whole alphabet is the identity. */
    pShifted = XCrypt_Casear(plain, strlen(plain), 52);
    CHECK(pShifted != NULL && strcmp(pShifted, plain) == 0, "A full alphabet shift changes nothing");
    free(pShifted);

    /* A zero shift is also the identity. */
    pShifted = XCrypt_Casear(plain, strlen(plain), 0);
    CHECK(pShifted != NULL && strcmp(pShifted, plain) == 0, "A zero shift changes nothing");
    free(pShifted);

    /* Binary input is shifted by length, not by strlen. */
    const char binary[] = {'a', 0x00, 'b'};
    pShifted = XCrypt_Casear(binary, sizeof(binary), 1);
    CHECK(pShifted != NULL, "Binary input is accepted");
    CHECK(pShifted[0] == 'b' && pShifted[2] == 'c', "Bytes after an embedded NUL are still shifted");
    free(pShifted);

    /* Every key over the whole alphabet must round trip, and must never
     * leave the letter's own case or touch a non-letter. */
    const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0 .!";
    for (size_t nKey = 0; nKey <= 130; nKey++)
    {
        char *pEnc = XCrypt_Casear(alphabet, strlen(alphabet), nKey);
        CHECK(pEnc != NULL, "Every key encrypts");

        for (size_t i = 0; i < strlen(alphabet); i++)
        {
            if (alphabet[i] >= 'a' && alphabet[i] <= 'z')
                CHECK(pEnc[i] >= 'a' && pEnc[i] <= 'z', "A lower case letter stays lower case");
            else if (alphabet[i] >= 'A' && alphabet[i] <= 'Z')
                CHECK(pEnc[i] >= 'A' && pEnc[i] <= 'Z', "An upper case letter stays upper case");
            else
                CHECK(pEnc[i] == alphabet[i], "A non-letter is left alone");
        }

        char *pDec = XDecrypt_Casear(pEnc, strlen(pEnc), nKey);
        CHECK(pDec != NULL && strcmp(pDec, alphabet) == 0, "Every key round trips");
        free(pEnc);
        free(pDec);
    }

    CHECK(XCrypt_Casear(NULL, 4, 1) == NULL, "A missing input is rejected");
    CHECK(XCrypt_Casear(plain, 0, 1) == NULL, "An empty input is rejected");
    CHECK(XDecrypt_Casear(NULL, 4, 1) == NULL, "A missing input is rejected on decryption");
    return 0;
}

static int XTest_multy(void)
{
    /* A colon separated chain applies each cipher in turn, and the reverse
     * chain must undo it exactly. */
    const uint8_t data[] = "chain me through several ciphers";
    char sForward[] = "base64:hex:reverse";
    char sBackward[] = "reverse:hex:base64";

    xcrypt_ctx_t ctx;
    crypt_ctx_t test;
    crypt_init(&test, &ctx, XFALSE, sForward);

    size_t nLength = sizeof(data) - 1;
    uint8_t *pCrypted = XCrypt_Multy(&ctx, data, &nLength);
    CHECK(pCrypted != NULL, "The cipher chain encrypts");
    CHECK(test.nErrors == 0, "A valid chain reports no error");

    crypt_init(&test, &ctx, XTRUE, sBackward);
    uint8_t *pPlain = XCrypt_Multy(&ctx, pCrypted, &nLength);
    CHECK(pPlain != NULL, "The reversed chain decrypts");
    CHECK(nLength == sizeof(data) - 1, "The chain restores the original length");
    CHECK(memcmp(pPlain, data, nLength) == 0, "The chain restores every byte");
    free(pCrypted);
    free(pPlain);

    /* A single cipher with no separator is still a valid chain. */
    char sSingle[] = "base64";
    crypt_init(&test, &ctx, XFALSE, sSingle);
    nLength = sizeof(data) - 1;
    pCrypted = XCrypt_Multy(&ctx, data, &nLength);
    CHECK(pCrypted != NULL, "A single cipher chain encrypts");

    crypt_init(&test, &ctx, XTRUE, sSingle);
    pPlain = XCrypt_Multy(&ctx, pCrypted, &nLength);
    CHECK(pPlain != NULL && memcmp(pPlain, data, nLength) == 0, "A single cipher chain round trips");
    free(pCrypted);
    free(pPlain);

    /* An unknown cipher anywhere in the chain fails the whole chain. */
    char sBad[] = "base64:nonsense:hex";
    crypt_init(&test, &ctx, XFALSE, sBad);
    nLength = sizeof(data) - 1;
    CHECK(XCrypt_Multy(&ctx, data, &nLength) == NULL, "An unknown cipher fails the chain");
    CHECK(test.nErrors > 0, "The unknown cipher is reported");

    /* A cipher that cannot run in this direction fails the chain too. */
    char sOneWay[] = "md5:hex";
    crypt_init(&test, &ctx, XTRUE, sOneWay);
    nLength = sizeof(data) - 1;
    CHECK(XCrypt_Multy(&ctx, data, &nLength) == NULL, "A one way cipher fails a decrypt chain");
    CHECK(test.nErrors > 0, "The one way cipher is reported");
    return 0;
}

static int XTest_hex_columns(void)
{
    /* The hex encoder can group its output into columns with a separator;
     * the decoder still has to read it back. */
    const uint8_t data[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};

    size_t nLength = sizeof(data);
    uint8_t *pHex = XCrypt_HEX(data, &nLength, " ", 4, XTRUE);
    CHECK(pHex != NULL, "Hex encodes with columns");
    CHECK(strchr((char*)pHex, ' ') != NULL, "The column separator is present");
    CHECK(strchr((char*)pHex, '\n') != NULL, "The column width wraps the output");
    free(pHex);

    /* Without a separator the output is a plain hex run. */
    nLength = sizeof(data);
    pHex = XCrypt_HEX(data, &nLength, NULL, 0, XTRUE);
    CHECK(pHex != NULL && nLength == sizeof(data) * 2, "Plain hex doubles the length");
    CHECK(strcmp((char*)pHex, "0011223344556677") == 0, "Plain hex has no separators");

    uint8_t *pPlain = XDecrypt_HEX(pHex, &nLength, XTRUE);
    CHECK(pPlain != NULL && nLength == sizeof(data), "Plain hex decodes back");
    CHECK(memcmp(pPlain, data, sizeof(data)) == 0, "Plain hex restores every byte");
    free(pPlain);
    free(pHex);

    CHECK(XCrypt_HEX(NULL, &nLength, NULL, 0, XTRUE) == NULL, "A missing input is rejected");
    nLength = 0;
    CHECK(XCrypt_HEX(data, &nLength, NULL, 0, XTRUE) == NULL, "An empty input is rejected");
    return 0;
}

static int crypt_hex_compare(const uint8_t *pInput, size_t nLength)
{
    uint8_t sExpected[32];
    size_t nExpected = 0;
    const char *pRead = (const char*)pInput;
    unsigned int nValue = 0;
    int nOffset = 0;

    while (sscanf(pRead, "%02x%n", &nValue, &nOffset) == 1)
    {
        CHECK(nOffset > 0 && nExpected < sizeof(sExpected), "The reference decoder advances within the output");
        sExpected[nExpected++] = (uint8_t)nValue;
        pRead += nOffset;
    }

    for (int nLower = 0; nLower < 2; nLower++)
    {
        size_t nDecoded = nLength;
        uint8_t *pDecoded = XDecrypt_HEX(pInput, &nDecoded, nLower);
        CHECK(pDecoded != NULL && nDecoded == nExpected, "Hex decoding keeps the reference output length");
        CHECK(!memcmp(pDecoded, sExpected, nExpected), "Hex decoding agrees with the reference byte for byte");
        free(pDecoded);
    }

    return 0;
}

static int XTest_hex_decode_agreement(void)
{
    for (unsigned int i = 0; i <= UINT16_MAX; i++)
    {
        uint8_t sInput[] = { (uint8_t)(i >> 8), (uint8_t)i, ' ', '7', '\0' };
        CHECK(crypt_hex_compare(sInput, sizeof(sInput) - 1) == 0, "Every byte pair preserves hex decoding");
    }

    const char *pInputs[] = {
        "0x12 0Xff 00", "+1 -1 +a -F", "a b c D E F", "123456789abcdefABCDEF0",
        " \t\r\n\v\f01 23\t45\n67\r89\vab\fCD ef", "00 ff zz 11", "00 + 11", "00 - 11"
    };

    for (size_t i = 0; i < sizeof(pInputs) / sizeof(*pInputs); i++)
        CHECK(crypt_hex_compare((const uint8_t*)pInputs[i], strlen(pInputs[i])) == 0, "Mixed hex tokens preserve decoding");

    return 0;
}

static int XTest_hex_scaling(void)
{
    /* Hex decoding ran sscanf() on the rest of the input for every byte, and
     * sscanf() measures all of it each time: a megabyte took about a minute.
     * Encoding grew its output a few bytes at a time, which is as slow under
     * any allocator that moves a block to grow it. Both have to stay linear,
     * and decoding still has to stop where the old parse stopped. */
    enum { HEX_BYTES = 1000000 };
    uint8_t *pData = (uint8_t*)malloc(HEX_BYTES);
    CHECK(pData != NULL, "Allocate a megabyte to encode");

    for (size_t i = 0; i < HEX_BYTES; i++) pData[i] = (uint8_t)(i * 131 + 7);

    size_t nLength = HEX_BYTES;
    uint8_t *pHex = XCrypt_HEX(pData, &nLength, " ", 16, XTRUE);
    CHECK(pHex != NULL && nLength > HEX_BYTES * 3, "Encode with separators and line breaks");

    size_t nDecoded = nLength;
    uint8_t *pBack = XDecrypt_HEX(pHex, &nDecoded, XTRUE);
    CHECK(pBack != NULL && nDecoded == HEX_BYTES, "Decode a megabyte of hex");
    CHECK(!memcmp(pBack, pData, HEX_BYTES), "The decoded bytes are the encoded ones");

    free(pBack);
    free(pHex);
    free(pData);

    /* Whitespace of every kind is skipped, and the first thing that is not a
     * hex value ends the input, as it always has */
    const char *pMixed = " 0a\t\n1B\v\f\r ff zz 00";
    nDecoded = strlen(pMixed);
    pBack = XDecrypt_HEX((const uint8_t*)pMixed, &nDecoded, XFALSE);
    CHECK(pBack != NULL && nDecoded == 3, "Decoding stops at the first non hex value");
    CHECK(pBack[0] == 0x0a && pBack[1] == 0x1b && pBack[2] == 0xff, "Mixed whitespace between values is skipped");
    free(pBack);

    return 0;
}

static int XTest_api_guards(void)
{
    const uint8_t data[] = "guarded";
    size_t nLength = sizeof(data) - 1;

    CHECK(XCrypt_XOR(NULL, 1, data, 1) == NULL && XCrypt_XOR(data, 1, NULL, 1) == NULL, "XOR needs input and key");
    CHECK(XCrypt_XOR(data, 0, data, 1) == NULL, "XOR needs a length");
    CHECK(XCrypt_HEX(data, NULL, NULL, 0, XTRUE) == NULL, "Hex encoding needs a length pointer");
    CHECK(XDecrypt_HEX(NULL, &nLength, XTRUE) == NULL && XDecrypt_HEX(data, NULL, XTRUE) == NULL, "Hex decoding needs input");
    nLength = 0;
    CHECK(XDecrypt_HEX(data, &nLength, XTRUE) == NULL, "Hex decoding needs a length");

    /* Both limits are checked before the input is read */
    nLength = (size_t)INT_MAX + 1;
    CHECK(XCrypt_HEX(data, &nLength, NULL, 0, XTRUE) == NULL, "Hex encoding refuses an input over INT_MAX");
    nLength = (size_t)INT_MAX / 2 + 1;
    CHECK(XCrypt_HEX(data, &nLength, " ", 0, XTRUE) == NULL && !nLength, "Hex encoding refuses an output over INT_MAX");

    CHECK(XCrypt_Reverse(NULL, 1) == NULL && XCrypt_Reverse("x", 0) == NULL, "Reverse needs input");
    CHECK(XDecrypt_Casear("x", 0, 1) == NULL, "The shift decryption needs a length");

    const uint8_t key[] = "0123456789abcdef";
    nLength = sizeof(data) - 1;
    CHECK(XCrypt_AES(NULL, &nLength, key, 16, NULL) == NULL && XCrypt_AES(data, &nLength, NULL, 16, NULL) == NULL,
        "AES encryption needs input and key");
    CHECK(XCrypt_AES(data, &nLength, key, 0, NULL) == NULL && XCrypt_AES(data, NULL, key, 16, NULL) == NULL,
        "AES encryption needs key and input lengths");
    CHECK(XDecrypt_AES(NULL, &nLength, key, 16, NULL) == NULL && XDecrypt_AES(data, &nLength, NULL, 16, NULL) == NULL,
        "AES decryption needs input and key");
    CHECK(XDecrypt_AES(data, &nLength, key, 0, NULL) == NULL && XDecrypt_AES(data, NULL, key, 16, NULL) == NULL,
        "AES decryption needs key and input lengths");
    nLength = 0;
    CHECK(XCrypt_AES(data, &nLength, key, 16, NULL) == NULL && XDecrypt_AES(data, &nLength, key, 16, NULL) == NULL,
        "AES needs an input length");

    /* The pseudo cipher has no single step in either direction */
    xcrypt_ctx_t ctx;
    crypt_ctx_t test;
    crypt_init(&test, &ctx, XFALSE, NULL);
    nLength = sizeof(data) - 1;
    CHECK(XCrypt_Single(&ctx, XC_MULTY, data, &nLength) == NULL, "The pseudo cipher does not encrypt");
    CHECK(test.nErrors == 1 && strstr(test.sLastError, "multy") != NULL, "The failure names the cipher");
    CHECK(XDecrypt_Single(&ctx, XC_MULTY, data, &nLength) == NULL && test.nErrors == 2, "The pseudo cipher does not decrypt");

    /* Without a callback a keyed cipher gets no key, and fails without one */
    XCrypt_Init(&ctx, XFALSE, NULL, NULL, NULL);
    CHECK(XCrypt_Single(&ctx, XC_XOR, data, &nLength) == NULL, "A keyed cipher without a key fails");

    /* An unknown cipher is reported by the name it was given */
    char sBad[] = "hex:bogus";
    crypt_init(&test, &ctx, XFALSE, sBad);
    nLength = sizeof(data) - 1;
    CHECK(XCrypt_Multy(&ctx, data, &nLength) == NULL, "An unknown cipher fails the chain");
    CHECK(strstr(test.sLastError, "bogus") != NULL, "The error names the unknown cipher");
    return 0;
}

static int XTest_keyed_dispatch(void)
{
    const char plain[] = "Shift Me";
    xcrypt_ctx_t ctx;
    crypt_ctx_t test;
    crypt_init(&test, &ctx, XFALSE, NULL);
    test.pKey = "3";

    size_t nLength = strlen(plain);
    uint8_t *pShifted = XCrypt_Single(&ctx, XC_CASEAR, (const uint8_t*)plain, &nLength);
    CHECK(pShifted != NULL && strcmp((char*)pShifted, "Vkliw Ph") == 0, "The dispatcher shifts by the supplied key");
    uint8_t *pPlain = XDecrypt_Single(&ctx, XC_CASEAR, pShifted, &nLength);
    CHECK(pPlain != NULL && strcmp((char*)pPlain, plain) == 0, "The dispatcher shifts back by the same key");
    free(pShifted);
    free(pPlain);

#ifdef XCRYPT_USE_SSL
    xrsa_ctx_t key;
    XRSA_Init(&key);
    CHECK(XRSA_GenerateKeys(&key, 1024, 65537) == XSTDOK, "Generate an ephemeral test-only key");
    CHECK(key.nPrivKeyLen < sizeof(((xcrypt_key_t*)0)->sKey), "The key fits the callback buffer");

    /* Public key encryption opens with the private key, private key encryption with the public one */
    const struct { xcrypt_chipher_t eCipher; const char *pEncKey; const char *pDecKey; } pairs[] = {
        { XC_RSA, key.pPublicKey, key.pPrivateKey }, { XC_RSAPR, key.pPrivateKey, key.pPublicKey }
    };

    for (size_t i = 0; i < sizeof(pairs) / sizeof(*pairs); i++)
    {
        crypt_init(&test, &ctx, XFALSE, NULL);
        test.pKey = pairs[i].pEncKey;
        nLength = strlen(plain);

        uint8_t *pCrypted = XCrypt_Single(&ctx, pairs[i].eCipher, (const uint8_t*)plain, &nLength);
        CHECK(pCrypted != NULL && nLength == 128, "The dispatcher encrypts with RSA");

        test.pKey = pairs[i].pDecKey;
        pPlain = XDecrypt_Single(&ctx, pairs[i].eCipher, pCrypted, &nLength);
        CHECK(pPlain != NULL && nLength == strlen(plain) && !memcmp(pPlain, plain, nLength), "The dispatcher decrypts with RSA");
        free(pCrypted);
        free(pPlain);
    }

    crypt_init(&test, &ctx, XFALSE, NULL);
    test.pKey = key.pPrivateKey;
    nLength = strlen(plain);
    uint8_t *pSignature = XCrypt_Single(&ctx, XC_RS256, (const uint8_t*)plain, &nLength);
    CHECK(pSignature != NULL && nLength == 128, "The dispatcher signs with RSA");
    CHECK(XCrypt_VerifyRS256(pSignature, nLength, (const uint8_t*)plain, strlen(plain), key.pPublicKey, key.nPubKeyLen) == XSTDOK,
        "The dispatched signature verifies");
    free(pSignature);
    XRSA_Destroy(&key);
#endif
    return 0;
}

static int XTest_aes_key_lengths(void)
{
    /* A key length in bytes, as documented, used to leave the key unset: any key decrypted the data */
    const uint8_t data[] = "top secret payload";
    const uint8_t key[] = "0123456789abcdef0123456789abcdef", other[] = "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ";
    const size_t lengths[][2] = { {16, 128}, {24, 192}, {32, 256} };

    for (size_t i = 0; i < sizeof(lengths) / sizeof(*lengths); i++)
    {
        size_t nLength = sizeof(data) - 1;
        uint8_t *pCrypted = XCrypt_AES(data, &nLength, key, lengths[i][0], NULL);
        CHECK(pCrypted != NULL && nLength == 16 + 32, "The ciphertext carries its IV and a padded payload");

        size_t nPlain = nLength;
        uint8_t *pPlain = XDecrypt_AES(pCrypted, &nPlain, key, lengths[i][1], NULL);
        CHECK(pPlain != NULL && nPlain == sizeof(data) - 1 && !memcmp(pPlain, data, nPlain), "Bytes and bits name the same key");
        free(pPlain);

        nPlain = nLength;
        pPlain = XDecrypt_AES(pCrypted, &nPlain, other, lengths[i][0], NULL);
        CHECK(pPlain == NULL || nPlain != sizeof(data) - 1 || memcmp(pPlain, data, nPlain), "Another key does not decrypt");
        free(pPlain);

        size_t nOther = sizeof(data) - 1;
        uint8_t *pOther = XCrypt_AES(data, &nOther, other, lengths[i][0], pCrypted);
        nPlain = nOther;
        pPlain = XDecrypt_AES(pOther, &nPlain, key, lengths[i][0], NULL);
        CHECK(pPlain == NULL || nPlain != sizeof(data) - 1 || memcmp(pPlain, data, nPlain), "Another key does not encrypt");
        free(pPlain);
        free(pOther);
        free(pCrypted);
    }

    const size_t invalid[] = { 1, 10, 15, 17, 64, 127, 129, 512 };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
    {
        size_t nLength = sizeof(data) - 1;
        CHECK(XCrypt_AES(data, &nLength, key, invalid[i], NULL) == NULL, "A key of no AES size does not encrypt");
        nLength = 32;
        CHECK(XDecrypt_AES(key, &nLength, key, invalid[i], NULL) == NULL, "A key of no AES size does not decrypt");
    }

    return 0;
}

XTEST_MAIN(
    XTEST_CASE(api_guards),
    XTEST_CASE(keyed_dispatch),
    XTEST_CASE(aes_key_lengths),
    XTEST_CASE(hex),
    XTEST_CASE(transforms),
    XTEST_CASE(dispatch),
    XTEST_CASE(cipher_names),
    XTEST_CASE(keyless_roundtrip),
    XTEST_CASE(keyed_roundtrip),
    XTEST_CASE(digests),
    XTEST_CASE(casear),
    XTEST_CASE(multy),
    XTEST_CASE(hex_columns),
    XTEST_CASE(hex_decode_agreement),
    XTEST_CASE(hex_scaling)
)
