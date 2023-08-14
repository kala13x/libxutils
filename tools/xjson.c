/*!
 *  @file libxutils/examples/xjson.c
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Parse, lint and minify json using xjson library.
 */

#include "xstd.h"
#include "xjson.h"
#include "xbuf.h"
#include "xstr.h"
#include "xlog.h"
#include "xfs.h"

extern char *optarg;

#define XJSON_LINT_VER_MAX  0
#define XJSON_LINT_VER_MIN  3

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
    xlog("Example: %s -i example.json -pl 4\n", pName);
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

    return (xstrused(pArgs->sFile)) ? XTRUE : XFALSE;
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

    if (XPath_LoadBuffer(args.sFile, &buffer) <= 0)
    {
        xloge("Failed to load file: %s (%s)", args.sFile, XSTRERR);
        return 1;
    }

    if (!XJSON_Parse(&json, (const char*)buffer.pData, buffer.nUsed))
    {
        char sError[256];
        XJSON_GetErrorStr(&json, sError, sizeof(sError));
        xloge("Failed to parse JSON: %s", sError);

        XByteBuffer_Clear(&buffer);
        XJSON_Destroy(&json);
        return 1;
    }

    xjson_writer_t writer;
    XJSON_InitWriter(&writer, NULL, buffer.nUsed);
    writer.nTabSize = !args.nMinify ? args.nTabSize : 0;
    writer.nPretty = args.nPretty;

    if (XJSON_WriteObject(json.pRootObj, &writer)) printf("%s\n", writer.pData);
    else xloge("Failed to serialize json: errno(%d) %s", errno, writer.pData ? writer.pData : "");

    XJSON_DestroyWriter(&writer);
    XByteBuffer_Clear(&buffer);
    XJSON_Destroy(&json);
    return 0;
}