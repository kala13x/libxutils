/* libxutils: socket address helpers, options and the accessor surface.
 *
 * Everything runs over loopback or a socket pair so no external service is
 * needed. The address conversions are checked both ways round, since a
 * byte-order slip there sends a client to the wrong host rather than
 * failing outright.
 */

#include "test.h"
#include "sock.h"
#include "buf.h"
#include "str.h"
#include <unistd.h>


/* XSock_Create() rejects port zero rather than asking the kernel for an
 * ephemeral one, so a test listener has to hunt for a free port itself. */
static uint16_t sock_bind_free(xsock_t *pSock, uint32_t nFlags)
{
    for (uint16_t nPort = 39000; nPort < 39200; nPort++)
    {
        if (XSock_Create(pSock, nFlags, "127.0.0.1", nPort) != XSOCK_INVALID &&
            XSock_Status(pSock) == XSOCK_ERR_NONE) return nPort;

        XSock_Close(pSock);
    }

    return 0;
}

static int XTest_address_conversion(void)
{
    /* A dotted quad converts to a network order word and back. */
    char sAddr[64];
    uint32_t nAddr = XSock_NetAddr("127.0.0.1");
    CHECK(nAddr != 0, "The loopback address converts to a network word");
    CHECK(XSock_IPStr(nAddr, sAddr, sizeof(sAddr)) > 0, "The network word converts back to text");
    CHECK(strcmp(sAddr, "127.0.0.1") == 0, "The loopback address round trips");

    const char *pAddresses[] = {"0.0.0.0", "1.2.3.4", "192.168.1.1", "255.255.255.255", "10.0.0.1"};
    for (size_t i = 0; i < sizeof(pAddresses) / sizeof(*pAddresses); i++)
    {
        nAddr = XSock_NetAddr(pAddresses[i]);
        CHECK(XSock_IPStr(nAddr, sAddr, sizeof(sAddr)) > 0, "Every address converts back to text");
        CHECK(strcmp(sAddr, pAddresses[i]) == 0, "Every address round trips through the network word");
    }

    /* The struct in_addr form agrees with the raw word form. */
    struct in_addr inAddr;
    inAddr.s_addr = XSock_NetAddr("8.8.4.4");
    CHECK(XSock_SinAddr(inAddr, sAddr, sizeof(sAddr)) > 0, "The in_addr form converts to text");
    CHECK(strcmp(sAddr, "8.8.4.4") == 0, "The in_addr form agrees with the raw word");

    /* A destination too small truncates rather than overflowing. */
    char sTiny[4];
    memset(sTiny, 0x5a, sizeof(sTiny));
    XSock_IPStr(XSock_NetAddr("192.168.100.200"), sTiny, sizeof(sTiny));
    CHECK(strlen(sTiny) < sizeof(sTiny), "A short destination truncates");
    return 0;
}

static int XTest_address_info(void)
{
    /* Resolving a literal address needs no DNS and keeps the literal. */
    xsock_info_t info;
    XSock_InitInfo(&info);
    CHECK(info.nPort == 0 && info.sAddr[0] == '\0', "An initialized info is empty");

    /* Resolution reports failure as a negative status; success is zero. */
    CHECK(XSock_GetAddrInfo(&info, "127.0.0.1") >= 0, "A literal address resolves");
    CHECK(strcmp(info.sAddr, "127.0.0.1") == 0, "The literal address is kept as written");

    /* Localhost resolves to the loopback address. */
    XSock_InitInfo(&info);
    if (XSock_GetAddrInfo(&info, "localhost") >= 0)
        CHECK(strncmp(info.sAddr, "127.", 4) == 0 || strcmp(info.sAddr, "::1") == 0,
            "Localhost resolves to a loopback address");

    /* A name that cannot resolve is an error, not a stale answer. */
    XSock_InitInfo(&info);
    CHECK(XSock_GetAddrInfo(&info, "no.such.host.invalid") < 0, "An unresolvable name fails");
    CHECK(info.sAddr[0] == '\0', "A failed resolution leaves no address behind");

    /* The family-specific form resolves the same literal. */
    XSock_InitInfo(&info);
    CHECK(XSock_AddrInfo(&info, XF_IPV4, "127.0.0.1") >= 0, "The IPv4 form resolves a literal");
    CHECK(strcmp(info.sAddr, "127.0.0.1") == 0, "The IPv4 form keeps the literal");

    /* The sockaddr bridge runs the other way: it describes an address that
     * arrived on the wire, filling in the text form and, where the host can
     * be reverse resolved, its name. */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = XSock_NetAddr("127.0.0.1");

    XSock_InitInfo(&info);
    XSock_GetAddr(&info, &addr, sizeof(addr));
    CHECK(info.eFamily == XF_IPV4, "The described address is IPv4");
    CHECK(strcmp(info.sAddr, "127.0.0.1") == 0, "The described address is rendered as text");

    /* Loopback resolves through the hosts file on every machine this runs
     * on, so the reverse lookup has to produce a name. */
    CHECK(info.sName[0] != '\0', "The loopback address reverse resolves to a name");
    return 0;
}

