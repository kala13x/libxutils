/* libxutils: RSA key import, encryption, signature integrity and malformed keys. */
#include "test.h"
#include "rsa.h"
#include "xfs.h"
#include <limits.h>
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
    nLength = 123;
    CHECK(XDecrypt_RSA((const uint8_t*)"x", 1, "invalid key", 11, &nLength) == NULL && !nLength,
        "Malformed decryption keys return no plaintext and reset the output length");
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


static int XTest_build_support(void)
{
    /* Whether RSA is available at all is a build-time answer, and every
     * other entry point in this module depends on it. A caller has to be
     * able to ask before it reaches for a key. */
    int nHave = XRSA_HaveSSL();
    CHECK(nHave == 0 || nHave == 1, "The support flag is a plain yes or no");

    if (!nHave)
    {
        /* Without support, nothing may quietly succeed. */
        xrsa_ctx_t key;
        XRSA_Init(&key);
        CHECK(XRSA_GenerateKeys(&key, 2048, 65537) != XSTDOK, "No key can be generated without support");
        XRSA_Destroy(&key);
        return 0;
    }

    /* With support, the smallest key the library will make still works
     * end to end, which is what the rest of this file assumes. */
    xrsa_ctx_t key;
    XRSA_Init(&key);
    CHECK(key.pPrivateKey == NULL && key.pPublicKey == NULL, "A fresh context holds no keys");
    CHECK(key.nPrivKeyLen == 0 && key.nPubKeyLen == 0, "A fresh context holds no lengths");

    CHECK(XRSA_GenerateKeys(&key, 1024, 65537) == XSTDOK, "A key is generated");
    CHECK(key.pPrivateKey != NULL && key.nPrivKeyLen > 0, "The private key is exported");
    CHECK(key.pPublicKey != NULL && key.nPubKeyLen > 0, "The public key is exported");
    CHECK(strstr(key.pPrivateKey, "PRIVATE KEY") != NULL, "The private key is PEM encoded");
    CHECK(strstr(key.pPublicKey, "PUBLIC KEY") != NULL, "The public key is PEM encoded");
    CHECK(strstr(key.pPublicKey, "PRIVATE") == NULL, "The public key carries no private material");

    XRSA_Destroy(&key);
    CHECK(key.pPrivateKey == NULL && key.pPublicKey == NULL, "Destroying releases both keys");
    return 0;
}

static int XTest_short_ciphertext(void)
{
    /* A ciphertext shorter than the modulus still decrypts to a block as
       wide as the modulus: without padding the value one decrypts to 255
       zero bytes and a one. The output has room for that whatever the input
       length is. */
    xrsa_ctx_t key;
    CHECK(XRSA_GenerateKeys(&key, 2048, 65537) == XSTDOK, "Generate a key");
    key.nPadding = RSA_NO_PADDING;

    const uint8_t one[] = { 0x01 };
    size_t nPlain = 0;
    uint8_t *pPlain = XRSA_Decrypt(&key, one, sizeof(one), &nPlain);
    CHECK(pPlain != NULL && nPlain == 256, "A one byte ciphertext decrypts to a full block");

    int bZeros = 1;
    for (size_t i = 0; i < 255; i++) if (pPlain[i] != 0) bZeros = 0;
    CHECK(bZeros && pPlain[255] == 0x01 && pPlain[256] == '\0', "The block is the value one, terminated");
    free(pPlain);

    /* A padded decrypt of a short input is refused or, where OpenSSL rejects
       implicitly, answered with a synthetic message; either fits the output */
    key.nPadding = RSA_PKCS1_PADDING;
    const uint8_t shortInput[] = { 0x00, 0x02, 0x11, 0x22 };
    nPlain = 7;
    pPlain = XRSA_Decrypt(&key, shortInput, sizeof(shortInput), &nPlain);
    CHECK(pPlain == NULL ? nPlain == 0 : (nPlain <= 256 - 11 && pPlain[nPlain] == '\0'),
        "A short padded ciphertext never yields more than the modulus holds");
    free(pPlain);
    XRSA_Destroy(&key);

    /* A context without a key has nothing to work with */
    xrsa_ctx_t empty;
    XRSA_Init(&empty);
    size_t nOut = 7;
    CHECK(XRSA_Crypt(&empty, one, sizeof(one), &nOut) == NULL && nOut == 0, "Encryption without a key is refused");
    CHECK(XRSA_PrivCrypt(&empty, one, sizeof(one), &nOut) == NULL, "Signing without a key is refused");
    CHECK(XRSA_PubDecrypt(&empty, one, sizeof(one), &nOut) == NULL, "Verifying without a key is refused");
    CHECK(XRSA_Decrypt(&empty, one, sizeof(one), &nOut) == NULL, "Decryption without a key is refused");
    XRSA_Destroy(&empty);
    return 0;
}

