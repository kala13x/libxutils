/*!
 *  @file libxutils/examples/xpass.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2022  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Simple and safe password manager for CLI
 */

#include "xstd.h"
#include "xjson.h"
#include "xtype.h"
#include "xlog.h"
#include "xstr.h"
#include "xcli.h"
#include "xver.h"
#include "xfs.h"
#include "crypt.h"
#include "crc32.h"
#include "md5.h"
#include "rsa.h"
#include "aes.h"

#define XPASS_VER_MAX       0
#define XPASS_VER_MIN       2
#define XPASS_BUILD_NUM     6

#define XPASS_AES_LEN       128
#define XPASS_NAME_LEN      6
#define XPASS_HEX_COLUMNS   16
#define XPASS_CIPHERS       "base64:aes:xor:hex"
#define XPASS_DATABASE      "/var/lib/xpass/data.xb"
#define XPASS_CONFIG        ".config/xpass/config.json"
#define XPASS_DIR_CHMOD     "rwxrwxr-x"

#define XPASS_FRAME_FMT     XSTR_FMT_DIM
#define XPASS_ENTRY_FMT     XSTR_CLR_NONE
#define XPASS_NAME_FMT      XSTR_CLR_GREEN

#define XPASS_ARG_FMT \
    XSTR_CLR_CYAN, \
    XSTR_FMT_RESET, \
    XSTR_FMT_DIM, \
    XSTR_FMT_RESET

#define XPASS_CORNER        "+"
#define XPASS_LINE          "-"
#define XPASS_EDGE          "|"

extern char *optarg;

typedef struct
{
    const char *pName;
    const char *pAddr;
    const char *pUser;
    const char *pPass;
    const char *pDesc;
} xpass_entry_t;

typedef struct
{
    char sUser[XSTR_TINY];
    char sName[XSTR_TINY];
    char sPass[XSTR_TINY];
    char sAddr[XSTR_MIN];
    char sDesc[XSTR_MIN];

    char sMasterPass[XSTR_TINY];
    char sCiphers[XPATH_MAX];
    char sFile[XPATH_MAX];
    char sConf[XPATH_MAX];
    char sKey[XSTR_TINY];

    size_t nAESKeyLength;
    size_t nFrameLength;
    size_t nNameLength;
    xjson_t dataBase;

    xbool_t bInitData;
    xbool_t bInitConf;
    xbool_t bRead;
    xbool_t bWrite;
    xbool_t bForce;
    xbool_t bDelete;
    xbool_t bUpdate;

    char cCorner;
    char cLine;
    char cEdge;
} xpass_ctx_t;

void XPass_InitCtx(xpass_ctx_t *pCtx)
{
    memset(pCtx, 0, sizeof(xpass_ctx_t));
    pCtx->dataBase.pRootObj = NULL;
    const char *pHomeDir;

    if ((pHomeDir = getenv("HOME")) == NULL) pHomeDir = getpwuid(getuid())->pw_dir;
    xstrncpyf(pCtx->sConf, sizeof(pCtx->sConf), "%s/%s", pHomeDir, XPASS_CONFIG);

    pCtx->cCorner = XPASS_CORNER[0];
    pCtx->cLine = XPASS_LINE[0];
    pCtx->cEdge = XPASS_EDGE[0];
}

static char *XPass_WhiteSpace(const int nLength)
{
    static char sRetVal[XSTR_MIN];
    xstrnul(sRetVal);
    int i = 0;

    int nLen = XSTD_MIN(nLength, sizeof(sRetVal) - 1);
    for (i = 0; i < nLen; i++) sRetVal[i] = ' ';

    sRetVal[i] = '\0';
    return sRetVal;
}

