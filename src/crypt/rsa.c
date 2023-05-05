/*!
 *  @file libxutils/src/crypt/rsa.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief RSA implementation with openssl library
 */

#include "rsa.h"
#include "xfs.h"

#ifdef XCRYPT_USE_SSL
static const uint8_t g_shaPadding[XSHA256_PADDING_SIZE] =
    {
        0x30, 0x31, 0x30, 0x0d, 0x06,
        0x09, 0x60, 0x86, 0x48, 0x01,
        0x65, 0x03, 0x04, 0x02, 0x01,
        0x05, 0x00, 0x04, 0x20
    };

char* XSSL_LastErrors(size_t *pOutLen)
{
    if (pOutLen) *pOutLen = 0;

    BIO *pBIO = BIO_new(BIO_s_mem());
    XASSERT(pBIO, NULL);

    ERR_print_errors(pBIO);
    char *pErrBuff = NULL;

    int nErrLen = BIO_get_mem_data(pBIO, &pErrBuff);
    if (pErrBuff == NULL || nErrLen <= 0)
    {
        BIO_free(pBIO);
        return NULL;
    }

    char *pOutput = (char*)malloc(nErrLen + 1);
    if (pOutput == NULL)
    {
        BIO_free(pBIO);
        return NULL;
    }

    xstrncpy(pOutput, nErrLen + 1, pErrBuff);
    if (pOutLen) *pOutLen = nErrLen;

    BIO_free(pBIO);
    return pOutput;
}

void XRSA_Init(xrsa_ctx_t *pCtx)
{
    XASSERT_VOID(pCtx);
    pCtx->nPadding = RSA_PKCS1_PADDING;
    pCtx->nPrivKeyLen = 0;
    pCtx->nPubKeyLen = 0;

    pCtx->pPrivateKey = NULL;
    pCtx->pPublicKey = NULL;
    pCtx->pKeyPair = NULL;
}

void XRSA_Destroy(xrsa_ctx_t *pCtx)
{
    XASSERT_VOID(pCtx);
    RSA_free(pCtx->pKeyPair);
    free(pCtx->pPrivateKey);
    free(pCtx->pPublicKey);

    pCtx->pPrivateKey = NULL;
    pCtx->pPublicKey = NULL;
    pCtx->pKeyPair = NULL;

    pCtx->nPrivKeyLen = 0;
    pCtx->nPubKeyLen = 0;
}

