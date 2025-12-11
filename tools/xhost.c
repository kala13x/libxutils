/*!
 *  @file libxutils/tools/xhost.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Modify hosts file, add or remove entries.
 */

#include "xstd.h"
#include "xfs.h"
#include "str.h"
#include "log.h"
#include "buf.h"
#include "cli.h"

#define XHOST_FILE_PATH     "/etc/hosts"
#define XHOST_VERSION_MAX   1
#define XHOST_VERSION_MIN   0
#define XHOST_BUILD_NUMBER  11

#define XHOST_ADDR_LEN_MAX  15

typedef struct {
    xbool_t bAdd;
    xbool_t bAppend;
    xbool_t bRemove;
    xbool_t bVerbose;
    xbool_t bSearch;
    xbool_t bComment;
    xbool_t bDisplay;
    xbool_t bUncomment;
    xbool_t bWholeWords;
    xbool_t bInsertLine;
    xbool_t bHideLines;
    size_t nLineNumber;
    size_t nTabSize;
    char sAddress[XLINE_MAX];
    char sHost[XLINE_MAX];
} xhost_args_t;

typedef struct {
    char sAddr[XLINE_MAX];
    char sHost[XLINE_MAX];
    char sLine[XLINE_MAX];
    xbool_t bWholeWords;
    xbool_t bWritten;
    xbool_t bSearch;
    size_t nLineNumber;
    size_t nTabSize;
    xstring_t hosts;
    xfile_t file;
} xhost_ctx_t;

typedef struct {
    char sAddr[XLINE_MAX];
    char sHost[XLINE_MAX];
    char sComment[XLINE_MAX];
} xhost_entry_t;

extern char *optarg;

static void XHost_Usage(const char *pName)
{
    printf("==========================================================\n");
    printf(" XHost (Add or modify hosts) - v%d.%d build %d (%s)\n",
        XHOST_VERSION_MAX, XHOST_VERSION_MIN, XHOST_BUILD_NUMBER, __DATE__);
    printf("==========================================================\n");

    int i, nLength = strlen(pName) + 6;
    char sWhiteSpace[nLength + 1];

    for (i = 0; i < nLength; i++) sWhiteSpace[i] = ' ';
    sWhiteSpace[nLength] = 0;

    printf("Usage: %s [-a <addr>] [-n <hostname>] [-l <line>] [-i]\n", pName);
    printf(" %s [-c] [-u] [-r] [-d] [-s] [-x] [-v] [-w] [-h]\n\n", sWhiteSpace);

    printf("Options are:\n");
    printf("  -a <address>          # IP address\n");
    printf("  -n <hostname>         # Host name\n");
    printf("  -l <line>             # Line number\n");
    printf("  -t <size>             # Tab size to lint entries\n");
    printf("  -i                    # Insert new line\n");
    printf("  -c                    # Comment entry\n");
    printf("  -u                    # Uncomment entry\n");
    printf("  -r                    # Remove entry\n");
    printf("  -s                    # Search Entry\n");
    printf("  -d                    # Display /etc/hosts file\n");
    printf("  -w                    # Match whole words in entry\n");
    printf("  -x                    # Hide lines during display\n");
    printf("  -v                    # Enable verbose logging\n");
    printf("  -h                    # Print version and usage\n\n");

    printf("Examples:\n");
    printf("1) %s -a 10.10.17.1 -n example.com\n", pName);
    printf("2) %s -a 192.168.0.17 -rw\n", pName);
    printf("3) %s -n test.com -rd\n", pName);
}

