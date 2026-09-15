/* libxutils: RSA key import, encryption, signature integrity and malformed keys. */
#include "test.h"
#include "rsa.h"
#include <unistd.h>

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


static int XTest_key_sizes(void)
{
    /* Every key size the library will generate has to sign and verify, and
     * the signature is always as wide as the modulus. */
    const struct { size_t nBits; size_t nSignature; } sizes[] = {
        {1024, 128}, {2048, 256}, {3072, 384}
    };

    const uint8_t data[] = {'m', 'e', 's', 's', 'a', 'g', 'e', 0x00, 0xff};

    for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); i++)
    {
        xrsa_ctx_t key;
        CHECK(XRSA_GenerateKeys(&key, sizes[i].nBits, 65537) == XSTDOK, "Every key size generates");
        CHECK(key.pPrivateKey != NULL && key.nPrivKeyLen > 0, "The private key is exported");
        CHECK(key.pPublicKey != NULL && key.nPubKeyLen > 0, "The public key is exported");
        CHECK(strstr(key.pPrivateKey, "PRIVATE KEY") != NULL, "The private key is in PEM form");
        CHECK(strstr(key.pPublicKey, "PUBLIC KEY") != NULL, "The public key is in PEM form");

        size_t nSignature = 0;
        uint8_t *pSignature = XCrypt_RS256(data, sizeof(data), key.pPrivateKey, key.nPrivKeyLen, &nSignature);
        CHECK(pSignature != NULL, "Every key size signs");
        CHECK(nSignature == sizes[i].nSignature, "The signature is as wide as the modulus");
        CHECK(XCrypt_VerifyRS256(pSignature, nSignature, data, sizeof(data),
            key.pPublicKey, key.nPubKeyLen) == XSTDOK, "Every key size verifies its own signature");

        free(pSignature);
        XRSA_Destroy(&key);
    }
    return 0;
}

static int XTest_cross_key(void)
{
    /* A signature only verifies under the key that made it: two keys of the
     * same size must not be interchangeable. The size is irrelevant to that,
     * and key generation is the slowest thing in this file, so this uses the
     * smallest size the library will produce. */
    xrsa_ctx_t first, second;
    CHECK(XRSA_GenerateKeys(&first, 1024, 65537) == XSTDOK, "The first key generates");
    CHECK(XRSA_GenerateKeys(&second, 1024, 65537) == XSTDOK, "The second key generates");
    CHECK(strcmp(first.pPrivateKey, second.pPrivateKey) != 0, "Two generated keys differ");

    const uint8_t data[] = "signed by the first key";
    size_t nSignature = 0;
    uint8_t *pSignature = XCrypt_RS256(data, sizeof(data) - 1, first.pPrivateKey, first.nPrivKeyLen, &nSignature);
    CHECK(pSignature != NULL, "The message is signed");

    CHECK(XCrypt_VerifyRS256(pSignature, nSignature, data, sizeof(data) - 1,
        first.pPublicKey, first.nPubKeyLen) == XSTDOK, "The signing key verifies its own signature");
    CHECK(XCrypt_VerifyRS256(pSignature, nSignature, data, sizeof(data) - 1,
        second.pPublicKey, second.nPubKeyLen) != XSTDOK, "Another key does not verify it");

    /* A different message under the same key does not verify either. */
    const uint8_t other[] = "signed by the first keY";
    CHECK(XCrypt_VerifyRS256(pSignature, nSignature, other, sizeof(other) - 1,
        first.pPublicKey, first.nPubKeyLen) != XSTDOK, "A different message does not verify");

    free(pSignature);
    XRSA_Destroy(&second);
    XRSA_Destroy(&first);
    return 0;
}