static int XTest_length_overflow(void)
{
#if SIZE_MAX <= UINT_MAX
    return 77;
#else
    xrsa_ctx_t key;
    CHECK(XRSA_GenerateKeys(&key, 1024, 65537) == XSTDOK, "Generate the integer-boundary key");
    key.nPadding = RSA_NO_PADDING;
    const uint8_t one = 1;
    const size_t lengths[] = {(size_t)UINT_MAX + 2, (size_t)INT_MAX + 1, (size_t)UINT_MAX + 129, SIZE_MAX};
    uint8_t *(*operations[])(xrsa_ctx_t*, const uint8_t*, size_t, size_t*) = {
        XRSA_Crypt, XRSA_PrivCrypt, XRSA_PubDecrypt, XRSA_Decrypt
    };
    for (size_t i = 0; i < sizeof(operations) / sizeof(*operations); i++)
        for (size_t j = 0; j < sizeof(lengths) / sizeof(*lengths); j++)
        {
            size_t nOutput = 17;
            uint8_t *pResult = operations[i](&key, &one, lengths[j], &nOutput);
            xbool_t bRejected = !pResult && !nOutput;
            free(pResult);
            if (!bRejected) XRSA_Destroy(&key);
            CHECK(bRejected, "RSA input lengths cannot wrap to a smaller message at the OpenSSL boundary");
        }
    XRSA_Destroy(&key);
    return 0;
#endif
}

static int XTest_key_length_overflow(void)
{
    xrsa_ctx_t key;
    XRSA_Init(&key);
    CHECK(XRSA_SetPubKey(&key, "x", SIZE_MAX) != XSTDOK && !key.pPublicKey && !key.nPubKeyLen,
        "An overflowing public PEM length is rejected before allocation or copying");
    CHECK(XRSA_SetPrivKey(&key, "x", SIZE_MAX) != XSTDOK && !key.pPrivateKey && !key.nPrivKeyLen,
        "An overflowing private PEM length is rejected before allocation or copying");
    CHECK(XRSA_GenerateKeys(&key, 1024, 65537) == XSTDOK, "Create a key for direct import length checks");
    size_t nPublic = key.nPubKeyLen, nPrivate = key.nPrivKeyLen;
    key.nPubKeyLen = (size_t)INT_MAX + 1;
    key.nPrivKeyLen = (size_t)INT_MAX + 1;
    int nPublicStatus = XRSA_LoadPubKey(&key), nPrivateStatus = XRSA_LoadPrivKey(&key);
    key.nPubKeyLen = nPublic;
    key.nPrivKeyLen = nPrivate;
    XRSA_Destroy(&key);
    CHECK(nPublicStatus != XSTDOK && nPrivateStatus != XSTDOK, "PEM import lengths must fit the BIO integer parameter");
#if SIZE_MAX > UINT_MAX
    int nStatus = XRSA_GenerateKeys(&key, (size_t)UINT_MAX + 1025, 65537);
    XRSA_Destroy(&key);
    CHECK(nStatus != XSTDOK, "A requested key size must not wrap into a smaller RSA key");
#endif
    return 0;
}

