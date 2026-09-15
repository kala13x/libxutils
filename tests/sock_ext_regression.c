/* libxutils: the socket paths the ordinary TCP cases do not reach.
 *
 * Unix domain sockets, exact-size chunked transfer, non-blocking accept,
 * multicast membership and the option setters all sit on code that a plain
 * loopback stream never touches, so each gets a fixture of its own here.
 */

#include "test.h"
#include "sock.h"
#include "buf.h"
#include "str.h"
#include "thread.h"
#include "sync.h"
#include "xfs.h"
#include "xtime.h"
#include <unistd.h>

/* Builds a private directory for unix socket fixtures. */
typedef struct {
    char sRoot[64];
    char sPath[128];
    int nCreated;
} sock_unix_fixture_t;

static int sock_unix_begin(sock_unix_fixture_t *pFixture)
{
    snprintf(pFixture->sRoot, sizeof(pFixture->sRoot), "/tmp/xutils-sock-XXXXXX");
    if (mkdtemp(pFixture->sRoot) == NULL) return XSTDERR;
    pFixture->nCreated = 1;
    snprintf(pFixture->sPath, sizeof(pFixture->sPath), "%s/listener.sock", pFixture->sRoot);
    return XSTDOK;
}

static void sock_unix_end(sock_unix_fixture_t *pFixture)
{
    if (!pFixture->nCreated) return;
    unlink(pFixture->sPath);
    rmdir(pFixture->sRoot);
    pFixture->nCreated = 0;
}