static int XTest_key_import(void)
{
    /* A key exported as PEM has to come back in through the import path and
     * behave the same as the one it was exported from. */
    xrsa_ctx_t source;
    CHECK(XRSA_GenerateKeys(&source, 1024, 65537) == XSTDOK, "The source key generates");

    char *pPrivPem = strdup(source.pPrivateKey);
    char *pPubPem = strdup(source.pPublicKey);
    CHECK(pPrivPem != NULL && pPubPem != NULL, "The key is exported as PEM");

    const uint8_t data[] = "imported key round trip";
    size_t nSignature = 0;
    uint8_t *pSignature = XCrypt_RS256(data, sizeof(data) - 1, source.pPrivateKey, source.nPrivKeyLen, &nSignature);
    CHECK(pSignature != NULL, "The source key signs");
    XRSA_Destroy(&source);

    /* The imported public key verifies what the original private key signed. */
    xrsa_ctx_t imported;
    XRSA_Init(&imported);
    CHECK(XRSA_SetPubKey(&imported, pPubPem, strlen(pPubPem)) == XSTDOK, "The public key is imported");
    CHECK(XRSA_LoadPubKey(&imported) == XSTDOK, "The imported public key loads");

    CHECK(XCrypt_VerifyRS256(pSignature, nSignature, data, sizeof(data) - 1,
        pPubPem, strlen(pPubPem)) == XSTDOK, "The exported public key verifies the signature");
    XRSA_Destroy(&imported);

    /* The imported private key produces the same signature. */
    size_t nAgain = 0;
    uint8_t *pAgain = XCrypt_RS256(data, sizeof(data) - 1, pPrivPem, strlen(pPrivPem), &nAgain);
    CHECK(pAgain != NULL && nAgain == nSignature, "The exported private key signs the same width");
    CHECK(memcmp(pAgain, pSignature, nSignature) == 0, "RSA signing is deterministic for the same key and message");

    free(pAgain);
    free(pSignature);
    free(pPrivPem);
    free(pPubPem);
    return 0;
}

static int XTest_key_files(void)
{
    /* Keys are usually loaded from disk, so the file path has to work as
     * well as the in-memory one. */
    char sDir[] = "/tmp/xutils-rsa-XXXXXX";
    CHECK(mkdtemp(sDir) != NULL, "A private key directory is created");

    char sPriv[128], sPub[128];
    snprintf(sPriv, sizeof(sPriv), "%s/private.pem", sDir);
    snprintf(sPub, sizeof(sPub), "%s/public.pem", sDir);

    xrsa_ctx_t key;
    CHECK(XRSA_GenerateKeys(&key, 1024, 65537) == XSTDOK, "The key generates");

    FILE *pFile = fopen(sPriv, "wb");
    CHECK(pFile != NULL, "The private key file opens");
    fwrite(key.pPrivateKey, 1, key.nPrivKeyLen, pFile);
    fclose(pFile);

    pFile = fopen(sPub, "wb");
    CHECK(pFile != NULL, "The public key file opens");
    fwrite(key.pPublicKey, 1, key.nPubKeyLen, pFile);
    fclose(pFile);

    const uint8_t data[] = "loaded from a file";
    size_t nSignature = 0;
    uint8_t *pSignature = XCrypt_RS256(data, sizeof(data) - 1, key.pPrivateKey, key.nPrivKeyLen, &nSignature);
    CHECK(pSignature != NULL, "The generated key signs");
    XRSA_Destroy(&key);

    /* Both halves load back from disk. */
    xrsa_ctx_t loaded;
    XRSA_Init(&loaded);
    CHECK(XRSA_LoadKeyFiles(&loaded, sPriv, sPub) == XSTDOK, "Both key files load");
    CHECK(loaded.pPrivateKey != NULL && loaded.pPublicKey != NULL, "Both halves are present after loading");

    CHECK(XCrypt_VerifyRS256(pSignature, nSignature, data, sizeof(data) - 1,
        loaded.pPublicKey, loaded.nPubKeyLen) == XSTDOK, "The loaded public key verifies the signature");
    XRSA_Destroy(&loaded);

    /* Each half loads on its own too. */
    XRSA_Init(&loaded);
    CHECK(XRSA_LoadPrivKeyFile(&loaded, sPriv) == XSTDOK, "The private key file loads on its own");
    XRSA_Destroy(&loaded);

    XRSA_Init(&loaded);
    CHECK(XRSA_LoadPubKeyFile(&loaded, sPub) == XSTDOK, "The public key file loads on its own");
    XRSA_Destroy(&loaded);

    /* A file that is not there is an error, not a silent empty key. */
    XRSA_Init(&loaded);
    CHECK(XRSA_LoadPrivKeyFile(&loaded, "/no/such/key.pem") != XSTDOK, "A missing private key file is reported");
    CHECK(XRSA_LoadPubKeyFile(&loaded, "/no/such/key.pem") != XSTDOK, "A missing public key file is reported");
    XRSA_Destroy(&loaded);

    free(pSignature);
    unlink(sPriv);
    unlink(sPub);
    rmdir(sDir);
    return 0;
}

