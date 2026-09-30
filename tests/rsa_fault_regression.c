/* Key generation and import failures must leave ownership recoverable. */
#include "test.h"
#include "rsa.h"

enum { RSA_FAULT_NONE, RSA_FAULT_BN, RSA_FAULT_WORD, RSA_FAULT_KEY, RSA_FAULT_GENERATE,
    RSA_FAULT_BIO, RSA_FAULT_PRIVATE, RSA_FAULT_PUBLIC, RSA_FAULT_ALLOC, RSA_FAULT_READ, RSA_FAULT_MEMORY };

static int g_nFault;
static int g_nCall;
static int g_nFailAt;
static int g_nHits;

static int rsa_fault(int nFault)
{
    if (g_nFault != nFault || ++g_nCall != g_nFailAt) return 0;
    g_nHits++;
    return 1;
}

static void rsa_arm(int nFault, int nCall)
{
    g_nFault = nFault;
    g_nCall = g_nHits = 0;
    g_nFailAt = nCall;
}

BIGNUM *__real_BN_new(void);
int __real_BN_set_word(BIGNUM*, BN_ULONG);
RSA *__real_RSA_new(void);
int __real_RSA_generate_key_ex(RSA*, int, BIGNUM*, BN_GENCB*);
BIO *__real_BIO_new(const BIO_METHOD*);
int __real_PEM_write_bio_RSAPrivateKey(BIO*, const RSA*, const EVP_CIPHER*, const unsigned char*, int, pem_password_cb*, void*);
int __real_PEM_write_bio_RSAPublicKey(BIO*, const RSA*);
int __real_BIO_read(BIO*, void*, int);
BIO *__real_BIO_new_mem_buf(const void*, int);
void *__real_malloc(size_t);

BIGNUM *__wrap_BN_new(void) { return rsa_fault(RSA_FAULT_BN) ? NULL : __real_BN_new(); }
int __wrap_BN_set_word(BIGNUM *pBN, BN_ULONG nValue) { return rsa_fault(RSA_FAULT_WORD) ? 0 : __real_BN_set_word(pBN, nValue); }
RSA *__wrap_RSA_new(void) { return rsa_fault(RSA_FAULT_KEY) ? NULL : __real_RSA_new(); }

int __wrap_RSA_generate_key_ex(RSA *pKey, int nBits, BIGNUM *pExponent, BN_GENCB *pCb)
{
    return rsa_fault(RSA_FAULT_GENERATE) ? 0 : __real_RSA_generate_key_ex(pKey, nBits, pExponent, pCb);
}

BIO *__wrap_BIO_new(const BIO_METHOD *pMethod) { return rsa_fault(RSA_FAULT_BIO) ? NULL : __real_BIO_new(pMethod); }

int __wrap_PEM_write_bio_RSAPrivateKey(BIO *pBio, const RSA *pRSA, const EVP_CIPHER *pCipher,
    const unsigned char *pKey, int nLength, pem_password_cb *pCb, void *pUser)
{
    return rsa_fault(RSA_FAULT_PRIVATE) ? 0 : __real_PEM_write_bio_RSAPrivateKey(pBio, pRSA, pCipher, pKey, nLength, pCb, pUser);
}

int __wrap_PEM_write_bio_RSAPublicKey(BIO *pBio, const RSA *pRSA)
{
    return rsa_fault(RSA_FAULT_PUBLIC) ? 0 : __real_PEM_write_bio_RSAPublicKey(pBio, pRSA);
}

int __wrap_BIO_read(BIO *pBio, void *pOut, int nLength)
{
    return rsa_fault(RSA_FAULT_READ) ? __real_BIO_read(pBio, pOut, nLength - 1) : __real_BIO_read(pBio, pOut, nLength);
}

BIO *__wrap_BIO_new_mem_buf(const void *pData, int nLength)
{
    return rsa_fault(RSA_FAULT_MEMORY) ? NULL : __real_BIO_new_mem_buf(pData, nLength);
}

