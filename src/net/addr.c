/*!
 *  @file libxutils/src/net/addr.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief This source includes functions for detect
 * IP and Mac address of the host operating system
 */

#include "xstd.h"
#include "array.h"
#include "addr.h"
#include "sock.h"
#include "str.h"

typedef struct XProtocolPorts {
    const char* pProtocol;
    const int nPort;
    const int nRange;
} xprotocol_ports_t;

static xprotocol_ports_t g_defaultPorts[] =
{
    { "ftp", 21, 0 },
    { "ssh", 22, 0 },
    { "smtp", 25, 0 },
    { "snmp", 161, 0 },
    { "http", 80, 0 },
    { "https", 443, 0 },
    { "redis", 6379, 0 },
    { "rediss", 6379, 0 },
    { "ws", 80, 0 },
    { "wss", 443, 0 },
    { "unknown", -1, -1}
};

static int XLink_HexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void XLink_DecodeUrlComponent(char *pValue)
{
    XCHECK_VOID_NL((pValue != NULL));

    char *pRead = pValue;
    char *pWrite = pValue;

    while (*pRead != XSTR_NUL)
    {
        if (pRead[0] == '%' && pRead[1] != XSTR_NUL && pRead[2] != XSTR_NUL)
        {
            int nHi = XLink_HexNibble(pRead[1]);
            int nLo = XLink_HexNibble(pRead[2]);
            if (nHi >= 0 && nLo >= 0)
            {
                *pWrite++ = (char)((nHi << 4) | nLo);
                pRead += 3;
                continue;
            }
        }

        *pWrite++ = *pRead++;
    }

    *pWrite = XSTR_NUL;
}

int XAddr_GetDefaultPort(const char *pProtocol)
{
    size_t i, nLength = strlen(pProtocol);
    if (!nLength) return XSTDERR;

    for (i = 0;; i++)
    {
        if (!strncmp(g_defaultPorts[i].pProtocol, pProtocol, nLength) ||
            g_defaultPorts[i].nPort < 0) return g_defaultPorts[i].nPort;
    }

    /* Never reached */
    return XSTDERR;
}

void XLink_Init(xlink_t *pLink)
{
    pLink->nPort = 0;
    pLink->sUri[0] = XSTR_NUL;
    pLink->sAddr[0] = XSTR_NUL;
    pLink->sHost[0] = XSTR_NUL;
    pLink->sUser[0] = XSTR_NUL;
    pLink->sPass[0] = XSTR_NUL;
    pLink->sFile[0] = XSTR_NUL;
    pLink->sProtocol[0] = XSTR_NUL;
}

int XLink_ParseUnix(xlink_t *pLink, const char *pInput)
{
    XCHECK((pLink != NULL), XSTDERR);
    XLink_Init(pLink);

    XCHECK((xstrused(pInput)), XSTDERR);
    size_t nLength = strlen(pInput);
    if (!nLength) return XSTDERR;

    size_t nSockLen = 0;
    size_t nUriPos = 0;
    size_t nPosit = 0;

    xstrncpy(pLink->sProtocol, sizeof(pLink->sProtocol), "unix");
    if (xstrsrc(pInput, "unix://") == 0) nPosit = 7;
    else if (xstrsrc(pInput, "unix:") == 0) nPosit = 5;

    const char *pPath = &pInput[nPosit];
    if (!pPath[0]) return XSTDERR;

    while (pPath[nSockLen] != XSTR_NUL)
    {
        if (pPath[nSockLen] == '?' || pPath[nSockLen] == '#')
        {
            nUriPos = nSockLen;
            break;
        }

        if (pPath[nSockLen] == ':' && pPath[nSockLen + 1] == '/')
        {
            nUriPos = nSockLen + 1;
            break;
        }

        nSockLen++;
    }

    if (!nSockLen) return XSTDERR;
    xstrncpys(pLink->sHost, sizeof(pLink->sHost), pPath, nSockLen);
    xstrncpys(pLink->sAddr, sizeof(pLink->sAddr), pPath, nSockLen);

    if (nUriPos > 0)
    {
        size_t nLeft = strlen(&pPath[nUriPos]);
        if (nLeft > 0) xstrncpys(pLink->sUri, sizeof(pLink->sUri), &pPath[nUriPos], nLeft);
    }

    if (!xstrused(pLink->sUri)) xstrncpy(pLink->sUri, sizeof(pLink->sUri), "/");
    size_t nUrlLength = strlen(pLink->sUri);

    if (nUrlLength > 0 && pLink->sUri[nUrlLength - 1] != '/')
    {
        xarray_t *pTokens = xstrsplit(pLink->sUri, "/");
        if (pTokens != NULL)
        {
            size_t nUsed = XArray_Used(pTokens);
            if (nUsed > 0)
            {
                const char *pLast = (const char*)XArray_GetData(pTokens, nUsed - 1);
                if (pLast != NULL) xstrncpy(pLink->sFile, sizeof(pLink->sFile), pLast);
            }

            XArray_Destroy(pTokens);
        }
    }

    return XSTDOK;
}