static int XHost_ParseArgs(xhost_args_t *pArgs, int argc, char *argv[])
{
    memset(pArgs, 0, sizeof(xhost_args_t));
    int opt = 0;

    while ((opt = getopt(argc, argv, "a:n:l:t:c1:d1:i1:u1:r1:s1:x1:v1:w1:h1")) != -1)
    {
        switch (opt)
        {
            case 'a': xstrncpy(pArgs->sAddress, sizeof(pArgs->sAddress), optarg); break;
            case 'n': xstrncpy(pArgs->sHost, sizeof(pArgs->sHost), optarg); break;
            case 'l': pArgs->nLineNumber = atoi(optarg); break;
            case 't': pArgs->nTabSize = atoi(optarg); break;
            case 'd': pArgs->bDisplay = XTRUE; break;
            case 'i': pArgs->bInsertLine = XTRUE; break;
            case 'c': pArgs->bComment = XTRUE; break;
            case 'u': pArgs->bUncomment = XTRUE; break;
            case 'x': pArgs->bHideLines = XTRUE; break;
            case 'r': pArgs->bRemove = XTRUE; break;
            case 's': pArgs->bSearch = XTRUE; break;
            case 'v': pArgs->bVerbose = XTRUE; break;
            case 'w': pArgs->bWholeWords = XTRUE; break;
            case 'h': return XSTDERR;
            default: return XSTDERR;
        }
    }

    xbool_t bHaveAddress = xstrused(pArgs->sAddress);
    xbool_t bHaveHost = xstrused(pArgs->sHost);
    xbool_t bModify = pArgs->bRemove || pArgs->bComment || pArgs->bUncomment;
    xbool_t bInsertLine = pArgs->bInsertLine && pArgs->nLineNumber;
    pArgs->bAppend = !bModify && !pArgs->bSearch && ((bHaveAddress && bHaveHost) || bInsertLine);

    if ((bModify || pArgs->bSearch) && !bHaveAddress && !bHaveHost && !pArgs->nLineNumber) return XSTDERR;
    if (!pArgs->bAppend && !bModify) pArgs->bDisplay = XTRUE;
    if (pArgs->bVerbose) xlog_enable(XLOG_DEBUG);
    if (pArgs->bSearch) pArgs->bDisplay = XTRUE;

    return XSTDOK;
}

static int XHost_InitContext(xhost_ctx_t *pCtx, xbool_t bReset)
{
    if (bReset)
    {
        pCtx->bSearch = XFALSE;
        pCtx->bWholeWords = XFALSE;
        pCtx->nLineNumber = 0;

        pCtx->sAddr[0] = XSTR_NUL;
        pCtx->sHost[0] = XSTR_NUL;
        pCtx->sLine[0] = XSTR_NUL;
    }

    XASSERT((XFile_Open(&pCtx->file, XHOST_FILE_PATH, "r", NULL) >= 0),
        xthrowe("Failed to open hosts file"));

    XASSERT_CALL((XString_Init(&pCtx->hosts, XSTDNON, XFALSE) >= 0),
        XFile_Close, &pCtx->file, xthrowe("Failed alloc hosts file buffer"));

    return XSTDOK;
}

static void XHost_ClearContext(xhost_ctx_t *pCtx)
{
    XString_Clear(&pCtx->hosts);
    XFile_Close(&pCtx->file);
}

static int XHost_RemoveHeader()
{
    xhost_ctx_t ctx;
    uint8_t nHeaderSkipped = 0;

    XASSERT((XHost_InitContext(&ctx, XTRUE) > 0),
        xthrowe("Failed to init context"));

    while (XFile_GetLine(&ctx.file, ctx.sLine, sizeof(ctx.sLine)) > 0)
    {
        if (ctx.sLine[0] == '#')
        {
            if (xstrncmp(ctx.sLine, "# Hosts file modified by XHost", 30) ||
                xstrncmp(ctx.sLine, "# Version", 9) ||
                xstrncmp(ctx.sLine, "# Tabs:", 7))
            {
                nHeaderSkipped++;
                continue;
            }
        }

        if (nHeaderSkipped == 3 && ctx.sLine[0] == '\n')
        {
            nHeaderSkipped = 0;
            continue;
        }

        XASSERT((XString_Append(&ctx.hosts, "%s", ctx.sLine) >= 0),
            xthrowe("Failed to add line to hosts buffer"));
    }

    XASSERT_CALL((XFile_Reopen(&ctx.file, XHOST_FILE_PATH, "cwt", NULL) >= 0),
        XHost_ClearContext, &ctx, xthrowe("Failed to open hosts file for writing"));

    XASSERT_CALL((XFile_Write(&ctx.file, ctx.hosts.pData, ctx.hosts.nLength) >= 0),
        XHost_ClearContext, &ctx, xthrowe("Failed to write hosts file"));

    XHost_ClearContext(&ctx);
    return XSTDOK;
}

