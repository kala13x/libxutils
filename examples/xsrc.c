/*!
 *  @file libxutils/examples/xsrc.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2022  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of advanced file search based on the xUtils.
 */

#include <xutils/xstd.h>
#include <xutils/xtype.h>
#include <xutils/xstr.h>
#include <xutils/xlog.h>
#include <xutils/xfs.h>

#define XSEARCH_VERSION_MAX     1
#define XSEARCH_VERSION_MIN     0
#define XSEARCH_BUILD_NUMBER    10

#define XSEARCH_INFO_LEN        128
#define XSEARCH_TIME_LEN        12
#define XSEARCH_SIZE_LEN        32

#define XSEARCH_ARG_COLORING \
    XSTR_CLR_CYAN, \
    XSTR_FMT_RESET, \
    XSTR_FMT_DIM, \
    XSTR_FMT_RESET

static xatomic_t g_interrupted = 0;
extern char *optarg;

typedef struct {
    char sDirectory[XPATH_MAX];
    char sFileName[XNAME_MAX];
    char sText[XSTR_MID];
    xbool_t bInsensitive;
    xbool_t bRecursive;
    xbool_t bVerbose;
    int nPermissions;
    int nLinkCount;
    int nFileTypes;
    int nFileSize;
} xsearch_args_t;

void signal_callback(int sig)
{
    printf("\nInterrupted with signal: %d\n", sig);
    XSYNC_ATOMIC_SET(&g_interrupted, 1);    
}

static int XSearch_GetFileTypes(const char *pTypes)
{
    size_t i, nLen = strlen(pTypes);
    int nTypes = 0;

    for (i = 0; i < nLen; i++)
    {
        switch (pTypes[i])
        {
            case 'b': nTypes |= XF_BLOCK_DEVICE; break;
            case 'c': nTypes |= XF_CHAR_DEVICE; break;
            case 'd': nTypes |= XF_DIRECTORY; break;
            case 'f': nTypes |= XF_REGULAR; break;
            case 'l': nTypes |= XF_SYMLINK; break;
            case 'p': nTypes |= XF_PIPE; break;
            case 's': nTypes |= XF_SOCKET; break;
            default:
            {
                xloge("Invalid file type");
                return XSTDERR;
            }
        }
    }

    return nTypes;
}

static int XSearch_GetPermissins(const char *pPerm)
{
    xmode_t nMode = 0;
    if (XPath_PermToMode(pPerm, &nMode) < 0)
    {
        xloge("Invalid permissions");
        return XSTDERR;
    }

    char sChmod[32];
    XPath_ModeToChmod(sChmod, sizeof(sChmod), nMode);

    return atoi(sChmod);
}