int XLink_Parse(xlink_t *pLink, const char *pInput)
{
    XCHECK((pLink != NULL), XSTDERR);
    XLink_Init(pLink);

    XCHECK((xstrused(pInput)), XSTDERR);
    size_t nLength = strlen(pInput);
    if (!nLength) return XSTDERR;

    if (xstrncasecmp(pInput, "unix:", 5))
        return XLink_ParseUnix(pLink, pInput);

    size_t nPosit = 0;
    int nTokenLen = xstrsrc(pInput, "://");
    if (nTokenLen > 0)
    {
        xstrncpys(pLink->sProtocol, sizeof(pLink->sProtocol), pInput, nTokenLen);
        xstrcase(pLink->sProtocol, XSTR_LOWER);
        nPosit += (size_t)nTokenLen + 3;
    }

    size_t nAuthorityEnd = nPosit;
    while (nAuthorityEnd < nLength && pInput[nAuthorityEnd] != '/') nAuthorityEnd++;

    size_t nAtPos = nAuthorityEnd;
    for (size_t i = nPosit; i < nAuthorityEnd; i++)
    {
        if (pInput[i] == '@')
        {
            nAtPos = i;
            break;
        }
    }

    if (nAtPos < nAuthorityEnd && nAtPos > nPosit)
    {
        nTokenLen = (int)(nAtPos - nPosit);
        xstrncpys(pLink->sUser, sizeof(pLink->sUser), &pInput[nPosit], nTokenLen);

        int nUserLen = xstrnsrc(pLink->sUser, strlen(pLink->sUser), ":", 0);
        if (nUserLen >= 0)
        {
            nUserLen++;
            if (nUserLen < nTokenLen)
            {
                size_t nPassLen = (size_t)nTokenLen - (size_t)nUserLen;
                xstrncpys(pLink->sPass, sizeof(pLink->sPass), &pLink->sUser[nUserLen], nPassLen);
            }

            size_t nTermPos = XSTD_MIN(nUserLen - 1, nTokenLen);
            pLink->sUser[nTermPos] = XSTR_NUL;
        }

        XLink_DecodeUrlComponent(pLink->sUser);
        XLink_DecodeUrlComponent(pLink->sPass);

        nPosit = nAtPos + 1;
    }

    char *pDst = &pLink->sUri[0];
    size_t nDstSize = sizeof(pLink->sUri);
    if (!nPosit && pInput[0] == '/') nPosit++;

    nTokenLen = xstrnsrc(pInput, nLength, "/", nPosit);
    if (nTokenLen > 0)
    {
        xstrncpys(pLink->sHost, sizeof(pLink->sHost), &pInput[nPosit], nTokenLen);
        nPosit += nTokenLen;
    }
    else
    {
        nDstSize = sizeof(pLink->sHost);
        pDst = &pLink->sHost[0];
    }

    size_t nLeft = (nPosit < nLength) ? (nLength - nPosit) : 0;
    if (nLeft > 0) xstrncpys(pDst, nDstSize, &pInput[nPosit], nLeft);

    size_t nAddrLen = strlen(pLink->sHost);
    nTokenLen = xstrnsrc(pLink->sHost, nAddrLen, ":", 0);

    if (nTokenLen > 0)
    {
        nTokenLen++;
        if (nTokenLen < (int)nAddrLen) pLink->nPort = atoi(&pLink->sHost[nTokenLen]);
        nAddrLen = XSTD_MIN(nAddrLen, (size_t)nTokenLen - 1);
    }

    if (!pLink->nPort)
    {
        int nPort = XAddr_GetDefaultPort(pLink->sProtocol);
        if (nPort > 0)
        {
            pLink->nPort = nPort;
            int nAvail = (int)sizeof(pLink->sHost) - (int)strlen(pLink->sHost);
            if (nAvail > 0) xstrncatf(pLink->sHost, (size_t)nAvail - 1, ":%d", pLink->nPort);
        }
    }

    xstrncpys(pLink->sAddr, sizeof(pLink->sAddr), pLink->sHost, nAddrLen);
    if (!xstrused(pLink->sUri)) xstrncpy(pLink->sUri, sizeof(pLink->sUri), "/");

    size_t nUrlLength = strlen(pLink->sUri);
    if (nUrlLength > 0 && pLink->sUri[nUrlLength - 1] != '/')
    {
        xarray_t *pTokens = xstrsplit(pLink->sUri, "/");
        if (pTokens != NULL)
        {
            size_t nUsed = XArray_Used(pTokens);
            if (nUsed > 0)
            {
                const char *pLast = (const char*)XArray_GetData(pTokens, nUsed - 1);
                if (pLast != NULL) xstrncpy(pLink->sFile, sizeof(pLink->sFile), pLast);
            }

            XArray_Destroy(pTokens);
        }
    }

    return XSTDOK;
}

