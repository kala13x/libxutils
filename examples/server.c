/*
 *  examples/server.c
 * 
 *  Copyleft (C) 2015  Sun Dro (a.k.a. kala13x)
 *
 * Simplest echo server based on XSocket.
 */

#include <xutils/xstd.h>
#include <xutils/sock.h>
#include <xutils/mdtp.h>
#include <xutils/xcpu.h>
#include <xutils/xlog.h>
#include <xutils/xtime.h>
#include <xutils/md5.h>
#include <xutils/crypt.h>

void packet_callback(xpacket_t *pPacket, uint8_t nType)
{
    if (nType == XPACKET_CB_UPDATE)
    {
        xjson_obj_t *pPayloadObj = XJSON_GetOrCreateObject(pPacket->pHeaderObj, "payload", 1);
        if (pPayloadObj != NULL)
        {
            XJSON_AddBool(pPayloadObj, "crypted", 1);
            XJSON_AddString(pPayloadObj, "payloadType", "text/plain");
        }

        xjson_obj_t *pInfoObj = XJSON_GetOrCreateObject(pPacket->pHeaderObj, "cmd", 1);
        if (pInfoObj != NULL)
        {
            XJSON_AddString(pInfoObj, "cmdType", "shell");
            XJSON_AddString(pInfoObj, "command", "systemctl");

            xjson_obj_t *pArrObj = XJSON_GetOrCreateArray(pInfoObj, "arguments", 1);
            if (pArrObj != NULL)
            {
                XJSON_AddString(pArrObj, NULL, "status");
                XJSON_AddString(pArrObj, NULL, "sshd");
            }
        }
    }
}

int main(int argc, char *argv[])
{
    int cpus[2] = { 0, 1 }; /* Run only on the first two CPU */
    XCPU_SetAffinity(cpus, 2, XCPU_CALLER_PID);

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
            xlogi("Received: %s", buf);

            const char *pTestString = "test_password";
            size_t nLength = strlen(pTestString);

            char *pCrypted = XMD5_EncryptHex((const uint8_t*)pTestString, nLength);
            if (pCrypted == NULL)
            {
                xloge("Failed to encrypt payload");
                XSock_Close(&sock2);
                break;
            }

            xpacket_t packet;
            XPacket_Init(&packet, (uint8_t*)pCrypted, XMD5_LENGTH);
            packet.header.eType = XPACKET_TYPE_MULTY;
            packet.header.nTimeStamp = XTime_GetUsec();
            packet.header.nSessionID = rand();
            packet.header.nPacketID = rand();
            packet.callback = packet_callback;
            XPacket_Assemble(&packet);

            XSock_Send(&sock2, packet.rawData.pData, packet.rawData.nUsed);
            XPacket_Clear(&packet);
            free(pCrypted);
        }
 
        XSock_Close(&sock2);
    }

    XSock_Close(&sock);
    return 0;
}