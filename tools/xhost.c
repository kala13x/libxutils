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
#define XHOST_BUILD_NUMBER  2

typedef struct {
    xbool_t bAdd;
    xbool_t bAppend;
    xbool_t bRemove;
    xbool_t bVerbose;
    xbool_t bNewLine;
    xbool_t bComment;
    xbool_t bDisplay;
    xbool_t bUncomment;
    char sAddress[XLINE_MAX];
    char sHost[XLINE_MAX];
} xhost_args_t;

typedef struct {
    char sAddr[XLINE_MAX];
    char sHost[XLINE_MAX];
    char sLine[XLINE_MAX];
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
    printf("===========================================================\n");
    printf(" XHost (Add or modify hosts) - v%d.%d build %d (%s)\n",
        XHOST_VERSION_MAX, XHOST_VERSION_MIN, XHOST_BUILD_NUMBER, __DATE__);
    printf("===========================================================\n");

    int i, nLength = strlen(pName) + 6;
    char sWhiteSpace[nLength + 1];

    for (i = 0; i < nLength; i++) sWhiteSpace[i] = ' ';
    sWhiteSpace[nLength] = 0;

    printf("Usage: %s [-a <address>] [-n <hostname>]\n", pName);
    printf(" %s [-c] [-u] [-r] [-d] [-l] [-v] [-h]\n\n", sWhiteSpace);

    printf("Options are:\n");
    printf("  -a <address>          # IP address\n");
    printf("  -n <hostname>         # Host name\n");
    printf("  -c                    # Comment entry\n");
    printf("  -u                    # Uncomment entry\n");
    printf("  -r                    # Remove entry\n");
    printf("  -l                    # Insert new line before entry\n");
    printf("  -d                    # Display /etc/hosts file\n");
    printf("  -v                    # Enable verbose logging\n");
    printf("  -h                    # Print version and usage\n\n");

    printf("Examples:\n");
    printf("1) %s -a 10.10.17.1 -n example.com\n", pName);
    printf("2) %s -a 10.12.19.1 -r\n", pName);
    printf("2) %s -n test.com -rd\n", pName);
}

static int XHost_ParseArgs(xhost_args_t *pArgs, int argc, char *argv[])
{
    memset(pArgs, 0, sizeof(xhost_args_t));
    int opt = 0;

    while ((opt = getopt(argc, argv, "a:n:c1:d1:u1:l1:r1:v1:h1")) != -1)
    {
        switch (opt)
        {
            case 'a': xstrncpy(pArgs->sAddress, sizeof(pArgs->sAddress), optarg); break;
            case 'n': xstrncpy(pArgs->sHost, sizeof(pArgs->sHost), optarg); break;
            case 'd': pArgs->bDisplay = XTRUE; break;
            case 'c': pArgs->bComment = XTRUE; break;
            case 'u': pArgs->bUncomment = XTRUE; break;
            case 'l': pArgs->bNewLine = XTRUE; break;
            case 'r': pArgs->bRemove = XTRUE; break;
            case 'v': pArgs->bVerbose = XTRUE; break;
            case 'h': return XSTDERR;
            default: return XSTDERR;
        }
    }

    xbool_t bHaveAddress = xstrused(pArgs->sAddress);
    xbool_t bHaveHost = xstrused(pArgs->sHost);
    xbool_t bModify = pArgs->bRemove || pArgs->bComment || pArgs->bUncomment;

    pArgs->bAppend = !bModify && bHaveAddress && bHaveHost;
    if (pArgs->bVerbose) xlog_enable(XLOG_DEBUG);
    if (!pArgs->bAppend && !bModify) pArgs->bDisplay = XTRUE;
    if (bModify && !bHaveAddress && !bHaveHost) return XSTDERR;

    return XSTDOK;
}

