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

XTEST_MAIN(XTEST_CASE(hex), XTEST_CASE(transforms), XTEST_CASE(dispatch))
