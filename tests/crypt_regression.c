/* libxutils: binary-safe utility encodings and dispatcher consistency. */
#include "test.h"
#include "crypt.h"

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

XTEST_MAIN(
    XTEST_CASE(hex),
    XTEST_CASE(transforms),
    XTEST_CASE(dispatch),
    XTEST_CASE(cipher_names),
    XTEST_CASE(keyless_roundtrip),
    XTEST_CASE(keyed_roundtrip),
    XTEST_CASE(digests),
    XTEST_CASE(casear),
    XTEST_CASE(multy),
    XTEST_CASE(hex_columns)
)