int XAddr_GetIP(const char *pDNS, char *pAddr, int nSize)
{
    int nPort = 53;
    struct sockaddr_in serv;

    XSOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == XSOCK_INVALID) return XSTDERR;

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = XSock_NetAddr(pDNS);
    serv.sin_port = htons(nPort);

    if (connect(sock, (const struct sockaddr*) &serv , sizeof(serv)) < 0)
    {
        xclosesock(sock);
        return XSTDERR;
    }

    struct sockaddr_in name;
    xsocklen_t nameLen = sizeof(name);

    if (getsockname(sock, (struct sockaddr*) &name, &nameLen) < 0)
    {
        xclosesock(sock);
        return XSTDERR;
    }

    inet_ntop(AF_INET, &name.sin_addr, pAddr, nSize);
    xclosesock(sock);

    return nameLen;
}

int XAddr_GetIFCIP(const char *pIFace, char *pAddr, int nSize)
{
#ifndef _WIN32
    int nFD = socket(AF_INET, SOCK_DGRAM, 0);
    if (nFD < 0) return XSTDERR;

    struct ifreq ifbuf;
    ifbuf.ifr_addr.sa_family = AF_INET;
    xstrncpy(ifbuf.ifr_name, sizeof(ifbuf.ifr_name), pIFace);

    if (ioctl(nFD, SIOCGIFADDR, &ifbuf) < 0)
    {
        close(nFD);
        return XSTDERR;
    }

    close(nFD);

    char *pIPAddr = inet_ntoa(((struct sockaddr_in *)&ifbuf.ifr_addr)->sin_addr);
    return (pAddr != NULL) ? xstrncpyf(pAddr, nSize, "%s", pIPAddr) : XSTDERR;
#endif
    return (int)xstrncpyf(pAddr, nSize, "0.0.0.0");
}

int XAddr_GetIFCMac(const char *pIFace, char *pAddr, int nSize)
{
#ifdef __linux__
    int nFD = socket(AF_INET, SOCK_DGRAM, 0);
    if (nFD < 0) return XSTDERR;

    struct ifreq ifbuf;
    xstrncpy(ifbuf.ifr_name, sizeof(ifbuf.ifr_name), pIFace);

    if (ioctl(nFD, SIOCGIFHWADDR, &ifbuf) < 0)
    {
        close(nFD);
        return XSTDERR;
    }

    unsigned char *hwaddr = (unsigned char*) ifbuf.ifr_hwaddr.sa_data;
    close(nFD);

    return xstrncpyf(pAddr, nSize, "%02x:%02x:%02x:%02x:%02x:%02x",
        hwaddr[0],hwaddr[1],hwaddr[2],hwaddr[3],hwaddr[4],hwaddr[5]);
#endif
    return (int)xstrncpyf(pAddr, nSize, "0:0:0:0:0:0");
}

int XAddr_GetMAC(char *pAddr, int nSize)
{
#ifdef __linux__
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) return XSTDERR;

    struct ifconf ifc; char buf[1024];
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;

    if (ioctl(sock, SIOCGIFCONF, &ifc) == -1)
    {
        close(sock);
        return XSTDERR;
    }

    struct ifreq* it = ifc.ifc_req;
    const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));
    struct ifreq ifr;
    int nLength = 0;

    for (;it != end; ++it)
    {
        strcpy(ifr.ifr_name, it->ifr_name);
        if (ioctl(sock, SIOCGIFFLAGS, &ifr)) break;

        if (!(ifr.ifr_flags & IFF_LOOPBACK) &&
            !ioctl(sock, SIOCGIFHWADDR, &ifr))
        {
            nLength = xstrncpyf(pAddr, nSize, "%02x:%02x:%02x:%02x:%02x:%02x",
                ifr.ifr_hwaddr.sa_data[0], ifr.ifr_hwaddr.sa_data[1],
                ifr.ifr_hwaddr.sa_data[2], ifr.ifr_hwaddr.sa_data[3],
                ifr.ifr_hwaddr.sa_data[4], ifr.ifr_hwaddr.sa_data[5]);

            break;
        }
    }

    close(sock);
    return nLength;
#endif
    return (int)xstrncpyf(pAddr, nSize, "0:0:0:0:0:0");
}
