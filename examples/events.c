/*!
 *  @file libxutils/examples/events.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of high performance event based non-blocking HTTP server.
 * The xUtils library will use poll() or epoll() depending on the operating system.
 */

#include <xutils/xstd.h>
#include <xutils/event.h>
#include <xutils/errex.h>
#include <xutils/http.h>
#include <xutils/sock.h>
#include <xutils/xbuf.h>
#include <xutils/xlog.h>
#include <xutils/xver.h>

static int g_nInterrupted = 0;

void signal_callback(int sig)
{
    if (sig == 2) printf("\n");
    g_nInterrupted = 1;
}

void clear_event(xevent_data_t *pEvData)
{
    if (pEvData != NULL)
    {
        if (pEvData->pContext != NULL)
        {
            XHTTP_Clear((xhttp_t*)pEvData->pContext);
            pEvData->pContext = NULL;
        }

        if (pEvData->nFD >= 0 && pEvData->bIsOpen)
        {
            shutdown(pEvData->nFD, XSHUT_RDWR);
            xclosesock(pEvData->nFD);
            pEvData->nFD = XSOCK_INVALID;
            pEvData->bIsOpen = XFALSE;
        }
    }
}

int handle_request(xevent_data_t *pEvData)
{
    xhttp_t *pHandle = (xhttp_t*)pEvData->pContext;
    xlogn("Received request: fd(%d), buff(%zu)", (int)pEvData->nFD, pHandle->dataRaw.nUsed);

    pHandle->dataRaw.pData[pHandle->nHeaderLength - 1] = XSTR_NUL;
    xlogi("Request header:\n\n%s", (char*)pHandle->dataRaw.pData);

    XHTTP_Reset(pHandle, XFALSE);
    pHandle->eType = XHTTP_RESPONSE;
    pHandle->nStatusCode = 200;

    if (XHTTP_AddHeader(pHandle, "Server", "xutils/%s", XUtils_VersionShort()) < 0 ||
        XHTTP_AddHeader(pHandle, "Content-Type", "text/plain") < 0)
    {
        xloge("Failed to initialize HTTP response: %s", strerror(errno));
        pEvData->pContext = NULL;
        return XEVENTS_DISCONNECT;
    }

    char sBody[XSTR_TINY];
    size_t nLen = xstrncpy(sBody, sizeof(sBody), "Here is your response.");

    if (XHTTP_Assemble(pHandle, (const uint8_t*)sBody, nLen) == NULL)
    {
        xloge("Failed to assemble HTTP response: %s", strerror(errno));
        pEvData->pContext = NULL;
        return XEVENTS_DISCONNECT;
    }

    xlogn("Sending response: fd(%d), buff(%zu)",
            (int)pEvData->nFD, pHandle->dataRaw.nUsed);

    char cSave = pHandle->dataRaw.pData[pHandle->nHeaderLength - 1];
    pHandle->dataRaw.pData[pHandle->nHeaderLength - 1] = XSTR_NUL;
    xlogi("Response header:\n\n%s", (char*)pHandle->dataRaw.pData);
    pHandle->dataRaw.pData[pHandle->nHeaderLength - 1] = cSave;

    pEvData->pContext = pHandle;
    return XEVENTS_CONTINUE;
}

int read_event(xevents_t *pEvents, xevent_data_t *pEvData)
{
    if (pEvents == NULL || pEvData == NULL) return XEVENTS_DISCONNECT;
    xsock_t *pListener = (xsock_t*)pEvents->pUserSpace;

    if (pListener->nFD == pEvData->nFD)
    {
        xsock_t newSock;

        if (XSock_Accept(pListener, &newSock) == XSOCK_INVALID ||
            XSock_NonBlock(&newSock, XTRUE)  == XSOCK_INVALID)
        {
            xlogw("%s", XSock_ErrStr(&newSock));
            return XEVENTS_CONTINUE;
        }

        xhttp_t *pRequest = XHTTP_Alloc(XHTTP_DUMMY, XSTDNON);
        if (pRequest == NULL)
        {
            xloge("Can not allocate memory for HTTP request: %d", errno);
            XSock_Close(&newSock);
            return XEVENTS_CONTINUE;
        }

        if (XEvents_RegisterEvent(pEvents, pRequest, newSock.nFD, XPOLLIN, 0)  == NULL)
        {
            xloge("Failed to register event for FD: %d (%s)", newSock.nFD, strerror(errno));
            XSock_Close(&newSock);
            XHTTP_Clear(pRequest);
            return XEVENTS_CONTINUE;
        }

        xlogn("Accepted connection: fd(%d)", (int)newSock.nFD);
        return XEVENTS_ACCEPT;
    }
    else
    {
        xsock_t clientSock;
        XSock_Init(&clientSock, XSOCK_TCP_PEER, pEvData->nFD, XTRUE);

        xhttp_t *pHandle = (xhttp_t*)pEvData->pContext;
        xhttp_status_t eStatus = XHTTP_Receive(pHandle, &clientSock);

        if (eStatus == XHTTP_COMPLETE)
        {
            int nStatus = handle_request(pEvData);
            if (nStatus != XEVENTS_CONTINUE) return nStatus;

            xevent_status_t eStatus = XEvents_Modify(pEvents, pEvData, XPOLLOUT);
            if (eStatus != XEVENT_STATUS_SUCCESS)
            {
                xloge("%s: %s", XEvents_Status(eStatus), strerror(errno));
                return XEVENTS_DISCONNECT;
            }

            return XEVENTS_CONTINUE;
        }
        else if (eStatus == XHTTP_ERRREAD)
        {
            const char *pError = XSock_ErrStr(&clientSock);

            if (clientSock.eStatus == XSOCK_EOF)
                xlogn("%s (%d)", pError, pEvData->nFD);
            else if (clientSock.eStatus != XSOCK_ERR_NONE)
                xloge("%s (%s)", pError, strerror(errno));

            pEvData->bIsOpen = XFALSE;
            return XEVENTS_DISCONNECT;
        }
        else if (eStatus != XHTTP_PARSED &&
                 eStatus != XHTTP_INCOMPLETE)
        {
            xloge("%s", XHTTP_GetStatusStr(eStatus));
            return XEVENTS_DISCONNECT;
        }

        xlogd("RX complete: fd(%d), buff(%zu)",
            (int)pEvData->nFD, pHandle->dataRaw.nUsed);
    }

    return XEVENTS_CONTINUE;
}

