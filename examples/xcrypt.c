/*!
 *  @file libxutils/examples/xcrpt.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2022  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Crypt or Decrypt input file
 */

#include <xutils/xstd.h>
#include <xutils/array.h>
#include <xutils/xtype.h>
#include <xutils/xaes.h>
#include <xutils/xlog.h>
#include <xutils/xstr.h>
#include <xutils/xcli.h>
#include <xutils/xfs.h>

/* Remove this definiton if there is no OpenSSL library support */
#define _XUTILS_USE_SSL
#include <xutils/crypt.h>

#define XCRYPT_VER_MAX      0
#define XCRYPT_VER_MIN      1
#define XCRYPT_BUILD_NUM    18

#define XCRYPT_HEX_COLUMNS  16
#define XAES_KEY_LENGTH     128
extern char *optarg;

typedef struct
{
    char sKeyFile[XSTR_MID];
    char sCiphers[XSTR_MIN];
    char sOutput[XPATH_MAX];
    char sInput[XPATH_MAX];
    char sText[XSTR_MID];
    char sKey[XSTR_MID];

    size_t nAESKeyLen;
    xbool_t bDecrypt;
    xbool_t bForce;
    xbool_t bPrint;
    xbool_t bHex;
} xcrypt_args_t;

static xbool_t XCrypt_DecriptSupport(xcrypt_chipher_t eCipher)
{
    switch (eCipher)
    {
        case XC_AES:
        case XC_HEX:
        case XC_XOR:
        case XC_BASE64:
        case XC_BASE64URL:
        case XC_CASEAR:
        case XC_REVERSE:
#ifdef _XUTILS_USE_SSL
        case XC_RSA:
#endif
            return XTRUE;
        case XC_MD5:
        case XC_HMD5:
        case XC_CRC32:
        case XC_HS256:
        case XC_SHA256:
            return XFALSE;
        default:
            break;
    }

    return XFALSE;
}

static char *XCrypt_WhiteSpace(const int nLength)
{
    static char sRetVal[XSTR_MIN];
    xstrnul(sRetVal);
    int i = 0;

    int nLen = XSTD_MIN(nLength, sizeof(sRetVal) - 1);
    for (i = 0; i < nLen; i++) sRetVal[i] = ' ';

    sRetVal[i] = '\0';
    return sRetVal;
}

static void XCrypt_DisplayUsage(const char *pName)
{
    int nLength = strlen(pName) + 6;

    xlog("==============================================================");
    xlog(" Crypt/Decrypt file or text - v%d.%d build %d (%s)",
        XCRYPT_VER_MAX, XCRYPT_VER_MIN, XCRYPT_BUILD_NUM, __DATE__);
    xlog("==============================================================");

    xlog("Usage: %s [-c <ciphers>] [-i <input>] [-o <output>]", pName);
    xlog(" %s [-k <key>] [-t <text>] [-K <key-file>]", XCrypt_WhiteSpace(nLength));
    xlog(" %s [-a] [-d] [-f] [-p] [-h] [-v]\n", XCrypt_WhiteSpace(nLength));

    xlog("Options are:");
    xlog("   -c <ciphers>        # Encrypt/Decrypt ciphers (%s*%s)", XSTR_CLR_RED, XSTR_FMT_RESET);
    xlog("   -i <input>          # Input file path to encrtypt/decrypt");
    xlog("   -o <output>         # Output file path to write data");
    xlog("   -k <key>            # Encrypt/Decrypt key");
    xlog("   -K <key-file>       # Encrypt/Decrypt key file");
    xlog("   -t <text>           # Text to encrtypt/decrypt");
    xlog("   -a                  # AES key length (default: 128)");
    xlog("   -d                  # Decryption mode");
    xlog("   -f                  # Force overwrite output");
    xlog("   -h                  # Display output as a HEX");
    xlog("   -p                  # Printf output to stdout");
    xlog("   -v                  # Version and usage\n");

    xlog("Supported ciphers:");
    xlog("   aes");
    xlog("   hex");
    xlog("   md5");
    xlog("   xor");
#ifdef _XUTILS_USE_SSL
    xlog("   rsa");
#endif
    xlog("   crc32");
    xlog("   crc32b");
    xlog("   casear");
    xlog("   base64");
    xlog("   base64url");
    xlog("   sha256");
    xlog("   hs256");
    xlog("   hmacmd5");
    xlog("   reverse\n");

    xlog("Examples:");
    xlog("%s -c aes -i rawFile.txt -o crypted.bin", pName);
    xlog("%s -dc aes -i crypted.bin -o decrypted.txt", pName);
    xlog("%s -dc hex:aes -i crypted.txt -o decrypted.bin\n", pName);

    xlog("%sNotes:%s", XSTR_CLR_YELLOW, XSTR_FMT_RESET);
    xlog("%s1%s) If you do not specify an argument key (-k <key>),", XSTR_FMT_BOLD, XSTR_FMT_RESET);
    xlog("the program will prompt you to enter the it securely.\n");
    xlog("%s2%s) You can use key file for RSA encrypt/decrypt with -K argument,", XSTR_FMT_BOLD, XSTR_FMT_RESET);
    xlog("%s%s -dc rsa -i crypted.bin -o decrypted.txt -K rsa_priv.pem%s\n", XSTR_FMT_DIM, pName, XSTR_FMT_RESET);
}