void XSearch_Usage(const char *pName)
{
    printf("==========================================================\n");
    printf("Advanced File Search - Version: %d.%d build %d (%s)\n", 
        XSEARCH_VERSION_MAX, XSEARCH_VERSION_MIN, XSEARCH_BUILD_NUMBER, __DATE__);
    printf("==========================================================\n");

    int i, nLength = strlen(pName) + 6;
    char sWhiteSpace[nLength + 1];

    for (i = 0; i < nLength; i++) sWhiteSpace[i] = ' ';
    sWhiteSpace[nLength] = 0;

    printf("Usage: %s [-f <name>] [-n <bytes>] [-g <text>] [-i]\n", pName);
    printf(" %s [-d <target_path>] [-l <link_count>] [-r]\n", sWhiteSpace);
    printf(" %s [-p <permissions>] [-t <types>] [-h] [-v]\n\n", sWhiteSpace);

    printf("Options are:\n");
    printf("  %s-d%s <target_path>    %s# Target directory path%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-f%s <file_name>      %s# Target file name%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-g%s <grep_text>      %s# Search file containing the text%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-n%s <file_size>      %s# Target file size in bytes%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-l%s <link_count>     %s# Target file link count%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-p%s <permissions>    %s# Target file permissions (e.g. 'rwxr-xr--')%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-t%s <types>          %s# Target file types (*)%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-i%s                  %s# Case insensitive search%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-r%s                  %s# Recursive search target directory%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-v%s                  %s# Display additional information (verbose)%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-h%s                  %s# Display version and usage information%s\n\n", XSEARCH_ARG_COLORING);

    printf("File types (*):\n");
    printf("   %sb%s: block device\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sc%s: character device\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sd%s: directory\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sf%s: regular file\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sl%s: symbolic link\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sp%s: pipe\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %ss%s: socket\n\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);

    printf("Notes:\n");
    printf("   1) <file_name> option is supporting wildcard character: '%s*%s'\n", XSTR_FMT_BOLD, XSTR_FMT_RESET);
    printf("   2) <file_types> option is supporting one and more file types: %s-t ldb%s\n", XSTR_FMT_BOLD, XSTR_FMT_RESET);
    printf("   3) One or more <file_name> argument can be specified by using delimiter: '%s;%s'\n\n", XSTR_FMT_BOLD, XSTR_FMT_RESET);

    printf("Examples:\n");
    printf("%sRecursive search of every symlink or a regular file in the root file%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("%ssystem that has permissions 777 and contains \".log\" in the file name:%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("%s[xutils@examples]$ %s -rvd / -t lf -f \"*.log\" -p rwxrwxrwx%s\n\n", XSTR_FMT_BOLD, pName, XSTR_FMT_RESET);

    printf("%sRecursive search of every .cpp and .java file in the \"/opt\" directory%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("%sthat contains the case insensitive text \"socket\" and verbose output:%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("%s[xutils@examples]$ %s -rvd /opt -f \"*.cpp;*.java\" -ig test%s\n\n", XSTR_FMT_BOLD, pName, XSTR_FMT_RESET);
}


static int XSearch_ParseArgs(xsearch_args_t *pArgs, int argc, char *argv[])
{
    memset(pArgs, 0, sizeof(xsearch_args_t));
    pArgs->sDirectory[0] = '.';
    pArgs->sDirectory[1] = '/';
    pArgs->sDirectory[2] = '\0';
    pArgs->nLinkCount = -1;
    pArgs->nFileSize = -1;
    int opt = 0;

    while ((opt = getopt(argc, argv, "d:f:g:p:t:l:n:i1:r1:v1:h1")) != -1)
    {
        switch (opt)
        {
            case 'd':
                xstrncpy(pArgs->sDirectory, sizeof(pArgs->sDirectory), optarg);
                break;
            case 'f':
                xstrncpy(pArgs->sFileName, sizeof(pArgs->sFileName), optarg);
                break;
            case 'g':
                xstrncpy(pArgs->sText, sizeof(pArgs->sText), optarg);
                break;
            case 'p':
                pArgs->nPermissions = XSearch_GetPermissins(optarg);
                break;
            case 't':
                pArgs->nFileTypes = XSearch_GetFileTypes(optarg);
                break;
            case 'l':
                pArgs->nLinkCount = atol(optarg);
                break;
            case 'n':
                pArgs->nFileSize = atol(optarg);
                break;
            case 'i':
                pArgs->bInsensitive = XTRUE;
                break;
            case 'r':
                pArgs->bRecursive = XTRUE;
                break;
            case 'v':
                pArgs->bVerbose = 1;
                break;
            case 'h':
            default:
                return 0;
        }
    }

    if (pArgs->bInsensitive && xstrused(pArgs->sFileName))
    {
        xstrcase(pArgs->sFileName, XSTR_LOWER);
        xstrcase(pArgs->sText, XSTR_LOWER);
    }

    /* Validate opts */
    if (pArgs->nPermissions < 0 || 
        pArgs->nFileTypes < 0) 
            return 0;

    return 1;
}

static void XSearch_ColorizeEntry(char *pOutput, size_t nSize, xfile_entry_t *pEntry)
{
    xbool_t bIsExec = pEntry->sPerm[XPERM_LEN-1] == 'x' ? XTRUE : XFALSE;
    char *pColor = XSTR_EMPTY;
    char *pBack = XSTR_EMPTY;
    char *pFmt = XSTR_EMPTY;

    if (pEntry->eType == XF_SYMLINK)
    {
        if (pEntry->pRealPath == NULL)
        {
            pColor = XSTR_CLR_RED;
            pBack = XSTR_BACK_BLACK;
            pFmt = XSTR_FMT_BOLD;
        }
        else
        {
            pColor = XSTR_CLR_CYAN;
            pFmt = XSTR_FMT_BOLD;
        }
    }
    else if (pEntry->eType == XF_DIRECTORY)
    {
        pColor = XSTR_CLR_BLUE;
        pFmt = XSTR_FMT_BOLD;
    }
    else if (pEntry->eType == XF_SOCKET)
    {
        pColor = XSTR_CLR_MAGENTA;
        pFmt = XSTR_FMT_BOLD;
    }
    else if (pEntry->eType == XF_PIPE)
    {
        pColor = XSTR_CLR_YELLOW;
        pBack = XSTR_BACK_BLACK;
    }
    else if (pEntry->eType == XF_REGULAR && bIsExec)
    {
        pColor = XSTR_CLR_GREEN;
        pFmt = XSTR_FMT_BOLD;
    }
    else if (pEntry->eType == XF_CHAR_DEVICE ||
            pEntry->eType == XF_BLOCK_DEVICE)
    {
        pColor = XSTR_CLR_YELLOW;
        pBack = XSTR_BACK_BLACK;
        pFmt = XSTR_FMT_BOLD;
    }

    size_t nOffset = 0;
    while (!strncmp(&pEntry->sPath[nOffset], "./", 2)) nOffset += 2;

    xstrncpyf(pOutput, nSize, "%s%s%s%s%s%s", pColor, pFmt, pBack,
        &pEntry->sPath[nOffset], pEntry->sName, XSTR_FMT_RESET);
}

static void XSearch_ColorizeSymlink(char *pSimlink, size_t nSize, xfile_entry_t *pEntry)
{
    if (pEntry->eType == XF_SYMLINK)
    {
        if (pEntry->pRealPath != NULL)
        {
            struct stat statbuf;
            xfile_entry_t linkEntry;
            xstat(pEntry->pRealPath, &statbuf);

            XFile_CreateEntry(&linkEntry, NULL, pEntry->sLink, &statbuf);
            XSearch_ColorizeEntry(pSimlink, nSize, &linkEntry);
        }
        else
        {
            xstrncpyf(pSimlink, nSize, "%s%s%s%s",
                XSTR_FMT_BOLD, XSTR_BACK_RED,
                pEntry->sLink, XSTR_FMT_RESET);
        }
    }
}

static void XSearch_DisplayEntry(xfile_search_t *pSearch, xfile_entry_t *pEntry)
{
    char sEntry[XPATH_MAX], sLinkPath[XPATH_MAX], sTime[XSEARCH_TIME_LEN + 1];
    const char *pArrow = pEntry->eType == XF_SYMLINK ? " -> " : XSTR_EMPTY;

    sLinkPath[0] = sEntry[0] = sTime[0] = XSTR_NUL;
    XSearch_ColorizeEntry(sEntry, sizeof(sEntry), pEntry);
    XSearch_ColorizeSymlink(sLinkPath, sizeof(sLinkPath), pEntry);

    /* Do not display additional info if verbose is not set */
    if (!((xsearch_args_t*)pSearch->pUserCtx)->bVerbose)
    {
        xlog("%s%s%s", sEntry, pArrow, sLinkPath);
        return;
    }

    /* Get username and group from uid and gid */
    struct passwd *pws = getpwuid(pEntry->nUID);
    struct group *grp = getgrgid(pEntry->nGID);
    const char *pUname = pws ? pws->pw_name : XSTR_EMPTY;
    const char *pGname = grp ? grp->gr_name : XSTR_EMPTY;

    /* Get last modification time */
    const char *pTime = ctime(&pEntry->nTime);
    if (pTime != NULL) xstrncpy(sTime, sizeof(sTime), &pTime[4]);

    /* Create size string with indentation */
    char sSize[XSEARCH_SIZE_LEN], sRound[XSEARCH_SIZE_LEN + 8];
    XBytesToUnit(sSize, sizeof(sSize), pEntry->nSize, XTRUE);
    xstrnlcpyf(sRound, sizeof(sRound), 7, XSTR_SPACE_CHAR, "%s", sSize);

    xlog("%c%s %lu %s %s %s [%s] %s%s%s",
        XFile_GetTypeChar(pEntry->eType),
        pEntry->sPerm, pEntry->nLinkCount,
        pUname, pGname, sRound, sTime,
        sEntry, pArrow, sLinkPath);
}

int XSearch_Callback(xfile_search_t *pSearch, xfile_entry_t *pEntry, const char *pMsg)
{
    if (pEntry != NULL) XSearch_DisplayEntry(pSearch, pEntry);
    if (pMsg != NULL) xloge("%s (%s)", pMsg, strerror(errno));
    return XSTDNON;
}

int main(int argc, char* argv[])
{
    xlog_defaults();
    xsearch_args_t args;
    xfile_search_t srcCtx;

    if (!XSearch_ParseArgs(&args, argc, argv))
    {
        XSearch_Usage(argv[0]);
        return XSTDERR;
    }

    const char *pDirectory = xstrused(args.sDirectory) ? args.sDirectory : NULL;
    const char *pFileName = xstrused(args.sFileName) ? args.sFileName : NULL;

    XFile_SearchInit(&srcCtx, pFileName);
    xstrncpy(srcCtx.sText, sizeof(srcCtx.sText), args.sText);
    srcCtx.nPermissions = args.nPermissions;
    srcCtx.bInsensitive = args.bInsensitive;
    srcCtx.bRecursive = args.bRecursive;
    srcCtx.nFileTypes = args.nFileTypes;
    srcCtx.nLinkCount = args.nLinkCount;
    srcCtx.nFileSize = args.nFileSize;
    srcCtx.callback = XSearch_Callback;
    srcCtx.pUserCtx = &args;

    srcCtx.pInterrupted = &g_interrupted;
    signal(SIGINT, signal_callback);

    XFile_Search(&srcCtx, pDirectory);
    XFile_SearchDestroy(&srcCtx);

    return XSTDNON;
}