XSTATUS XRSA_GenerateKeys(xrsa_ctx_t *pCtx, size_t nKeyLength, size_t nPubKeyExp)
{
    XASSERT(pCtx, XSTDINV);
    XRSA_Init(pCtx);

    BIGNUM *pBigNum = BN_new();
    XASSERT(pBigNum, XSTDERR);

    int nRetVal = BN_set_word(pBigNum, nPubKeyExp);
    if (nRetVal != XSTDOK)
    {
        BN_free(pBigNum);
        return XSTDERR;
    }

    RSA *pKeyPair = RSA_new();
    if (pKeyPair == NULL)
    {
        BN_free(pBigNum);
        return XSTDERR;
    }

    nRetVal = RSA_generate_key_ex(pKeyPair, nKeyLength, pBigNum, NULL);
    if (nRetVal != XSTDOK)
    {
        RSA_free(pKeyPair);
        BN_free(pBigNum);
        return XSTDERR;
    }

    BIO *pBioPriv = BIO_new(BIO_s_mem());
    if (pBioPriv == NULL)
    {
        RSA_free(pKeyPair);
        BN_free(pBigNum);
        return XSTDERR;
    }

    BIO *pBioPub = BIO_new(BIO_s_mem());
    if (pBioPriv == NULL)
    {
        RSA_free(pKeyPair);
        BIO_free(pBioPriv);
        BN_free(pBigNum);
        return XSTDERR;
    }

    if (PEM_write_bio_RSAPrivateKey(pBioPriv, pKeyPair, NULL, NULL, 0, NULL, NULL) != XSTDOK ||
        PEM_write_bio_RSAPublicKey(pBioPub, pKeyPair) != XSTDOK)
    {
        RSA_free(pKeyPair);
        BIO_free(pBioPriv);
        BIO_free(pBioPub);
        BN_free(pBigNum);
        return XSTDERR;
    }

    pCtx->nPrivKeyLen = BIO_pending(pBioPriv);
    pCtx->nPubKeyLen = BIO_pending(pBioPub);
    pCtx->pKeyPair = pKeyPair;

    pCtx->pPrivateKey = malloc(pCtx->nPrivKeyLen + 1);
    pCtx->pPublicKey = malloc(pCtx->nPubKeyLen + 1);

    if (pCtx->pPrivateKey == NULL || pCtx->pPublicKey == NULL)
    {
        XRSA_Destroy(pCtx);
        BIO_free(pBioPriv);
        BIO_free(pBioPub);
        BN_free(pBigNum);
        return XSTDERR;
    }

    int nRead = BIO_read(pBioPriv, pCtx->pPrivateKey, pCtx->nPrivKeyLen);
    if ((size_t)nRead != pCtx->nPrivKeyLen)
    {
        XRSA_Destroy(pCtx);
        BIO_free(pBioPriv);
        BIO_free(pBioPub);
        BN_free(pBigNum);
        return XSTDERR;
    }

    nRead = BIO_read(pBioPub, pCtx->pPublicKey, pCtx->nPubKeyLen);
    if ((size_t)nRead != pCtx->nPubKeyLen)
    {
        XRSA_Destroy(pCtx);
        BIO_free(pBioPriv);
        BIO_free(pBioPub);
        BN_free(pBigNum);
        return XSTDERR;
    }

    pCtx->pPrivateKey[pCtx->nPrivKeyLen] = '\0';
    pCtx->pPublicKey[pCtx->nPubKeyLen] = '\0';

    BIO_free(pBioPriv);
    BIO_free(pBioPub);
    BN_free(pBigNum);

    return XSTDOK;
}

uint8_t* XRSA_Crypt(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength)
{
    XASSERT((pCtx && pData && nLength), NULL);
    if (pOutLength) *pOutLength = 0;
    RSA *pRSA = pCtx->pKeyPair;

    int nRSASize = RSA_size(pRSA);
    XASSERT(nRSASize, NULL);

    uint8_t *pOutput = malloc(nRSASize + 1);
    XASSERT(pOutput, NULL);

    int nOutLength = RSA_public_encrypt(nLength, pData, pOutput, pRSA, pCtx->nPadding);
    XASSERT_FREE((nOutLength > 0 && nOutLength <= nRSASize), pOutput, NULL);

    if (pOutLength) *pOutLength = (size_t)nOutLength;
    pOutput[nOutLength] = '\0';

    return pOutput;
}

uint8_t* XRSA_PrivCrypt(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength)
{
    XASSERT((pCtx && pData && nLength), NULL);
    if (pOutLength) *pOutLength = 0;
    RSA *pRSA = pCtx->pKeyPair;

    int nRSASize = RSA_size(pRSA);
    XASSERT(nRSASize, NULL);

    uint8_t *pOutput = malloc(nRSASize + 1);
    XASSERT(pOutput, NULL);

    int nOutLength = RSA_private_encrypt(nLength, pData, pOutput, pRSA, pCtx->nPadding);
    XASSERT_FREE((nOutLength > 0 && nOutLength <= nRSASize), pOutput, NULL);

    if (pOutLength) *pOutLength = (size_t)nOutLength;
    pOutput[nOutLength] = '\0';

    return pOutput;
}