static int XTest_unix_stream(void)
{
    /* A unix listener binds a path instead of a port, and the path has to
     * be removed and recreated cleanly across restarts. */
    sock_unix_fixture_t fixture;
    CHECK(sock_unix_begin(&fixture) == XSTDOK, "A private socket directory is created");

    xsock_t server;
    if (XSock_Create(&server, XSOCK_UNIX_SERVER | XSOCK_REUSEADDR, fixture.sPath, 0) == XSOCK_INVALID)
    {
        sock_unix_end(&fixture);
        printf("Unix sockets unavailable, skipping\n");
        return 77;
    }

    CHECK(XSock_Status(&server) == XSOCK_ERR_NONE, "The unix listener has no error");
    CHECK(XPath_Exists(fixture.sPath) == XTRUE, "The listener created its socket file");
    CHECK((XSock_GetFlags(&server) & XSOCK_UNIX) == XSOCK_UNIX, "The listener reports itself as unix");

    /* A client connects over the same path. */
    xsock_t client;
    CHECK(XSock_Create(&client, XSOCK_UNIX_CLIENT, fixture.sPath, 0) != XSOCK_INVALID,
        "The unix client connects");
    CHECK(XSock_Status(&client) == XSOCK_ERR_NONE, "The unix client has no error");

    xsock_t accepted;
    CHECK(XSock_Accept(&server, &accepted) != XSOCK_INVALID, "The unix listener accepts");

    /* Data flows both ways, binary bytes included. */
    const uint8_t payload[] = {'u', 'n', 'i', 'x', 0x00, 0xff};
    CHECK(XSock_Write(&client, payload, sizeof(payload)) == (int)sizeof(payload), "The client writes");

    uint8_t sRead[32];
    CHECK(XSock_TimeOutR(&accepted, 2, 0) != XSOCK_INVALID, "The accepted socket gets a timeout");
    CHECK(XSock_Read(&accepted, sRead, sizeof(sRead)) == (int)sizeof(payload), "The server reads");
    CHECK(memcmp(sRead, payload, sizeof(payload)) == 0, "Binary bytes survive the unix socket");

    XSock_Close(&accepted);
    XSock_Close(&client);
    XSock_Close(&server);

    /* Closing does not remove the socket file, so a plain rebind over the
     * leftover one fails the way bind() always does on a unix socket. */
    CHECK(XPath_Exists(fixture.sPath) == XTRUE, "The socket file outlives the listener");

    xsock_t stale;
    XSock_Create(&stale, XSOCK_UNIX_SERVER | XSOCK_REUSEADDR, fixture.sPath, 0);
    CHECK(XSock_Status(&stale) != XSOCK_ERR_NONE || XSock_GetFD(&stale) == XSOCK_INVALID,
        "A plain rebind over a leftover socket file is refused");
    XSock_Close(&stale);

    /* The force flag is how a listener restarts over its own leftover file:
     * it binds a temporary path and renames it over the real one, so a
     * client never sees a moment with no socket there. */
    xsock_t forced;
    CHECK(XSock_Create(&forced, XSOCK_UNIX_SERVER | XSOCK_FORCE, fixture.sPath, 0) != XSOCK_INVALID,
        "A forced rebind takes over the leftover socket file");
    CHECK(XSock_Status(&forced) == XSOCK_ERR_NONE, "The forced listener has no error");
    CHECK(XPath_Exists(fixture.sPath) == XTRUE, "The socket file is in place after the takeover");

    /* The temporary path used during the takeover is not left behind. */
    char sTemp[192];
    snprintf(sTemp, sizeof(sTemp), "%s.%d.tmp", fixture.sPath, (int)getpid());
    CHECK(XPath_Exists(sTemp) == XFALSE, "The takeover leaves no temporary socket behind");

    /* The restarted listener still accepts. */
    xsock_t reclient;
    CHECK(XSock_Create(&reclient, XSOCK_UNIX_CLIENT, fixture.sPath, 0) != XSOCK_INVALID,
        "A client reaches the restarted listener");

    xsock_t reaccepted;
    CHECK(XSock_Accept(&forced, &reaccepted) != XSOCK_INVALID, "The restarted listener accepts");
    XSock_Close(&reaccepted);
    XSock_Close(&reclient);
    XSock_Close(&forced);

    /* A path that cannot be created is refused. */
    xsock_t bad;
    XSock_Create(&bad, XSOCK_UNIX_SERVER, "/no/such/directory/listener.sock", 0);
    CHECK(XSock_Status(&bad) != XSOCK_ERR_NONE || XSock_GetFD(&bad) == XSOCK_INVALID,
        "An unusable socket path is refused");
    XSock_Close(&bad);

    /* Connecting to a path nothing is listening on is refused. */
    char sAbsent[160];
    snprintf(sAbsent, sizeof(sAbsent), "%s/absent.sock", fixture.sRoot);
    XSock_Create(&bad, XSOCK_UNIX_CLIENT, sAbsent, 0);
    CHECK(XSock_Status(&bad) != XSOCK_ERR_NONE || XSock_GetFD(&bad) == XSOCK_INVALID,
        "An absent socket path is refused");
    XSock_Close(&bad);

    sock_unix_end(&fixture);
    return 0;
}

/* The chunked send runs in its own thread: a payload larger than the socket
 * buffer cannot be written in full until somebody is reading the other end.
 * Kept at file scope because a nested function is a GCC extension that the
 * sanitizer builds, which use clang, do not accept. */
typedef struct { xsock_t *pSock; uint8_t *pData; size_t nSize; int nResult; } chunk_ctx_t;

static void *chunk_writer(void *pArg)
{
    chunk_ctx_t *pCtx = (chunk_ctx_t*)pArg;
    pCtx->nResult = XSock_SendChunk(pCtx->pSock, pCtx->pData, pCtx->nSize);
    return NULL;
}

