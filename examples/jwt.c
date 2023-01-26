/*!
 *  @file libxutils/examples/jwt.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2023  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Example file of working with JSON Web Tokens
 */

#include <xutils/xstd.h>
#include <xutils/xjson.h>
#include <xutils/xlog.h>
#include <xutils/jwt.h>
#include <xutils/xfs.h>

int main()
{
    xlog_defaults();
    xlog_timing(XLOG_TIME);

//////////////////////////////////////////////////////////////////////////////////////////////
// Create JWT from raw C strings
//////////////////////////////////////////////////////////////////////////////////////////////

    const char *pPayload = "{\"test\":\"value\"}";
    const char *pSecret = "myHiddenSecret";

    size_t nPayloadLen = strlen(pPayload);
    size_t nSecretLen = strlen(pSecret);
    size_t nHeaderLen = 0, nJWTLen = 0;

    char *pJWTStr = XJWT_Create(pPayload, nPayloadLen, (uint8_t*)pSecret, nSecretLen, &nJWTLen);
    if (pJWTStr == NULL)
    {
        xloge("Failed to create JWT: %s", strerror(errno));
        return -1;
    }

    xlog("Created JWT: %s", pJWTStr);

//////////////////////////////////////////////////////////////////////////////////////////////
// Validate and parse JWT string
//////////////////////////////////////////////////////////////////////////////////////////////

    xjwt_t jwt;
    XSTATUS nStatus = XJWT_Parse(&jwt, pJWTStr, nJWTLen, (uint8_t*)pSecret, nSecretLen);

    if (nStatus == XSTDERR) xloge("Failed to parse JWT: %s", strerror(errno));
    else if (nStatus == XSTDINV) xloge("Invalid arguments!");
    else if (nStatus == XSTDNON) xloge("Invalid signature!");

    char *pHeader = XJSON_DumpObj(jwt.pHeaderObj, 0, &nHeaderLen);
    if (pHeader != NULL)
    {
        xlog("Parsed header: %s", pHeader);
        free(pHeader);
    }

    char *pParsedPayload = XJSON_DumpObj(jwt.pPayloadObj, 0, &nPayloadLen);
    if (pParsedPayload != NULL)
    {
        xlog("Parsed payload: %s", pParsedPayload);
        free(pParsedPayload);
    }

    free(pJWTStr);

//////////////////////////////////////////////////////////////////////////////////////////////
// Re-create JWT again from JSON objects
//////////////////////////////////////////////////////////////////////////////////////////////

    pJWTStr = XJWT_Dump(&jwt, (uint8_t*)pSecret, nSecretLen, NULL);
    if (pJWTStr == NULL)
    {
        xloge("Failed to re-create JWT: %s", strerror(errno));
        return -1;
    }

    xlog("Re-created JWT: %s", pJWTStr);
    XJWT_Destroy(&jwt);
    free(pJWTStr);

//////////////////////////////////////////////////////////////////////////////////////////////
// Create new JWT from JSON objects
//////////////////////////////////////////////////////////////////////////////////////////////

    xjwt_t newJwt;
    newJwt.pHeaderObj = XJWT_CreateHeaderObj("HS256");
    newJwt.pPayloadObj = XJSON_NewObject(NULL, 0);

    XJSON_AddObject(newJwt.pPayloadObj, XJSON_NewString("sub", "1234567890"));
    XJSON_AddObject(newJwt.pPayloadObj, XJSON_NewString("name", "John Doe"));
    XJSON_AddObject(newJwt.pPayloadObj, XJSON_NewU32("iat", 1516239022));

    pJWTStr = XJWT_Dump(&newJwt, (uint8_t*)pSecret, nSecretLen, NULL);
    if (pJWTStr == NULL)
    {
        xloge("Failed to re-create JWT: %s", strerror(errno));
        return -1;
    }

    xlog("New JWT: %s", pJWTStr);
    XJWT_Destroy(&newJwt);
    free(pJWTStr);

    return 0;
}