static int XHost_WriteHeader(xhost_ctx_t *pCtx)
{
    XHost_ClearContext(pCtx);
    XHost_RemoveHeader();

    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, XSTDNON, XFALSE);

    XASSERT((XPath_LoadBuffer(XHOST_FILE_PATH, &buffer) > 0),
        xthrowe("Failed to load hosts file for header analysis"));

    char sHeader[XSTR_MIN];
    int nWritten = xstrncpyf(sHeader, sizeof(sHeader),
        "# Hosts file modified by XHost\n"
        "# Version %d.%d build %d on %s\n"
        "# Tabs: %zu\n\n",
        XHOST_VERSION_MAX,
        XHOST_VERSION_MIN,
        XHOST_BUILD_NUMBER,
        __DATE__,
        pCtx->nTabSize);

    xfile_t file;
    XASSERT_CALL((XFile_Open(&file, XHOST_FILE_PATH, "cwt", NULL) >= 0),
        XByteBuffer_Clear, &buffer, xthrowe("Failed to open hosts file for writing"));

    XASSERT_CALL2((XFile_Write(&file, sHeader, nWritten) >= 0),
        XFile_Close, &file, XByteBuffer_Clear, &buffer,
        xthrowe("Failed to write hosts header"));

    XASSERT_CALL2((XFile_Write(&file, buffer.pData, buffer.nUsed) >= 0),
        XFile_Close, &file, XByteBuffer_Clear, &buffer,
        xthrowe("Failed to write hosts data"));

    XFile_Close(&file);
    XByteBuffer_Clear(&buffer);

    return XSTDNON;
}

static int XHost_Write(xhost_ctx_t *pCtx)
{
    xstring_t *pString = &pCtx->hosts;
    xfile_t file;

    if (XFile_Open(&file, XHOST_FILE_PATH, "cwt", NULL) < 0)
    {
        xloge("Failed to open hosts file for writing: %s", XSTRERR);
        return XSTDERR;
    }

    if (XFile_Write(&file, pString->pData, pString->nLength) < 0)
    {
        xloge("Failed to write hosts file: %s", XSTRERR);
        XFile_Close(&file);
        return XSTDERR;
    }

    pCtx->bWritten = XTRUE;
    XFile_Close(&file);

    return XSTDNON;
}

static void XHost_RemoveTailSpace(char *sEntry, size_t nLength)
{
    if (!nLength || !xstrused(sEntry)) return;
    size_t nPosit = nLength - 1;

    while (nPosit > 0 &&
           (isspace((unsigned char)sEntry[nPosit]) ||
           sEntry[nPosit] == '\n' ||
           sEntry[nPosit] == '\r' ||
           sEntry[nPosit] == '\t')) nPosit--;

    sEntry[nPosit + 1] = XSTR_NUL;
}

