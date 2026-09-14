/* libxutils: bounded parser fuzzing with cleanup on both success and rejection. */
#include "xstd.h"
#include "log.h"
#include "json.h"
#include "http.h"
#include "ws.h"
#include "jwt.h"
#include "base64.h"
#include "addr.h"

int LLVMFuzzerTestOneInput(const uint8_t *pData, size_t nSize)
{
    xlog_setfl(0);
    if (nSize == 0 || nSize > 65536) return 0;
    uint8_t nTarget = pData[0] % 6;
    pData++;
    nSize--;
    if (nTarget == 0)
    {
        xjson_t json;
        XJSON_Parse(&json, NULL, (const char*)pData, nSize);
        XJSON_Destroy(&json);
    }
    else if (nTarget == 1)
    {
        xhttp_t http;
        XHTTP_ParseData(&http, (uint8_t*)pData, nSize);
        size_t nExtra = XHTTP_GetExtraSize(&http);
        const uint8_t *pExtra = XHTTP_GetExtraData(&http);
        if (pExtra && nExtra)
        {
            uintptr_t nStart = (uintptr_t)http.rawData.pData;
            if ((uintptr_t)pExtra < nStart || (uintptr_t)pExtra - nStart > http.rawData.nUsed ||
                nExtra > http.rawData.nUsed - ((uintptr_t)pExtra - nStart))
                abort();
        }
        XHTTP_Clear(&http);
    }
    else if (nTarget == 2)
    {
        xws_frame_t frame;
        XWebFrame_ParseData(&frame, (uint8_t*)pData, nSize);
        XWebFrame_Clear(&frame);
    }
    else if (nTarget == 3)
    {
        size_t nLength = nSize;
        char *pDecoded = XBase64_Decrypt(pData, &nLength);
        free(pDecoded);
        nLength = nSize;
        pDecoded = XBase64_UrlDecrypt(pData, &nLength);
        free(pDecoded);
    }
    else if (nTarget == 4)
    {
        xjwt_t jwt;
        XJWT_Init(&jwt, XJWT_ALG_HS256);
        XJWT_Parse(&jwt, (const char*)pData, nSize, NULL, 0);
        XJWT_Destroy(&jwt);
    }
    else
    {
        char *pURL = (char*)malloc(nSize + 1);
        if (pURL == NULL) return 0;
        memcpy(pURL, pData, nSize);
        pURL[nSize] = 0;
        xlink_t link;
        XLink_Parse(&link, pURL);
        free(pURL);
    }
    return 0;
}
