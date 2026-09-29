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

/* XSOCK_KEEPOPEN: the end of the stream and a failed write are reported the
   same way, but the descriptor stays open until its owner closes it. */
static int XTest_keep_open(void)
{
    XSOCKET pair[2];
    xsock_t reader;
    char data[4];
    CHECK(XSock_CreatePair(pair) == XSTDOK, "Create a kept-open EOF fixture");
    CHECK(XSock_Init(&reader, XSOCK_TCP_PEER | XSOCK_KEEPOPEN, pair[0]) != XSOCK_ERROR, "Wrap a kept-open reader");
    xclosesock(pair[1]);

    CHECK(XSock_Read(&reader, data, sizeof(data)) == 0 && reader.eStatus == XSOCK_EOF, "EOF is still reported as EOF");
    CHECK(reader.nFD == pair[0] && XSock_IsOpen(&reader) == XSOCK_SUCCESS, "A kept-open socket is left for its owner to close");
    CHECK(XSock_Read(&reader, data, sizeof(data)) == 0 && reader.eStatus == XSOCK_EOF, "Reading again reports EOF again");

#ifndef _WIN32
    /* Sending to a peer that is gone fails the same way: reported, not closed. */
    reader.eStatus = XSOCK_ERR_NONE;
    CHECK(XSock_Send(&reader, "abcd", 4) <= 0 && reader.eStatus == XSOCK_ERR_SEND, "A send to a closed peer fails");
    CHECK(XSock_IsOpen(&reader) == XSOCK_SUCCESS, "A failed send leaves a kept-open socket open");
#endif

    XSock_Close(&reader);
    CHECK(reader.nFD == XSOCK_INVALID, "The owner's close releases the descriptor");
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

static int XTest_accept_flags(void)
{
#ifdef _WIN32
    return 77;
#else
    /* An accepted socket is always close-on-exec, and non-blocking exactly
       when its listener is, with the flags word saying the same as the kernel. */
    char sDir[] = "/tmp/xutils_sock_XXXXXX";
    CHECK(mkdtemp(sDir) != NULL, "Create a private directory for the listeners");

    for (int nBlocking = 0; nBlocking < 2; nBlocking++)
    {
        char sPath[64];
        xstrncpyf(sPath, sizeof(sPath), "%s/listen%d.sock", sDir, nBlocking);

        uint32_t nFlags = XSOCK_UNIX_SERVER | (nBlocking ? 0 : XSOCK_NB);
        xsock_t listener, client, peer;
        CHECK(XSock_Create(&listener, nFlags, sPath, 0) != XSOCK_INVALID, "Listen on a Unix socket");
        CHECK(XSock_Create(&client, XSOCK_UNIX_CLIENT, sPath, 0) != XSOCK_INVALID, "Connect to the listener");
        CHECK(XSock_Accept(&listener, &peer) != XSOCK_INVALID, "Accept the connection");

        int nDescFlags = fcntl(peer.nFD, F_GETFD);
        int nFileFlags = fcntl(peer.nFD, F_GETFL);
        CHECK(nDescFlags >= 0 && (nDescFlags & FD_CLOEXEC), "An accepted socket is close-on-exec");
        CHECK(nFileFlags >= 0 && !!(nFileFlags & O_NONBLOCK) == !nBlocking, "It blocks exactly when its listener does");
        CHECK(XSock_IsNB(&peer) == !nBlocking, "And its flags say so");
        CHECK(XFLAGS_CHECK(XSock_GetFlags(&peer), XSOCK_PEER) && !XFLAGS_CHECK(XSock_GetFlags(&peer), XSOCK_SERVER),
            "It is a peer, not a server");

        /* Nothing is waiting: a non-blocking listener says so instead of blocking */
        if (!nBlocking)
        {
            xsock_t none;
            CHECK(XSock_Accept(&listener, &none) == XSOCK_INVALID, "An empty queue accepts nothing");
            CHECK(XSock_Status(&listener) == XSOCK_WANT_READ, "And asks to be called again when readable");
            listener.eStatus = XSOCK_ERR_NONE;
        }

        XSock_Close(&peer);
        XSock_Close(&client);
        XSock_Close(&listener);
        unlink(sPath);
    }

    rmdir(sDir);
    return 0;
#endif
}

#ifndef _WIN32
static volatile sig_atomic_t g_nPipeSignals;

static void XTest_PipeSignal(int nSignal)
{
    if (nSignal == SIGPIPE) g_nPipeSignals++;
}
#endif

static int XTest_closed_write(void)
{
#ifndef _WIN32
    struct sigaction action = {0}, previous;
    action.sa_handler = XTest_PipeSignal;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGPIPE, &action, &previous) == 0, "Observe unexpected pipe signals");
    g_nPipeSignals = 0;
    int nErrors = 0;
    for (int nSend = 0; nSend < 2; nSend++)
    {
        for (int nKeep = 0; nKeep < 2; nKeep++)
        {
            for (int nUnix = 0; nUnix < 2; nUnix++)
            {
                XSOCKET pair[2];
                if (XSock_CreatePair(pair) != XSTDOK) { nErrors++; continue; }
                xsock_t writer;
                uint32_t nFlags = nUnix ? XSOCK_UNIX_PEER : XSOCK_TCP_PEER;
                if (nKeep) nFlags |= XSOCK_KEEPOPEN;
                if (XSock_Init(&writer, nFlags, pair[0]) == XSOCK_ERROR) nErrors++;
                xclosesock(pair[1]);
                int nSent = nSend ? XSock_Send(&writer, "x", 1) : XSock_Write(&writer, "x", 1);
                if (nSent >= 0) nErrors++;
                if (writer.eStatus != (nSend ? XSOCK_ERR_SEND : XSOCK_ERR_WRITE)) nErrors++;
                if (writer.nFD != (nKeep ? pair[0] : XSOCK_INVALID)) nErrors++;
                XSock_Close(&writer);
            }
        }
    }
    sigaction(SIGPIPE, &previous, NULL);
    CHECK(nErrors == 0, "A closed peer reports a write error and obeys descriptor ownership");
    CHECK(g_nPipeSignals == 0, "A disconnected socket must not signal or terminate its caller");