static xbool_t XCrypt_GetKey(xcrypt_args_t *pArgs, xcrypt_key_t *pKey)
{
    if (xstrused(pArgs->sKeyFile))
    {
        pKey->nLength = XPath_Read(pArgs->sKeyFile, (uint8_t*)pArgs->sKey, sizeof(pArgs->sKey));
        if (pKey->nLength && pKey->eCipher == XC_AES) pKey->nLength = pArgs->nAESKeyLen;
        pArgs->sKeyFile[0] = XSTR_NUL;
    }

    if (xstrused(pArgs->sKey))
    {
        pKey->nLength = xstrncpy(pKey->sKey, sizeof(pKey->sKey), pArgs->sKey);
        if (pKey->eCipher == XC_AES) pKey->nLength = pArgs->nAESKeyLen;
        return pKey->nLength ? XTRUE : XFALSE;
    }

    const char *pCipher = XCrypt_GetCipherStr(pKey->eCipher);
    printf("Enter keyword for the cipher '%s': ", pCipher);

    if (!XCLI_GetPass(NULL, pKey->sKey, sizeof(pKey->sKey)))
    {
        xloge("Failed to read master keyword: %d", errno);
        return XFALSE;
    }

    if (!pArgs->bDecrypt || pArgs->bForce)
    {
        char sKey[XSTR_MIN];
        printf("Re-enter keyword for the cipher '%s': ", pCipher);

        if (!XCLI_GetPass(NULL, sKey, sizeof(sKey)))
        {
            xloge("Failed to read keyword: %d", errno);
            return XFALSE;
        }

        if (strcmp(pKey->sKey, sKey))
        {
            xloge("Keyword do not match");
            return XFALSE;
        }
    }

    if (pKey->eCipher == XC_AES) pKey->nLength = pArgs->nAESKeyLen;
    else pKey->nLength = strlen(pKey->sKey);

    return XTRUE;
}

static XSTATUS XCrypt_ValidateArgs(xcrypt_args_t *pArgs)
{
    if ((!pArgs->bPrint &&
        !xstrused(pArgs->sOutput)) ||
        (!xstrused(pArgs->sText) && 
        !xstrused(pArgs->sInput))) return XSTDNON;

    if (XPath_Exists(pArgs->sOutput) && pArgs->bForce == XFALSE)
    {
        xlogw("File already exists: %s", pArgs->sOutput);
        xlogi("Use option -f to force overwrite output");
        return XSTDERR;
    }

    if (!xstrused(pArgs->sCiphers))
    {
        xlogw("No cipher is specified for ecnrypt/decrypt");
        return XSTDNON;
    }

    xarray_t *pCiphers = xstrsplit(pArgs->sCiphers, ":");
    if (pCiphers != NULL)
    {
        size_t i, nUsed = XArray_Used(pCiphers);
        for (i = 0; i < nUsed; i++)
        {
            const char *pCipher = XArray_GetData(pCiphers, i);
            if (pCipher == NULL) continue;
    
            xcrypt_chipher_t eCipher = XCrypt_GetCipher(pCipher);
            if (eCipher == XC_INVALID)
            {
                xloge("Invalid or unsupported cipher: %s", pCipher);
                XArray_Destroy(pCiphers);
                return XSTDERR;
            }
            else if (pArgs->bDecrypt && !XCrypt_DecriptSupport(eCipher))
            {
                xloge("Decryption is not supported for cipher: %s", pCipher);
                XArray_Destroy(pCiphers);
                return XSTDERR;
            }
        }

        XArray_Destroy(pCiphers);
        return XSTDOK;
    }

    xcrypt_chipher_t eCipher = XCrypt_GetCipher(pArgs->sCiphers);
    if (eCipher == XC_INVALID)
    {
        xloge("Invalid or unsupported cipher: %s", pArgs->sCiphers);
        return XSTDERR;
    }
    else if (pArgs->bDecrypt && !XCrypt_DecriptSupport(eCipher))
    {
        xloge("Decryption is not supported for cipher: %s", pArgs->sCiphers);
        return XSTDERR;
    }

    return XSTDOK;
}