static xbool_t XHost_ParseEntry(xhost_entry_t *pEntry, const char *pLine)
{
    pEntry->sAddr[0] = XSTR_NUL;
    pEntry->sHost[0] = XSTR_NUL;
    pEntry->sComment[0] = XSTR_NUL;

    // Skip empty spaces
    int nPosit = 0;
    while (pLine[nPosit] && isspace((unsigned char)pLine[nPosit])) nPosit++;
    if (!pLine[nPosit]) return XFALSE;

    if (pLine[nPosit] == '#')
    {
        while (pLine[nPosit] && (pLine[nPosit] == '#' ||
               isspace((unsigned char)pLine[nPosit]))) nPosit++;

        if (!pLine[nPosit]) return XFALSE;
        xstrncpy(pEntry->sComment, sizeof(pEntry->sComment), &pLine[nPosit]);
        XHost_RemoveTailSpace(pEntry->sComment, strlen(pEntry->sComment));
        return XTRUE;
    }

    // Get IP addr
    int nEnd = nPosit;
    while (pLine[nEnd] && !isspace((unsigned char)pLine[nEnd])) nEnd++;
    if (!pLine[nPosit]) return XFALSE;
    xstrncpy(pEntry->sAddr, sizeof(pEntry->sAddr), &pLine[nPosit]);
    pEntry->sAddr[nEnd - nPosit] = XSTR_NUL;

    // Get host name
    nPosit = nEnd;
    while (pLine[nPosit] && isspace((unsigned char)pLine[nPosit])) nPosit++;
    if (!pLine[nPosit]) return XFALSE;
    xstrncpy(pEntry->sHost, sizeof(pEntry->sHost), &pLine[nPosit]);

    nPosit = 0;
    while (pEntry->sHost[nPosit] &&
           pEntry->sHost[nPosit] != '\n' &&
           pEntry->sHost[nPosit] != '#') nPosit++;

    if (pEntry->sHost[nPosit] == '#')
    {
        pEntry->sHost[nPosit++] = XSTR_NUL;
        while (pEntry->sHost[nPosit] && isspace((unsigned char)pEntry->sHost[nPosit])) nPosit++;
        xstrncpy(pEntry->sComment, sizeof(pEntry->sComment), &pEntry->sHost[nPosit]);

        nPosit = 0;
        while (pEntry->sComment[nPosit] && pEntry->sComment[nPosit] != '\n') nPosit++;
        if (pEntry->sComment[nPosit] == '\n') pEntry->sComment[nPosit] = XSTR_NUL;
    }

    if (pEntry->sHost[nPosit] == '\n') pEntry->sHost[nPosit] = XSTR_NUL;
    XHost_RemoveTailSpace(pEntry->sAddr, strlen(pEntry->sAddr));
    XHost_RemoveTailSpace(pEntry->sHost, strlen(pEntry->sHost));
    XHost_RemoveTailSpace(pEntry->sComment, strlen(pEntry->sComment));

    return XTRUE;
}

static xbool_t XHost_SearchEntry(xhost_ctx_t *pCtx)
{
    xhost_entry_t entry;
    XHost_ParseEntry(&entry, pCtx->sLine);

    if (!xstrused(entry.sAddr) ||
        !xstrused(entry.sHost)) return XFALSE;

    xlogd_wn("Checking entry: %s", pCtx->sLine);
    char sSearchLine[XLINE_MAX];

    xstrncpyf(sSearchLine, sizeof(sSearchLine), "%s %s",
        xstrused(pCtx->sAddr) ? pCtx->sAddr : "-",
        xstrused(pCtx->sHost) ? pCtx->sHost : "-");

    xhost_entry_t search;
    XHost_ParseEntry(&search, sSearchLine);

    if (!xstrused(pCtx->sAddr)) search.sAddr[0] = XSTR_NUL;
    if (!xstrused(pCtx->sHost)) search.sHost[0] = XSTR_NUL;

    if (pCtx->bWholeWords)
    {
        if (xstrused(search.sHost) && xstrused(search.sAddr))
        {
            if (xstrcmp(entry.sHost, search.sHost) &&
                xstrcmp(entry.sAddr, search.sAddr)) return XTRUE;
        }
        else
        {
            if (xstrused(search.sHost) && xstrcmp(entry.sHost, search.sHost)) return XTRUE;
            if (xstrused(search.sAddr) && xstrcmp(entry.sAddr, search.sAddr)) return XTRUE;
        }
    }
    else
    {
        if (xstrused(search.sHost) && xstrused(search.sAddr))
        {
            if (xstrsrc(entry.sHost, search.sHost) >= 0 &&
                xstrsrc(entry.sAddr, search.sAddr) >= 0) return XTRUE;
        }
        else
        {
            if (xstrused(search.sHost) && xstrsrc(entry.sHost, search.sHost) >= 0) return XTRUE;
            if (xstrused(search.sAddr) && xstrsrc(entry.sAddr, search.sAddr) >= 0) return XTRUE;
        }
    }

    return XFALSE;
}

