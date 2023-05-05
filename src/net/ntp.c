/*!
 *  @file libxutils/src/net/ntp.c
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of NTP protocol functionality
 */

#include "xstd.h"
#include "sock.h"
#include "ntp.h"

#define XNTP_BUF_SIZE   12
#define XNTP_DEF_PORT   123
#define XNTP_TIMEO_SEC  10

#define XNTP_TIME_GAP   2208988800U
#define XNTP_JAN_1970   0x83aa7e80
#define XNTP_FRAC(x)    (4294 * (x) + ((1981 * (x)) >> 11))

#define XNTP_LI         0
#define XNTP_VN         3
#define XNTP_MODE       3
#define XNTP_STRA       0
#define XNTP_POLL       4
#define XNTP_PREC       -6

#define XNTP_HDR        \
    (XNTP_LI << 30) |   \
    (XNTP_VN << 27) |   \
    (XNTP_MODE << 24) | \
    (XNTP_STRA << 16) | \
    (XNTP_POLL << 8) |  \
    (XNTP_PREC & 0xff)

int XNTP_SendRequest(xsock_t *pSock)
{
    uint32_t buffer[XNTP_BUF_SIZE];
    memset(buffer, 0, sizeof(buffer));

     /* Root Delay (seconds) */
    buffer[0] = htonl(XNTP_HDR);
    buffer[1] = htonl(1 << 16);
    buffer[2] = htonl(1 << 16);

    struct timeval now;
    xtime_spec_t ts;

    /* Get time */
    XTime_GetClock(&ts);
    now.tv_sec = (long) ts.nSec;
    now.tv_usec = (long)ts.nNanoSec / 1000;

    /* Transmit Timestamp coarse */
    buffer[10] = htonl(now.tv_sec + XNTP_JAN_1970);
    buffer[11] = htonl(XNTP_FRAC(now.tv_usec));

    if (XSock_TimeOutS(pSock, XNTP_TIMEO_SEC, 0) < 0) return XSTDERR;
    return XSock_Send(pSock, buffer, sizeof(buffer));
}

uint32_t XNTP_ReceiveTime(xsock_t *pSock)
{
    uint32_t buffer[XNTP_BUF_SIZE];
    XSock_TimeOutR(pSock, XNTP_TIMEO_SEC, 0);
    XSock_Read(pSock, buffer, sizeof(buffer));
    if (XSock_Status(pSock) != XSOCK_ERR_NONE) return XSTDNON;

    time_t rawtime = ntohl((time_t)buffer[10]);
    return (uint32_t)rawtime - XNTP_TIME_GAP;
}

int XNTP_GetDate(const char *pAddr, uint16_t nPort, xtime_t *pTime)
{
    if (pAddr == NULL) return XSTDERR;
    if (!nPort) nPort = XNTP_DEF_PORT;

    xsock_addr_t sockAddr;
    XSock_GetAddr(&sockAddr, pAddr);
    sockAddr.nPort = nPort;

    xsock_t sock;
    XSock_Open(&sock, XSOCK_UDP_CLIENT, &sockAddr);
    if (sock.eStatus != XSOCK_ERR_NONE) return XSTDERR;

    if (XNTP_SendRequest(&sock) <= 0) return XSTDERR;
    time_t nEpoch = XNTP_ReceiveTime(&sock);
    if (!nEpoch) return XSTDERR;

    XTime_FromEpoch(pTime, nEpoch);
    XSock_Close(&sock);
    return XSTDOK;
}