static void XPass_DisplayUsage(const char *pName)
{
    int nLength = strlen(pName) + 6;

    xlog("==============================================================");
    xlog(" Secure Password Manager for CLI - v%d.%d build %d (%s)",
        XPASS_VER_MAX, XPASS_VER_MIN, XPASS_BUILD_NUM, __DATE__);
    xlog("==============================================================");

    xlog("Usage: %s [-c <path>] [-i <path>] [-k <size>] [-a <addr>]", pName);
    xlog(" %s [-d <desc>] [-n <name>] [-u <user>] [-p <pass>]", XPass_WhiteSpace(nLength));
    xlog(" %s [-C <ciphers>] [-P <pass>] [-I] [-J] [-F] [-D]", XPass_WhiteSpace(nLength));
    xlog(" %s [-R] [-W] [-U] [-h]\n", XPass_WhiteSpace(nLength));

    xlog("These arguments are optional:");
    xlog("   %s-a%s <addr>               %s# Address value of the entry%s", XPASS_ARG_FMT);
    xlog("   %s-d%s <desc>               %s# Description value of the entry%s", XPASS_ARG_FMT);
    xlog("   %s-n%s <name>               %s# Unique name value of the entry%s", XPASS_ARG_FMT);
    xlog("   %s-u%s <user>               %s# Username value of the the entry%s", XPASS_ARG_FMT);
    xlog("   %s-p%s <pass>               %s# Password value of the the entry (not safe)%s%s*%s\n",
                                                XPASS_ARG_FMT, XSTR_CLR_RED, XSTR_FMT_RESET);
    xlog("   %s-P%s <pass>               %s# Master password from CLI (not safe)%s%s*%s",
                                                XPASS_ARG_FMT, XSTR_CLR_RED, XSTR_FMT_RESET);

    xlog("   %s-C%s <ciphers>            %s# Cipher or ciphers by encryption order%s", XPASS_ARG_FMT);
    xlog("   %s-c%s <path>               %s# Configuration file path%s", XPASS_ARG_FMT);
    xlog("   %s-i%s <path>               %s# Input/Database file path%s", XPASS_ARG_FMT);
    xlog("   %s-k%s <size>               %s# AES encryt/decrypt key size%s", XPASS_ARG_FMT);
    xlog("   %s-f%s                      %s# Force overwrite db/cfg files%s\n", XPASS_ARG_FMT);
    xlog("   %s-h%s                      %s# Version and usage%s\n", XPASS_ARG_FMT);

    xlog("Required one operation from this list%s*%s:", XSTR_CLR_RED, XSTR_FMT_RESET);
    xlog("   %s-I%s                      %s# Initialize database file%s", XPASS_ARG_FMT);
    xlog("   %s-J%s                      %s# Initialize JSON config file%s", XPASS_ARG_FMT);
    xlog("   %s-D%s                      %s# Delete entry from the database%s", XPASS_ARG_FMT);
    xlog("   %s-R%s                      %s# Search or read entries from the database%s", XPASS_ARG_FMT);
    xlog("   %s-W%s                      %s# Write new entry to the database%s", XPASS_ARG_FMT);
    xlog("   %s-U%s                      %s# Update existing entry in the database%s\n", XPASS_ARG_FMT);

    xlog("Supported ciphers:");
    xlog("   aes");
    xlog("   hex");
    xlog("   xor");
    xlog("   base64");
    xlog("   reverse\n");

    xlog("%sNotes:%s", XSTR_CLR_YELLOW, XSTR_FMT_RESET);
    xlog("%s1%s) If you do not specify an argument password (-p or -P),", XSTR_FMT_BOLD, XSTR_FMT_RESET);
    xlog("the program will prompt you to enter the password securely.%s*%s\n", XSTR_CLR_RED, XSTR_FMT_RESET);

    xlog("%s2%s) The delimiter \":\" can be used to specify more than one ciphers.", XSTR_FMT_BOLD, XSTR_FMT_RESET);
    xlog("The program will use the ciphers to encrypt/decrypt database by order.\n");

    xlog("%s3%s) Unique entry name (-n <name>) option must be used while reading the", XSTR_FMT_BOLD, XSTR_FMT_RESET);
    xlog("database to read password value of the entry, otherwise all the values");
    xlog("will be displayed from the found entry except the username and password.\n");

    xlog("%sExamples:%s", XSTR_CLR_YELLOW, XSTR_FMT_RESET);
    xlog("Initialize database and config files with default values.");
    xlog("%s[xutils@examples]$ %s -IJ -i /my/data.xb -c ~/.config/xpass/config.json%s\n", XSTR_FMT_DIM, pName, XSTR_FMT_RESET);

    xlog("Read github.com entry from the database for username \"kala13x\".");
    xlog("%s[xutils@examples]$ %s -R -a github.com -u kala13x%s\n", XSTR_FMT_DIM, pName, XSTR_FMT_RESET);

    xlog("Update description in the database for the entry with name \"FB231\".");
    xlog("%s[xutils@examples]$ %s -U -n FB231 -d \"Personal account\"%s\n", XSTR_FMT_DIM, pName, XSTR_FMT_RESET);

    xlog("Read all entries for user \"kala\" and use custom cipher order to decryt database.");
    xlog("%s[xutils@examples]$ %s -R -u kala -c \"hex:aes:xor:base64\"%s\n", XSTR_FMT_DIM, pName, XSTR_FMT_RESET);
}

static xbool_t XPass_GetPass(xpass_ctx_t *pCtx)
{
    if (XCLI_GetPass("Enter password for entry: ", pCtx->sPass, sizeof(pCtx->sPass)) < 0)
    {
        xloge("Failed to read password: %d", errno);
        return XFALSE;
    }

    char sPwd[XSTR_MIN];
    if (XCLI_GetPass("Re-enter password for entry: ", sPwd, sizeof(sPwd)) < 0)
    {
        xloge("Failed to read password: %d", errno);
        return XFALSE;
    }

    if (xstrused(pCtx->sPass) && xstrused(sPwd) && strcmp(pCtx->sPass, sPwd))
    {
        xloge("Password do not match.");
        return XFALSE;
    }

    return XTRUE;
}

static xbool_t XPass_GetKey(xpass_ctx_t *pCtx)
{
    char sPwd[XSTR_MIN];
    uint8_t *pMasterPwd = NULL;
    size_t nLength = 0;

    if (xstrused(pCtx->sMasterPass))
    {
        pMasterPwd = (uint8_t*)pCtx->sMasterPass;
        nLength = strnlen(pCtx->sMasterPass, sizeof(pCtx->sMasterPass) - 1);
    }
    else
    {
        if (XCLI_GetPass("Enter master password: ", sPwd, sizeof(sPwd)) <= 0)
        {
            xloge("Failed to read master password: %d", errno);
            return XFALSE;
        }

        if (!pCtx->bRead)
        {
            char sPwd2[XSTR_MIN];

            if (XCLI_GetPass("Re-enter master password: ", sPwd2, sizeof(sPwd2)) <= 0)
            {
                xloge("Failed to read password: %d", errno);
                return XFALSE;
            }

            if (strcmp(sPwd, sPwd2))
            {
                xloge("Password do not match.");
                return XFALSE;
            }
        }

        pMasterPwd = (uint8_t*)sPwd;
        nLength = strnlen(sPwd, sizeof(sPwd) - 1);

    }

    char *pCrypted = XMD5_Sum(pMasterPwd, nLength);
    if (pCrypted == NULL)
    {
        xloge("Failed to crypt master password: %d", errno);
        return XFALSE; 
    }

    xstrncpy(pCtx->sKey, sizeof(pCtx->sKey), pCrypted);
    free(pCrypted);

    return XTRUE;
}

