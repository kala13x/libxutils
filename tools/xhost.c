/*!
 *  @file libxutils/tools/xhost.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Modify hosts file, add or remove entries.
 */

#define _XUTILS_DEBUG

#include "xstd.h"
#include "xfs.h"
#include "str.h"
#include "log.h"
#include "cli.h"

#define XHOST_FILE_PATH     "/etc/hosts"
#define XHOST_VERSION_MAX   1
#define XHOST_VERSION_MIN   0
#define XHOST_BUILD_NUMBER  1

typedef struct {
    xbool_t bAdd;
    xbool_t bRemove;
    xbool_t bVerbose;
    xbool_t bNewLine;
    xbool_t bComment;
    xbool_t bUncomment;
    char sAddress[XLINE_MAX];
    char sHost[XLINE_MAX];
} xhost_args_t;

extern char *optarg;

static void XHost_Usage(const char *pName)
{
    printf("===========================================================\n");
    printf(" XHost (Add or modify hosts) - v%d.%d build %d (%s)\n",
        XHOST_VERSION_MAX, XHOST_VERSION_MIN, XHOST_BUILD_NUMBER, __DATE__);
    printf("===========================================================\n");

    printf("Usage: %s [-a <address>] [-n <host>] [-l] [-r] [-v] [-h]\n\n", pName);
    printf("Options are:\n");
    printf("  -a <address>          # IP address\n");
    printf("  -n <host>             # Host name\n");
    printf("  -l                    # Insert new line before entry\n");
    printf("  -c                    # Comment host and/or address entry\n");
    printf("  -u                    # Uncomment host and/or address entry\n");
    printf("  -r                    # Remove host and/or address entry\n");
    printf("  -v                    # Enable verbose logging\n");
    printf("  -h                    # Print version and usage\n\n");

    printf("Examples:\n");
    printf("1) %s -a 10.10.17.1 -n example.com\n", pName);
    printf("2) %s -a 10.12.19.1 -r\n", pName);
    printf("2) %s -n test.com -r\n", pName);
}

