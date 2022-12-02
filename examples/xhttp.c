/*!
 *  @file libxutils/examples/xhttp.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Example file for working with the HTTP request/responses.
 * Send custom HTTP request, analyze headers, download content, etc.
 */

#include <xutils/xstd.h>
#include <xutils/sock.h>
#include <xutils/http.h>
#include <xutils/xlog.h>
#include <xutils/xstr.h>
#include <xutils/xver.h>
#include <xutils/xcli.h>
#include <xutils/xtime.h>
#include <xutils/xfs.h>

extern char *optarg;

#define XHTTP_VERSION_MAJ   0
#define XHTTP_VERSION_MIN   6

#define XHTTP_INTERVAL_SEC  1

typedef struct xhttp_args_ {
    xhttp_method_t eMethod;
    xcli_bar_t progressBar;
    xfile_t *pOutputFile;

    xtime_t lastTime;
    xbool_t nAutoFollow;
    xbool_t nForceWrite;
    xbool_t nDownload;
    xbool_t nVerbose;
    xbool_t nSSL;
    size_t nTimeout;
    size_t nBytes;
    size_t nDone;

    xbyte_buffer_t content;
    char sAddress[XHTTP_URL_MAX];
    char sHeaders[XLINE_MAX];
    char sContent[XPATH_MAX];
    char sOutput[XPATH_MAX];
    char sSpeed[XSTR_MIN];
} xhttp_args_t;

static char *XHTTPApp_WhiteSpace(const int nLength)
{
    static char sRetVal[XHTTP_FIELD_MAX];
    xstrnul(sRetVal);
    int i = 0;

    int nLen = XSTD_MIN(nLength, sizeof(sRetVal) - 1);
    for (i = 0; i < nLen; i++) sRetVal[i] = ' ';

    sRetVal[i] = '\0';
    return sRetVal;
}

void XHTTPApp_DisplayUsage(const char *pName)
{
    int nLength = strlen(pName) + 6;

    printf("==========================================================================\n");
    printf(" XHTTP tool v%d.%d - (c) 2022 Sandro Kalatozishvili (f4tb0y@protonmail.com)\n",
        XHTTP_VERSION_MAJ, XHTTP_VERSION_MIN);
    printf("==========================================================================\n");

    printf("Usage: %s [-l <address>] [-c <content>] [-m <method>] [-d] [-f] [-s]\n", pName);
    printf(" %s [-t <seconds>] [-o <output>] [-x <headers>] [-v] [-w] [-h]\n", XHTTPApp_WhiteSpace(nLength));

    printf("Options are:\n");
    printf("  -l <address>          # HTTP/S address/link (%s*%s)\n", XSTR_CLR_RED, XSTR_FMT_RESET);
    printf("  -c <content>          # Content file path\n");
    printf("  -m <method>           # HTTP request method\n");
    printf("  -o <output>           # Output file path\n");
    printf("  -t <seconds>          # Receive timeout (sec)\n");
    printf("  -x <headers>          # Custom HTTP headers\n");
    printf("  -d                    # Download output as a file\n");
    printf("  -f                    # Follow redirected locations\n");
    printf("  -s                    # Force SSL connection\n");
    printf("  -v                    # Enable verbose logging\n");
    printf("  -w                    # Force overwrite output\n");
    printf("  -h                    # Print version and usage\n\n");
    printf("Examples:\n");
    printf("1) %s -l https://endpoint.com/ -c body.json -m POST\n", pName);
    printf("2) %s -l endpoint.com/test -t 20 -wo output.txt -s -v\n", pName);
    printf("2) %s -l endpoint.com -x 'X-Is-Custom: True; X-My-Header: Test'\n", pName);
}

