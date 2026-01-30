/*
 *  examples/basic-server.c
 *
 *  Copyleft (C) 2015  Sun Dro (a.k.a. kala13x)
 *
 * Simplest echo server based on XSocket.
 */

#include "xstd.h"
#include "sock.h"
#include "log.h"
#include "xtime.h"

int main(int argc, char *argv[])
{
    xlog_defaults();
    char buf[1024];

    if (argc <= 2)
    {
        xlog("Usage: %s [address] [port]\n", argv[0]);
        xlog("Example: %s 127.0.0.1 6969\n", argv[0]);
        return 1;
    }

    xsock_t sock;
    if (XSock_Create(&sock, XSOCK_TCP_SERVER, argv[1], atoi(argv[2])) == XSOCK_INVALID)
    {
        XLog_Error("%s", XSock_ErrStr(&sock));
        return 1;
    }

    xlogi("Socket started listen to port: %d", atoi(argv[2]));
    while (sock.nFD != XSOCK_INVALID)
    {
        xsock_t sock2;
        buf[0] = '\0';

        if (XSock_Accept(&sock, &sock2) == XSOCK_INVALID)
        {
            XLog_Error("%s", XSock_ErrStr(&sock));
            continue;
        }

        int nRead = XSock_Read(&sock2, buf, sizeof(buf));
        if (nRead > 0)
        {
            buf[nRead] = '\0';
            xlogi("Recv: %s", buf);

            int nStatus = XSock_Send(&sock2, buf, nRead);
            if (nStatus >= 0) xlogi("Sent: %s", buf);
        }

        XSock_Close(&sock2);
    }

    XSock_Close(&sock);
    return 0;
}