static int XHost_InsertEntry(xhost_ctx_t *pCtx)
{
    if (!xstrused(pCtx->sHost) || !xstrused(pCtx->sAddr)) return XSTDNON;
    XASSERT((XHost_InitContext(pCtx, XFALSE) > 0), xthrowe("Failed to init context"));

    xbool_t bFound = XFALSE;
    size_t nLineNumber = 0;

    while (XFile_GetLine(&pCtx->file, pCtx->sLine, sizeof(pCtx->sLine)) > 0)
    {
        if (pCtx->nLineNumber == ++nLineNumber)
        {
            XASSERT((XString_Append(&pCtx->hosts, "%s %s\n",
                pCtx->sAddr, pCtx->sHost) >= 0),
                xthrowe("Failed to add new line to hosts buffer"));

            bFound = XTRUE;
        }

        XASSERT((XString_Append(&pCtx->hosts, "%s", pCtx->sLine) >= 0),
            xthrowe("Failed to add line to hosts buffer"));
    }

    XFile_Close(&pCtx->file);

    if (bFound)
    {
        XASSERT((XHost_Write(pCtx) >= 0), XSTDERR);
        xlogd("Inserted new entry: %s %s", pCtx->sAddr, pCtx->sHost);
    }

    return XSTDNON;
}

static int XHost_ParseHeader(xhost_ctx_t *pCtx)
{
    if (pCtx->nTabSize) return XSTDOK;
    int nStatus = XSTDNON;

    xhost_ctx_t ctx;
    XASSERT((XHost_InitContext(&ctx, XFALSE) > 0),
        xthrowe("Failed to init context"));

    while (XFile_GetLine(&ctx.file, ctx.sLine, sizeof(ctx.sLine)) > 0)
    {
        if (xstrncmp(ctx.sLine, "# Tabs:", 7))
        {
            char *pPosit = ctx.sLine + 7;
            while (*pPosit && isspace((unsigned char)*pPosit)) pPosit++;

            pCtx->nTabSize = (size_t)atoi(pPosit);
            nStatus = XSTDOK;
            break;
        }
    }

    XHost_ClearContext(&ctx);
    return nStatus;
}

static int XHost_AddEntry(xhost_ctx_t *pCtx, xbool_t bNewLine)
{
    xbool_t bHaveEntry = xstrused(pCtx->sHost) && xstrused(pCtx->sAddr);
    if (!bHaveEntry && !pCtx->nLineNumber) return XSTDNON;

    xbool_t bAddedLine = XFALSE;
    xbool_t bFound = XFALSE;
    size_t nLineNumber = 0;

    while (XFile_GetLine(&pCtx->file, pCtx->sLine, sizeof(pCtx->sLine)) > 0)
    {
        if (pCtx->nLineNumber && bNewLine && !bHaveEntry)
        {
            if (pCtx->nLineNumber == ++nLineNumber)
            {
                XASSERT((XString_Append(&pCtx->hosts, "\n") >= 0),
                    xthrowe("Failed to add line to hosts buffer"));

                bAddedLine = XTRUE;
            }
        }
        else if (XHost_SearchEntry(pCtx))
        {
            xlogd_wn("Found entry: %s", pCtx->sLine);
            bFound = XTRUE;
            break;
        }

        XASSERT((XString_Append(&pCtx->hosts, "%s", pCtx->sLine) >= 0),
            xthrowe("Failed to add existing line to hosts buffer"));
    }

    XFile_Close(&pCtx->file);

    if (bAddedLine)
    {
        XASSERT((XHost_Write(pCtx) >= 0), XSTDERR);
        xlogd("Added newline at: %d", pCtx->nLineNumber);
        return XSTDNON;
    }

    if (!bFound)
    {
        if (pCtx->nLineNumber && bHaveEntry)
        {
            XHost_ClearContext(pCtx);
            return XHost_InsertEntry(pCtx);
        }

        if (pCtx->hosts.pData != NULL && pCtx->hosts.nLength > 0 &&
            pCtx->hosts.pData[pCtx->hosts.nLength - 1] != '\n' &&
            XString_Append(&pCtx->hosts, "\n") < 0)
        {
            xloge("Failed to append new line to hosts file: %s", XSTRERR);
            return XSTDERR;
        }

        XASSERT((XString_Append(&pCtx->hosts, "%s %s\n",
            pCtx->sAddr, pCtx->sHost) >= 0),
            xthrowe("Failed to append new host entry"));

        XASSERT((XHost_Write(pCtx) >= 0), XSTDERR);
        xlogd("Added new entry: %s %s", pCtx->sAddr, pCtx->sHost);
    }

    return XSTDNON;
}