static xbool_t XPass_ParseArgs(xpass_ctx_t *pCtx, int argc, char *argv[])
{
    int nChar = 0;
    while ((nChar = getopt(argc, argv, "a:c:C:d:n:i:u:p:k:P:f1:D1:I1:J1:R1:W1:U1:h1")) != -1)
    {
        switch (nChar)
        {
            case 'a':
                xstrncpy(pCtx->sAddr, sizeof(pCtx->sAddr), optarg);
                break;
            case 'c':
                xstrncpy(pCtx->sConf, sizeof(pCtx->sConf), optarg);
                break;
            case 'C':
                xstrncpy(pCtx->sCiphers, sizeof(pCtx->sCiphers), optarg);
                break;
            case 'd':
                xstrncpy(pCtx->sDesc, sizeof(pCtx->sDesc), optarg);
                break;
            case 'n':
                xstrncpy(pCtx->sName, sizeof(pCtx->sName), optarg);
                break;
            case 'i':
                xstrncpy(pCtx->sFile, sizeof(pCtx->sFile), optarg);
                break;
            case 'u':
                xstrncpy(pCtx->sUser, sizeof(pCtx->sUser), optarg);
                break;
            case 'p':
                xstrncpy(pCtx->sPass, sizeof(pCtx->sPass), optarg);
                break;
            case 'P':
                xstrncpy(pCtx->sMasterPass, sizeof(pCtx->sMasterPass), optarg);
                break;
            case 'k':
                pCtx->nAESKeyLength = (size_t)atoi(optarg);
                break;
            case 'f':
                pCtx->bForce = XTRUE;
                break;
            case 'D':
                pCtx->bDelete = XTRUE;
                break;
            case 'I':
                pCtx->bInitData = XTRUE;
                break;
            case 'J':
                pCtx->bInitConf = XTRUE;
                break;
            case 'R':
                pCtx->bRead = XTRUE;
                break;
            case 'W':
                pCtx->bWrite = XTRUE;
                break;
            case 'U':
                pCtx->bUpdate = XTRUE;
                break;
            case 'h':
            default:
                XPass_DisplayUsage(argv[0]);
                return XFALSE;
        }
    }

    if (!pCtx->bInitData &&
        !pCtx->bInitConf &&
        !pCtx->bRead &&
        !pCtx->bWrite &&
        !pCtx->bDelete &&
        !pCtx->bUpdate)
    {
        xloge("Please specify the operation.");
        XPass_DisplayUsage(argv[0]);
        return XFALSE;
    }

    if (!xstrused(pCtx->sConf))
    {
        xloge("Invalid or missing config file argument.");
        XPass_DisplayUsage(argv[0]);
        return XFALSE;  
    }

    return XTRUE;
}

static xbool_t XPass_FindEntry(xpass_ctx_t *pCtx, const char *pName)
{
    if (!xstrused(pName)) return XFALSE;
    xjson_t *pJson = &pCtx->dataBase;

    xjson_obj_t *pArrObj = XJSON_GetObject(pJson->pRootObj, "entries");
    if (pArrObj == NULL)
    {
        xloge("Database file does not contain entries.");
        return XFALSE;
    }

    size_t i, nLength = XJSON_GetArrayLength(pArrObj);
    for (i = 0; i < nLength; i++)
    {
        xjson_obj_t *pItem = XJSON_GetArrayItem(pArrObj, i);
        if (pItem != NULL)
        {
            const char *pEntryName = XJSON_GetString(XJSON_GetObject(pItem, "name"));
            if (xstrused(pEntryName) && !strcmp(pName, pEntryName)) return XTRUE;
        }
    }

    return XFALSE;
}

static xbool_t XPass_GetName(xpass_ctx_t *pCtx)
{
    printf("Enter unique name of the entry: ");
    XASSERT((fgets(pCtx->sName, sizeof(pCtx->sName), stdin)), XFALSE);

    size_t nLength = strlen(pCtx->sName);
    pCtx->sName[nLength - 1] = XSTR_NUL;
    return nLength ? XTRUE : XFALSE;
}

static xbool_t XPass_GenerateName(xpass_ctx_t *pCtx)
{
    if (!xstrrand(pCtx->sName, sizeof(pCtx->sName), pCtx->nNameLength, XFALSE, XTRUE)) return XFALSE;
    if (XPass_FindEntry(pCtx, pCtx->sName)) return XPass_GenerateName(pCtx);
    return XTRUE;
}