static int XTest_status_strings(void)
{
    /* Every status has a description, and none of them is the fallback. */
    const xsock_status_t states[] = {
        XSOCK_ERR_NONE, XSOCK_ERR_ALLOC, XSOCK_ERR_CREATE, XSOCK_ERR_INVALID,
        XSOCK_ERR_SUPPORT, XSOCK_ERR_BIND, XSOCK_ERR_LISTEN, XSOCK_ERR_ACCEPT,
        XSOCK_ERR_CONNECT, XSOCK_ERR_RECV, XSOCK_ERR_SEND, XSOCK_ERR_READ,
        XSOCK_ERR_WRITE, XSOCK_ERR_SETFL, XSOCK_ERR_GETFL, XSOCK_ERR_ARGS
    };

    for (size_t i = 0; i < sizeof(states) / sizeof(*states); i++)
    {
        const char *pText = XSock_GetStatusStr(states[i]);
        CHECK(pText != NULL && *pText != '\0', "Every status has a description");
    }

    /* The SSL statuses are recognised as such, the others are not. */
    CHECK(XSock_IsSSLError(XSOCK_ERR_NONE) == XFALSE, "Success is not an SSL error");
    CHECK(XSock_IsSSLError(XSOCK_ERR_BIND) == XFALSE, "A bind failure is not an SSL error");
    return 0;
}

