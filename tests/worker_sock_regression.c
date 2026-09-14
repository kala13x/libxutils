/* libxutils: inherited listeners and nonblocking worker transports. */
#include "test.h"
#include "sock.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

static int XTest_listener_inheritance(void)
{
    int nFD = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(nFD >= 0, "Create listener");
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(nFD, (struct sockaddr*)&addr, sizeof(addr)) == 0 && listen(nFD, 4) == 0, "Bind ephemeral listener");
    socklen_t nLength = sizeof(addr);
    CHECK(getsockname(nFD, (struct sockaddr*)&addr, &nLength) == 0, "Find assigned port");
    xsock_t listener, inherited;
    CHECK(XSock_Init(&listener, XSOCK_TCP_SERVER, nFD) != XSOCK_ERROR, "Wrap listener");
    CHECK(XSock_NonBlock(&listener, XTRUE) != XSOCK_INVALID, "Nonblocking listener");
    int nDuplicate = dup(nFD);
    CHECK(nDuplicate >= 0, "Duplicate the shared listener as fork does");
    CHECK(XSock_Init(&inherited, XSOCK_TCP_SERVER, nDuplicate) != XSOCK_ERROR, "Wrap inherited descriptor");
    XSock_Close(&inherited);
    int nClient = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(nClient >= 0 && connect(nClient, (struct sockaddr*)&addr, sizeof(addr)) == 0,
        "Closing one worker's descriptor must not shut down the shared listener");
    xsock_t accepted;
    CHECK(XSock_Accept(&listener, &accepted) != XSOCK_INVALID, "The remaining worker can still accept");
    CHECK((fcntl(accepted.nFD, F_GETFL) & O_NONBLOCK) != 0, "Accepted socket inherits nonblocking behavior before TLS");
    CHECK(send(nClient, "ready", 5, 0) == 5, "Send to the surviving listener");
    char data[5];
    CHECK(XSock_Read(&accepted, data, sizeof(data)) == 5 && memcmp(data, "ready", 5) == 0, "Accepted connection works");
    close(nClient);
    XSock_Close(&accepted);
    XSock_Close(&listener);
    return 0;
}

#if defined(__linux__)
static int XTest_unix_backlog(void)
{
    char sDirectory[] = "/tmp/xutils-backlog.XXXXXX";
    CHECK(mkdtemp(sDirectory) != NULL, "Create private socket directory");
    char sPath[108];
    snprintf(sPath, sizeof(sPath), "%s/listener", sDirectory);
    xsock_t listener;
    CHECK(XSock_CreateAdv(&listener, XSOCK_UNIX_SERVER | XSOCK_NB, 1, sPath, 0, NULL) != XSOCK_INVALID,
        "Create a listener with a one-connection backlog");
    int clients[32], nCount = 0;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, sPath, strlen(sPath) + 1);
    for (; nCount < 32; nCount++)
    {
        clients[nCount] = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
        CHECK(clients[nCount] >= 0, "Create queued connection");
        if (connect(clients[nCount], (struct sockaddr*)&addr, sizeof(addr)) < 0)
        {
            CHECK(errno == EAGAIN, "Fill the Unix accept backlog");
            close(clients[nCount]);
            break;
        }
    }
    CHECK(nCount > 0 && nCount < 32, "Backlog must actually be saturated");
    pid_t nChild = fork();
    CHECK(nChild >= 0, "Isolate a potentially blocking connect");
    if (nChild == 0)
    {
        alarm(2);
        xsock_t connector;
        int nResult = XSock_Create(&connector, XSOCK_UNIX_CLIENT | XSOCK_NB, sPath, 0);
        XSock_Close(&connector);
        _exit(nResult == XSOCK_INVALID ? 0 : 1);
    }
    int nStatus = 0;
    CHECK(waitpid(nChild, &nStatus, 0) == nChild, "Reap connector");
    for (int i = 0; i < nCount; i++) close(clients[i]);
    XSock_Close(&listener);
    unlink(sPath);
    rmdir(sDirectory);
    CHECK(WIFEXITED(nStatus) && WEXITSTATUS(nStatus) == 0, "A full accept queue must fail without blocking the worker");
    return 0;
}
XTEST_MAIN(XTEST_CASE(listener_inheritance), XTEST_CASE(unix_backlog))
#else
XTEST_MAIN(XTEST_CASE(listener_inheritance))
#endif