int XHTTPApp_ParseArgs(xhttp_args_t *pArgs, int argc, char *argv[])
{
    XByteBuffer_Init(&pArgs->content, 0, 0);
    XTime_Init(&pArgs->lastTime);

    pArgs->pOutputFile = NULL;
    pArgs->eMethod = XHTTP_GET;
    pArgs->nAutoFollow = XFALSE;
    pArgs->nForceWrite = XFALSE;
    pArgs->nDownload = XFALSE;
    pArgs->nVerbose = XFALSE;
    pArgs->nSSL = XFALSE;
    pArgs->nTimeout = 0;
    pArgs->nBytes = 0;
    pArgs->nDone = 0;

    xstrncpy(pArgs->sSpeed, sizeof(pArgs->sSpeed), "N/A");
    xstrnul(pArgs->sAddress);
    xstrnul(pArgs->sHeaders);
    xstrnul(pArgs->sContent);
    xstrnul(pArgs->sOutput);
    int nChar = 0;

    while ((nChar = getopt(argc, argv, "l:c:m:o:t:x:d1:f1:s1:v1:w1:h1")) != -1)
    {
        switch (nChar)
        {
            case 'l':
                xstrncpy(pArgs->sAddress, sizeof(pArgs->sAddress), optarg);
                break;
            case 'c':
                xstrncpy(pArgs->sContent, sizeof(pArgs->sContent), optarg);
                break;
            case 'o':
                xstrncpy(pArgs->sOutput, sizeof(pArgs->sOutput), optarg);
                break;
            case 'x':
                xstrncpy(pArgs->sHeaders, sizeof(pArgs->sHeaders), optarg);
                break;
            case 'm':
                pArgs->eMethod = XHTTP_GetMethodType(optarg);
                break;
            case 't':
                pArgs->nTimeout = atoi(optarg);
                break;
            case 'd':
                pArgs->nDownload = XTRUE;
                break;
            case 'f':
                pArgs->nAutoFollow = XTRUE;
                break;
            case 's':
                pArgs->nSSL = XTRUE;
                break;
            case 'v':
                pArgs->nVerbose = XTRUE;
                break;
            case 'w':
                pArgs->nForceWrite = XTRUE;
                break;
            case 'h':
            default:
                return 0;
        }
    }

    if (!xstrused(pArgs->sAddress)) return XFALSE;
    xcli_bar_t *pBar = &pArgs->progressBar;

    if (xstrused(pArgs->sContent) && XPath_LoadBuffer(pArgs->sContent, &pArgs->content) <= 0)
    {
        xloge("Failed to load content from file: %s (%d)", pArgs->sContent, errno);
        return XFALSE;
    }

    if (xstrused(pArgs->sOutput) || pArgs->nDownload)
    {
        XProgBar_GetDefaults(pBar);
        pArgs->nAutoFollow = XTRUE;
        pArgs->nDownload = XTRUE;
        pBar->bInPercent = XTRUE;
    }

    if (pArgs->nVerbose)
    {
        xlog_timing(XLOG_TIME);
        xlog_enable(XLOG_ALL);
    }

    return XTRUE;
}

int XHTTPApp_AppendArgHeaders(xhttp_t *pHandle, xhttp_args_t *pArgs)
{
    xarray_t *pArr = xstrsplit(pArgs->sHeaders, ";");
    if (pArr == NULL) return XSTDERR;

    pHandle->nAllowUpdate = XTRUE;
    size_t i, nUsed = pArr->nUsed;

    for (i = 0; i < nUsed; i++)
    {
        char *pHeader = (char*)XArray_GetData(pArr, i);
        if (pHeader == NULL) continue;

        char *pSavePtr = NULL;
        char *pOption = xstrtok(pHeader, ":", &pSavePtr);
        if (pOption == NULL)
        {
            XArray_Destroy(pArr);
            return XSTDERR;
        }

        char *pField = xstrtok(NULL, ":", &pSavePtr);
        if (pField == NULL)
        {
            XArray_Destroy(pArr);
            return XSTDERR;
        }

        char *pOptionPtr = pOption;
        char *pFieldPtr = pField;

        while (*pOptionPtr == XSTR_SPACE_CHAR) pOptionPtr++;
        while (*pFieldPtr == XSTR_SPACE_CHAR) pFieldPtr++;

        XHTTP_AddHeader(pHandle, pOptionPtr, "%s", pFieldPtr);
        xlogd("Adding header: %s: %s", pOptionPtr, pFieldPtr);
    }

    XArray_Destroy(pArr);
    return XSTDOK;
}