static int XHost_RemoveEntry(xhost_ctx_t *pCtx, xbool_t bComment)
{
    size_t nLineNumber = 0;
    int nCount = 0;

    while (XFile_GetLine(&pCtx->file, pCtx->sLine, sizeof(pCtx->sLine)) > 0)
    {
        if (pCtx->nLineNumber == ++nLineNumber || XHost_SearchEntry(pCtx))
        {
            xlogd_wn("Found entry: %s", pCtx->sLine);

            if (bComment)
            {
                int nPosit = 0;
                while (pCtx->sLine[nPosit] && isspace((unsigned char)pCtx->sLine[nPosit])) nPosit++;

                if (pCtx->sLine[nPosit] && pCtx->sLine[nPosit] == '#')
                {
                    XASSERT((XString_Append(&pCtx->hosts, "%s", pCtx->sLine) >= 0),
                        xthrow("Failed to add line to hosts buffer (%s)", XSTRERR));

                    nCount++;
                    continue;
                }

                XASSERT((XString_Append(&pCtx->hosts, "#%s", pCtx->sLine) >= 0),
                    xthrow("Failed to add line to hosts buffer (%s)", XSTRERR));
            }

            nCount++;
            continue;
        }

        XASSERT((XString_Append(&pCtx->hosts, "%s", pCtx->sLine) >= 0),
            xthrow("Failed to add line to hosts buffer (%s)", XSTRERR));
    }

    XFile_Close(&pCtx->file);

    if (nCount)
    {
        XASSERT((XHost_Write(pCtx) >= 0), XSTDERR);
        xlogd("%s entres: %d", bComment ? "Commented" : "Removed", nCount);
    }

    return XSTDNON;
}

static int XHost_UncommentEntry(xhost_ctx_t *pCtx)
{
    size_t nLineNumber = 0;
    int nPosit = 0;
    int nCount = 0;

    while (XFile_GetLine(&pCtx->file, pCtx->sLine, sizeof(pCtx->sLine)) > 0)
    {
        nPosit = 0;
        nLineNumber++;
        xbool_t bComment = XFALSE;

        while (pCtx->sLine[nPosit] && isspace((unsigned char)pCtx->sLine[nPosit])) nPosit++;

        if (pCtx->sLine[nPosit] && pCtx->sLine[nPosit] == '#')
        {
            pCtx->sLine[nPosit] = XSTR_SPACE_CHAR;
            bComment = XTRUE;
        }

        if (bComment && (pCtx->nLineNumber == nLineNumber || XHost_SearchEntry(pCtx)))
        {
            xlogd_wn("Found entry: %s", pCtx->sLine);

            if (bComment) pCtx->sLine[nPosit] = '#';
            while (pCtx->sLine[nPosit] && pCtx->sLine[nPosit] == '#') nPosit++;

            if (pCtx->sLine[nPosit] && XString_Append(&pCtx->hosts, "%s", &pCtx->sLine[nPosit]) < 0)
            {
                xloge("Failed to add uncommented line to hosts buffer: %s", XSTRERR);
                return XSTDERR;
            }

            nCount++;
            continue;
        }

        if (bComment) pCtx->sLine[nPosit] = '#';
        XASSERT((XString_Append(&pCtx->hosts, "%s", pCtx->sLine) >= 0),
            xthrow("Failed to add line to hosts buffer (%s)", XSTRERR));
    }

    XFile_Close(&pCtx->file);

    if (nCount)
    {
        XASSERT((XHost_Write(pCtx) >= 0), XSTDERR);
        xlogd("Uncommented host entres: %d", nCount);
    }

    return XSTDNON;
}