void *__wrap_malloc(size_t nSize) { return rsa_fault(RSA_FAULT_ALLOC) ? NULL : __real_malloc(nSize); }

static int rsa_roundtrip(xrsa_ctx_t *pKey)
{
    const uint8_t data[] = {'k', 'e', 'y', 0, 0xff, 0x81};
    size_t nCrypt = 0, nPlain = 0;
    uint8_t *pCrypt = XRSA_Crypt(pKey, data, sizeof(data), &nCrypt);
    uint8_t *pPlain = pCrypt ? XRSA_Decrypt(pKey, pCrypt, nCrypt, &nPlain) : NULL;
    int nValid = pPlain && nPlain == sizeof(data) && !memcmp(pPlain, data, sizeof(data));
    free(pPlain);
    free(pCrypt);
    CHECK(nValid, "A recovered key encrypts and decrypts every byte of a binary message");
    return 0;
}

static int XTest_generate(void)
{
    for (int nFault = RSA_FAULT_BN; nFault <= RSA_FAULT_READ; nFault++)
        for (int nCall = 1; nCall <= ((nFault == RSA_FAULT_BIO || nFault >= RSA_FAULT_ALLOC) ? 2 : 1); nCall++)
        {
            xrsa_ctx_t key;
            rsa_arm(nFault, nCall);
            int nStatus = XRSA_GenerateKeys(&key, 1024, 65537);
            xbool_t bEmpty = !key.pKeyPair && !key.pPrivateKey && !key.pPublicKey && !key.nPrivKeyLen && !key.nPubKeyLen;
            int nHits = g_nHits;
            rsa_arm(RSA_FAULT_NONE, 0);
            XRSA_Destroy(&key);
            CHECK(nHits == 1 && nStatus == XSTDERR && bEmpty, "Every failed generation stage releases its partial key");
            CHECK(XRSA_GenerateKeys(&key, 1024, 65537) == XSTDOK, "The context can generate again after a failed attempt");
            int nResult = rsa_roundtrip(&key);
            XRSA_Destroy(&key);
            CHECK(nResult == 0, "The retry produces a usable key pair");
        }
    return 0;
}

static int XTest_import(void)
{
    xrsa_ctx_t source;
    CHECK(XRSA_GenerateKeys(&source, 1024, 65537) == XSTDOK, "Generate the source key");
    const int faults[] = {RSA_FAULT_ALLOC, RSA_FAULT_KEY, RSA_FAULT_MEMORY};
    for (int nPrivate = 0; nPrivate < 2; nPrivate++)
        for (size_t i = 0; i < sizeof(faults) / sizeof(*faults); i++)
        {
            xrsa_ctx_t key;
            XRSA_Init(&key);
            const char *pPEM = nPrivate ? source.pPrivateKey : source.pPublicKey;
            size_t nLength = nPrivate ? source.nPrivKeyLen : source.nPubKeyLen;
            rsa_arm(faults[i], 1);
            int nStatus = nPrivate ? XRSA_SetPrivKey(&key, pPEM, nLength) : XRSA_SetPubKey(&key, pPEM, nLength);
            int nHits = g_nHits;
            rsa_arm(RSA_FAULT_NONE, 0);
            CHECK(nStatus != XSTDOK && nHits == 1, "A failed key import cannot report success");
            nStatus = nPrivate ? XRSA_SetPrivKey(&key, pPEM, nLength) : XRSA_SetPubKey(&key, pPEM, nLength);
            CHECK(nStatus == XSTDOK, "Import retries on the same context after allocation or BIO failure");
            const uint8_t data[] = {'r', 0, 's', 0xff};
            size_t nCrypt = 0, nPlain = 0;
            uint8_t *pCrypt = XRSA_Crypt(nPrivate ? &source : &key, data, sizeof(data), &nCrypt);
            uint8_t *pPlain = pCrypt ? XRSA_Decrypt(nPrivate ? &key : &source, pCrypt, nCrypt, &nPlain) : NULL;
            xbool_t bValid = pPlain && nPlain == sizeof(data) && !memcmp(pPlain, data, sizeof(data));
            free(pPlain);
            free(pCrypt);
            XRSA_Destroy(&key);
            CHECK(bValid, "The retried import corresponds to the original key and exact payload");
        }
    XRSA_Destroy(&source);
    return 0;
}