static xbool_t XPass_ParseConfig(xpass_ctx_t *pCtx)
{
    xbyte_buffer_t buffer;
    if (!XPath_LoadBuffer(pCtx->sConf, &buffer))
    {
        xloge("Can not parse config file: %s (%d)", pCtx->sConf, errno);
        XByteBuffer_Clear(&buffer);
        return XFALSE;
    }

    xjson_t json;
    if (!XJSON_Parse(&json, NULL, (const char*)buffer.pData, buffer.nUsed))
    {
        char sError[XMSG_MID];
        XJSON_GetErrorStr(&json, sError, sizeof(sError));
        xloge("Failed to parse database file: %s", sError);

        XByteBuffer_Clear(&buffer);
        return XFALSE;
    }

    XByteBuffer_Clear(&buffer);
    xjson_obj_t *pRoot = json.pRootObj;

    xjson_obj_t *pCfgObj = XJSON_GetObject(pRoot, "config");
    if (pCfgObj == NULL)
    {
        xloge("Invalid configuration file: %s", pCtx->sConf);
        XJSON_Destroy(&json);
        return XFALSE;
    }

    if (!pCtx->nAESKeyLength)
    {
        xjson_obj_t *pAesLenObj = XJSON_GetObject(pCfgObj, "aesKeyLength");
        if (pAesLenObj == NULL)
        {
            xloge("Missing \"aesKeyLength\" entry in the config file: %s", pCtx->sConf);
            XJSON_Destroy(&json);
            return XFALSE;
        }

        pCtx->nAESKeyLength = XJSON_GetU32(pAesLenObj);
    }

    if (!xstrused(pCtx->sFile))
    {
        const char* pFilePath = XJSON_GetString(XJSON_GetObject(pCfgObj, "databasePath"));
        if (!xstrused(pFilePath))
        {
            xloge("Missing \"databasePath\" entry in the config file: %s", pCtx->sConf);
            XJSON_Destroy(&json);
            return XFALSE;
        }

        xstrncpy(pCtx->sFile, sizeof(pCtx->sFile), pFilePath);
    }

    if (!xstrused(pCtx->sCiphers))
    {
        const char *pCiphers = XJSON_GetString(XJSON_GetObject(pCfgObj, "ciphers"));
        if (!xstrused(pCiphers))
        {
            xloge("Missing \"ciphers\" entry in the config file: %s", pCtx->sConf);
            XJSON_Destroy(&json);
            return XFALSE;
        }

        xstrncpy(pCtx->sCiphers, sizeof(pCtx->sCiphers), pCiphers);
    }

    xjson_obj_t *pNameLenObj = XJSON_GetObject(pCfgObj, "nameLength");
    if (pNameLenObj == NULL)
    {
        xloge("Missing \"nameLength\" entry in the config file: %s", pCtx->sConf);
        XJSON_Destroy(&json);
        return XFALSE;
    }

    pCtx->nNameLength = XJSON_GetU32(pNameLenObj);

    xjson_obj_t *pLayoytObj = XJSON_GetObject(pRoot, "layoyt");
    if (pLayoytObj != NULL)
    {
        const char *pCorner = XJSON_GetString(XJSON_GetObject(pCfgObj, "cornerChar"));
        if (xstrused(pCorner)) pCtx->cCorner = pCorner[0];

        const char *pLine = XJSON_GetString(XJSON_GetObject(pCfgObj, "lineChar"));
        if (xstrused(pLine)) pCtx->cLine = pLine[0];

        const char *pEdge = XJSON_GetString(XJSON_GetObject(pCfgObj, "edgeChar"));
        if (xstrused(pEdge)) pCtx->cEdge = pEdge[0];
    }

    xcli_size_t cliSize;
    XCLI_GetWindowSize(&cliSize);
    pCtx->nFrameLength = cliSize.nWinColumns - 1;

    XJSON_Destroy(&json);
    return XTRUE;
}

static void XPass_PrintEntry(xpass_ctx_t *pCtx, const char *pName, const char *pVal, size_t nFillSize)
{
    char sBuffer[XPATH_MAX], sFill[XPATH_MAX];

    size_t nBytes = xstrncpyf(sBuffer, sizeof(sBuffer), "%s%c%s %s%s%s: %s%s%s",
        XPASS_FRAME_FMT, pCtx->cEdge, XSTR_FMT_RESET,
        XPASS_NAME_FMT, pName, XSTR_FMT_RESET,
        XPASS_ENTRY_FMT, pVal, XSTR_FMT_RESET);

    int nFreeSpace = (int)sizeof(sBuffer) - (int)nBytes;
    nBytes -= xstrextra(sBuffer, nBytes, 0, NULL, NULL);

    if (nFillSize > nBytes)
    {
        xstrnfill(sFill, sizeof(sFill), nFillSize - nBytes, ' ');
        nFreeSpace -= xstrncat(sBuffer, nFreeSpace, "%s", sFill);
    }

    if (nFreeSpace > 0)
    {
        xstrncat(sBuffer, (size_t)nFreeSpace, "%s%c%s",
            XPASS_FRAME_FMT, pCtx->cEdge, XSTR_FMT_RESET);
    }

    printf("%s\n", sBuffer);
}

static void XPass_DisplayEntry(xpass_ctx_t *pCtx, xpass_entry_t *pEntry, xbool_t bFirst)
{
    char sLineBuff[XPATH_MAX], tmpBuff[XPATH_MAX];
    size_t nFillSize = pCtx->nFrameLength;

    xstrncpyfl(tmpBuff, sizeof(tmpBuff), nFillSize, pCtx->cLine, "%c", pCtx->cCorner);
    xstrncat(tmpBuff, sizeof(tmpBuff) - nFillSize, "%c", pCtx->cCorner);
    xstrnclr(sLineBuff, sizeof(sLineBuff), XPASS_FRAME_FMT, "%s", tmpBuff);
    if (bFirst) printf("%s\n", sLineBuff);

    xbool_t bPrintSecrets = (xstrused(pCtx->sName)) ? XTRUE : XFALSE;
    if (xstrused(pEntry->pName)) XPass_PrintEntry(pCtx, "Name", pEntry->pName, nFillSize);
    if (xstrused(pEntry->pAddr)) XPass_PrintEntry(pCtx, "Addr", pEntry->pAddr, nFillSize);
    if (xstrused(pEntry->pUser) && bPrintSecrets) XPass_PrintEntry(pCtx, "User", pEntry->pUser, nFillSize);
    if (xstrused(pEntry->pPass) && bPrintSecrets) XPass_PrintEntry(pCtx, "Pass", pEntry->pPass, nFillSize);
    if (xstrused(pEntry->pDesc)) XPass_PrintEntry(pCtx, "Desc", pEntry->pDesc, nFillSize);
    printf("%s\n", sLineBuff);
}

