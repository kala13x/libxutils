#include <stdio.h>
#include <string.h>

#include "sock.h"
#include "xtime.h"

#include "test.h"

/*
 * SO_RCVTIMEO / SO_SNDTIMEO take a DWORD of milliseconds on Winsock and a
 * struct timeval on POSIX. Handing Windows the struct makes it read tv_sec as
 * milliseconds, so a 20 second timeout becomes 20 ms and every recv fails
 * immediately - which is exactly how `dgcli login` broke on Windows while
 * working perfectly on Linux.
 *
 * The assertion is on observable behaviour rather than the option encoding,
 * so it is meaningful on whichever platform it runs.
 */
static int test_receive_timeout_is_honoured(void)
{
    XSOCKET pair[2] = {XSOCK_INVALID, XSOCK_INVALID};
    CHECK(XSock_CreatePair(pair) == XSTDOK, "create socket pair");

    xsock_t reader;
    CHECK(XSock_Init(&reader, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "wrap the read end");

    CHECK(XSock_TimeOutR(&reader, 2, 0) != XSOCK_INVALID, "set a two second receive timeout");

    /* Nothing is ever written, so the read can only end on the timeout */
    uint8_t sBuffer[16];
    uint64_t nStartMs = XTime_GetMs();
    int nRead = XSock_Read(&reader, sBuffer, sizeof(sBuffer));
    uint64_t nElapsedMs = XTime_GetMs() - nStartMs;

    CHECK(nRead <= 0, "a read with no data must not succeed");

    /*
     * Before the fix this returned in about 2 ms on Windows. The window is
     * wide on both sides so the test cannot flake on a loaded machine.
     */
    CHECK(nElapsedMs >= 1500, "a two second timeout must not expire in milliseconds");
    CHECK(nElapsedMs < 10000, "a two second timeout must not block far longer");

    XSock_Close(&reader);
    xclosesock(pair[1]);
    return 0;
}

/* A zero timeout means "no timeout"; sub-millisecond requests must not round
 * down into it and block forever. */
static int test_timeout_arguments(void)
{
    XSOCKET pair[2] = {XSOCK_INVALID, XSOCK_INVALID};
    CHECK(XSock_CreatePair(pair) == XSTDOK, "create socket pair");

    xsock_t sock;
    CHECK(XSock_Init(&sock, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "wrap the socket");

    CHECK(XSock_TimeOutS(&sock, 1, 0) != XSOCK_INVALID, "set a send timeout");
    CHECK(XSock_TimeOutR(&sock, 0, 500) != XSOCK_INVALID, "set a sub-millisecond receive timeout");

    uint8_t sBuffer[8];
    uint64_t nStartMs = XTime_GetMs();
    XSock_Read(&sock, sBuffer, sizeof(sBuffer));
    uint64_t nElapsedMs = XTime_GetMs() - nStartMs;

    CHECK(nElapsedMs < 5000, "a sub-millisecond timeout must not become no timeout");

    XSock_Close(&sock);
    xclosesock(pair[1]);
    return 0;
}

static int XTest_boundaries(void)
{
    xlog_setfl(XLOG_NONE);

    int nStatus = test_receive_timeout_is_honoured();
    if (nStatus) return nStatus;

    nStatus = test_timeout_arguments();
    if (nStatus) return nStatus;

    puts("sock_timeout_regression: OK");
    return 0;
}

XTEST_MAIN(XTEST_CASE(boundaries))