static int XTest_partial_key_files(void)
{
    char root[] = "/tmp/xutils-rsa-files-XXXXXX";
    CHECK(mkdtemp(root) != NULL, "Create the key reload directory");
    char priv[128], pub[128], bad[128], missing[128];
    snprintf(priv, sizeof(priv), "%s/private.pem", root);
    snprintf(pub, sizeof(pub), "%s/public.pem", root);
    snprintf(bad, sizeof(bad), "%s/invalid.pem", root);
    snprintf(missing, sizeof(missing), "%s/missing.pem", root);
    xrsa_ctx_t source, key;
    CHECK(XRSA_GenerateKeys(&source, 1024, 65537) == XSTDOK, "Generate a matching file pair");
    CHECK(XPath_Write(priv, (const uint8_t*)source.pPrivateKey, source.nPrivKeyLen, "cwt") == (int)source.nPrivKeyLen &&
        XPath_Write(pub, (const uint8_t*)source.pPublicKey, source.nPubKeyLen, "cwt") == (int)source.nPubKeyLen &&
        XPath_Write(bad, (const uint8_t*)"invalid key", 11, "cwt") == 11, "Write complete and malformed key files");
    struct { const char *pPrivate; const char *pPublic; xbool_t bValid; } cases[] = {
        {missing, pub, XFALSE}, {bad, pub, XFALSE}, {priv, missing, XFALSE}, {priv, bad, XFALSE},
        {missing, missing, XFALSE}, {bad, bad, XFALSE}, {priv, pub, XTRUE}, {priv, NULL, XTRUE}, {NULL, pub, XTRUE}
    };
    xbool_t bValid = XTRUE;
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
    {
        XRSA_Init(&key);
        int nStatus = XRSA_LoadKeyFiles(&key, cases[i].pPrivate, cases[i].pPublic);
        if (cases[i].bValid)
        {
            if (nStatus != XSTDOK || !key.pKeyPair) bValid = XFALSE;
            if (cases[i].pPrivate && (!key.pPrivateKey || strcmp(key.pPrivateKey, source.pPrivateKey))) bValid = XFALSE;
            if (cases[i].pPublic && (!key.pPublicKey || strcmp(key.pPublicKey, source.pPublicKey))) bValid = XFALSE;
        }
        else if (nStatus == XSTDOK || key.pKeyPair || key.pPrivateKey || key.pPublicKey || key.nPrivKeyLen || key.nPubKeyLen)
            bValid = XFALSE;
        XRSA_Destroy(&key);
    }
    XRSA_Init(&key);
    CHECK(XRSA_LoadKeyFiles(&key, priv, pub) == XSTDOK, "Load the initial key pair");
    CHECK(XRSA_LoadKeyFiles(&key, priv, pub) == XSTDOK, "Reload both PEM buffers on the same context");
    const uint8_t data[] = {'f', 0, 'i', 0xff};
    size_t nCipher = 0, nPlain = 0;
    uint8_t *pCipher = XRSA_Crypt(&key, data, sizeof(data), &nCipher);
    uint8_t *pPlain = pCipher ? XRSA_Decrypt(&source, pCipher, nCipher, &nPlain) : NULL;
    xbool_t bRoundtrip = pPlain && nPlain == sizeof(data) && !memcmp(pPlain, data, sizeof(data));
    free(pPlain);
    free(pCipher);
    XRSA_Destroy(&key);
    XRSA_Destroy(&source);
    unlink(priv);
    unlink(pub);
    unlink(bad);
    rmdir(root);
    CHECK(bValid, "Loading a requested key pair fails if either member is missing or malformed and clears partial state");
    CHECK(bRoundtrip, "Repeated file import retains the exact key identity and binary payload");
    return 0;
}

typedef uint8_t* (*rsa_ctx_op_t)(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength);
typedef uint8_t* (*rsa_key_op_t)(const uint8_t *pInput, size_t nLength, const char *pKey, size_t nKeyLen, size_t *pOutLen);

