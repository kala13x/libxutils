/* libxutils: RSA key import, encryption, signature integrity and malformed keys. */
#include "test.h"
#include "rsa.h"

static int XTest_signatures(void)
{
    xrsa_ctx_t key;
    CHECK(XRSA_GenerateKeys(&key, 2048, 65537) == XSTDOK, "Generate an ephemeral test-only key");
    const uint8_t data[] = {'a', 0, 'b', 0xff};
    size_t nSignature = 0;
    uint8_t *pSignature = XCrypt_RS256(data, sizeof(data), key.pPrivateKey, key.nPrivKeyLen, &nSignature);
    CHECK(pSignature && nSignature == 256, "Sign binary content with RSA-2048");
    CHECK(XCrypt_VerifyRS256(pSignature, nSignature, data, sizeof(data), key.pPublicKey, key.nPubKeyLen) == XSTDOK,
        "Verify the original signature");
    pSignature[0] ^= 1;
    CHECK(XCrypt_VerifyRS256(pSignature, nSignature, data, sizeof(data), key.pPublicKey, key.nPubKeyLen) != XSTDOK,
        "A modified signature must fail");
    pSignature[0] ^= 1;
    CHECK(XCrypt_VerifyRS256(pSignature, nSignature, data, sizeof(data) - 1, key.pPublicKey, key.nPubKeyLen) != XSTDOK,
        "A different message length must fail");
    CHECK(XCrypt_VerifyRS256(pSignature, nSignature - 1, data, sizeof(data), key.pPublicKey, key.nPubKeyLen) != XSTDOK,
        "A truncated signature must fail");
    free(pSignature);
    XRSA_Destroy(&key);
    XRSA_Destroy(&key);
    return 0;
}

static int XTest_encryption(void)
{
    xrsa_ctx_t key;
    CHECK(XRSA_GenerateKeys(&key, 2048, 65537) == XSTDOK, "Generate encryption fixture");
    uint8_t data[245];
    for (size_t i = 0; i < sizeof(data); i++) data[i] = (uint8_t)i;
    size_t nCipher = 0, nPlain = 0;
    uint8_t *pCipher = XCrypt_RSA(data, sizeof(data), key.pPublicKey, key.nPubKeyLen, &nCipher);
    CHECK(pCipher && nCipher == 256, "Encrypt the largest PKCS1 v1.5 plaintext for RSA-2048");
    uint8_t *pPlain = XDecrypt_RSA(pCipher, nCipher, key.pPrivateKey, key.nPrivKeyLen, &nPlain);
    CHECK(pPlain && nPlain == sizeof(data) && memcmp(pPlain, data, nPlain) == 0, "Imported private key decrypts exact bytes");
    free(pPlain);
    free(pCipher);
    uint8_t oversized[246] = {0};
    CHECK(XCrypt_RSA(oversized, sizeof(oversized), key.pPublicKey, key.nPubKeyLen, &nCipher) == NULL,
        "Reject a message exceeding the padding limit");
    XRSA_Destroy(&key);
    return 0;
}

static int XTest_invalid_keys(void)
{
    size_t nLength = 0;
    CHECK(XCrypt_RSA((const uint8_t*)"x", 1, "invalid key", 11, &nLength) == NULL, "Reject malformed public key");
    CHECK(XCrypt_RS256((const uint8_t*)"x", 1, "invalid key", 11, &nLength) == NULL, "Reject malformed signing key");
    CHECK(XCrypt_VerifyRS256((const uint8_t*)"x", 1, (const uint8_t*)"x", 1, "invalid key", 11) != XSTDOK,
        "A parse failure cannot authenticate a signature");
    char *pErrors = XSSL_LastErrors(&nLength);
    free(pErrors);
    return 0;
}

XTEST_MAIN(XTEST_CASE(signatures), XTEST_CASE(encryption), XTEST_CASE(invalid_keys))
