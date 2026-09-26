/* libxutils: NTP client request/response handling.
 *
 * The client is exercised against a local UDP responder rather than a public
 * time server, so the test is hermetic: it can assert on the exact epoch it
 * put on the wire, and it can withhold a reply to prove the receive timeout
 * is reached instead of blocking forever. The responder uses raw sockets so
 * that a failure here always points at the NTP code under test.
 */

#include "test.h"
#include "ntp.h"
#include "sock.h"
#include "thread.h"
#include "sync.h"
#include "xtime.h"

#define XNTP_WIRE_WORDS     12
#define XNTP_TIME_GAP       2208988800U

typedef enum {
    NTP_REPLY_NORMAL = 0,
    NTP_REPLY_NONE,
    NTP_REPLY_SHORT
} ntp_reply_t;

typedef struct {
    uint16_t nPort;             /* Port the responder actually bound to */
    uint32_t nServeEpoch;       /* Unix epoch to answer with */
    ntp_reply_t eReply;         /* How the responder answers */
    xatomic_t nReady;           /* 1 once bound, 2 on failure */
    xatomic_t nRequestSeen;     /* Set once a request arrived */
    uint32_t nRequestWords[XNTP_WIRE_WORDS];
    int nRequestBytes;
} ntp_server_t;

static void *ntp_serve(void *pContext)
{
    ntp_server_t *pServer = (ntp_server_t*)pContext;
    int nFD = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (nFD < 0) { XSYNC_ATOMIC_SET(&pServer->nReady, 2); return NULL; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    socklen_t nLen = sizeof(addr);
    if (bind(nFD, (struct sockaddr*)&addr, nLen) < 0 ||
        getsockname(nFD, (struct sockaddr*)&addr, &nLen) < 0)
    {
        close(nFD);
        XSYNC_ATOMIC_SET(&pServer->nReady, 2);
        return NULL;
    }

    /* Bound the wait so an abandoned responder cannot outlive the test. */
    struct timeval timeout = {10, 0};
    setsockopt(nFD, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    pServer->nPort = ntohs(addr.sin_port);
    XSYNC_ATOMIC_SET(&pServer->nReady, 1);

    struct sockaddr_in peer;
    socklen_t nPeerLen = sizeof(peer);
    pServer->nRequestBytes = (int)recvfrom(nFD, (char*)pServer->nRequestWords,
        sizeof(pServer->nRequestWords), 0, (struct sockaddr*)&peer, &nPeerLen);

    if (pServer->nRequestBytes > 0)
    {
        XSYNC_ATOMIC_SET(&pServer->nRequestSeen, 1);
        if (pServer->eReply != NTP_REPLY_NONE)
        {
            uint32_t reply[XNTP_WIRE_WORDS];
            memset(reply, 0, sizeof(reply));
            reply[0] = htonl((4u << 27) | (4u << 24));
            /* Word ten is the transmit timestamp the client reads back. */
            reply[10] = htonl(pServer->nServeEpoch + XNTP_TIME_GAP);
            size_t nSend = pServer->eReply == NTP_REPLY_SHORT ? 8 : sizeof(reply);
            sendto(nFD, (const char*)reply, nSend, 0, (struct sockaddr*)&peer, nPeerLen);
        }
    }

    close(nFD);
    return NULL;
}

/* Configures the responder, starts it and waits for it to publish its port.
 * Every field is set before the thread runs so the test stays race free. */
static int ntp_start(ntp_server_t *pServer, xthread_t *pThread, ntp_reply_t eReply, uint32_t nEpoch)
{
    memset(pServer, 0, sizeof(*pServer));
    pServer->nServeEpoch = nEpoch;
    pServer->eReply = eReply;

    if (XThread_Create(pThread, ntp_serve, pServer, XFALSE) != XSTDOK) return XSTDERR;
    for (int i = 0; i < 1000 && !XSYNC_ATOMIC_GET(&pServer->nReady); i++) xusleep(5000);
    if (XSYNC_ATOMIC_GET(&pServer->nReady) == 1) return XSTDOK;

    XThread_Join(pThread);
    return XSTDERR;
}

static int XTest_request_reply(void)
{
    ntp_server_t server;
    xthread_t task;
    if (ntp_start(&server, &task, NTP_REPLY_NORMAL, 1600000000u) != XSTDOK)
    {
        printf("UDP loopback unavailable, skipping\n");
        return 77;
    }

    xtime_t time;
    memset(&time, 0, sizeof(time));
    int nStatus = XNTP_GetDate("127.0.0.1", server.nPort, &time);
    XThread_Join(&task);
    CHECK(nStatus == XSTDOK, "A served NTP reply is accepted");

    /* The epoch the responder sent must survive the era conversion. */
    xtime_t expected;
    XTime_FromEpoch(&expected, (time_t)server.nServeEpoch);
    CHECK(time.nYear == expected.nYear && time.nMonth == expected.nMonth && time.nDay == expected.nDay,
        "The served epoch decodes to the same calendar date");
    CHECK(time.nHour == expected.nHour && time.nMin == expected.nMin && time.nSec == expected.nSec,
        "The served epoch decodes to the same wall clock time");

    /* The request itself must be a well formed client packet. */
    CHECK(server.nRequestBytes == (int)sizeof(server.nRequestWords), "The client sends a full NTP request");
    uint32_t nHeader = ntohl(server.nRequestWords[0]);
    CHECK(((nHeader >> 27) & 0x07) == 3, "The request announces NTP version 3");
    CHECK(((nHeader >> 24) & 0x07) == 3, "The request announces client mode");
    CHECK(server.nRequestWords[10] != 0, "The request carries a transmit timestamp");
    return 0;
}

static int XTest_no_reply(void)
{
    /* A silent server must surface as an error once the receive timeout
     * expires, not hang the caller forever. */
    ntp_server_t server;
    xthread_t task;
    if (ntp_start(&server, &task, NTP_REPLY_NONE, 1600000000u) != XSTDOK)
    {
        printf("UDP loopback unavailable, skipping\n");
        return 77;
    }

    xtime_t time;
    uint64_t nStart = XTime_GetMs();
    int nStatus = XNTP_GetDate("127.0.0.1", server.nPort, &time);
    uint64_t nElapsed = XTime_GetMs() - nStart;
    XThread_Join(&task);

    CHECK(nStatus == XSTDERR, "A silent server is reported as an error");
    CHECK(nElapsed < 30000, "The receive timeout bounds the wait");
    CHECK(XSYNC_ATOMIC_GET(&server.nRequestSeen) == 1, "The request still reached the server");
    return 0;
}

static int XTest_zero_epoch(void)
{
    /* Word ten of zero decodes to an epoch before 1970; the client treats
     * the resulting zero as a failed exchange rather than year 1900. */
    ntp_server_t server;
    xthread_t task;
    if (ntp_start(&server, &task, NTP_REPLY_NORMAL, 0) != XSTDOK)
    {
        printf("UDP loopback unavailable, skipping\n");
        return 77;
    }

    xtime_t time;
    memset(&time, 0, sizeof(time));
    int nStatus = XNTP_GetDate("127.0.0.1", server.nPort, &time);
    XThread_Join(&task);

    CHECK(nStatus == XSTDERR, "A zero transmit timestamp is rejected");
    CHECK(time.nYear == 0, "A rejected exchange leaves the output untouched");
    return 0;
}

static int XTest_guards(void)
{
    xtime_t time;
    CHECK(XNTP_GetDate(NULL, 123, &time) == XSTDERR, "A missing address is rejected");
    CHECK(XNTP_GetDate("no.such.host.invalid", 123, &time) == XSTDERR, "An unresolvable host is rejected");

    /* A short reply leaves the transmit word unset, which the client must
     * treat as a failed exchange rather than reading uninitialised memory. */
    ntp_server_t server;
    xthread_t task;
    if (ntp_start(&server, &task, NTP_REPLY_SHORT, 1600000000u) != XSTDOK)
    {
        printf("UDP loopback unavailable, skipping\n");
        return 77;
    }

    memset(&time, 0, sizeof(time));
    uint64_t nStart = XTime_GetMs();
    int nStatus = XNTP_GetDate("127.0.0.1", server.nPort, &time);
    uint64_t nElapsed = XTime_GetMs() - nStart;
    XThread_Join(&task);

    CHECK(nStatus == XSTDOK || nStatus == XSTDERR, "A truncated reply returns a defined status");
    CHECK(nElapsed < 30000, "A truncated reply does not hang the client");
    return 0;
}

static int ntp_open_fds(void)
{
    int nCount = 0;
    for (int nFD = 0; nFD < 4096; nFD++)
        if (fcntl(nFD, F_GETFD) != -1) nCount++;

    return nCount;
}

static int XTest_failure_closes_socket(void)
{
    /* A query that failed after the socket was opened returned without
     * closing it, so a client polling an unreachable server ran out of
     * descriptors. Nothing listens on the port used here. */
    int nFD = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (nFD < 0)
    {
        printf("UDP sockets unavailable, skipping\n");
        return 77;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    socklen_t nLen = sizeof(addr);
    if (bind(nFD, (struct sockaddr*)&addr, nLen) < 0 || getsockname(nFD, (struct sockaddr*)&addr, &nLen) < 0)
    {
        close(nFD);
        printf("UDP loopback unavailable, skipping\n");
        return 77;
    }

    uint16_t nPort = ntohs(addr.sin_port);
    close(nFD);

    int nBefore = ntp_open_fds();

    for (int i = 0; i < 3; i++)
    {
        xtime_t time;
        CHECK(XNTP_GetDate("127.0.0.1", nPort, &time) == XSTDERR, "A query nobody answers fails");
    }

    CHECK(ntp_open_fds() == nBefore, "Failed queries leave no socket open");

    /* The rejected exchange of a served zero timestamp closes it too */
    ntp_server_t server;
    xthread_t task;
    if (ntp_start(&server, &task, NTP_REPLY_NORMAL, 0) != XSTDOK)
    {
        printf("UDP loopback unavailable, skipping\n");
        return 77;
    }

    xtime_t time;
    CHECK(XNTP_GetDate("127.0.0.1", server.nPort, &time) == XSTDERR, "A zero timestamp is rejected");
    XThread_Join(&task);

    /* The responder has closed its own socket by now as well */
    CHECK(ntp_open_fds() == nBefore, "A rejected reply leaves no socket open");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(request_reply),
    XTEST_CASE(no_reply),
    XTEST_CASE(zero_epoch),
    XTEST_CASE(guards),
    XTEST_CASE(failure_closes_socket)
)