static int XHost_LintEntries(xhost_ctx_t *pCtx)
{
    XHost_ClearContext(pCtx);
    XASSERT((XHost_InitContext(pCtx, XFALSE) > 0), xthrowe("Failed to init context"));

    int nCount = 0;
    while (XFile_GetLine(&pCtx->file, pCtx->sLine, sizeof(pCtx->sLine)) > 0)
    {
        xhost_entry_t entry;
        XHost_ParseEntry(&entry, pCtx->sLine);

        if (xstrused(entry.sAddr) && xstrused(entry.sHost))
        {
            size_t nAddrLen = XSTD_MIN(strlen(entry.sAddr), XHOST_ADDR_LEN_MAX);
            size_t nPadding = XHOST_ADDR_LEN_MAX - nAddrLen;

            XASSERT((XString_Append(&pCtx->hosts, "%s", entry.sAddr) >= 0),
                xthrow("Failed to add line to hosts buffer (%s)", XSTRERR));

            for (size_t i = 0; i < nPadding + pCtx->nTabSize; i++)
            {
                XASSERT((XString_Add(&pCtx->hosts, XSTR_SPACE, 1) >= 0),
                    xthrow("Failed to add spaces to the buffer (%s)", XSTRERR));
            }

            XASSERT((XString_Append(&pCtx->hosts, "%s", entry.sHost) >= 0),
                xthrow("Failed to add host line to the buffer (%s)", XSTRERR));

            if (xstrused(entry.sComment))
                XASSERT((XString_Append(&pCtx->hosts, " # %s", entry.sComment) >= 0),
                    xthrow("Failed to add comment line to hosts buffer (%s)", XSTRERR));

            XASSERT((XString_Add(&pCtx->hosts, XSTR_NEW_LINE, 1) >= 0),
                xthrow("Failed to add line to hosts buffer (%s)", XSTRERR));

            nCount++;
            continue;
        }
        else if (xstrused(entry.sComment))
        {
            XASSERT((XString_Append(&pCtx->hosts, "# %s\n", entry.sComment) >= 0),
                xthrow("Failed to add line to hosts buffer (%s)", XSTRERR));

            nCount++;
            continue;
        }

        XASSERT((XString_Append(&pCtx->hosts, "%s", pCtx->sLine) >= 0),
            xthrow("Failed to add line to hosts buffer (%s)", XSTRERR));
    }

    XFile_Close(&pCtx->file);

    if (pCtx->hosts.nLength && pCtx->hosts.pData)
    {
        XASSERT((XHost_Write(pCtx) >= 0), XSTDERR);
        xlogd("Linted host entries: %d", nCount);
    }

    return XSTDNON;
}

static void XHost_AddLineNumber(xstring_t *pString, int nLine)
{
    if (nLine < 10) XString_Append(pString, " ");
    XString_Append(pString, "  %s", XSTR_FMT_DIM);
    XString_Append(pString, "%d", nLine);
    if (nLine < 100) XString_Append(pString, " ");
    if (nLine < 1000) XString_Append(pString, " ");
    XString_Append(pString, " │%s ", XSTR_FMT_RESET);
}