int XHTTPApp_DisplayRequest(xhttp_t *pHandle)
{
    xhttp_args_t *pArgs = (xhttp_args_t*)pHandle->pUserCtx;
    XTime_Get(&pArgs->lastTime);
    if (!pArgs->nVerbose) return XSTDOK;

    xlogd("Sending %s request: %zu bytes",
        XHTTP_GetMethodStr(pHandle->eMethod),
        pHandle->dataRaw.nUsed);

    printf("%s", (char*)pHandle->dataRaw.pData);
    if (XHTTP_GetBodySize(pHandle)) printf("\n");

    return XSTDOK;
}

void XHTTPApp_UpdateProgress(xhttp_t *pHandle)
{
    xhttp_args_t *pArgs = (xhttp_args_t*)pHandle->pUserCtx;
    xcli_bar_t *pBar = &pArgs->progressBar;
    char sReceivedSize[XHTTP_FIELD_MAX];

    if (!pHandle->nContentLength) pBar->fPercent = -1.;
    else pBar->fPercent = (double)100 / pHandle->nContentLength * pArgs->nDone;

    XBytesToUnit(sReceivedSize, sizeof(sReceivedSize), pArgs->nDone, XFALSE);
    xstrncpyf(pBar->sPrefix, sizeof(pBar->sPrefix), "Downloading... %s ", pArgs->sSpeed);
    xstrncpyf(pBar->sSuffix, sizeof(pBar->sSuffix), " %s", sReceivedSize);
    XProgBar_Update(pBar);
}

int XHTTPApp_DumpResponse(xhttp_t *pHandle, xhttp_ctx_t *pCbCtx)
{
    xhttp_args_t *pArgs = (xhttp_args_t*)pHandle->pUserCtx;
    if (!pArgs->nDownload || !XHTTP_IsSuccessCode(pHandle)) return XSTDUSR;

    if (pArgs->pOutputFile == NULL)
    {
        char sTmpPath[XPATH_MAX];
        xstrncpyf(sTmpPath, sizeof(sTmpPath), "%s.part", pArgs->sOutput);

        pArgs->pOutputFile = XFile_New(sTmpPath, "cwt", NULL);
        if (pArgs->pOutputFile == NULL)
        {
            xloge("Failed to open output file: %s (%d)", sTmpPath, errno);
            return XSTDERR;
        }
    }

    xtime_t currTime;
    XTime_Get(&currTime);
    pArgs->nBytes += pCbCtx->nLength;

    int nDiff = XTime_Diff(&currTime, &pArgs->lastTime);
    if (nDiff > XHTTP_INTERVAL_SEC)
    {
        char sPerSec[XSTR_MIN], sSpeed[XSTR_MIN];
        size_t nPerSec = pArgs->nBytes / nDiff;
        XBytesToUnit(sPerSec, sizeof(sPerSec), nPerSec, XFALSE);
        xstrncpyfl(sSpeed, sizeof(sSpeed), 12, XSTR_SPACE_CHAR, "%s/s", sPerSec);
        xstrnclr(pArgs->sSpeed, sizeof(pArgs->sSpeed), XSTR_FMT_BOLD, "%s", sSpeed);
        XTime_Copy(&pArgs->lastTime, &currTime);
        pArgs->nBytes = 0;
    }

    pArgs->nDone += pCbCtx->nLength;
    XHTTPApp_UpdateProgress(pHandle);

    if (XFile_Write(pArgs->pOutputFile, pCbCtx->pData, pCbCtx->nLength) <= 0)
    {
        xloge("Failed to write data to output file: %s (%d)", pArgs->sOutput, errno);
        return XSTDERR;
    }

    return XSTDOK;
}