static xbool_t XPass_WriteObj(xpass_ctx_t *pCtx, xjson_obj_t *pObj, xbool_t bUpdate)
{
    if (!bUpdate)
    {
        if (!xstrused(pCtx->sName) && !XPass_GenerateName(pCtx)) return XFALSE;
        if (!xstrused(pCtx->sPass) && !XPass_GetPass(pCtx)) return XFALSE;
    }

    if (!xstrused(pCtx->sName)) return XFALSE;
    XSTATUS nStatus = XJSON_ERR_NONE;

    nStatus = XJSON_AddString(pObj, "name", pCtx->sName);
    if (!nStatus && xstrused(pCtx->sAddr)) nStatus = XJSON_AddString(pObj, "addr", pCtx->sAddr);
    if (!nStatus && xstrused(pCtx->sUser)) nStatus = XJSON_AddString(pObj, "user", pCtx->sUser);
    if (!nStatus && xstrused(pCtx->sPass)) nStatus = XJSON_AddString(pObj, "pass", pCtx->sPass);
    if (!nStatus && xstrused(pCtx->sDesc)) nStatus = XJSON_AddString(pObj, "desc", pCtx->sDesc);
    return nStatus == XJSON_ERR_NONE ? XTRUE : XFALSE;
}

xbool_t XPass_Callback(xcrypt_cb_type_t eType, void *pData, void *pCtx)
{
    if (eType == XCB_KEY)
    {
        xcrypt_key_t *pKey = (xcrypt_key_t*)pData;
        xpass_ctx_t *pArgs = (xpass_ctx_t*)pCtx;

        xstrncpyf(pKey->sKey, sizeof(pKey->sKey), pArgs->sKey);
        if (pKey->eCipher == XC_AES) pKey->nLength = pArgs->nAESKeyLength;
        else pKey->nLength = strlen(pArgs->sKey);

        return XTRUE;
    }

    xloge("%s", (const char*)pData);
    return XFALSE;
}

static xbool_t XPass_ReverseCiphers(char *pDst, size_t nSize, const char *pSrc)
{
    xarray_t *pCiphers = xstrsplit(pSrc, ":");
    if (pCiphers == NULL)
    {
        xstrncpy(pDst, nSize, pSrc);
        return XFALSE;
    }

    int i, nUsed = (int)XArray_Used(pCiphers);
    size_t nAvail = nSize;

    for (i = nUsed; i >= 0; i--)
    {
        char *pCipher = XArray_GetData(pCiphers, i);
        if (pCipher == NULL) continue;

        nAvail -= xstrncat(pDst, nAvail, "%s", pCipher);
        if (i - 1 >= 0) nAvail -= xstrncat(pDst, nAvail, ":");
    }

    XArray_Destroy(pCiphers);
    return XTRUE;
}

static xbool_t XPass_LoadDatabase(xpass_ctx_t *pCtx)
{
    xbyte_buffer_t buffer;
    if (!XPath_LoadBuffer(pCtx->sFile, &buffer))
    {
        xloge("Can not load database file: %s (%d)", pCtx->sFile, errno);
        return XFALSE;
    }

    char sCiphers[XSTR_MIN];
    sCiphers[0] = XSTR_NUL;

    XPass_ReverseCiphers(sCiphers, sizeof(sCiphers), pCtx->sCiphers);
    const uint8_t *pInput = (const  uint8_t*)buffer.pData;
    size_t nLength = buffer.nUsed;

    xcrypt_ctx_t crypter;
    XCrypt_Init(&crypter, XTRUE, sCiphers, XPass_Callback, pCtx);
    crypter.nColumns = XPASS_HEX_COLUMNS;

    uint8_t *pData = XCrypt_Multy(&crypter, pInput, &nLength);
    if (pData == NULL)
    {
        xloge("Failed to decrypt database: %d", errno);
        XByteBuffer_Clear(&buffer);
        return XFALSE;
    }

    XByteBuffer_Clear(&buffer);
    xjson_t *pJson = &pCtx->dataBase;

    if (!XJSON_Parse(pJson, NULL, (const char*)pData, nLength))
    {
        char sError[XMSG_MID];
        XJSON_GetErrorStr(pJson, sError, sizeof(sError));
        xloge("Failed to parse database file: %s", sError);

        free(pData);
        return XFALSE;
    }

    free(pData);
    xjson_obj_t *pRoot = pJson->pRootObj;

    xjson_obj_t *pCrcObj = XJSON_GetObject(pRoot, "crc32");
    if (pCrcObj == NULL)
    {
        xloge("Invalid database file.");
        XJSON_Destroy(pJson);
        return XFALSE;
    }

    const uint8_t *pKey = (const uint8_t*)pCtx->sKey;
    size_t nKeyLength = strlen(pCtx->sKey);

    uint32_t nDecryptedCRC = XJSON_GetU32(pCrcObj);
    uint32_t nCurrentCRC = XCRC32_Compute(pKey, nKeyLength);

    if (nDecryptedCRC != nCurrentCRC)
    {
        xloge("CRC32 missmatch in database file.");
        XJSON_Destroy(pJson);
        return XFALSE;
    }

    return XTRUE;
}

