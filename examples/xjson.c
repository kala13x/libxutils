/*!
 *  @file libxutils/examples/xjson.c
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Parse, lint and minify json using xjson library.
 */

#include <xutils/xstd.h>
#include <xutils/xjson.h>
#include <xutils/xbuf.h>
#include <xutils/xstr.h>
#include <xutils/xlog.h>
#include <xutils/xfs.h>

extern char *optarg;

#define XJSON_LINT_VER_MAX  0
#define XJSON_LINT_VER_MIN  2

typedef struct xjson_args_ {
    char sFile[XPATH_MAX];
    uint16_t nTabSize;
    uint8_t nMinify;
} xjson_args_t;

void XJSON_DisplayUsage(const char *pName)
{
    xlog("================================================");
    xlog(" Lint and Minify JSON file - v%d.%d (%s)",
        XJSON_LINT_VER_MAX, XJSON_LINT_VER_MIN, __DATE__);
    xlog("================================================");

    xlog("Usage: %s [-f <file>] [-l <size>] [-m] [-h]\n", pName);
    xlog("Options are:");
    xlog("  -f <file>           # JSON file path (%s*%s)", XSTR_CLR_RED, XSTR_FMT_RESET);
    xlog("  -l <size>           # Linter tab size");
    xlog("  -m                  # Minify json file");
    xlog("  -h                  # Version and usage\n");
    xlog("Example: %s -f example.json -l 4\n", pName);
}

int XJSON_ParseArgs(xjson_args_t *pArgs, int argc, char *argv[])
{
    xstrnul(pArgs->sFile);
    pArgs->nTabSize = 4;
    pArgs->nMinify = 0;
    int nChar = 0;

    while ((nChar = getopt(argc, argv, "f:l:m1:h1")) != -1) 
    {
        switch (nChar)
        {
            case 'f':
                xstrncpy(pArgs->sFile, sizeof(pArgs->sFile), optarg);
                break;
            case 'l':
                pArgs->nTabSize = atoi(optarg);
                break;
            case 'm':
                pArgs->nMinify = 1;
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
        xloge("Failed to load file: %s (%s)", args.sFile, strerror(errno));
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
    if (!args.nMinify) writer.nTabSize = args.nTabSize;

    if (XJSON_WriteObject(json.pRootObj, &writer)) printf("%s\n", writer.pData);
    else xloge("Failed to serialize json: errno(%d) %s", errno, writer.pData ? writer.pData : "");

    XJSON_DestroyWriter(&writer);
    XByteBuffer_Clear(&buffer);
    XJSON_Destroy(&json);
    return 0;
}