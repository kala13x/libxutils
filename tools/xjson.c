/*!
 *  @file libxutils/examples/json.c
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Parse, lint and minify json using xjson library.
 */

#include "xstd.h"
#include "str.h"
#include "log.h"
#include "xfs.h"
#include "pool.h"
#include "json.h"
#include "buf.h"

extern char *optarg;

#define XJSON_LINT_VER_MAX  0
#define XJSON_LINT_VER_MIN  5

#define XJSON_POOL_SIZE     1024 * 64

typedef struct xjson_args_ {
    char sFile[XPATH_MAX];
    uint16_t nTabSize;
    uint8_t nMinify;
    uint8_t nPretty;
} xjson_args_t;

void XJSON_DisplayUsage(const char *pName)
{
    xlog("======================================================");
    xlog(" XJSON - Lint / Minify JSON file - v%d.%d (%s)",
        XJSON_LINT_VER_MAX, XJSON_LINT_VER_MIN, __DATE__);
    xlog("======================================================");

    xlog("Usage: %s [-i <path>] [-l <size>] [-m] [-p] [-h]\n", pName);
    xlog("Options are:");
    xlog("  -i <path>           # Input file path (%s*%s)", XSTR_CLR_RED, XSTR_FMT_RESET);
    xlog("  -l <size>           # Linter tab size");
    xlog("  -m                  # Minify json file");
    xlog("  -p                  # Pretty print");
    xlog("  -h                  # Version and usage\n");
    xlog("Examples:");
    xlog("1) %s -i example.json -pl 4", pName);
    xlog("2) cat example.json | %s -p\n", pName);
}

int XJSON_ParseArgs(xjson_args_t *pArgs, int argc, char *argv[])
{
    xstrnul(pArgs->sFile);
    pArgs->nTabSize = 4;
    pArgs->nMinify = 0;
    pArgs->nPretty = 0;
    int nChar = 0;

    while ((nChar = getopt(argc, argv, "i:l:m1:p1:h1")) != -1)
    {
        switch (nChar)
        {
            case 'i':
                xstrncpy(pArgs->sFile, sizeof(pArgs->sFile), optarg);
                break;
            case 'l':
                pArgs->nTabSize = atoi(optarg);
                break;
            case 'm':
                pArgs->nMinify = 1;
                break;
            case 'p':
                pArgs->nPretty = 1;
                break;
            case 'h':
            default:
                return XFALSE;
        }
    }

    return XTRUE;
}

size_t XJSon_ReadFromStdin(xbyte_buffer_t *pBuffer)
{
    XByteBuffer_Init(pBuffer, XSTR_MID, XFALSE);
    char sBuffer[XSTR_MID];
    int nRead = 0;

    while ((nRead = fread(sBuffer, 1, sizeof(sBuffer), stdin)) > 0)
    {
        if (!XByteBuffer_Add(pBuffer, (const uint8_t*)sBuffer, nRead))
        {
            xloge("Failed append stdin data to buffer: %s", XSTRERR);
            return XSTDNON;
        }
    }

    return pBuffer->nUsed;
}

int main(int argc, char *argv[])
{
    xlog_defaults();
    xbyte_buffer_t buffer;
    xjson_args_t args;
    xjson_t json;

    if (!XJSON_ParseArgs(&args, argc, argv))
    {
        XJSON_DisplayUsage(argv[0]);
        return 1;
    }

    if (xstrused(args.sFile))
    {
        if (XPath_LoadBuffer(args.sFile, &buffer) <= 0)
        {
            xloge("Failed to load file: %s (%s)", args.sFile, XSTRERR);
            return 1;
        }
    }
    else if (!XJSon_ReadFromStdin(&buffer))
    {
        xloge("Failed to read from stdin: %s", XSTRERR);
        return 1;
    }

    xpool_t pool;
    XPool_Init(&pool, XJSON_POOL_SIZE);

    if (!XJSON_Parse(&json, &pool, (const char*)buffer.pData, buffer.nUsed))
    {
        char sError[256];
        XJSON_GetErrorStr(&json, sError, sizeof(sError));
        xloge("Failed to parse JSON: %s", sError);

        XByteBuffer_Clear(&buffer);
        XPool_Destroy(&pool);
        XJSON_Destroy(&json);
        return 1;
    }

    xjson_writer_t writer;
    XJSON_InitWriter(&writer, &pool, NULL, buffer.nUsed);
    writer.nTabSize = !args.nMinify ? args.nTabSize : 0;
    writer.nPretty = args.nPretty;

    if (XJSON_WriteObject(json.pRootObj, &writer)) printf("%s\n", writer.pData);
    else xloge("Failed to serialize json: errno(%d) %s", errno, writer.pData ? writer.pData : "");

    XJSON_DestroyWriter(&writer);
    XByteBuffer_Clear(&buffer);
    XJSON_Destroy(&json);
    XPool_Destroy(&pool);

    return 0;
}