static xbool_t XPass_WriteDatabase(xpass_ctx_t *pCtx)
{
    xjson_t *pJson = &pCtx->dataBase;
    if (pJson->pRootObj == NULL) return XFALSE;

    xjson_writer_t writer;
    XJSON_InitWriter(&writer, NULL, NULL, XSTR_MIN);

    if (!XJSON_WriteObject(pJson->pRootObj, &writer))
    {
        xloge("Failed to serialize entries in JSON format.");
        XJSON_DestroyWriter(&writer);
        return XFALSE;
    }

    xcrypt_ctx_t crypter;
    XCrypt_Init(&crypter, XFALSE, pCtx->sCiphers, XPass_Callback, pCtx);
    crypter.nColumns = XPASS_HEX_COLUMNS;

    size_t nLength = writer.nLength;
    const uint8_t *pInput = (const  uint8_t*)writer.pData;

    uint8_t *pData = XCrypt_Multy(&crypter, pInput, &nLength);
    if (pData == NULL)
    {
        xloge("Failed to encrypt database entries: %d", errno);
        XJSON_DestroyWriter(&writer);
        return XFALSE;
    }

    if (XPath_Write(pCtx->sFile, "cwt", pData, nLength) <= 0)
    {
        xloge("Failed to wite data: %s (%d)", pCtx->sFile, errno);
        XJSON_DestroyWriter(&writer);
        free(pData);
        return XFALSE;
    }

    XJSON_DestroyWriter(&writer);
    free(pData);
    return XTRUE;
}

static xbool_t XPass_InitDatabase(xpass_ctx_t *pCtx)
{
    if (!xstrused(pCtx->sFile)) return XTRUE;
    xjson_t *pJson = &pCtx->dataBase;

    if (!pCtx->bForce)
    {
        xlogw("This operation will erase and reinitialize following file:");
        xlog("%sDatabase:%s %s", XSTR_FMT_BOLD, XSTR_FMT_RESET, pCtx->sFile);
        printf("Do you want to continue? (y/n): ");

        char sAnswer[XSTR_TINY];
        sAnswer[0] = XSTR_NUL;

        XASSERT((fgets(sAnswer, sizeof(sAnswer), stdin)), XFALSE);
        XASSERT((sAnswer[0] == 'Y' && sAnswer[0] == 'y'), XFALSE);
    }

    xpath_t path;
    XPath_Parse(&path, pCtx->sFile, XTRUE);

    xmode_t nMode = 0;
    XPath_PermToMode(XPASS_DIR_CHMOD, &nMode);

    if (xstrused(path.sPath) && XDir_Create(path.sPath, nMode) <= 0)
    {
        xloge("Failed create directory: %s (%d)", path.sPath, errno);
        return XFALSE;
    }

    pJson->pRootObj = XJSON_NewObject(NULL, NULL, XTRUE);
    if (pJson->pRootObj == NULL)
    {
        xloge("Failed to allocate memory for JSON object: %d", errno);
        return XFALSE;
    }

    xjson_obj_t *pArrObj = XJSON_NewArray(NULL, "entries", XTRUE);
    if (pArrObj == NULL)
    {
        xloge("Failed to allocate memory for JSON array: %d", errno);
        return XFALSE;
    }

    if (XJSON_AddObject(pJson->pRootObj, pArrObj) != XJSON_ERR_NONE)
    {
        xloge("Failed to initialize database entries: %d", errno);
        return XFALSE;
    }

    const uint8_t* pKey = (const uint8_t*)pCtx->sKey;
    const char *pXUtils = XUtils_VersionShort();

    size_t nKeyLength = strlen(pCtx->sKey);
    uint32_t nCRC32 = XCRC32_Compute(pKey, nKeyLength);

    char sVersion[XSTR_TINY];
    xstrncpyf(sVersion, sizeof(sVersion), "%d.%d.%d",
        XPASS_VER_MAX, XPASS_VER_MIN, XPASS_BUILD_NUM);

    if (XJSON_AddString(pJson->pRootObj, "version", sVersion) != XJSON_ERR_NONE ||
        XJSON_AddString(pJson->pRootObj, "xutils", pXUtils) != XJSON_ERR_NONE ||
        XJSON_AddU32(pJson->pRootObj, "crc32", nCRC32) != XJSON_ERR_NONE)
    {
        xloge("Failed to initialize json database: %d", errno);
        return XFALSE;
    }

    return XTRUE;
}