static int XHost_InitContext(xhost_ctx_t *pCtx)
{
    pCtx->sAddr[0] = XSTR_NUL;
    pCtx->sHost[0] = XSTR_NUL;
    pCtx->sLine[0] = XSTR_NUL;

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

static int XHost_Write(xstring_t *pString)
{
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

    XFile_Close(&file);
    return XSTDNON;
}

static void XHost_RemoveTailSpace(char *sEntry, size_t nLength)
{
    if (nLength == 0) return;
    int nPosit = (int)nLength - 1;

    while (nPosit >= 0 &&
           (isspace((unsigned char)sEntry[nPosit]) ||
           sEntry[nPosit] == '\n')) nPosit--;

    if (nPosit >= 0) sEntry[nPosit + 1] = XSTR_NUL;
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
    xlogd_wn("Checking entry: %s", pCtx->sLine);

    xhost_entry_t entry;
    XHost_ParseEntry(&entry, pCtx->sLine);

    if (!xstrused(entry.sAddr) ||
        !xstrused(entry.sHost)) return XFALSE;

    char sSearchLine[XLINE_MAX];
    xstrncpyf(sSearchLine, sizeof(sSearchLine), "%s %s", pCtx->sAddr, pCtx->sHost);

    xhost_entry_t search;
    XHost_ParseEntry(&search, sSearchLine);

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

    return XFALSE;
}

static int XHost_AddEntry(xhost_ctx_t *pCtx, xbool_t bNewLine)
{
    if (!xstrused(pCtx->sHost) && !xstrused(pCtx->sAddr)) return XSTDNON;
    xbool_t bFound = XFALSE;

    while (XFile_GetLine(&pCtx->file, pCtx->sLine, sizeof(pCtx->sLine)) > 0)
    {
        if (XHost_SearchEntry(pCtx))
        {
            xlogd_wn("Found entry: %s", pCtx->sLine);
            bFound = XTRUE;
            break;
        }

        XASSERT((XString_Append(&pCtx->hosts, "%s", pCtx->sLine) >= 0),
            xthrowe("Failed to add line to hosts buffer"));
    }

    XFile_Close(&pCtx->file);

    if (!bFound)
    {
        if (pCtx->hosts.pData != NULL && pCtx->hosts.nLength > 0 &&
            pCtx->hosts.pData[pCtx->hosts.nLength - 1] != '\n' &&
            XString_Append(&pCtx->hosts, "\n") < 0)
        {
            xloge("Failed to append new line to hosts file: %s", XSTRERR);
            return XSTDERR;
        }

        XASSERT((XString_Append(&pCtx->hosts, "%s%s %s\n",
            bNewLine ? XSTR_NEW_LINE : XSTR_EMPTY,
            pCtx->sAddr, pCtx->sHost) >= 0),
            xthrowe("Failed to append new host entry"));

        XASSERT((XHost_Write(&pCtx->hosts) >= 0), XSTDERR);
        xlogd("Added new entry: %s %s", pCtx->sAddr, pCtx->sHost);
    }

    return XSTDNON;
}

static int XHost_DisplayHosts()
{
    xhost_ctx_t ctx;
    XASSERT((XHost_InitContext(&ctx) > 0), xthrowe("Failed to init context"));

    while (XFile_GetLine(&ctx.file, ctx.sLine, sizeof(ctx.sLine)) > 0)
    {
        xhost_entry_t entry;
        XHost_ParseEntry(&entry, ctx.sLine);

        if (xstrused(entry.sAddr) && xstrused(entry.sHost))
        {
            XString_Append(&ctx.hosts, "%s%s%s %s",
                    XSTR_CLR_CYAN, entry.sAddr,
                    XSTR_FMT_RESET, entry.sHost);

            if (!xstrused(entry.sComment)) XString_Append(&ctx.hosts, "\n");
            else XString_Append(&ctx.hosts, " %s# %s%s\n", XSTR_FMT_DIM, entry.sComment, XSTR_FMT_RESET);
        }
        else if (!xstrused(entry.sComment)) XString_Append(&ctx.hosts, "\n");
        else XString_Append(&ctx.hosts, "%s# %s%s\n", XSTR_FMT_DIM, entry.sComment, XSTR_FMT_RESET);
    }

    if (ctx.hosts.nLength)
    {
        xlog_useheap(XTRUE);
        xlog("%s", ctx.hosts.pData);
    }

    XHost_ClearContext(&ctx);
    return XSTDNON;
}

static int XHost_RemoveEntry(xhost_ctx_t *pCtx, xbool_t bComment)
{
    int nCount = 0;

    while (XFile_GetLine(&pCtx->file, pCtx->sLine, sizeof(pCtx->sLine)) > 0)
    {
        if (XHost_SearchEntry(pCtx))
        {
            xlogd_wn("Found entry: %s", pCtx->sLine);

            if (bComment && XString_Append(&pCtx->hosts, "#%s", pCtx->sLine) < 0)
            {
                xloge("Failed to add commented line to hosts buffer: %s", XSTRERR);
                return XSTDERR;
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
        XASSERT((XHost_Write(&pCtx->hosts) >= 0), XSTDERR);
        xlogd("%s entres: %d", bComment ? "Commented" : "Removed", nCount);
    }

    return XSTDNON;
}

static int XHost_UncommentEntry(xhost_ctx_t *pCtx)
{
    int nPosit = 0;
    int nCount = 0;

    while (XFile_GetLine(&pCtx->file, pCtx->sLine, sizeof(pCtx->sLine)) > 0)
    {
        nPosit = 0;
        while (pCtx->sLine[nPosit] && isspace((unsigned char)pCtx->sLine[nPosit])) nPosit++;

        if (pCtx->sLine[nPosit] && pCtx->sLine[nPosit] == '#' && XHost_SearchEntry(pCtx))
        {
            xlogd_wn("Found entry: %s", pCtx->sLine);
            while (pCtx->sLine[nPosit] && pCtx->sLine[nPosit] == '#') nPosit++;

            if (pCtx->sLine[nPosit] && XString_Append(&pCtx->hosts, "%s", &pCtx->sLine[nPosit]) < 0)
            {
                xloge("Failed to add uncommented line to hosts buffer: %s", XSTRERR);
                return XSTDERR;
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
        XASSERT((XHost_Write(&pCtx->hosts) >= 0), XSTDERR);
        xlogd("Uncommented host entres: %d", nCount);
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
    int nStatus = 0;

    XASSERT((XHost_InitContext(&ctx) > 0), xthrowe("Failed to init context"));
    xstrncpy(ctx.sAddr, sizeof(ctx.sAddr), args.sAddress);
    xstrncpy(ctx.sHost, sizeof(ctx.sHost), args.sHost);

    if (args.bAppend) nStatus = XHost_AddEntry(&ctx, args.bNewLine);
    else if (args.bRemove) nStatus = XHost_RemoveEntry(&ctx, XFALSE);
    else if (args.bComment) nStatus = XHost_RemoveEntry(&ctx, XTRUE);
    else if (args.bUncomment) nStatus = XHost_UncommentEntry(&ctx);
    if (!nStatus && args.bDisplay) XHost_DisplayHosts();

    XHost_ClearContext(&ctx);
    return nStatus;
}