static int XHost_ParseArgs(xhost_args_t *pArgs, int argc, char *argv[])
{
    memset(pArgs, 0, sizeof(xhost_args_t));
    int opt = 0;

    while ((opt = getopt(argc, argv, "a:n:c1:u1:l1:r1:v1:h1")) != -1)
    {
        switch (opt)
        {
            case 'a': xstrncpy(pArgs->sAddress, sizeof(pArgs->sAddress), optarg); break;
            case 'n': xstrncpy(pArgs->sHost, sizeof(pArgs->sHost), optarg); break;
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

    if (!bModify && (!bHaveAddress || !bHaveHost)) return XSTDERR;
    if (bModify && !bHaveAddress && !bHaveHost) return XSTDERR;

    return XSTDOK;
}

static int XHost_Write(xstring_t *pString)
{
    xfile_t file;
    if (XFile_Open(&file, XHOST_FILE_PATH, "cwt", NULL) < 0)
    {
        xloge("Failed to open hosts file: %s", XSTRERR);
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

static xbool_t XHost_SearchEntry(const char *pLine, const char *pAddress, const char *pHost)
{
    xlogd_wn("Checking entry: %s", pLine);

    if (xstrused(pHost) && xstrused(pAddress))
    {
        if (xstrsrc(pLine, pHost) >= 0 &&
            xstrsrc(pLine, pAddress) >= 0)
                return XTRUE;
    }
    else 
    {
        if (xstrused(pHost) && xstrsrc(pLine, pHost) >= 0) return XTRUE;
        if (xstrused(pAddress) && xstrsrc(pLine, pAddress) >= 0) return XTRUE;
    }

    return XFALSE;
}

static int XHost_AddEntry(const char *pAddress, const char *pHost, xbool_t bNewLine)
{
    xfile_t file;
    if (XFile_Open(&file, XHOST_FILE_PATH, "r", NULL) < 0)
    {
        xloge("Failed to open hosts file: %s", XSTRERR);
        return XSTDERR;
    }

    char sLine[XLINE_MAX];
    xbool_t bFound = XFALSE;

    xstring_t hosts;
    XString_Init(&hosts, XLINE_MAX, XFALSE);

    while (XFile_GetLine(&file, sLine, sizeof(sLine)) > 0)
    {
        int nPosit = 0;
        while (sLine[nPosit] && isspace((unsigned char)sLine[nPosit])) nPosit++;

        if (sLine[nPosit] && sLine[nPosit] != '#' && XHost_SearchEntry(sLine, pAddress, pHost))
        {
            xlogd_wn("Found entry: %s", sLine);
            bFound = XTRUE;
            break;
        }

        if (XString_Append(&hosts, "%s", sLine) < 0)
        {
            xloge("Failed to add line to hosts buffer: %s", XSTRERR);
            XString_Clear(&hosts);
            XFile_Close(&file);
            return XSTDERR;
        }
    }

    XFile_Close(&file);

    if (!bFound)
    {
        if (hosts.pData != NULL &&
            hosts.nLength > 0 &&
            hosts.pData[hosts.nLength - 1] != '\n' &&
            XString_Append(&hosts, "\n") < 0)
        {
            xloge("Failed to append new line to hosts file: %s", XSTRERR);
            XString_Clear(&hosts);
            return XSTDERR;
        }

        const char *pNewLine = bNewLine ? XSTR_NEW_LINE : XSTR_EMPTY;
        if (XString_Append(&hosts, "%s%s %s\n", pNewLine, pAddress, pHost) < 0)
        {
            xloge("Failed to append new host entry: %s", XSTRERR);
            XString_Clear(&hosts);
            return XSTDERR;
        }

        if (XHost_Write(&hosts) < 0)
        {
            XString_Clear(&hosts);
            return XSTDERR;
        }

        xlogd("Added new entry: %s %s", pAddress, pHost);
    }

    XString_Clear(&hosts);
    return XSTDNON;
}

static int XHost_RemoveEntry(const char *pHost, const char *pAddress, xbool_t bComment)
{
    xfile_t file;
    if (XFile_Open(&file, XHOST_FILE_PATH, "r", NULL) < 0)
    {
        xloge("Failed to open hosts file: %s", XSTRERR);
        return XSTDERR;
    }

    char sLine[XLINE_MAX];
    int nCount = 0;

    xstring_t hosts;
    XString_Init(&hosts, XLINE_MAX, XFALSE);

    while (XFile_GetLine(&file, sLine, sizeof(sLine)) > 0)
    {
        int nPosit = 0;
        while (sLine[nPosit] && isspace((unsigned char)sLine[nPosit])) nPosit++;

        if (sLine[nPosit] && sLine[nPosit] != '#' && XHost_SearchEntry(sLine, pAddress, pHost))
        {
            xlogd_wn("Found entry: %s", sLine);

            if (bComment && XString_Append(&hosts, "#%s", sLine) < 0)
            {
                xloge("Failed to add commented line to hosts buffer: %s", XSTRERR);
                XString_Clear(&hosts);
                XFile_Close(&file);
                return XSTDERR;
            }

            nCount++;
            continue;
        }

        if (XString_Append(&hosts, "%s", sLine) < 0)
        {
            xloge("Failed to add line to hosts buffer: %s", XSTRERR);
            XString_Clear(&hosts);
            XFile_Close(&file);
            return XSTDERR;
        }
    }

    XFile_Close(&file);

    if (nCount)
    {
        if (XHost_Write(&hosts) < 0)
        {
            XString_Clear(&hosts);
            return XSTDERR;
        }

        xlogd("%s entres: %d", bComment ? "Commented" : "Removed", nCount);
    }

    XString_Clear(&hosts);
    return XSTDNON;
}

static int XHost_UncommentEntry(const char *pHost, const char *pAddress)
{
    xfile_t file;
    if (XFile_Open(&file, XHOST_FILE_PATH, "r", NULL) < 0)
    {
        xloge("Failed to open hosts file: %s", XSTRERR);
        return XSTDERR;
    }

    char sLine[XLINE_MAX];
    int nCount = 0;

    xstring_t hosts;
    XString_Init(&hosts, XLINE_MAX, XFALSE);

    while (XFile_GetLine(&file, sLine, sizeof(sLine)) > 0)
    {
        int nPosit = 0;
        while (sLine[nPosit] && isspace((unsigned char)sLine[nPosit])) nPosit++;

        if (sLine[nPosit] && sLine[nPosit] == '#' && XHost_SearchEntry(sLine, pAddress, pHost))
        {
            xlogd_wn("Found entry: %s", sLine);
            while (sLine[nPosit] && sLine[nPosit] == '#') nPosit++;

            if (sLine[nPosit] && XString_Append(&hosts, "%s", &sLine[nPosit]) < 0)
            {
                xloge("Failed to add uncommented line to hosts buffer: %s", XSTRERR);
                XString_Clear(&hosts);
                XFile_Close(&file);
                return XSTDERR;
            }

            nCount++;
            continue;
        }

        if (XString_Append(&hosts, "%s", sLine) < 0)
        {
            xloge("Failed to add line to hosts buffer: %s", XSTRERR);
            XString_Clear(&hosts);
            XFile_Close(&file);
            return XSTDERR;
        }
    }

    XFile_Close(&file);

    if (nCount)
    {
        if (XHost_Write(&hosts) < 0)
        {
            XString_Clear(&hosts);
            return XSTDERR;
        }

        xlogd("Uncommented entres: %d", nCount);
    }

    XString_Clear(&hosts);
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

    if (args.bVerbose)
    {
        xlog_enable(XLOG_DEBUG);
        xlog_indent(XTRUE);
    }

    if (args.bRemove) return XHost_RemoveEntry(args.sHost, args.sAddress, XFALSE);
    if (args.bComment) return XHost_RemoveEntry(args.sHost, args.sAddress, XTRUE);
    if (args.bUncomment) return XHost_UncommentEntry(args.sHost, args.sAddress);
    return XHost_AddEntry(args.sAddress, args.sHost, args.bNewLine);
}