#endif
    return 0;
}

static int XTest_pipe_write(void)
{
#ifndef _WIN32
    int nPipe[2];
    CHECK(pipe(nPipe) == 0, "Create a non-socket event descriptor");
    xsock_t writer;
    CHECK(XSock_Init(&writer, XSOCK_EVENT | XSOCK_TCP, nPipe[1]) != XSOCK_ERROR, "Wrap the writable pipe");
    int nSent = XSock_Write(&writer, "a\0b", 3);
    char data[3] = {0};
    int nRead = nSent == 3 ? (int)read(nPipe[0], data, sizeof(data)) : 0;
    XSock_Close(&writer);
    close(nPipe[0]);
    CHECK(nSent == 3 && nRead == 3 && !memcmp(data, "a\0b", 3), "Writing generic event descriptors stays supported");
#endif
    return 0;
}

static int XTest_empty_datagram(void)
{
#ifdef _WIN32
    return 0;
#else
    /* A datagram socket has no end of stream. An empty datagram, which anyone can send, is read as nothing,
       the socket stays open, and the datagram after it is read as usual. */
    for (int nRecv = 0; nRecv < 2; nRecv++)
    {
        int nReader = socket(AF_INET, SOCK_DGRAM, 0);
        int nWriter = socket(AF_INET, SOCK_DGRAM, 0);
        CHECK(nReader >= 0 && nWriter >= 0, "Create datagram sockets");

        struct sockaddr_in addr;
        socklen_t nAddrLen = sizeof(addr);
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(bind(nReader, (struct sockaddr*)&addr, sizeof(addr)) == 0, "Bind the reader to loopback");
        CHECK(getsockname(nReader, (struct sockaddr*)&addr, &nAddrLen) == 0, "Learn its port");

        xsock_t reader;
        CHECK(XSock_Init(&reader, XSOCK_UDP_CLIENT, nReader) != XSOCK_ERROR, "Wrap the reader");
        CHECK(XSock_GetSockType(&reader) == SOCK_DGRAM, "As a datagram socket");

        CHECK(sendto(nWriter, "", 0, 0, (struct sockaddr*)&addr, sizeof(addr)) == 0, "Send an empty datagram");
        CHECK(sendto(nWriter, "data", 4, 0, (struct sockaddr*)&addr, sizeof(addr)) == 4, "And one with data");

        char sData[16];
        int nBytes = nRecv ? XSock_Recv(&reader, sData, sizeof(sData)) : XSock_Read(&reader, sData, sizeof(sData));
        CHECK(nBytes == 0, "The empty datagram is read as nothing");
        CHECK(reader.nFD == nReader && reader.eStatus != XSOCK_EOF, "It is not the end, and the socket stays open");

        nBytes = nRecv ? XSock_Recv(&reader, sData, sizeof(sData)) : XSock_Read(&reader, sData, sizeof(sData));
        CHECK(nBytes == 4 && !memcmp(sData, "data", 4), "The next datagram is read whole");

        XSock_Close(&reader);
        close(nWriter);
    }

    return 0;
#endif
}

XTEST_MAIN(
    XTEST_CASE(retry_read),
    XTEST_CASE(retry_write),
    XTEST_CASE(empty_and_eof),
    XTEST_CASE(keep_open),
    XTEST_CASE(interrupted_io),
    XTEST_CASE(accept_cloexec),
    XTEST_CASE(accept_flags),
    XTEST_CASE(empty_datagram),
    XTEST_CASE(closed_write),
    XTEST_CASE(pipe_write)
)