uint8_t* XRSA_PubDecrypt(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength)
{
    XASSERT((pCtx && pData && nLength), NULL);
    if (pOutLength) *pOutLength = 0;
    RSA *pRSA = pCtx->pKeyPair;

    int nRSASize = RSA_size(pRSA);
    XASSERT((nRSASize > 0), NULL);

    uint8_t *pOutput = malloc(nRSASize + 1);
    XASSERT(pOutput, NULL);

    int nOutLength = RSA_public_decrypt(nLength, pData, pOutput, pRSA, pCtx->nPadding);
    XASSERT_FREE((nOutLength > 0 && nOutLength <= nRSASize), pOutput, NULL);

    if (pOutLength) *pOutLength = (size_t)nOutLength;
    pOutput[nOutLength] = '\0';

    return pOutput;
}

uint8_t* XRSA_Decrypt(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength)
{
    XASSERT((pCtx && pData && nLength), NULL);
    if (pOutLength) *pOutLength = 0;
    RSA *pRSA = pCtx->pKeyPair;

    uint8_t *pOutput = malloc(nLength + 1);
    XASSERT(pOutput, NULL);

    int nOutLength = RSA_private_decrypt(nLength, pData, pOutput, pRSA, pCtx->nPadding);
    XASSERT_FREE((nOutLength > 0), pOutput, NULL);

    if (pOutLength) *pOutLength = (size_t)nOutLength;
    size_t nTermPos = (size_t)nOutLength < nLength ?
                    (size_t)nOutLength : nLength;

    pOutput[nTermPos] = '\0';
    return pOutput;
}

XSTATUS XRSA_LoadPrivKey(xrsa_ctx_t *pCtx)
{
    XASSERT((pCtx && pCtx->pPrivateKey && pCtx->nPrivKeyLen), XSTDINV);

    if (pCtx->pKeyPair == NULL)
    {
        pCtx->pKeyPair = RSA_new();
        XASSERT(pCtx->pKeyPair, XSTDERR);
    }

    BIO* pBIO = BIO_new_mem_buf((void*)pCtx->pPrivateKey, pCtx->nPrivKeyLen);
    XASSERT(pBIO, XSTDERR);

    RSA *pRSA = PEM_read_bio_RSAPrivateKey(pBIO, &pCtx->pKeyPair, NULL, NULL);
    BIO_free(pBIO);

    XASSERT(pRSA, XSTDERR);
    return XSTDOK;
}

XSTATUS XRSA_LoadPubKey(xrsa_ctx_t *pCtx)
{
    XASSERT((pCtx && pCtx->pPublicKey && pCtx->nPubKeyLen), XSTDINV);

    if (pCtx->pKeyPair == NULL)
    {
        pCtx->pKeyPair = RSA_new();
        XASSERT(pCtx->pKeyPair, XSTDERR);
    }

    BIO* pBIO = BIO_new_mem_buf((void*)pCtx->pPublicKey, pCtx->nPubKeyLen);
    XASSERT(pBIO, XSTDERR);

    RSA *pRSA = PEM_read_bio_RSAPublicKey(pBIO, &pCtx->pKeyPair, NULL, NULL);
    BIO_free(pBIO);

    XASSERT(pRSA, XSTDERR);
    return XSTDOK;
}

XSTATUS XRSA_SetPubKey(xrsa_ctx_t *pCtx, const char *pPubKey, size_t nLength)
{
    XASSERT((pCtx && pPubKey && nLength), XSTDINV);
    if (pCtx->pPublicKey) free(pCtx->pPublicKey);

    pCtx->pPublicKey = (char*)malloc(nLength + 1);
    XASSERT(pCtx->pPublicKey, XSTDERR);

    memcpy(pCtx->pPublicKey, pPubKey, nLength);
    pCtx->pPublicKey[nLength] = '\0';
    pCtx->nPubKeyLen = nLength;

    return XRSA_LoadPubKey(pCtx);
}