int XHTTPApp_DisplayResponseHdr(xhttp_t *pHandle)
{
    xhttp_args_t *pArgs = (xhttp_args_t*)pHandle->pUserCtx;
    if (!pArgs->nVerbose) return XSTDOK;

    const char *pStatus = XHTTP_GetCodeStr(pHandle->nStatusCode);
    const char *pCntType = XHTTP_GetHeader(pHandle, "Content-Type");

    char cSave = pHandle->dataRaw.pData[pHandle->nHeaderLength - 1];
    pHandle->dataRaw.pData[pHandle->nHeaderLength - 1] = XSTR_NUL;
    xlogd("Received response header: %s", pStatus);
    printf("%s\n", (char*)pHandle->dataRaw.pData);
    pHandle->dataRaw.pData[pHandle->nHeaderLength - 1] = cSave;
  
    xbool_t bFollowing = (pHandle->nStatusCode >= 300 && pHandle->nStatusCode < 400 &&
        pArgs->nAutoFollow && XHTTP_GetHeader(pHandle, "Location") != NULL) ? XTRUE : XFALSE;

    if ((pHandle->nContentLength > 0 || xstrused(pCntType)) && !bFollowing)
    {
        char sBytes[XSTR_TINY];
        if (!pHandle->nContentLength) xstrncpy(sBytes, sizeof(sBytes), "N/A");
        else xstrncpyf(sBytes, sizeof(sBytes), "%zu", pHandle->nContentLength);
        xlogd("Downloading body: %s bytes", sBytes);
    }

    return XSTDOK;
}

int XHTTPApp_Callback(xhttp_t *pHttp, xhttp_ctx_t *pCbCtx)
{
    switch (pCbCtx->eCbType)
    {
        case XHTTP_STATUS:
            if (pCbCtx->eStatus == XHTTP_PARSED) 
                return XHTTPApp_DisplayResponseHdr(pHttp);
            xlogd("%s", (const char*)pCbCtx->pData);
            return XSTDOK;
        case XHTTP_READ_CNT:
            return XHTTPApp_DumpResponse(pHttp, pCbCtx);
        case XHTTP_WRITE:
            return XHTTPApp_DisplayRequest(pHttp);
        case XHTTP_ERROR:
            xloge("%s", (const char*)pCbCtx->pData);
            return XSTDERR;
        default:
            break;
    }

    return XSTDUSR;
}

int XHTTPApp_Prepare(xhttp_args_t *pArgs, xlink_t *pLink)
{
    if (XLink_Parse(pLink, pArgs->sAddress) == XSTDERR)
    {
        xloge("Unsupported link: %s", pArgs->sAddress);
        return XSTDERR;
    }

    if (pArgs->nVerbose)
    {
        xlogd("Parsed link: %s", pArgs->sAddress);
        printf("Protocol: %s\nHost: %s\nAddr: %s\nPort: %d\nUser: %s\nPass: %s\nFile: %s\nURL: %s\n\n",
            pLink->sProtocol, pLink->sHost, pLink->sAddr, pLink->nPort, pLink->sUser, pLink->sPass, pLink->sFile, pLink->sUrl);
    }

    if (pArgs->nDownload && !xstrused(pArgs->sOutput))
    {
        const char *pFileName = xstrused(pLink->sFile) ? pLink->sFile : "xhttp.out";
        xstrncpy(pArgs->sOutput, sizeof(pArgs->sOutput), pFileName);
        size_t nFileCount = 1;

        while (XPath_Exists(pArgs->sOutput))
        {
            xstrncpyf(pArgs->sOutput, sizeof(pArgs->sOutput), "%s.%zu", pFileName, nFileCount);
            nFileCount++;
        }
    }

    if (XPath_Exists(pArgs->sOutput) && pArgs->nForceWrite == XFALSE)
    {
        xlogw("File already exists: %s", pArgs->sOutput);
        xlogi("Use option -w to force overwrite output");
        return XSTDERR;
    }

    if (pArgs->nSSL && strncmp(pLink->sProtocol, "https", 5))
    {
        xlogd("Upgrading to HTTPS: %s: %d -> %d", pLink->sAddr, pLink->nPort, XHTTP_SSL_PORT);
        xstrncpyf(pLink->sHost, sizeof(pLink->sHost), "%s:%d", pLink->sAddr, XHTTP_SSL_PORT);
        xstrncpy(pLink->sProtocol, sizeof(pLink->sProtocol), "https");
        pLink->nPort = XHTTP_SSL_PORT;
    }

    return XSTDOK;
}