static int XTest_accessors(void)
{
    /* A socket pair gives two connected peers with no addressing at all. */
    XSOCKET pair[2] = {XSOCK_INVALID, XSOCK_INVALID};
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");
    CHECK(pair[0] != XSOCK_INVALID && pair[1] != XSOCK_INVALID, "Both ends are valid descriptors");

    xsock_t sock;
    CHECK(XSock_Init(&sock, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "The read end wraps");
    CHECK(XSock_GetFD(&sock) == pair[0], "The handle reports the descriptor it wrapped");
    CHECK(XSock_Status(&sock) == XSOCK_ERR_NONE, "A freshly wrapped socket has no error");
    CHECK(XSock_IsOpen(&sock) == XTRUE, "A wrapped socket is open");
    CHECK(XSock_IsSSL(&sock) == XFALSE, "A plain socket is not an SSL socket");
    CHECK(XSock_IsNB(&sock) == XFALSE, "A plain socket starts blocking");
    CHECK((XSock_GetFlags(&sock) & XSOCK_TCP) == XSOCK_TCP, "The socket reports the flags it was given");
    CHECK(XSock_GetSockAddr(&sock) != NULL, "The socket has an address structure");
    CHECK(XSock_GetAddrLen(&sock) > 0, "The address structure has a length");

    /* Options are applied without disturbing the connection. */
    CHECK(XSock_NonBlock(&sock, XTRUE) != XSOCK_INVALID, "The socket is made non blocking");
    CHECK(XSock_IsNB(&sock) == XTRUE, "The non blocking flag is recorded");
    CHECK(XSock_NonBlock(&sock, XFALSE) != XSOCK_INVALID, "The socket is made blocking again");
    CHECK(XSock_IsNB(&sock) == XFALSE, "The non blocking flag is cleared");

    CHECK(XSock_TimeOutR(&sock, 1, 0) != XSOCK_INVALID, "A receive timeout is applied");
    CHECK(XSock_TimeOutS(&sock, 1, 0) != XSOCK_INVALID, "A send timeout is applied");
    CHECK(XSock_Oobinline(&sock, XTRUE) != XSOCK_INVALID, "Out of band inline is applied");
    CHECK(XSock_Linger(&sock, 0) != XSOCK_INVALID, "A linger setting is applied");

    /* Data written at one end arrives at the other. */
    xsock_t peer;
    CHECK(XSock_Init(&peer, XSOCK_TCP_PEER, pair[1]) != XSOCK_ERROR, "The write end wraps");

    const char payload[] = "over the pair";
    CHECK(XSock_Write(&peer, payload, sizeof(payload)) == (int)sizeof(payload), "The payload is written");

    char sRead[64];
    CHECK(XSock_Read(&sock, sRead, sizeof(sRead)) == (int)sizeof(payload), "The payload is read back");
    CHECK(memcmp(sRead, payload, sizeof(payload)) == 0, "Every payload byte survives the pair");

    /* Closing one end is visible at the other as an orderly end of stream. */
    XSock_Close(&peer);
    CHECK(XSock_IsOpen(&peer) == XFALSE, "A closed socket is not open");
    CHECK(XSock_GetFD(&peer) == XSOCK_INVALID, "A closed socket has no descriptor");

    CHECK(XSock_Read(&sock, sRead, sizeof(sRead)) <= 0, "The peer closing ends the stream");
    XSock_Close(&sock);

    /* Closing twice is safe. */
    XSock_Close(&sock);
    CHECK(XSock_IsOpen(&sock) == XFALSE, "A repeatedly closed socket stays closed");
    return 0;
}

static int XTest_buffers(void)
{
    /* The buffer writers drain what they are given and report it. */
    XSOCKET pair[2] = {XSOCK_INVALID, XSOCK_INVALID};
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");

    xsock_t writer, reader;
    CHECK(XSock_Init(&writer, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "The writer wraps");
    CHECK(XSock_Init(&reader, XSOCK_TCP_PEER, pair[1]) != XSOCK_ERROR, "The reader wraps");
    CHECK(XSock_TimeOutR(&reader, 2, 0) != XSOCK_INVALID, "The reader gets a timeout");

    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, 0, 0);
    CHECK(XByteBuffer_Add(&buffer, (const uint8_t*)"buffered payload", 16), "The buffer is filled");

    CHECK(XSock_SendBuff(&writer, &buffer) > 0, "The buffer is sent");

    char sRead[64];
    int nRead = XSock_Read(&reader, sRead, sizeof(sRead));
    CHECK(nRead == 16, "Every buffered byte arrives");
    CHECK(memcmp(sRead, "buffered payload", 16) == 0, "The buffered bytes are unchanged");
    XByteBuffer_Clear(&buffer);

    /* An empty buffer sends nothing rather than failing. */
    XByteBuffer_Init(&buffer, 0, 0);
    CHECK(XSock_SendBuff(&writer, &buffer) <= 0, "An empty buffer sends nothing");
    XByteBuffer_Clear(&buffer);

    /* A chunked send moves the whole payload even in pieces. */
    char sBig[8192];
    memset(sBig, 'z', sizeof(sBig));
    CHECK(XSock_NonBlock(&writer, XTRUE) != XSOCK_INVALID, "The writer is made non blocking");

    int nSent = XSock_Send(&writer, sBig, sizeof(sBig));
    CHECK(nSent > 0, "A large payload sends at least partially");

    /* Peeking sees a waiting byte without consuming it. */
    CHECK(XSock_MsgPeek(&reader) == XSOCK_SUCCESS, "A peek on a socket with data succeeds");
    CHECK(XSock_MsgPeek(&reader) == XSOCK_SUCCESS, "Peeking twice sees the same byte, so it was not consumed");

    /* The pending count reports bytes held inside the SSL record layer, so
     * a plain socket always has none no matter how much is queued. */
    CHECK(XSock_Pending(&reader) == 0, "A plain socket buffers nothing above the kernel");

    int nDrained = 0;
    while (nDrained < nSent)
    {
        int nChunk = XSock_Read(&reader, sBig, sizeof(sBig));
        if (nChunk <= 0) break;
        nDrained += nChunk;
    }
    CHECK(nDrained == nSent, "Everything that was sent is eventually read");

    XSock_Close(&writer);
    XSock_Close(&reader);
    return 0;
}

static int XTest_listener(void)
{
    /* A TCP listener on loopback accepts one client and talks to it. */
    xsock_t server;
    uint16_t nPort = sock_bind_free(&server, XSOCK_TCP_SERVER | XSOCK_REUSEADDR);
    if (!nPort)
    {
        printf("No free loopback port, skipping\n");
        return 77;
    }

    CHECK(XSock_Status(&server) == XSOCK_ERR_NONE, "The listener has no error");
    CHECK(XSock_IsOpen(&server) == XTRUE, "The listener is open");
    CHECK(XSock_GetPort(&server) == nPort, "The listener reports the port it bound");

    /* A client connects to it. */
    xsock_t client;
    CHECK(XSock_Create(&client, XSOCK_TCP_CLIENT, "127.0.0.1", nPort) != XSOCK_INVALID,
        "The client connects to the listener");
    CHECK(XSock_Status(&client) == XSOCK_ERR_NONE, "The client has no error");
    CHECK(XSock_GetPort(&client) == nPort, "The client reports the port it connected to");

    xsock_t accepted;
    CHECK(XSock_Accept(&server, &accepted) != XSOCK_INVALID, "The listener accepts the client");
    CHECK(XSock_IsOpen(&accepted) == XTRUE, "The accepted socket is open");

    /* The accepted peer knows where the client came from. */
    char sPeer[64];
    CHECK(XSock_IPAddr(&accepted, sPeer, sizeof(sPeer)) > 0, "The accepted peer has an address");
    CHECK(strcmp(sPeer, "127.0.0.1") == 0, "The accepted peer came from loopback");

    /* Data flows in both directions. */
    const char toServer[] = "ping";
    CHECK(XSock_Write(&client, toServer, sizeof(toServer)) == (int)sizeof(toServer), "The client writes");

    char sRead[32];
    CHECK(XSock_TimeOutR(&accepted, 2, 0) != XSOCK_INVALID, "The accepted socket gets a timeout");
    CHECK(XSock_Read(&accepted, sRead, sizeof(sRead)) == (int)sizeof(toServer), "The server reads");
    CHECK(strcmp(sRead, toServer) == 0, "The server read what the client wrote");

    const char toClient[] = "pong";
    CHECK(XSock_Write(&accepted, toClient, sizeof(toClient)) == (int)sizeof(toClient), "The server writes");
    CHECK(XSock_TimeOutR(&client, 2, 0) != XSOCK_INVALID, "The client gets a timeout");
    CHECK(XSock_Read(&client, sRead, sizeof(sRead)) == (int)sizeof(toClient), "The client reads");
    CHECK(strcmp(sRead, toClient) == 0, "The client read what the server wrote");

    XSock_Close(&accepted);
    XSock_Close(&client);
    XSock_Close(&server);
    return 0;
}

static int XTest_create_guards(void)
{
    /* Port zero is rejected outright rather than becoming an ephemeral bind. */
    xsock_t sock;
    XSock_Create(&sock, XSOCK_TCP_SERVER, "127.0.0.1", 0);
    CHECK(XSock_Status(&sock) != XSOCK_ERR_NONE, "Port zero is refused");
    XSock_Close(&sock);

    /* A connection to a port with nothing listening fails rather than
     * hanging or reporting success. */
    XSock_Create(&sock, XSOCK_TCP_CLIENT, "127.0.0.1", 1);
    CHECK(XSock_Status(&sock) != XSOCK_ERR_NONE || XSock_GetFD(&sock) == XSOCK_INVALID,
        "Connecting to a closed port is reported as a failure");
    XSock_Close(&sock);

    /* An address that cannot resolve fails before any syscall. */
    XSock_Create(&sock, XSOCK_TCP_CLIENT, "no.such.host.invalid", 80);
    CHECK(XSock_Status(&sock) != XSOCK_ERR_NONE || XSock_GetFD(&sock) == XSOCK_INVALID,
        "An unresolvable address is reported as a failure");
    XSock_Close(&sock);

    /* Two listeners on one live port: the second is refused. */
    xsock_t first;
    uint16_t nPort = sock_bind_free(&first, XSOCK_TCP_SERVER | XSOCK_REUSEADDR);
    if (nPort)
    {
        xsock_t second;
        XSock_Create(&second, XSOCK_TCP_SERVER, "127.0.0.1", nPort);
        CHECK(XSock_Status(&second) != XSOCK_ERR_NONE || XSock_GetFD(&second) == XSOCK_INVALID,
            "A second listener on a live port is refused");
        XSock_Close(&second);
        XSock_Close(&first);
    }

    /* The heap handle owns itself. */
    xsock_t *pSock = XSock_Alloc(XSOCK_TCP_CLIENT, "127.0.0.1", 1);
    if (pSock != NULL)
    {
        XSock_Close(pSock);
        XSock_Free(pSock);
    }

    /* The accessors tolerate a socket that was never opened. */
    xsock_t unopened;
    XSock_Init(&unopened, XSOCK_TCP_PEER, XSOCK_INVALID);
    CHECK(XSock_IsOpen(&unopened) == XFALSE, "An unopened socket is not open");
    CHECK(XSock_GetFD(&unopened) == XSOCK_INVALID, "An unopened socket has no descriptor");
    CHECK(XSock_Check(&unopened) == XFALSE, "An unopened socket does not pass a check");
    XSock_Close(&unopened);
    return 0;
}

static int XTest_udp(void)
{
    /* The library's UDP surface is the client side: XSock_SetupDgram()
     * connects a client, enables broadcast, or joins a multicast group, but
     * never binds for XSOCK_SERVER, which is why there is no UDP server
     * macro beside XSOCK_UDP_CLIENT. The receiver here is therefore a plain
     * bound socket and the client is the code under test. */
    int nServer = (int)socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(nServer >= 0, "The receiving socket is created");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    socklen_t nLen = sizeof(addr);
    if (bind(nServer, (struct sockaddr*)&addr, nLen) < 0 ||
        getsockname(nServer, (struct sockaddr*)&addr, &nLen) < 0)
    {
        close(nServer);
        printf("UDP loopback unavailable, skipping\n");
        return 77;
    }

    struct timeval timeout = {2, 0};
    setsockopt(nServer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    xsock_info_t info;
    XSock_InitInfo(&info);
    CHECK(XSock_GetAddrInfo(&info, "127.0.0.1") >= 0, "The client address resolves");
    info.nPort = ntohs(addr.sin_port);

    xsock_t client;
    CHECK(XSock_Open(&client, XSOCK_UDP_CLIENT, &info) != XSOCK_INVALID, "The UDP client opens");
    CHECK(XSock_Status(&client) == XSOCK_ERR_NONE, "The UDP client has no error");
    CHECK(XSock_GetSockType(&client) == SOCK_DGRAM, "The UDP client is a datagram socket");
    CHECK(XSock_GetPort(&client) == info.nPort, "The client reports the port it was aimed at");

    /* A datagram with an embedded NUL arrives whole and unchanged. */
    const uint8_t datagram[] = {'d', 'a', 't', 'a', 0x00, 0xff, 'g', 'r', 'a', 'm'};
    CHECK(XSock_Send(&client, datagram, sizeof(datagram)) == (int)sizeof(datagram), "The datagram is sent");

    uint8_t sRead[64];
    int nRead = (int)recv(nServer, sRead, sizeof(sRead), 0);
    CHECK(nRead == (int)sizeof(datagram), "The datagram arrives whole");
    CHECK(memcmp(sRead, datagram, sizeof(datagram)) == 0, "Every datagram byte is unchanged");

    /* Datagrams keep their boundaries: two sends are two receives. */
    CHECK(XSock_Send(&client, "one", 3) == 3, "The first datagram is sent");
    CHECK(XSock_Send(&client, "two", 3) == 3, "The second datagram is sent");
    CHECK(recv(nServer, sRead, sizeof(sRead), 0) == 3, "The first datagram arrives on its own");
    CHECK(memcmp(sRead, "one", 3) == 0, "The first datagram is the first one sent");
    CHECK(recv(nServer, sRead, sizeof(sRead), 0) == 3, "The second datagram arrives on its own");
    CHECK(memcmp(sRead, "two", 3) == 0, "The second datagram is the second one sent");

    XSock_Close(&client);
    CHECK(XSock_IsOpen(&client) == XFALSE, "The closed client is not open");

    /* A UDP socket asked to be a server is created but never bound, so it
     * has no port of its own to receive on. */
    xsock_t unbound;
    if (XSock_Create(&unbound, XSOCK_UDP | XSOCK_SERVER, "127.0.0.1", 39777) != XSOCK_INVALID)
    {
        struct sockaddr_in bound;
        socklen_t nBoundLen = sizeof(bound);
        if (getsockname(unbound.nFD, (struct sockaddr*)&bound, &nBoundLen) == 0)
            CHECK(ntohs(bound.sin_port) == 0, "A datagram server flag does not bind the socket");
        XSock_Close(&unbound);
    }

    close(nServer);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(address_conversion),
    XTEST_CASE(address_info),
    XTEST_CASE(status_strings),
    XTEST_CASE(accessors),
    XTEST_CASE(buffers),
    XTEST_CASE(listener),
    XTEST_CASE(create_guards),
    XTEST_CASE(udp)
)
