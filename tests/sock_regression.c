/* libxutils: retryable nonblocking errors and descriptor ownership. */
#include "test.h"
#include "sock.h"

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

XTEST_MAIN(
    XTEST_CASE(retry_read),
    XTEST_CASE(retry_write),
    XTEST_CASE(empty_and_eof)
)