XSTATUS XRSA_SetPrivKey(xrsa_ctx_t *pCtx, const char *pPrivKey, size_t nLength)
{
    XASSERT((pCtx && pPrivKey && nLength), XSTDINV);
    if (pCtx->pPrivateKey) free(pCtx->pPrivateKey);

    pCtx->pPrivateKey = (char*)malloc(nLength + 1);
    XASSERT(pCtx->pPrivateKey, XSTDERR);

    memcpy(pCtx->pPrivateKey, pPrivKey, nLength);
    pCtx->pPrivateKey[nLength] = '\0';
    pCtx->nPrivKeyLen = nLength;

    return XRSA_LoadPrivKey(pCtx);
}

XSTATUS XRSA_LoadPubKeyFile(xrsa_ctx_t *pCtx, const char *pPath)
{
    XASSERT(pCtx && pPath, XSTDINV);
    if (pCtx->pPublicKey)
    {
        free(pCtx->pPublicKey);
        pCtx->pPublicKey = NULL;
        pCtx->nPubKeyLen = 0;
    }

    pCtx->pPublicKey = (char*)XPath_Load(pPath, &pCtx->nPubKeyLen);
    XASSERT(pCtx->pPublicKey, XSTDERR);

    return XRSA_LoadPubKey(pCtx);
}

XSTATUS XRSA_LoadPrivKeyFile(xrsa_ctx_t *pCtx, const char *pPath)
{
    XASSERT((pCtx && pPath), XSTDINV);
    if (pCtx->pPrivateKey)
    {
        free(pCtx->pPrivateKey);
        pCtx->pPrivateKey = NULL;
        pCtx->nPrivKeyLen = 0;
    }

    pCtx->pPrivateKey = (char*)XPath_Load(pPath, &pCtx->nPrivKeyLen);
    XASSERT(pCtx->pPrivateKey, XSTDERR);

    return XRSA_LoadPrivKey(pCtx);
}

XSTATUS XRSA_LoadKeyFiles(xrsa_ctx_t *pCtx, const char *pPrivPath, const char *pPubPath)
{
    XASSERT(pCtx, XSTDINV);
    XSTATUS nStatus = XSTDNON;

    if (pPrivPath != NULL) nStatus = XRSA_LoadPrivKeyFile(pCtx, pPrivPath);
    if (pPubPath != NULL) nStatus = XRSA_LoadPubKeyFile(pCtx, pPubPath);
    if (nStatus != XSTDOK) XRSA_Destroy(pCtx);

    return nStatus;
}

uint8_t* XCrypt_RSA(const uint8_t *pInput, size_t nLength, const char *pPubKey, size_t nKeyLen, size_t *pOutLen)
{
    XASSERT((pInput && nLength && pPubKey && nKeyLen), NULL);
    if (pOutLen) *pOutLen = 0;

    xrsa_ctx_t key;
    XRSA_Init(&key);

    XSTATUS nStat = XRSA_SetPubKey(&key, pPubKey, nKeyLen);
    if (nStat != XSTDOK)
    {
        XRSA_Destroy(&key);
        return NULL;
    }

    uint8_t *pCrypted = XRSA_Crypt(&key, pInput, nLength, pOutLen);
    XRSA_Destroy(&key);

    return pCrypted;
}

uint8_t* XDecrypt_RSA(const uint8_t *pInput, size_t nLength, const char *pPrivKey, size_t nKeyLen, size_t *pOutLen)
{
    XASSERT((pInput && nLength && pPrivKey && nKeyLen), NULL);
    if (pOutLen) *pOutLen = 0;

    xrsa_ctx_t key;
    XRSA_Init(&key);

    XSTATUS nStat = XRSA_SetPrivKey(&key, pPrivKey, nKeyLen);
    if (nStat != XSTDOK)
    {
        XRSA_Destroy(&key);
        return NULL;
    }

    uint8_t *pDecrypted = XRSA_Decrypt(&key, pInput, nLength, pOutLen);
    XRSA_Destroy(&key);

    return pDecrypted;
}