static int XTest_call_guards(void)
{
    const uint8_t sData[] = "data";
    const char *pKey = "key";
    size_t nLength = 1;
    xrsa_ctx_t key;

    XRSA_Init(NULL);
    XRSA_Destroy(NULL);
    CHECK(XRSA_GenerateKeys(NULL, 1024, 65537) == XSTDINV, "Keys are generated into a context");

    /* Without a key every operation of a context does nothing, whatever it is given */
    const rsa_ctx_op_t ctxOps[] = { XRSA_Crypt, XRSA_Decrypt, XRSA_PrivCrypt, XRSA_PubDecrypt };
    XRSA_Init(&key);

    for (size_t i = 0; i < sizeof(ctxOps) / sizeof(*ctxOps); i++)
    {
        CHECK(ctxOps[i](NULL, sData, 4, &nLength) == NULL && ctxOps[i](&key, NULL, 4, &nLength) == NULL &&
            ctxOps[i](&key, sData, 0, &nLength) == NULL, "An operation needs a context and data");
        CHECK(ctxOps[i](&key, sData, (size_t)INT_MAX + 1, &nLength) == NULL && !nLength, "Data an int counts");
        CHECK(ctxOps[i](&key, sData, 4, NULL) == NULL, "And a key");
    }

    CHECK(XRSA_LoadPrivKey(NULL) == XSTDINV && XRSA_LoadPrivKey(&key) == XSTDINV, "No private key text loads nothing");
    CHECK(XRSA_LoadPubKey(NULL) == XSTDINV && XRSA_LoadPubKey(&key) == XSTDINV, "No public key text loads nothing");
    CHECK(XRSA_SetPubKey(NULL, pKey, 3) == XSTDINV && XRSA_SetPubKey(&key, NULL, 3) == XSTDINV &&
        XRSA_SetPubKey(&key, pKey, 0) == XSTDINV && XRSA_SetPubKey(&key, pKey, (size_t)INT_MAX + 1) == XSTDINV,
        "A public key is set from text");
    CHECK(XRSA_SetPrivKey(NULL, pKey, 3) == XSTDINV && XRSA_SetPrivKey(&key, NULL, 3) == XSTDINV &&
        XRSA_SetPrivKey(&key, pKey, 0) == XSTDINV && XRSA_SetPrivKey(&key, pKey, (size_t)INT_MAX + 1) == XSTDINV,
        "So is a private key");
    CHECK(XRSA_LoadPubKeyFile(NULL, "/x") == XSTDINV && XRSA_LoadPubKeyFile(&key, NULL) == XSTDINV &&
        XRSA_LoadPubKeyFile(NULL, NULL) == XSTDINV, "A public key file needs a context and a path");
    CHECK(XRSA_LoadPrivKeyFile(NULL, "/x") == XSTDINV && XRSA_LoadPrivKeyFile(&key, NULL) == XSTDINV,
        "So does a private one");
    CHECK(XRSA_LoadKeyFiles(NULL, "/x", "/y") == XSTDINV, "And both");

    /* The one call forms check what they are given the same way */
    const rsa_key_op_t keyOps[] = { XCrypt_RSA, XDecrypt_RSA, XCrypt_PrivRSA, XDecrypt_PubRSA };
    for (size_t i = 0; i < sizeof(keyOps) / sizeof(*keyOps); i++)
    {
        CHECK(keyOps[i](NULL, 4, pKey, 3, &nLength) == NULL && keyOps[i](sData, 0, pKey, 3, &nLength) == NULL &&
            keyOps[i](sData, 4, NULL, 3, &nLength) == NULL && keyOps[i](sData, 4, pKey, 0, &nLength) == NULL,
            "A one call operation needs data and a key");
        CHECK(keyOps[i](sData, 4, pKey, 3, NULL) == NULL, "A key that is not one is refused");
    }

    CHECK(XCrypt_RS256(NULL, 4, pKey, 3, &nLength) == NULL && XCrypt_RS256(sData, 0, pKey, 3, &nLength) == NULL,
        "Signing needs data");
    CHECK(XCrypt_VerifyRS256(NULL, 4, sData, 4, pKey, 3) == XSTDINV && XCrypt_VerifyRS256(sData, 0, sData, 4, pKey, 3) == XSTDINV,
        "Verifying needs a signature");
    CHECK(XCrypt_VerifyRS256(sData, 4, NULL, 4, pKey, 3) == XSTDINV && XCrypt_VerifyRS256(sData, 4, sData, 0, pKey, 3) == XSTDINV,
        "And data");
    CHECK(XCrypt_VerifyRS256(sData, 4, sData, 4, NULL, 3) == XSTDINV, "And a key");
    CHECK(XCrypt_VerifyRS256(sData, 4, sData, 4, pKey, 0) == XSTDINV, "Of some length");

    ERR_clear_error();
    CHECK(XSSL_LastErrors(NULL) == NULL, "No error is no report");

    /* With a key, every operation can leave its length unasked */
    CHECK(XRSA_GenerateKeys(&key, 1024, 65537) == XSTDOK, "Generate a key");
    uint8_t *pCipher = XRSA_Crypt(&key, sData, 4, NULL);
    CHECK(pCipher != NULL, "Encrypt without asking the length");
    uint8_t *pPlain = XRSA_Decrypt(&key, pCipher, 128, NULL);
    CHECK(pPlain != NULL && !memcmp(pPlain, sData, 4), "Decrypt without asking it");
    free(pCipher);
    free(pPlain);

    pCipher = XRSA_PrivCrypt(&key, sData, 4, NULL);
    CHECK(pCipher != NULL, "Sign without asking the length");
    pPlain = XRSA_PubDecrypt(&key, pCipher, 128, NULL);
    CHECK(pPlain != NULL && !memcmp(pPlain, sData, 4), "Recover without asking it");
    free(pCipher);
    free(pPlain);

    /* No path to load a public key from leaves the one that is there */
    char *pPublic = key.pPublicKey;
    CHECK(XRSA_LoadPubKeyFile(&key, NULL) == XSTDINV && key.pPublicKey == pPublic && key.nPubKeyLen > 0,
        "A missing path does not drop the public key");

    xrsa_ctx_t other;
    XRSA_Init(&other);
    CHECK(XRSA_SetPrivKey(&other, "not a key", 9) == XSTDERR, "Text that is not a key is refused");
    char *pErrors = XSSL_LastErrors(NULL);
    CHECK(pErrors != NULL, "And leaves an error to report without its length");
    free(pErrors);

    XRSA_Destroy(&other);
    XRSA_Destroy(&key);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(call_guards),
    XTEST_CASE(signatures),
    XTEST_CASE(encryption),
    XTEST_CASE(invalid_keys),
    XTEST_CASE(key_sizes),
    XTEST_CASE(cross_key),
    XTEST_CASE(key_import),
    XTEST_CASE(key_files),
    XTEST_CASE(context),
    XTEST_CASE(size_limits),
    XTEST_CASE(build_support),
    XTEST_CASE(short_ciphertext),
    XTEST_CASE(length_overflow),
    XTEST_CASE(key_length_overflow),
    XTEST_CASE(partial_key_files)
)