static int XTest_replace(void)
{
    xrsa_ctx_t source;
    CHECK(XRSA_GenerateKeys(&source, 1024, 65537) == XSTDOK, "Generate the key replacement fixture");
    xbool_t bPreserved = XTRUE;
    for (int nPrivate = 0; nPrivate < 2 && bPreserved; nPrivate++)
    {
        xrsa_ctx_t key;
        XRSA_Init(&key);
        const char *pPEM = nPrivate ? source.pPrivateKey : source.pPublicKey;
        size_t nLength = nPrivate ? source.nPrivKeyLen : source.nPubKeyLen;
        CHECK((nPrivate ? XRSA_SetPrivKey(&key, pPEM, nLength) : XRSA_SetPubKey(&key, pPEM, nLength)) == XSTDOK,
            "Install the initial key");
        char *pOriginal = nPrivate ? key.pPrivateKey : key.pPublicKey;
        RSA *pOriginalPair = key.pKeyPair;
        rsa_arm(RSA_FAULT_ALLOC, 1);
        int nStatus = nPrivate ? XRSA_SetPrivKey(&key, pPEM, nLength) : XRSA_SetPubKey(&key, pPEM, nLength);
        int nHits = g_nHits;
        rsa_arm(RSA_FAULT_NONE, 0);
        char *pRetained = nPrivate ? key.pPrivateKey : key.pPublicKey;
        bPreserved = nStatus != XSTDOK && nHits == 1 && pRetained == pOriginal && key.pKeyPair == pOriginalPair &&
            (nPrivate ? key.nPrivKeyLen : key.nPubKeyLen) == nLength && pRetained && !memcmp(pRetained, pPEM, nLength + 1);
        if (bPreserved)
        {
            nStatus = nPrivate ? XRSA_SetPrivKey(&key, pRetained, nLength) : XRSA_SetPubKey(&key, pRetained, nLength);
            pRetained = nPrivate ? key.pPrivateKey : key.pPublicKey;
            bPreserved = nStatus == XSTDOK && pRetained && !memcmp(pRetained, pPEM, nLength + 1);
        }
        XRSA_Destroy(&key);
    }
    XRSA_Destroy(&source);
    CHECK(bPreserved, "PEM replacement preserves the original key on allocation failure and accepts its own buffer as input");
    return 0;
}

static int XTest_errors(void)
{
    ERR_clear_error();
    size_t nLength = 19;
    CHECK(XSSL_LastErrors(&nLength) == NULL && nLength == 0, "An empty error queue returns no text and resets its length");
    const int faults[] = {RSA_FAULT_BIO, RSA_FAULT_ALLOC};
    for (size_t i = 0; i < sizeof(faults) / sizeof(*faults); i++)
    {
        ERR_put_error(ERR_LIB_RSA, 0, ERR_R_MALLOC_FAILURE, __FILE__, __LINE__);
        rsa_arm(faults[i], 1);
        nLength = 19;
        char *pErrors = XSSL_LastErrors(&nLength);
        int nHits = g_nHits;
        rsa_arm(RSA_FAULT_NONE, 0);
        CHECK(!pErrors && !nLength && nHits == 1, "Failed error formatting has no partial output");
        ERR_clear_error();
    }
    ERR_put_error(ERR_LIB_RSA, 0, ERR_R_MALLOC_FAILURE, __FILE__, __LINE__);
    char *pErrors = XSSL_LastErrors(&nLength);
    xbool_t bValid = pErrors && nLength == strlen(pErrors) && strstr(pErrors, "malloc failure");
    free(pErrors);
    CHECK(bValid && !ERR_peek_error(), "Formatting recovers and drains the complete error queue");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(generate),
    XTEST_CASE(import),
    XTEST_CASE(replace),
    XTEST_CASE(errors)
)