uint8_t* XCrypt_PrivRSA(const uint8_t *pInput, size_t nLength, const char *pPrivKey, size_t nKeyLen, size_t *pOutLen)
{
    XASSERT((pInput && nLength && pPrivKey && nKeyLen), NULL);
    if (pOutLen) *pOutLen = 0;

    xrsa_ctx_t key;
    XRSA_Init(&key);

    XSTATUS nStat = XRSA_SetPrivKey(&key, pPrivKey, nKeyLen);
    if (nStat != XSTDOK)
    {
        XRSA_Destroy(&key);
        return NULL;
    }

    uint8_t *pSignature = XRSA_PrivCrypt(&key, pInput, nLength, pOutLen);
    XRSA_Destroy(&key);

    return pSignature;
}

uint8_t* XDecrypt_PubRSA(const uint8_t *pInput, size_t nLength, const char *pPubKey, size_t nKeyLen, size_t *pOutLen)
{
    XASSERT((pInput && nLength && pPubKey && nKeyLen), NULL);
    if (pOutLen) *pOutLen = 0;

    xrsa_ctx_t key;
    XRSA_Init(&key);

    XSTATUS nStat = XRSA_SetPubKey(&key, pPubKey, nKeyLen);
    if (nStat != XSTDOK)
    {
        XRSA_Destroy(&key);
        return NULL;
    }

    uint8_t *pOutput = XRSA_PubDecrypt(&key, pInput, nLength, pOutLen);
    XRSA_Destroy(&key);

    return pOutput;
}

static XSTATUS XRSA_PadSHA256(uint8_t *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength)
{
    size_t nFinalSize = XSHA256_DIGEST_SIZE + XSHA256_PADDING_SIZE;
    XASSERT((pInput && nSize >= nFinalSize), XSTDERR);

    uint8_t hash[XSHA256_DIGEST_SIZE];
    XSHA256_Compute(hash, sizeof(hash), pInput, nLength);

    memcpy(pOutput, g_shaPadding, sizeof(g_shaPadding));
    memcpy(pOutput + sizeof(g_shaPadding), hash, sizeof(hash));
    return XSTDOK;
}

uint8_t* XCrypt_RS256(const uint8_t *pInput, size_t nLength, const char *pPrivKey, size_t nKeyLen, size_t *pOutLen)
{
    XASSERT((pInput && nLength), NULL);
    if (pOutLen) *pOutLen = 0;

    uint8_t paddingHash[XSHA256_DIGEST_SIZE + sizeof(g_shaPadding)];
    XRSA_PadSHA256(paddingHash, sizeof(paddingHash), pInput, nLength);
    return XCrypt_PrivRSA(paddingHash, sizeof(paddingHash), pPrivKey, nKeyLen, pOutLen);
}

XSTATUS XCrypt_VerifyRS256(const uint8_t *pSignature, size_t nSignatureLen, const uint8_t *pData, size_t nLength, const char *pPubKey, size_t nKeyLen)
{
    XASSERT((pData && nLength && pSignature && nSignatureLen && pPubKey && nKeyLen), XSTDINV);

    uint8_t inputHash[XSHA256_DIGEST_SIZE + XSHA256_PADDING_SIZE];
    XRSA_PadSHA256(inputHash, sizeof(inputHash), pData, nLength);
    size_t i, nHashLen = 0;

    uint8_t *pDecrypted = XDecrypt_PubRSA(pSignature, nSignatureLen, pPubKey, nKeyLen, &nHashLen);
    XASSERT_FREE((pDecrypted && nHashLen == sizeof(inputHash)), pDecrypted, XSTDEXC);

    for (i = 0; i < nHashLen; i++)
    {
        if (inputHash[i] != pDecrypted[i])
        {
            free(pDecrypted);
            return XSTDNON;
        }
    }

    free(pDecrypted);
    return XSTDOK;
}

#endif /* XCRYPT_USE_SSL */
////////////////////////////////////////////////////////////////
// END OF: RSA implementation with openssl library
////////////////////////////////////////////////////////////////