int XHTTPApp_Perform(xhttp_args_t *pArgs, xlink_t *pLink)
{
    xhttp_t handle;
    XHTTP_InitRequest(&handle, pArgs->eMethod, pLink->sUrl, NULL);
    XHTTP_AddHeader(&handle, "Host", "%s", pLink->sHost);
    XHTTP_AddHeader(&handle, "User-Agent", "xutils/%s", XUtils_VersionShort());
    handle.nTimeout = pArgs->nTimeout;

    uint16_t nCallbacks = XHTTP_ERROR | XHTTP_READ_CNT | XHTTP_WRITE | XHTTP_STATUS;
    XHTTP_SetCallback(&handle, XHTTPApp_Callback, pArgs, nCallbacks);

    if (xstrused(pArgs->sHeaders) && XHTTPApp_AppendArgHeaders(&handle, pArgs) < 0)
    {
        xloge("Failed to appen custom headers: %s (%d)", pArgs->sHeaders, errno);
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    xbool_t bHaveOutput = xstrused(pArgs->sOutput);
    xhttp_status_t eStatus;

    eStatus = XHTTP_LinkPerform(&handle, pLink, pArgs->content.pData, pArgs->content.nUsed);
    if (eStatus != XHTTP_COMPLETE && !XHTTP_GetBodySize(&handle))
    {
        if (eStatus == XHTTP_BIGCNT) xlogi("Too big content. Try to use output file (-o <file>)");
        XFile_Clean(pArgs->pOutputFile);
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    if (bHaveOutput)
    {
        char sTmpPath[XPATH_MAX];
        xstrncpyf(sTmpPath, sizeof(sTmpPath), "%s.part", pArgs->sOutput);

        if (XPath_Exists(sTmpPath) && rename(sTmpPath, pArgs->sOutput) < 0)
            xloge("Failed to rename file: %s -> %s (%d)", sTmpPath, pArgs->sOutput, errno);
    }

    if (pArgs->nDone && !handle.nContentLength)
        XProgBar_Finish(&pArgs->progressBar);

    if (pArgs->nAutoFollow &&
        handle.nStatusCode < 400 &&
        handle.nStatusCode >= 300)
    {
        const char *pStatus = XHTTP_GetCodeStr(handle.nStatusCode);
        xlogd("HTTP redirect: %d (%s)", handle.nStatusCode, pStatus);

        const char *pLocation = XHTTP_GetHeader(&handle, "Location");
        if (pLocation != NULL)
        {
            xstrncpy(pArgs->sAddress, sizeof(pArgs->sAddress), pLocation);
            xlogd("Following location: %s", pArgs->sAddress);

            XFile_Clean(pArgs->pOutputFile);
            XHTTP_Clear(&handle);
            return XSTDOK;
        }
    }

    if (!XHTTP_IsSuccessCode(&handle))
    {
        const char *pStatus = XHTTP_GetCodeStr(handle.nStatusCode);
        xlogw("HTTP response: %d (%s)", handle.nStatusCode, pStatus);
    }

    const char *pBody = (const char *)XHTTP_GetBody(&handle);
    if (pBody != NULL && !bHaveOutput) printf("%s\n", pBody);

    XFile_Clean(pArgs->pOutputFile);
    XHTTP_Clear(&handle);
    return XSTDNON;
}

int main(int argc, char* argv[])
{
    xlog_defaults();
    xlog_cfg_t xcfg;

    xlog_get(&xcfg);
    xcfg.eColorFormat = XLOG_COLORING_FULL;
    xcfg.nUseHeap = XTRUE;
    xcfg.nIndent = XTRUE;
    xcfg.nFlags |= XLOG_INFO;
    xlog_set(&xcfg);

    xhttp_args_t args;
    if (!XHTTPApp_ParseArgs(&args, argc, argv))
    {
        XHTTPApp_DisplayUsage(argv[0]);
        return XSTDERR;
    }

    int nStatus = XSTDOK;
    while (nStatus == XSTDOK)
    {
        xlink_t link;
        nStatus = XHTTPApp_Prepare(&args, &link);
        if (nStatus < 0) break;
        nStatus = XHTTPApp_Perform(&args, &link);
    }

    XByteBuffer_Clear(&args.content);
    XSock_DeinitSSL();
    return nStatus;
}