static int XTest_context(void)
{
    /* The context based encryption path has to round trip like the one-shot
     * helpers, and the two must agree. */
    xrsa_ctx_t key;
    CHECK(XRSA_GenerateKeys(&key, 2048, 65537) == XSTDOK, "The key generates");

    const uint8_t data[] = {'c', 't', 'x', 0x00, 0xfe, 'd'};
    size_t nCipher = 0;
    uint8_t *pCipher = XRSA_Crypt(&key, data, sizeof(data), &nCipher);
    CHECK(pCipher != NULL && nCipher == 256, "The context encrypts to the modulus width");

    size_t nPlain = 0;
    uint8_t *pPlain = XRSA_Decrypt(&key, pCipher, nCipher, &nPlain);
    CHECK(pPlain != NULL && nPlain == sizeof(data), "The context decrypts back to the original length");
    CHECK(memcmp(pPlain, data, sizeof(data)) == 0, "Every byte survives the context round trip");
    free(pPlain);
    free(pCipher);

    /* The private/public direction is the signing direction. */
    size_t nPrivOut = 0;
    uint8_t *pPrivCrypt = XRSA_PrivCrypt(&key, data, sizeof(data), &nPrivOut);
    CHECK(pPrivCrypt != NULL && nPrivOut == 256, "The private key encrypts to the modulus width");

    size_t nPubOut = 0;
    uint8_t *pPubPlain = XRSA_PubDecrypt(&key, pPrivCrypt, nPrivOut, &nPubOut);
    CHECK(pPubPlain != NULL && nPubOut == sizeof(data), "The public key decrypts it back");
    CHECK(memcmp(pPubPlain, data, sizeof(data)) == 0, "Every byte survives the reverse direction");
    free(pPubPlain);
    free(pPrivCrypt);

    /* Ciphertext that was tampered with does not decrypt to the message. */
    pCipher = XRSA_Crypt(&key, data, sizeof(data), &nCipher);
    CHECK(pCipher != NULL, "The message encrypts again");
    pCipher[10] ^= 0xff;

    nPlain = 0;
    pPlain = XRSA_Decrypt(&key, pCipher, nCipher, &nPlain);
    CHECK(pPlain == NULL || nPlain != sizeof(data) || memcmp(pPlain, data, sizeof(data)) != 0,
        "Tampered ciphertext does not decrypt to the original message");
    free(pPlain);
    free(pCipher);

    XRSA_Destroy(&key);
    return 0;
}

static int XTest_size_limits(void)
{
    /* RSA cannot encrypt more than the modulus allows once padding is
     * accounted for, and the refusal has to be reported. */
    xrsa_ctx_t key;
    CHECK(XRSA_GenerateKeys(&key, 2048, 65537) == XSTDOK, "The key generates");

    uint8_t sTooBig[400];
    memset(sTooBig, 'x', sizeof(sTooBig));

    size_t nCipher = 0;
    CHECK(XRSA_Crypt(&key, sTooBig, sizeof(sTooBig), &nCipher) == NULL,
        "A message past the padded modulus size is refused");

    /* The largest message that does fit still works. */
    uint8_t sAtLimit[245];
    memset(sAtLimit, 'y', sizeof(sAtLimit));
    uint8_t *pCipher = XRSA_Crypt(&key, sAtLimit, sizeof(sAtLimit), &nCipher);
    CHECK(pCipher != NULL, "The largest fitting message is accepted");

    size_t nPlain = 0;
    uint8_t *pPlain = XRSA_Decrypt(&key, pCipher, nCipher, &nPlain);
    CHECK(pPlain != NULL && nPlain == sizeof(sAtLimit), "The largest fitting message decrypts back");
    CHECK(memcmp(pPlain, sAtLimit, sizeof(sAtLimit)) == 0, "Every byte of it survives");
    free(pPlain);
    free(pCipher);

    /* An empty message is refused rather than producing an empty block. */
    CHECK(XRSA_Crypt(&key, sAtLimit, 0, &nCipher) == NULL, "An empty message is refused");
    CHECK(XRSA_Crypt(&key, NULL, 16, &nCipher) == NULL, "A missing message is refused");

    /* Signing has no such limit, since it hashes first. */
    size_t nSignature = 0;
    uint8_t *pSignature = XCrypt_RS256(sTooBig, sizeof(sTooBig), key.pPrivateKey, key.nPrivKeyLen, &nSignature);
    CHECK(pSignature != NULL && nSignature == 256, "Signing hashes first, so any length signs");
    CHECK(XCrypt_VerifyRS256(pSignature, nSignature, sTooBig, sizeof(sTooBig),
        key.pPublicKey, key.nPubKeyLen) == XSTDOK, "A long message verifies");
    free(pSignature);

    XRSA_Destroy(&key);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(signatures),
    XTEST_CASE(encryption),
    XTEST_CASE(invalid_keys),
    XTEST_CASE(key_sizes),
    XTEST_CASE(cross_key),
    XTEST_CASE(key_import),
    XTEST_CASE(key_files),
    XTEST_CASE(context),
    XTEST_CASE(size_limits)
)