static xbool_t XPass_InitConfigFile(xpass_ctx_t *pCtx)
{
    if (!pCtx->bForce)
    {
        xlogw("This operation will erase and reinitialize following file:");
        xlog("%sConfig:%s %s", XSTR_FMT_BOLD, XSTR_FMT_RESET, pCtx->sConf);
        printf("Do you want to continue? (y/n): ");

        char sAnswer[XSTR_TINY];
        sAnswer[0] = XSTR_NUL;

        XASSERT((fgets(sAnswer, sizeof(sAnswer), stdin)), XFALSE);
        XASSERT((sAnswer[0] == 'Y' || sAnswer[0] == 'y'), XFALSE);
    }

    xpath_t path;
    XPath_Parse(&path, pCtx->sConf, XTRUE);

    xmode_t nMode = 0;
    XPath_PermToMode(XPASS_DIR_CHMOD, &nMode);

    if (xstrused(path.sPath) && XDir_Create(path.sPath, nMode) <= 0)
    {
        xloge("Failed create directory: %s (%d)", path.sPath, errno);
        return XFALSE;
    }

    xjson_obj_t *pRootObj = XJSON_NewObject(NULL, NULL, XFALSE);
    if (pRootObj == NULL)
    {
        xloge("Failed to allocate memory for JSON object: %d", errno);
        return XFALSE;
    }

    xjson_obj_t *pCfgObj = XJSON_NewObject(NULL, "config", XFALSE);
    if (pCfgObj == NULL)
    {
        xloge("Failed to allocate memory for config JSON object: %s", pCtx->sConf);
        XJSON_FreeObject(pRootObj);
        return XFALSE;
    }

    if (!xstrused(pCtx->sCiphers)) xstrncpy(pCtx->sCiphers, sizeof(pCtx->sCiphers), XPASS_CIPHERS);
    if (!xstrused(pCtx->sFile)) xstrncpy(pCtx->sFile, sizeof(pCtx->sFile), XPASS_DATABASE);
    pCtx->nAESKeyLength = pCtx->nAESKeyLength ? pCtx->nAESKeyLength : XPASS_AES_LEN;
    pCtx->nNameLength = pCtx->nNameLength ? pCtx->nNameLength : XPASS_NAME_LEN;

    if (XJSON_AddU32(pCfgObj, "aesKeyLength", pCtx->nAESKeyLength) != XJSON_ERR_NONE ||
        XJSON_AddU32(pCfgObj, "nameLength", pCtx->nNameLength) != XJSON_ERR_NONE ||
        XJSON_AddString(pCfgObj, "databasePath", pCtx->sFile) != XJSON_ERR_NONE ||
        XJSON_AddString(pCfgObj, "ciphers", pCtx->sCiphers) != XJSON_ERR_NONE ||
        XJSON_AddObject(pRootObj, pCfgObj) != XJSON_ERR_NONE)
    {
        xloge("Failed to initialize JSON config object: %s", pCtx->sConf);
        XJSON_FreeObject(pRootObj);
        XJSON_FreeObject(pCfgObj);
        return XFALSE;
    }

    xjson_obj_t *pLayoutObj = XJSON_NewObject(NULL, "layout", XFALSE);
    if (pLayoutObj == NULL)
    {
        xloge("Failed to allocate memory for layout JSON object: %s", pCtx->sConf);
        XJSON_FreeObject(pRootObj);
        return XFALSE;
    }

    if (XJSON_AddString(pLayoutObj, "cornerChar", XPASS_CORNER) != XJSON_ERR_NONE ||
        XJSON_AddString(pLayoutObj, "lineChar", XPASS_LINE) != XJSON_ERR_NONE ||
        XJSON_AddString(pLayoutObj, "edgeChar", XPASS_EDGE) != XJSON_ERR_NONE ||
        XJSON_AddObject(pRootObj, pLayoutObj) != XJSON_ERR_NONE)
    {
        xloge("Failed to initialize layout JSON object: %s", pCtx->sConf);
        XJSON_FreeObject(pRootObj);
        XJSON_FreeObject(pLayoutObj);
        return XFALSE;
    }

    xjson_writer_t writer;
    XJSON_InitWriter(&writer, NULL, NULL, XSTR_MIN);
    writer.nTabSize = 4; // Enable linter

    if (!XJSON_WriteObject(pRootObj, &writer))
    {
        xloge("Failed to serialize config entries in JSON format: %d", errno);
        XJSON_DestroyWriter(&writer);
        XJSON_FreeObject(pRootObj);
        return XFALSE;
    }

    const uint8_t *pData = (const  uint8_t*)writer.pData;
    size_t nLength = writer.nLength;

    if (XPath_Write(pCtx->sConf, "cwt", pData, nLength) <= 0)
    {
        xloge("Failed to wite config data: %s (%d)", pCtx->sConf, errno);
        XJSON_FreeObject(pRootObj);
        XJSON_DestroyWriter(&writer);
        return XFALSE;
    }

    XJSON_FreeObject(pRootObj);
    XJSON_DestroyWriter(&writer);
    return XTRUE;
}

static xbool_t XPass_ReadEntry(xpass_ctx_t *pCtx)
{
    xjson_t *pJson = &pCtx->dataBase;

    xjson_obj_t *pArrObj = XJSON_GetObject(pJson->pRootObj, "entries");
    if (pArrObj == NULL)
    {
        xloge("Database file does not contain entries.");
        return XFALSE;
    }

    size_t i, nLength = XJSON_GetArrayLength(pArrObj);
    xbool_t bFirst = XTRUE;

    for (i = 0; i < nLength; i++)
    {
        xjson_obj_t *pArrItemObj = XJSON_GetArrayItem(pArrObj, i);
        if (pArrItemObj != NULL)
        {
            xpass_entry_t entry;
            entry.pName = XJSON_GetString(XJSON_GetObject(pArrItemObj, "name"));
            entry.pUser = XJSON_GetString(XJSON_GetObject(pArrItemObj, "user"));
            entry.pAddr = XJSON_GetString(XJSON_GetObject(pArrItemObj, "addr"));
            entry.pDesc = XJSON_GetString(XJSON_GetObject(pArrItemObj, "desc"));

            if (xstrused(pCtx->sName) && (!xstrused(entry.pName) || xstrsrc(entry.pName, pCtx->sName) < 0)) continue;
            if (xstrused(pCtx->sUser) && (!xstrused(entry.pUser) || xstrsrc(entry.pUser, pCtx->sUser) < 0)) continue;
            if (xstrused(pCtx->sAddr) && (!xstrused(entry.pAddr) || xstrsrc(entry.pAddr, pCtx->sAddr) < 0)) continue;
            if (xstrused(pCtx->sDesc) && (!xstrused(entry.pDesc) || xstrsrc(entry.pDesc, pCtx->sDesc) < 0)) continue;

            entry.pPass = XJSON_GetString(XJSON_GetObject(pArrItemObj, "pass"));
            XPass_DisplayEntry(pCtx, &entry, bFirst);
            bFirst = XFALSE;
        }
    }

    return XTRUE;
}

