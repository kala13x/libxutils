/*!
 *  @file libxutils/examples/jwt.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2023  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Example file of working with JSON Web Tokens
 */

#include <xutils/xstd.h>
#include <xutils/crypt.h>
#include <xutils/xjson.h>
#include <xutils/xlog.h>
#include <xutils/jwt.h>
#include <xutils/xfs.h>

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
        xloge("Failed to create JWT: %s", strerror(errno));
        XJWT_Destroy(&jwt);
        return -1;
    }

    xlogi("Created HS256 JWT:\n%s\n", pJWTStr);

    XJWT_Destroy(&jwt);
    free(pJWTStr);

//////////////////////////////////////////////////////////////////////////////////////////////
// Create JWT using RS256 signature
//////////////////////////////////////////////////////////////////////////////////////////////
#ifdef XCRYPT_USE_SSL
    xrsa_ctx_t pair;
    XRSA_Init(&pair);
    XRSA_GenerateKeys(&pair, XRSA_KEY_SIZE, XRSA_PUB_EXP);

    xlogi("Generated keys:\n%s\n%s", pair.pPrivateKey, pair.pPublicKey);

    XJWT_Init(&jwt, XJWT_ALG_RS256);
    XJWT_AddPayload(&jwt, pPayload, nPayloadLen, XFALSE);

    pJWTStr = XJWT_Create(&jwt, (uint8_t*)pair.pPrivateKey, pair.nPrivKeyLen, &nJWTLen);
    if (pJWTStr == NULL)
    {
        xloge("Failed to create JWT: %s", strerror(errno));
        XJWT_Destroy(&jwt);
        return -1;
    }

    xlogi("Created RS256 JWT:\n%s\n", pJWTStr);

    XRSA_Destroy(&pair);
    XJWT_Destroy(&jwt);
    free(pJWTStr);
#endif

    return 0;
}