static xbool_t XCrypt_ParseArgs(xcrypt_args_t *pArgs, int argc, char *argv[])
{
    memset(pArgs, 0, sizeof(xcrypt_args_t));
    pArgs->nAESKeyLen = XAES_KEY_LENGTH; 
    int nChar = 0;

    while ((nChar = getopt(argc, argv, "c:i:o:k:K:t:a:d1:f1:h1:p1:s1:v1")) != -1)
    {
        switch (nChar)
        {
            case 'c':
                xstrncpy(pArgs->sCiphers, sizeof(pArgs->sCiphers), optarg);
                break;
            case 'i':
                xstrncpy(pArgs->sInput, sizeof(pArgs->sInput), optarg);
                break;
            case 'o':
                xstrncpy(pArgs->sOutput, sizeof(pArgs->sOutput), optarg);
                break;
            case 'k':
                xstrncpy(pArgs->sKey, sizeof(pArgs->sKey), optarg);
                break;
            case 'K':
                xstrncpy(pArgs->sKeyFile, sizeof(pArgs->sKeyFile), optarg);
                break;
            case 't':
                xstrncpy(pArgs->sText, sizeof(pArgs->sText), optarg);
                break;
            case 'a':
                pArgs->nAESKeyLen = atoi(optarg);
                break;
            case 'd':
                pArgs->bDecrypt = XTRUE;
                break;
            case 'f':
                pArgs->bForce = XTRUE;
                break;
            case 'h':
                pArgs->bHex = XTRUE;
                break;
            case 'p':
                pArgs->bPrint = XTRUE;
                break;
            case 'v':
            default:
                XCrypt_DisplayUsage(argv[0]);
                return XFALSE;
        }
    }

    XSTATUS nStatus = XCrypt_ValidateArgs(pArgs);
    if (!nStatus) XCrypt_DisplayUsage(argv[0]);
    return nStatus == XSTDOK ? XTRUE : XFALSE;
}

static void XCrypt_HEXDump(const uint8_t *pData, size_t nSize)
{
    uint8_t *pHex = XCrypt_HEX(pData, &nSize, XSTR_SPACE, 16, XFALSE);
    if (pHex != NULL)
    {
        printf("\n%s\n", (char*)pHex);
        free(pHex);
    }
}

static void XCrypt_Print(xcrypt_args_t *pArgs, uint8_t *pData, size_t nLength)
{
    if (!pArgs->bPrint) return;
    pData[nLength] = XSTR_NUL;

    if (pArgs->bHex) XCrypt_HEXDump(pData, nLength);
    else printf("%s\n", (char*)pData);
}

xbool_t XCrypt_Callback(xcrypt_cb_type_t eType, void *pData, void *pCtx)
{
    if (eType == XCB_KEY)
    {
        xcrypt_args_t *pArgs = (xcrypt_args_t*)pCtx;
        xcrypt_key_t *pKey = (xcrypt_key_t*)pData;
        return XCrypt_GetKey(pArgs, pKey);
    }

    xloge("%s (%d)", (const char*)pData, errno);
    return XFALSE;
}

int main(int argc, char* argv[])
{
    xlog_defaults();
    xlog_enable(XLOG_INFO);

    xcrypt_args_t args;
    if (!XCrypt_ParseArgs(&args, argc, argv)) return XSTDERR;

    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, 0, 0);

    if (xstrused(args.sInput) && !XPath_LoadBuffer(args.sInput, &buffer))
    {
        xloge("Can not load file: %s (%d)", args.sInput, errno);
        return XSTDERR;
    }
    else if (!xstrused(args.sInput) && XByteBuffer_AddFmt(&buffer, "%s", args.sText) <= 0)
    {
        xloge("Can not init crypt buffer: %d", errno);
        return XSTDERR;
    }

    xcrypt_ctx_t crypter;
    XCrypt_Init(&crypter, args.bDecrypt, args.sCiphers, XCrypt_Callback, &args);
    crypter.nColumns = XCRYPT_HEX_COLUMNS;

    size_t nLength = buffer.nUsed;
    uint8_t *pData = XCrypt_Multy(&crypter, buffer.pData, &nLength);
    if (pData == NULL)
    {
        xloge("Multy %s failed for ciphers: %s",
            args.bDecrypt ? "decrypt" : "crypt",
            args.sCiphers);

        XByteBuffer_Clear(&buffer);
        return XSTDERR;
    }

    if (xstrused(args.sOutput) && XPath_Write(args.sOutput, "cwt", pData, nLength) <= 0)
    {
        xloge("Failed to open output file: %s (%d)", args.sOutput, errno);
        XByteBuffer_Clear(&buffer);
        free(pData);
        return XSTDERR;
    }

    XCrypt_Print(&args, pData, nLength);
    XByteBuffer_Clear(&buffer);
    free(pData);

    return XSTDNON;
}