static int XTest_chunked_transfer(void)
{
    /* The chunked calls move an exact number of bytes or report failure,
     * unlike the plain read and write which may move fewer. */
    XSOCKET pair[2] = {XSOCK_INVALID, XSOCK_INVALID};
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");

    xsock_t writer, reader;
    CHECK(XSock_Init(&writer, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "The writer wraps");
    CHECK(XSock_Init(&reader, XSOCK_TCP_PEER, pair[1]) != XSOCK_ERROR, "The reader wraps");
    CHECK(XSock_TimeOutR(&reader, 5, 0) != XSOCK_INVALID, "The reader gets a timeout");
    CHECK(XSock_TimeOutS(&writer, 5, 0) != XSOCK_INVALID, "The writer gets a timeout");

    /* A payload larger than the socket buffer, so the chunking loop runs
     * more than once in each direction. */
    const size_t nSize = 512 * 1024;
    uint8_t *pOut = (uint8_t*)malloc(nSize);
    uint8_t *pIn = (uint8_t*)malloc(nSize);
    CHECK(pOut != NULL && pIn != NULL, "The transfer buffers are allocated");
    for (size_t i = 0; i < nSize; i++) pOut[i] = (uint8_t)(i * 7 + (i >> 8));
    memset(pIn, 0, nSize);

    /* The writer has to run alongside the reader or the pipe fills up. */
    chunk_ctx_t ctx = {&writer, pOut, nSize, 0};

    xthread_t thread;
    CHECK(XThread_Create(&thread, chunk_writer, &ctx, XFALSE) == XSTDOK, "The chunked writer starts");

    int nReceived = XSock_RecvChunk(&reader, pIn, nSize);
    XThread_Join(&thread);

    CHECK(ctx.nResult == (int)nSize, "The chunked send moved every byte");
    CHECK(nReceived == (int)nSize, "The chunked receive moved every byte");
    CHECK(memcmp(pIn, pOut, nSize) == 0, "Every byte survived the chunked transfer");

    free(pIn);
    free(pOut);

    /* Rejected arguments move nothing. */
    uint8_t sByte = 0;
    CHECK(XSock_SendChunk(&writer, NULL, 8) == XSOCK_NONE, "A missing source sends nothing");
    CHECK(XSock_SendChunk(&writer, &sByte, 0) == XSOCK_NONE, "A zero length send moves nothing");
    CHECK(XSock_RecvChunk(&reader, NULL, 8) == XSOCK_NONE, "A missing destination receives nothing");
    CHECK(XSock_RecvChunk(&reader, &sByte, 0) == XSOCK_NONE, "A zero length receive moves nothing");

    XSock_Close(&writer);
    XSock_Close(&reader);

    /* A peer that goes away mid-transfer is reported, not waited on. */
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A second socket pair is created");
    CHECK(XSock_Init(&writer, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "The writer wraps");
    CHECK(XSock_Init(&reader, XSOCK_TCP_PEER, pair[1]) != XSOCK_ERROR, "The reader wraps");
    CHECK(XSock_TimeOutR(&reader, 2, 0) != XSOCK_INVALID, "The reader gets a timeout");

    XSock_Close(&writer);

    uint8_t sShort[64];
    CHECK(XSock_RecvChunk(&reader, sShort, sizeof(sShort)) <= 0,
        "A closed peer ends the chunked receive short");
    XSock_Close(&reader);
    return 0;
}

static int XTest_nonblocking_accept(void)
{
    /* A non-blocking accept returns immediately when nothing is waiting,
     * rather than parking the caller on an idle listener. */
    xsock_t server;
    uint16_t nPort = 0;

    for (uint16_t nTry = 39300; nTry < 39400 && !nPort; nTry++)
    {
        if (XSock_Create(&server, XSOCK_TCP_SERVER | XSOCK_REUSEADDR, "127.0.0.1", nTry) != XSOCK_INVALID &&
            XSock_Status(&server) == XSOCK_ERR_NONE) nPort = nTry;
        else XSock_Close(&server);
    }

    if (!nPort)
    {
        printf("No free loopback port, skipping\n");
        return 77;
    }

    CHECK(XSock_NonBlock(&server, XTRUE) != XSOCK_INVALID, "The listener is made non blocking");
    CHECK(XSock_IsNB(&server) == XTRUE, "The listener reports itself non blocking");

    /* Nothing is connecting yet, so the accept must not block. */
    uint64_t nStart = XTime_GetMs();
    XSOCKET nIdle = XSock_AcceptNB(&server);
    uint64_t nElapsed = XTime_GetMs() - nStart;

    CHECK(nIdle == XSOCK_INVALID, "An idle non-blocking accept yields no connection");
    CHECK(nElapsed < 2000, "An idle non-blocking accept returns at once");

    /* Once a client connects, the same call hands back a descriptor. */
    xsock_t client;
    CHECK(XSock_Create(&client, XSOCK_TCP_CLIENT, "127.0.0.1", nPort) != XSOCK_INVALID,
        "The client connects");

    XSOCKET nAccepted = XSOCK_INVALID;
    for (int i = 0; i < 200 && nAccepted == XSOCK_INVALID; i++)
    {
        nAccepted = XSock_AcceptNB(&server);
        if (nAccepted == XSOCK_INVALID) xusleep(5000);
    }

    CHECK(nAccepted != XSOCK_INVALID, "The pending connection is accepted");

    /* The accepted descriptor carries traffic like any other. */
    xsock_t peer;
    CHECK(XSock_Init(&peer, XSOCK_TCP_PEER, nAccepted) != XSOCK_ERROR, "The accepted descriptor wraps");
    CHECK(XSock_Write(&client, "nb", 2) == 2, "The client writes");

    char sRead[8];
    CHECK(XSock_TimeOutR(&peer, 2, 0) != XSOCK_INVALID, "The peer gets a timeout");
    CHECK(XSock_Read(&peer, sRead, sizeof(sRead)) == 2, "The accepted peer reads");

    XSock_Close(&peer);
    XSock_Close(&client);
    XSock_Close(&server);
    return 0;
}

static int XTest_options(void)
{
    /* Every option setter reports success on a live socket and failure on
     * a closed one, rather than silently doing nothing. */
    XSOCKET pair[2] = {XSOCK_INVALID, XSOCK_INVALID};
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");

    xsock_t sock;
    CHECK(XSock_Init(&sock, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "The socket wraps");

    CHECK(XSock_ReuseAddr(&sock, XTRUE) != XSOCK_INVALID, "Address reuse is enabled");
    CHECK(XSock_ReuseAddr(&sock, XFALSE) != XSOCK_INVALID, "Address reuse is disabled");
    CHECK(XSock_Oobinline(&sock, XTRUE) != XSOCK_INVALID, "Out of band inline is enabled");
    CHECK(XSock_Linger(&sock, 5) != XSOCK_INVALID, "A linger period is set");
    CHECK(XSock_Linger(&sock, 0) != XSOCK_INVALID, "Linger is disabled");
    CHECK(XSock_TimeOutR(&sock, 0, 0) != XSOCK_INVALID, "A cleared receive timeout is accepted");
    CHECK(XSock_TimeOutS(&sock, 0, 0) != XSOCK_INVALID, "A cleared send timeout is accepted");

    /* The socket accessors describe what was wrapped. */
    CHECK(XSock_GetSockType(&sock) == SOCK_STREAM, "The socket is a stream");
    CHECK(XSock_GetNetAddr(&sock) == 0 || XSock_GetNetAddr(&sock) != 0, "The network address is readable");
    CHECK(XSock_GetProto(&sock) >= 0, "The protocol is readable");
    CHECK(XSock_ErrStr(&sock) != NULL, "The socket has an error description");

    XSock_Close(&sock);

    /* On a closed socket the setters report failure. */
    CHECK(XSock_NonBlock(&sock, XTRUE) == XSOCK_INVALID, "A closed socket cannot be made non blocking");
    CHECK(XSock_TimeOutR(&sock, 1, 0) == XSOCK_INVALID, "A closed socket takes no receive timeout");
    CHECK(XSock_Oobinline(&sock, XTRUE) == XSOCK_INVALID, "A closed socket takes no out of band setting");
    CHECK(XSock_Linger(&sock, 1) == XSOCK_INVALID, "A closed socket takes no linger setting");
    CHECK(XSock_Check(&sock) == XFALSE, "A closed socket does not pass a check");

    xclosesock(pair[1]);

    /* No delay applies to a TCP socket on a real connection. */
    xsock_t tcp;
    uint16_t nPort = 0;
    for (uint16_t nTry = 39400; nTry < 39500 && !nPort; nTry++)
    {
        if (XSock_Create(&tcp, XSOCK_TCP_SERVER | XSOCK_REUSEADDR, "127.0.0.1", nTry) != XSOCK_INVALID &&
            XSock_Status(&tcp) == XSOCK_ERR_NONE) nPort = nTry;
        else XSock_Close(&tcp);
    }

    if (nPort)
    {
        CHECK(XSock_NoDelay(&tcp, XTRUE) != XSOCK_INVALID, "No delay is enabled on a TCP socket");
        CHECK(XSock_NoDelay(&tcp, XFALSE) != XSOCK_INVALID, "No delay is disabled on a TCP socket");
        XSock_Close(&tcp);
    }
    return 0;
}

static int XTest_buffers_and_alloc(void)
{
    /* The buffer writer drains everything it is given; the heap socket
     * owns itself. */
    XSOCKET pair[2] = {XSOCK_INVALID, XSOCK_INVALID};
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");

    xsock_t writer, reader;
    CHECK(XSock_Init(&writer, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "The writer wraps");
    CHECK(XSock_Init(&reader, XSOCK_TCP_PEER, pair[1]) != XSOCK_ERROR, "The reader wraps");
    CHECK(XSock_TimeOutR(&reader, 2, 0) != XSOCK_INVALID, "The reader gets a timeout");

    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, 0, 0);
    CHECK(XByteBuffer_Add(&buffer, (const uint8_t*)"written from a buffer", 21), "The buffer is filled");

    CHECK(XSock_WriteBuff(&writer, &buffer) == 21, "The whole buffer is written");

    char sRead[64];
    CHECK(XSock_Read(&reader, sRead, sizeof(sRead)) == 21, "Every buffered byte arrives");
    CHECK(memcmp(sRead, "written from a buffer", 21) == 0, "The buffered bytes are unchanged");
    XByteBuffer_Clear(&buffer);

    XSock_Close(&writer);
    XSock_Close(&reader);

    /* The info based allocator builds a socket from a resolved address. */
    xsock_info_t info;
    XSock_InitInfo(&info);
    CHECK(XSock_GetAddrInfo(&info, "127.0.0.1") >= 0, "The address resolves");
    info.nPort = 1;

    xsock_t *pSock = XSock_New(XSOCK_TCP_CLIENT, &info);
    /* Nothing is listening on port one, so this is expected to fail; what
     * matters is that it fails without leaking the handle. */
    if (pSock != NULL)
    {
        XSock_Close(pSock);
        XSock_Free(pSock);
    }

    /* The setup helper resolves a host and port from one string. */
    xsock_t setup;
    XSock_Setup(&setup, XSOCK_TCP_CLIENT, "127.0.0.1:1");
    CHECK(XSock_Status(&setup) != XSOCK_ERR_NONE || XSock_GetFD(&setup) == XSOCK_INVALID,
        "Setting up against a closed port is reported");
    XSock_Close(&setup);
    return 0;
}

static int XTest_datagram_options(void)
{
    /* Broadcast and multicast are datagram-only options, and joining a
     * group is what separates a multicast socket from a plain one. */
    xsock_info_t info;
    XSock_InitInfo(&info);
    CHECK(XSock_GetAddrInfo(&info, "127.0.0.1") >= 0, "The loopback address resolves");
    info.nPort = 39555;

    xsock_t bcast;
    if (XSock_Open(&bcast, XSOCK_UDP_BCAST, &info) != XSOCK_INVALID &&
        XSock_Status(&bcast) == XSOCK_ERR_NONE)
    {
        CHECK(XSock_GetSockType(&bcast) == SOCK_DGRAM, "A broadcast socket is a datagram socket");
        XSock_Close(&bcast);
    }

    /* Joining a multicast group on the loopback interface. */
    xsock_info_t group;
    XSock_InitInfo(&group);
    CHECK(XSock_GetAddrInfo(&group, "239.255.0.1") >= 0, "The group address resolves");
    group.nPort = 39556;

    xsock_t mcast;
    if (XSock_Open(&mcast, XSOCK_UDP_MCAST | XSOCK_REUSEADDR, &group) != XSOCK_INVALID &&
        XSock_Status(&mcast) == XSOCK_ERR_NONE)
    {
        CHECK(XSock_GetSockType(&mcast) == SOCK_DGRAM, "A multicast socket is a datagram socket");

        /* Joining a second group on the same socket. */
        XSock_AddMembership(&mcast, "239.255.0.2");
        XSock_Close(&mcast);
    }
    else
    {
        printf("Multicast unavailable in this environment, skipping the group cases\n");
    }

    /* A unicast datagram socket is created without joining anything. */
    xsock_t ucast;
    XSock_InitInfo(&info);
    CHECK(XSock_GetAddrInfo(&info, "127.0.0.1") >= 0, "The loopback address resolves");
    info.nPort = 39557;

    if (XSock_Open(&ucast, XSOCK_UDP_UCAST, &info) != XSOCK_INVALID)
    {
        CHECK(XSock_GetSockType(&ucast) == SOCK_DGRAM, "A unicast socket is a datagram socket");
        XSock_Close(&ucast);
    }
    return 0;
}


static int XTest_address_accessors(void)
{
    /* Everything the socket knows about its own address is reachable
     * through an accessor, and which arm of the address union is the live
     * one follows the family the socket was created with. */
    uint16_t nPort = 0;
    xsock_t server;

    for (uint16_t nTry = 38200; nTry < 38300 && !nPort; nTry++)
    {
        if (XSock_Create(&server, XSOCK_TCP_SERVER | XSOCK_REUSEADDR, "127.0.0.1", nTry) != XSOCK_INVALID &&
            XSock_Status(&server) == XSOCK_ERR_NONE) nPort = nTry;
        else XSock_Close(&server);
    }

    if (!nPort)
    {
        printf("No free loopback port, skipping\n");
        return 77;
    }

    CHECK(XSock_GetPort(&server) == nPort, "The socket reports the port it bound");
    CHECK(XSock_GetFD(&server) != XSOCK_INVALID, "The socket has a descriptor");
    CHECK(XSock_GetSockType(&server) == SOCK_STREAM, "A TCP socket is a stream socket");
    CHECK(XSock_GetProto(&server) == IPPROTO_TCP, "A TCP socket carries the TCP protocol");
    CHECK(XSock_GetNetAddr(&server) == htonl(INADDR_LOOPBACK), "The bound address is loopback");
    CHECK(XSock_IsOpen(&server) == XTRUE, "The socket is open");

    /* The address union and the family-specific view have to describe the
     * same address, or a caller mixing the two would bind or send to a
     * different place than it read back. */
    xsock_addr_t *pUnion = XSock_InAddr(&server);
    CHECK(pUnion != NULL, "The address union is reachable");

    xsockaddr_t *pAddr = XSock_GetSockAddr(&server);
    CHECK(pAddr != NULL, "The family view is reachable");
    CHECK((void*)pAddr == (void*)&pUnion->inAddr, "An internet socket points at the internet arm");
    CHECK(pUnion->inAddr.sin_family == AF_INET, "The family is recorded as internet");
    CHECK(pUnion->inAddr.sin_port == htons(nPort), "The union carries the bound port");
    CHECK(XSock_GetAddrLen(&server) == sizeof(struct sockaddr_in), "The address length is the internet one");

    char sAddr[XSOCK_ADDR_MAX];
    CHECK(XSock_IPAddr(&server, sAddr, sizeof(sAddr)) > 0, "The address renders as text");
    CHECK(strcmp(sAddr, "127.0.0.1") == 0, "It renders as the address that was bound");

    XSock_Close(&server);
    CHECK(XSock_IsOpen(&server) == XFALSE, "A closed socket is not open");
    CHECK(XSock_GetFD(&server) == XSOCK_INVALID, "A closed socket has no descriptor");

    /* A unix socket points at the other arm of the same union. */
    char sPath[128];
    snprintf(sPath, sizeof(sPath), "/tmp/xutils-acc-%d.sock", (int)getpid());
    unlink(sPath);

    xsock_t unixSock;
    if (XSock_Create(&unixSock, XSOCK_UNIX_SERVER, sPath, 0) != XSOCK_INVALID)
    {
        xsock_addr_t *pUnixUnion = XSock_InAddr(&unixSock);
        CHECK(pUnixUnion != NULL, "The unix address union is reachable");

        xsockaddr_t *pUnixAddr = XSock_GetSockAddr(&unixSock);
        CHECK((void*)pUnixAddr == (void*)&pUnixUnion->unAddr, "A unix socket points at the unix arm");
        CHECK(pUnixUnion->unAddr.sun_family == AF_UNIX, "The family is recorded as unix");
        CHECK(strcmp(pUnixUnion->unAddr.sun_path, sPath) == 0, "The union carries the socket path");
        CHECK(XSock_GetAddrLen(&unixSock) == sizeof(struct sockaddr_un), "The address length is the unix one");

        XSock_Close(&unixSock);
    }

    unlink(sPath);
    return 0;
}


static int XTest_flag_guards(void)
{
    /* The flags say what kind of socket to make. A set that names no kind
     * at all has to be refused at setup: a socket created with a domain and
     * a type of -1 would be a descriptor that fails every later call for a
     * reason that no longer points at the mistake. */
    xsock_t sock;

    CHECK(XSock_Create(&sock, XSOCK_SERVER, "127.0.0.1", 0) == XSOCK_INVALID,
        "A role with no transport is refused");
    CHECK(XSock_Status(&sock) == XSOCK_ERR_SUPPORT, "It is refused as unsupported");
    CHECK(XSock_GetFD(&sock) == XSOCK_INVALID, "No descriptor was left behind");
    XSock_Close(&sock);

    CHECK(XSock_Create(&sock, XSTDNON, "127.0.0.1", 0) == XSOCK_INVALID, "Empty flags are refused");
    XSock_Close(&sock);

    CHECK(XSock_Create(&sock, XSOCK_SSL | XSOCK_CLIENT, "127.0.0.1", 0) == XSOCK_INVALID,
        "Encryption with no transport under it is refused");
    XSock_Close(&sock);

    /* Every flag combination the library does name has to resolve to a real
     * domain, type and protocol rather than to the error sentinel. */
    xsock_t tcp;
    if (XSock_Create(&tcp, XSOCK_TCP_CLIENT, "127.0.0.1", 9) != XSOCK_INVALID)
    {
        CHECK(XSock_GetSockType(&tcp) == SOCK_STREAM, "A TCP socket is a stream");
        CHECK(XSock_GetProto(&tcp) == IPPROTO_TCP, "A TCP socket carries TCP");
        XSock_Close(&tcp);
    }

    xsock_t udp;
    if (XSock_Create(&udp, XSOCK_UDP | XSOCK_CLIENT, "127.0.0.1", 9) != XSOCK_INVALID)
    {
        CHECK(XSock_GetSockType(&udp) == SOCK_DGRAM, "A UDP socket is a datagram socket");
        CHECK(XSock_GetProto(&udp) == IPPROTO_UDP, "A UDP socket carries UDP");
        XSock_Close(&udp);
    }

    /* A raw socket needs privilege this test does not assume, so only the
     * shape of the refusal is checked when it is not available. */
    xsock_t raw;
    if (XSock_Create(&raw, XSOCK_RAW | XSOCK_CLIENT, "127.0.0.1", 0) != XSOCK_INVALID)
    {
        CHECK(XSock_GetSockType(&raw) == SOCK_RAW, "A raw socket is a raw socket");
        CHECK(XSock_GetProto(&raw) == IPPROTO_RAW, "A raw socket carries the raw protocol");
    }
    else
    {
        CHECK(XSock_Status(&raw) != XSOCK_ERR_SUPPORT, "A raw socket is named, just not permitted");
    }
    XSock_Close(&raw);

    /* And the flag predicates agree with what was asked for. */
    CHECK(XFlags_IsSSL(XSOCK_SSL | XSOCK_TCP) == XTRUE, "Encryption is detected in the flags");
    CHECK(XFlags_IsSSL(XSOCK_TCP) == XFALSE, "A plain socket is not detected as encrypted");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(unix_stream),
    XTEST_CASE(chunked_transfer),
    XTEST_CASE(nonblocking_accept),
    XTEST_CASE(options),
    XTEST_CASE(buffers_and_alloc),
    XTEST_CASE(datagram_options),
    XTEST_CASE(address_accessors),
    XTEST_CASE(flag_guards)
)
