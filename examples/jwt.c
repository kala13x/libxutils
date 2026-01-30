/*!
 *  @file libxutils/examples/jwt.c
 *
 *  This source is part of "libxutils" project
 *  2015-2023  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Example file of working with JSON Web Tokens
 */

#include "xstd.h"
#include "crypt.h"
#include "json.h"
#include "log.h"
#include "jwt.h"
#include "xfs.h"

int main()
{
    xlog_defaults();
    xlog_enable(XLOG_ALL);
    xlog_timing(XLOG_TIME);

//////////////////////////////////////////////////////////////////////////////////////////////
// Create JWT using HS256 signature
//////////////////////////////////////////////////////////////////////////////////////////////

    const char *pPayload = "{\"test\":\"value\"}";
    const char *pSecret = "myHiddenSecret";

    size_t nPayloadLen = strlen(pPayload);
    size_t nSecretLen = strlen(pSecret);
    size_t nJWTLen = 0;

    xjwt_t jwt;
    XJWT_Init(&jwt, XJWT_ALG_HS256);
    XJWT_AddPayload(&jwt, pPayload, nPayloadLen, XFALSE);

    char *pJWTStr = XJWT_Create(&jwt, (uint8_t*)pSecret, nSecretLen, &nJWTLen);
    if (pJWTStr == NULL)
    {
        xloge("Failed to create JWT: %s", XSTRERR);
        XJWT_Destroy(&jwt);
        return -1;
    }

    xlogi("Created HS256 JWT:\n%s\n", pJWTStr);
    XJWT_Destroy(&jwt);

//////////////////////////////////////////////////////////////////////////////////////////////
// Parse and verify JWT with HS256 signature
//////////////////////////////////////////////////////////////////////////////////////////////

    XSTATUS nStatus = XJWT_Parse(&jwt, pJWTStr, nJWTLen, (uint8_t*)pSecret, nSecretLen);
    if (nStatus != XSTDOK)
    {
        xloge("Failed to parse JWT%s", !jwt.bVerified ?
            ": Invalid JWT signature!" : XSTR_EMPTY);

        XJWT_Destroy(&jwt);
        free(pJWTStr);
        return -1;
    }

    char *pHeaderRaw = XJWT_GetHeader(&jwt, XTRUE, NULL);
    xlogi("Parsed JWT header: %s", pHeaderRaw);
    free(pHeaderRaw);

    char *pPayloadRaw = XJWT_GetPayload(&jwt, XTRUE, NULL);
    xlogi("Parsed JWT payload: %s\n", pPayloadRaw);
    free(pPayloadRaw);

    XJWT_Destroy(&jwt);
    free(pJWTStr);

#ifdef XCRYPT_USE_SSL
//////////////////////////////////////////////////////////////////////////////////////////////
// Create JWT using RS256 signature
//////////////////////////////////////////////////////////////////////////////////////////////

    xrsa_ctx_t pair;
    XRSA_Init(&pair);
    XRSA_GenerateKeys(&pair, XRSA_KEY_SIZE, XRSA_PUB_EXP);
    //XRSA_LoadKeyFiles(&pair, "./rsa_priv.pem", "./rsa_pub.pem");

    xlogi("Generated keys:\n%s\n%s", pair.pPrivateKey, pair.pPublicKey);

    XJWT_Init(&jwt, XJWT_ALG_RS256);
    XJWT_AddPayload(&jwt, pPayload, nPayloadLen, XFALSE);

    pJWTStr = XJWT_Create(&jwt, (uint8_t*)pair.pPrivateKey, pair.nPrivKeyLen, &nJWTLen);
    if (pJWTStr == NULL)
    {
        xloge("Failed to create JWT: %s", XSTRERR);
        XJWT_Destroy(&jwt);
        return -1;
    }

    xlogi("Created RS256 JWT:\n%s\n", pJWTStr);
    XJWT_Destroy(&jwt);

//////////////////////////////////////////////////////////////////////////////////////////////
// Parse and verify JWT with RS256 signature
//////////////////////////////////////////////////////////////////////////////////////////////

    nStatus = XJWT_Parse(&jwt, pJWTStr, nJWTLen, (uint8_t*)pair.pPublicKey, pair.nPubKeyLen);
    if (nStatus != XSTDOK)
    {
        xloge("Failed to parse JWT%s", !jwt.bVerified ?
            ": Signature is not verified" : XSTR_EMPTY);

        char *pSSLErrors = XSSL_LastErrors(NULL);
        if (pSSLErrors != NULL)
        {
            printf("%s\n", pSSLErrors);
            free(pSSLErrors);
        }

        XRSA_Destroy(&pair);
        XJWT_Destroy(&jwt);
        free(pJWTStr);
        return -1;
    }

    pHeaderRaw = XJWT_GetHeader(&jwt, XTRUE, NULL);
    xlogi("Parsed JWT header: %s", pHeaderRaw);
    free(pHeaderRaw);

    pPayloadRaw = XJWT_GetPayload(&jwt, XTRUE, NULL);
    xlogi("Parsed JWT payload: %s\n", pPayloadRaw);
    free(pPayloadRaw);

    XRSA_Destroy(&pair);
    XJWT_Destroy(&jwt);
    free(pJWTStr);
#endif

    return 0;
}