static int XHost_DisplayHosts(xhost_ctx_t *pCtx, xbool_t bHideLines)
{
    XHost_ClearContext(pCtx);
    XASSERT((XHost_InitContext(pCtx, XFALSE) > 0), xthrowe("Failed to init context"));

    size_t nLineNumber = 0;
    int nPosit = 0;

    while (XFile_GetLine(&pCtx->file, pCtx->sLine, sizeof(pCtx->sLine)) > 0)
    {
        nPosit = 0;
        nLineNumber++;

        if (pCtx->bSearch && (pCtx->nLineNumber != nLineNumber && !XHost_SearchEntry(pCtx))) continue;
        while (pCtx->sLine[nPosit] && isspace((unsigned char)pCtx->sLine[nPosit])) nPosit++;
        if (!bHideLines) XHost_AddLineNumber(&pCtx->hosts, nLineNumber);

        if (!pCtx->sLine[nPosit] || pCtx->sLine[nPosit] == '\n')
        {
            XString_Append(&pCtx->hosts, "\n");
            continue;
        }

        if (pCtx->sLine[nPosit] == '#')
        {
            XHost_RemoveTailSpace(pCtx->sLine, strlen(pCtx->sLine));
            XString_Append(&pCtx->hosts, "%s%s%s\n", XSTR_FMT_DIM, pCtx->sLine, XSTR_FMT_RESET);
            continue;
        }

        int nEnd = nPosit;
        XString_Add(&pCtx->hosts, pCtx->sLine, nPosit);
        while (pCtx->sLine[nEnd] && !isspace((unsigned char)pCtx->sLine[nEnd])) nEnd++;

        char sColor[XSTR_MICRO]; // Create amethyst color
        xstrnrgb(sColor, sizeof(sColor), 198, 145, 255);

        XString_Append(&pCtx->hosts, "%s", sColor);
        XString_Add(&pCtx->hosts, &pCtx->sLine[nPosit], nEnd - nPosit);
        XString_Append(&pCtx->hosts, "%s", XSTR_FMT_RESET);

        int nCommentPosit = nEnd;
        while (pCtx->sLine[nCommentPosit] && pCtx->sLine[nCommentPosit] != '#') nCommentPosit++;

        if (pCtx->sLine[nCommentPosit] && pCtx->sLine[nCommentPosit] == '#')
        {
            XString_Add(&pCtx->hosts, &pCtx->sLine[nEnd], nCommentPosit - nEnd);
            XHost_RemoveTailSpace(&pCtx->sLine[nCommentPosit], strlen(&pCtx->sLine[nCommentPosit]));
            XString_Append(&pCtx->hosts, "%s%s%s\n", XSTR_FMT_DIM, &pCtx->sLine[nCommentPosit], XSTR_FMT_RESET);
        }
        else
        {
            size_t nLength = strlen(&pCtx->sLine[nEnd]);
            XString_Add(&pCtx->hosts, &pCtx->sLine[nEnd], nLength);
        }
    }

    if (pCtx->hosts.nLength && pCtx->hosts.pData)
    {
        char *pData = (char*)pCtx->hosts.pData;
        size_t nLength = pCtx->hosts.nLength;
        XHost_RemoveTailSpace(pData, nLength);

        xlog_useheap(XTRUE);
        xlog("%s", pData);
    }

    return XSTDNON;
}

int main(int argc, char *argv[])
{
    xlog_defaults();
    xlog_timing(XLOG_DISABLE);

    xhost_args_t args;
    if (XHost_ParseArgs(&args, argc, argv) < 0)
    {
        XHost_Usage(argv[0]);
        return XSTDERR;
    }

    xhost_ctx_t ctx;
    ctx.bWritten = XFALSE;
    int nStatus = 0;

    XASSERT((XHost_InitContext(&ctx, XTRUE) > 0),
        xthrowe("Failed to init context"));

    xstrncpy(ctx.sAddr, sizeof(ctx.sAddr), args.sAddress);
    xstrncpy(ctx.sHost, sizeof(ctx.sHost), args.sHost);
    ctx.nLineNumber = args.nLineNumber;
    ctx.bWholeWords = args.bWholeWords;
    ctx.nTabSize = args.nTabSize;
    ctx.bSearch = args.bSearch;

    XHost_ParseHeader(&ctx);

    if (args.bAppend) nStatus = XHost_AddEntry(&ctx, args.bInsertLine);
    else if (args.bUncomment) nStatus = XHost_UncommentEntry(&ctx);
    else if (args.bComment) nStatus = XHost_RemoveEntry(&ctx, XTRUE);
    else if (args.bRemove) nStatus = XHost_RemoveEntry(&ctx, XFALSE);
    if (ctx.bWritten && ctx.nTabSize) nStatus = XHost_LintEntries(&ctx);
    if (ctx.bWritten && !nStatus) nStatus = XHost_WriteHeader(&ctx);
    if (!nStatus && args.bDisplay) XHost_DisplayHosts(&ctx, args.bHideLines);

    XHost_ClearContext(&ctx);
    return nStatus;
}