int write_event(xevents_t *pEvents, xevent_data_t *pEvData)
{
    if (pEvents == NULL || pEvData == NULL) return XEVENTS_DISCONNECT;
    xhttp_t *pResponse = (xhttp_t*)pEvData->pContext;

    xbyte_buffer_t *pBuffer = &pResponse->dataRaw;
    if (!pBuffer->nUsed) return XEVENTS_DISCONNECT;

    xsock_t socket;
    XSock_Init(&socket, XSOCK_TCP_PEER, pEvData->nFD, XTRUE);

    int nSent = XSock_Write(&socket, pBuffer->pData, pBuffer->nUsed);
    if (nSent <= 0)
    {
        xloge("%s (%s)", XSock_ErrStr(&socket), strerror(errno));
        pEvData->bIsOpen = XFALSE;
        return XEVENTS_DISCONNECT;
    }

    int nDataLeft = XByteBuffer_Advance(pBuffer, nSent);
    xlogd("TX complete: fd(%d), len(%d), left(%d)",
        (int)pEvData->nFD, nSent, nDataLeft);

    return nDataLeft ? XEVENTS_CONTINUE : XEVENTS_DISCONNECT;
}

int event_callback(void *events, void* data, XSOCKET fd, int reason)
{
    xevent_data_t *pData = (xevent_data_t*)data;
    xevents_t *pEvents = (xevents_t*)events;

    switch(reason)
    {
        case XEVENT_INTERRUPT:
            xlogi("Interrupted by signal");
            if (g_nInterrupted) return XEVENTS_BREAK;
            break;
        case XEVENT_CLEAR:
            xlogn("Closing connection: fd(%d)", (int)fd);
            clear_event(pData);
            break;
        case XEVENT_READ:
            xlogd("RX callback: fd(%d)", (int)fd);
            return read_event(pEvents, pData);
        case XEVENT_WRITE:
            xlogd("TX callback: fd(%d)", (int)fd);
            return write_event(pEvents, pData);
        case XEVENT_HUNGED:
            xlogw("Connection hunged: fd(%d)", (int)fd);
            return XEVENTS_DISCONNECT;
        case XEVENT_CLOSED:
            xlogn("Connection closed: fd(%d)", (int)fd);
            return XEVENTS_DISCONNECT;
        case XEVENT_DESTROY:
            xlogi("Service destroyed");
            break;
        default:
            break;
    }

    return XEVENTS_CONTINUE;
}

int main(int argc, char* argv[])
{
    xlog_defaults();
    xlog_timing(XLOG_TIME);
    xlog_setfl(XLOG_ALL);
    xlog_indent(XTRUE);

    /* Register interrupt/termination signals */
    int nSignals[2];
    nSignals[0] = SIGTERM;
    nSignals[1] = SIGINT;
    XSig_Register(nSignals, 2, signal_callback);

    /* Used variables */
    xevent_status_t status;
    xevents_t events;
    xsock_t socket;

    /* Check valid args */
    if (argc < 2)
    {
        xlog("Usage: %s [address] [port]", argv[0]);
        xlog("Example: %s 127.0.0.1 6969", argv[0]);
        return 1;
    }

    /* Create server socket */
    XSock_Create(&socket, XSOCK_TCP_SERVER, argv[1], atoi(argv[2]));
    if (socket.nFD == XSOCK_INVALID)
    {
        xloge("%s", XSock_ErrStr(&socket));
        return 1;
    }

    xlogi("Socket started listen to port: %d", atoi(argv[2]));

    /* Create event instance */
    status = XEvents_Create(&events, 0, &socket, event_callback, XTRUE);
    if (status != XEVENT_STATUS_SUCCESS)
    {
        xloge("%s", XEvents_Status(status));
        XSock_Close(&socket);
        return 1;
    }

    /* Add listener socket to the event instance */
    if (XEvents_RegisterEvent(&events, NULL, socket.nFD, XPOLLIN, 0) == NULL)
    {
        xloge("Failed to register listener event");
        XEvents_Destroy(&events);
        XSock_Close(&socket);
        return 1;
    }

    /* Main service loop */
    while (status == XEVENT_STATUS_SUCCESS)
        status = XEvents_Service(&events, 100);

    XEvents_Destroy(&events);
    return 0;
}