static xbool_t XPass_UpdateEntry(xpass_ctx_t *pCtx)
{
    xjson_t *pJson = &pCtx->dataBase;

    xjson_obj_t *pArrObj = XJSON_GetObject(pJson->pRootObj, "entries");
    if (pArrObj == NULL)
    {
        xloge("Database file does not contain entries.");
        return XFALSE;
    }

    if (!xstrused(pCtx->sName) && !XPass_GetName(pCtx)) return XFALSE;
    size_t i, nLength = XJSON_GetArrayLength(pArrObj);
    xbool_t bUpdated = XFALSE, bFound = XFALSE;

    for (i = 0; i < nLength; i++)
    {
        xjson_obj_t *pArrItemObj = XJSON_GetArrayItem(pArrObj, i);
        if (pArrItemObj != NULL)
        {
            const char *pName = XJSON_GetString(XJSON_GetObject(pArrItemObj, "name"));
            if (!xstrused(pName) || strcmp(pName, pCtx->sName)) continue;

            pArrItemObj->nAllowUpdate = XTRUE;
            bUpdated = XPass_WriteObj(pCtx, pArrItemObj, XTRUE);
            bFound = XTRUE;
            break;
        }
    }

    if (!bFound) xloge("Entry not found: %s", pCtx->sName);
    else if (!bUpdated) xloge("Failed to update entry: %s", pCtx->sName);

    return bUpdated;
}

static xbool_t XPass_DeleteEntry(xpass_ctx_t *pCtx)
{
    xjson_t *pJson = &pCtx->dataBase;

    xjson_obj_t *pArrObj = XJSON_GetObject(pJson->pRootObj, "entries");
    if (pArrObj == NULL)
    {
        xloge("Database file does not contain entries.");
        return XFALSE;
    }

    if (!xstrused(pCtx->sName) && !XPass_GetName(pCtx)) return XFALSE;
    size_t i, nLength = XJSON_GetArrayLength(pArrObj);
    xbool_t bDeleted = XFALSE;

    for (i = 0; i < nLength; i++)
    {
        xjson_obj_t *pArrItemObj = XJSON_GetArrayItem(pArrObj, i);
        if (pArrItemObj != NULL)
        {
            const char *pName = XJSON_GetString(XJSON_GetObject(pArrItemObj, "name"));
            if (!xstrused(pName) || strcmp(pName, pCtx->sName)) continue;

            XJSON_RemoveArrayItem(pArrObj, i);
            bDeleted = XTRUE;
            break;
        }
    }

    if (!bDeleted) xloge("Entry not found: %s", pCtx->sName);
    return bDeleted;
}

static xbool_t XPass_AppendEntry(xpass_ctx_t *pCtx)
{
    xjson_t *pJson = &pCtx->dataBase;

    xjson_obj_t *pArrObj = XJSON_GetObject(pJson->pRootObj, "entries");
    if (pArrObj == NULL)
    {
        xloge("Database file does not contain entries.");
        return XFALSE;
    }

    xjson_obj_t *pNewObj = XJSON_NewObject(NULL, NULL, XTRUE);
    if (pNewObj == NULL)
    {
        xloge("Failed to allocate memory for JSON object: %d", errno);
        return XFALSE;
    }

    if (!XPass_WriteObj(pCtx, pNewObj, XFALSE))
    {
        xloge("Failed to write JSON object: %d", errno);
        XJSON_FreeObject(pNewObj);
        return XFALSE;
    }

    if (XJSON_AddObject(pArrObj, pNewObj) != XJSON_ERR_NONE)
    {
        xloge("Failed to store new database object: %d", errno);
        XJSON_FreeObject(pNewObj);
        return XFALSE;
    }

    return XTRUE;
}

static xbool_t XPass_ProcessDatabase(xpass_ctx_t *pCtx)
{
    if (!XPass_GetKey(pCtx)) return XFALSE;

    if ((pCtx->bRead || pCtx->bWrite ||
        pCtx->bUpdate || pCtx->bDelete) &&
        !XPass_LoadDatabase(pCtx)) return XFALSE;

    if (pCtx->bWrite && XPass_FindEntry(pCtx, pCtx->sName))
    {
        xloge("Entry with name '%s' already exists.", pCtx->sName);
        xlogi("Press enter for auto unique name generation.");
        if (!XPass_GetName(pCtx) && !XPass_GenerateName(pCtx)) return XFALSE;
    }

    if (pCtx->bInitData) return XPass_InitDatabase(pCtx);
    else if (pCtx->bRead) return XPass_ReadEntry(pCtx);
    else if (pCtx->bWrite) return XPass_AppendEntry(pCtx);
    else if (pCtx->bUpdate) return XPass_UpdateEntry(pCtx);
    else if (pCtx->bDelete) return XPass_DeleteEntry(pCtx);
    return XTRUE;
}

int main(int argc, char* argv[])
{
    srand(time(0));
    xlog_defaults();
    xlog_enable(XLOG_INFO);

    xpass_ctx_t ctx;
    XPass_InitCtx(&ctx);

    if (!XPass_ParseArgs(&ctx, argc, argv)) return XSTDERR;
    else if (ctx.bInitConf && !XPass_InitConfigFile(&ctx)) return XSTDERR;
    else if (!ctx.bInitConf && !XPass_ParseConfig(&ctx)) return XSTDERR;

    if (!XPass_ProcessDatabase(&ctx))
    {
        XJSON_Destroy(&ctx.dataBase);
        return XSTDERR;
    }

    if (!ctx.bRead) XPass_WriteDatabase(&ctx);
    XJSON_Destroy(&ctx.dataBase);
    return XSTDNON;
}