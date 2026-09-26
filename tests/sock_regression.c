/* libxutils: retryable nonblocking errors and descriptor ownership. */
#include "test.h"
#include "sock.h"
#include "sync.h"

#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>

typedef struct {
    pthread_t target;
    XSOCKET nPeer;
} xtest_interrupter_t;

static void XTest_OnSignal(int nSignal)
{
    (void)nSignal;
}

/* Interrupts the reader while it is blocked, then gives it the data it waits for */
static void *XTest_Interrupt(void *pContext)
{
    xtest_interrupter_t *pInterrupter = (xtest_interrupter_t*)pContext;
    xusleep(50000);
    pthread_kill(pInterrupter->target, SIGUSR1);
    xusleep(50000);
    if (send(pInterrupter->nPeer, "a\0b", 3, XMSG_NOSIGNAL) != 3) return (void*)1;
    return NULL;
}
#endif

static int XTest_retry_read(void)
{
    for (int nRecv = 0; nRecv < 2; nRecv++)
    {
        XSOCKET pair[2];
        xsock_t reader;
        CHECK(XSock_CreatePair(pair) == XSTDOK, "Create retry fixture");
        CHECK(XSock_Init(&reader, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "Wrap reader");
        CHECK(XSock_NonBlock(&reader, XTRUE) != XSOCK_INVALID, "Set nonblocking reads");
        char data[8];
        int nRead = nRecv ? XSock_Recv(&reader, data, sizeof(data)) : XSock_Read(&reader, data, sizeof(data));
        CHECK(nRead < 0 && reader.eStatus == XSOCK_WANT_READ && reader.nFD == pair[0], "EAGAIN keeps the read descriptor open");
        CHECK(send(pair[1], "a\0b", 3, 0) == 3, "Make previously empty socket readable");
        nRead = nRecv ? XSock_Recv(&reader, data, sizeof(data)) : XSock_Read(&reader, data, sizeof(data));
        CHECK(nRead == 3 && memcmp(data, "a\0b", 3) == 0, "The same descriptor succeeds on retry");
        XSock_Close(&reader);
        XSock_Close(&reader);
        xclosesock(pair[1]);
    }
    return 0;
}

static int XTest_retry_write(void)
{
    for (int nSend = 0; nSend < 2; nSend++)
    {
        XSOCKET pair[2];
        xsock_t writer;
        CHECK(XSock_CreatePair(pair) == XSTDOK, "Create write-pressure fixture");
        CHECK(XSock_Init(&writer, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "Wrap writer");
        CHECK(XSock_NonBlock(&writer, XTRUE) != XSOCK_INVALID, "Set nonblocking writes");
        int nCapacity = 4096;
        CHECK(setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, (const char*)&nCapacity, sizeof(nCapacity)) == 0,
            "Constrain write queue");
        uint8_t data[8192] = {0};
        int nWritten = 0;
        for (int i = 0; i < 1024; i++)
        {
            nWritten = nSend ? XSock_Send(&writer, data, sizeof(data)) : XSock_Write(&writer, data, sizeof(data));
            if (nWritten < 0) break;
        }
        CHECK(nWritten < 0 && writer.eStatus == XSOCK_WANT_WRITE && writer.nFD == pair[0],
            "A full write queue preserves the socket");
        CHECK(recv(pair[1], (char*)data, sizeof(data), 0) > 0, "Drain data to release write capacity");
        nWritten = nSend ? XSock_Send(&writer, "x", 1) : XSock_Write(&writer, "x", 1);
        CHECK(nWritten == 1, "Writing resumes after backpressure clears");
        XSock_Close(&writer);
        xclosesock(pair[1]);
    }
    return 0;
}

static int XTest_empty_and_eof(void)
{
    XSOCKET pair[2];
    xsock_t reader;
    CHECK(XSock_CreatePair(pair) == XSTDOK, "Create EOF fixture");
    CHECK(XSock_Init(&reader, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "Wrap reader");
    char data[4];
    CHECK(XSock_Read(&reader, data, 0) == 0 && reader.nFD == pair[0], "A zero-size request must not consume or close");
    xclosesock(pair[1]);
    CHECK(XSock_Read(&reader, data, sizeof(data)) == 0 && reader.eStatus == XSOCK_EOF, "Peer shutdown reports EOF");
    CHECK(reader.nFD == XSOCK_INVALID, "EOF releases the owned descriptor");
    XSock_Close(&reader);
    return 0;
}

static int XTest_interrupted_io(void)
{
#ifdef _WIN32
    return 77;
#else
    /* No SA_RESTART: a blocking call the signal interrupts fails with EINTR */
    struct sigaction action, previous;
    memset(&action, 0, sizeof(action));
    action.sa_handler = XTest_OnSignal;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, &previous) == 0, "Install an interrupting signal handler");

    for (int nMode = 0; nMode < 3; nMode++)
    {
        XSOCKET pair[2];
        xsock_t reader;
        CHECK(XSock_CreatePair(pair) == XSTDOK, "Create an interrupt fixture");
        CHECK(XSock_Init(&reader, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "Wrap the blocking reader");

        xtest_interrupter_t interrupter;
        interrupter.target = pthread_self();
        interrupter.nPeer = pair[1];

        pthread_t thread;
        CHECK(pthread_create(&thread, NULL, XTest_Interrupt, &interrupter) == 0, "Start the interrupting thread");

        char data[3];
        int nRead = nMode == 0 ? XSock_Recv(&reader, data, sizeof(data)) :
                    nMode == 1 ? XSock_RecvChunk(&reader, data, sizeof(data)) :
                                 XSock_Read(&reader, data, sizeof(data));

        void *pResult = NULL;
        pthread_join(thread, &pResult);
        CHECK(pResult == NULL, "The data is sent after the interruption");
        CHECK(nRead == 3 && memcmp(data, "a\0b", 3) == 0, "An interrupted blocking read is retried, not failed");
        CHECK(reader.nFD == pair[0], "A signal does not cost the connection");

        XSock_Close(&reader);
        xclosesock(pair[1]);
    }

    sigaction(SIGUSR1, &previous, NULL);
    return 0;
#endif
}

static int XTest_accept_cloexec(void)
{
#ifdef _WIN32
    return 77;
#else
    char sDir[] = "/tmp/xutils_sock_XXXXXX";
    CHECK(mkdtemp(sDir) != NULL, "Create a private directory for the listener");

    char sPath[64];
    xstrncpyf(sPath, sizeof(sPath), "%s/listen.sock", sDir);

    xsock_t listener, client, peer;
    CHECK(XSock_Create(&listener, XSOCK_UNIX_SERVER, sPath, 0) != XSOCK_INVALID, "Listen on a Unix socket");
    CHECK(XSock_Create(&client, XSOCK_UNIX_CLIENT, sPath, 0) != XSOCK_INVALID, "Connect to the listener");
    CHECK(XSock_Accept(&listener, &peer) != XSOCK_INVALID, "Accept the connection");

    int nFlags = fcntl(peer.nFD, F_GETFD);
    CHECK(nFlags >= 0 && (nFlags & FD_CLOEXEC), "An accepted socket is not inherited by child processes");

    XSock_Close(&peer);
    XSock_Close(&client);
    XSock_Close(&listener);
    unlink(sPath);
    rmdir(sDir);
    return 0;
#endif
}

XTEST_MAIN(
    XTEST_CASE(retry_read),
    XTEST_CASE(retry_write),
    XTEST_CASE(empty_and_eof),
    XTEST_CASE(interrupted_io),
    XTEST_CASE(accept_cloexec)
)
