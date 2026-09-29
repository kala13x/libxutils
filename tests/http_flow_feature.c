/* Stateful HTTP exchanges with independent request and response expectations. */
#include "test.h"
#include "api.h"
#include "json.h"
#include "thread.h"
#include <poll.h>
#include <unistd.h>

typedef struct {
    xhttp_method_t eMethod;
    const char *pUri;
    const char *pRequest;
    const char *pID;
    int nStatus;
    const char *pResponse;
} flow_step_t;

static const flow_step_t g_steps[] = {
    {XHTTP_POST, "/items", "{\"name\":\"alpha\",\"value\":7}", "create", 201,
        "{\"id\":1,\"name\":\"alpha\",\"value\":7}"},
    {XHTTP_GET, "/items/1", NULL, "read-created", 200, "{\"id\":1,\"name\":\"alpha\",\"value\":7}"},
    {XHTTP_PUT, "/items/1", "{\"name\":\"beta\",\"value\":42}", "update", 200,
        "{\"id\":1,\"name\":\"beta\",\"value\":42}"},
    {XHTTP_GET, "/items/1", NULL, "read-updated", 200, "{\"id\":1,\"name\":\"beta\",\"value\":42}"},
    {XHTTP_GET, "/items/99", NULL, "read-missing", 404, "{\"error\":\"not found\"}"},
    {XHTTP_DELETE, "/items/1", NULL, "delete", 204, ""},
    {XHTTP_GET, "/items/1", NULL, "read-deleted", 404, "{\"error\":\"not found\"}"},
    {XHTTP_POST, "/sum", "{\"left\":7,\"right\":-2}", "sum", 200, "{\"result\":5}"},
    {XHTTP_POST, "/sum", "{\"left\":\"wrong\",\"right\":2}", "invalid-sum", 400,
        "{\"error\":\"invalid input\"}"}
};

enum {
    FLOW_VALID, FLOW_BAD_STATUS, FLOW_BAD_BODY, FLOW_BAD_TYPE, FLOW_BAD_ID,
    FLOW_BAD_REQUEST, FLOW_BAD_REQUEST_METHOD, FLOW_BAD_REQUEST_BODY, FLOW_BAD_REQUEST_TYPE, FLOW_BAD_REQUEST_ID
};

typedef struct {
    xapi_t api;
    xevents_t events;
    xsock_t listener;
    xsock_t peer;
    xbyte_buffer_t rx;
    xbyte_buffer_t tx;
    xatomic_t nStop;
    size_t nRequests;
    int nAccepted;
    int nClosed;
    int nErrors;
    int nRejectedRequests;
    int nLayer;
    int nFault;
    int nValue;
    xbool_t bPresent;
    xbool_t bInitialized;
    char sName[32];
} flow_t;

static xbool_t flow_header(xhttp_t *pHttp, const char *pName, const char *pExpected)
{
    const char *pValue = XHTTP_GetHeader(pHttp, pName);
    return pExpected ? pValue && !strcmp(pValue, pExpected) : pValue == NULL;
}

static xbool_t flow_request_matches(xhttp_t *pRequest, const flow_step_t *pStep)
{
    size_t nLength = pStep->pRequest ? strlen(pStep->pRequest) : 0;
    return pRequest->eType == XHTTP_REQUEST && pRequest->eMethod == pStep->eMethod &&
        !strcmp(pRequest->sUri, pStep->pUri) && !strcmp(pRequest->sVersion, "1.1") &&
        flow_header(pRequest, "Host", "localhost") && flow_header(pRequest, "Connection", "keep-alive") &&
        flow_header(pRequest, "X-Request-ID", pStep->pID) &&
        flow_header(pRequest, "Content-Type", pStep->pRequest ? "application/json" : NULL) &&
        pRequest->nContentLength == nLength && XHTTP_GetBodySize(pRequest) >= nLength &&
        (!nLength || !memcmp(XHTTP_GetBody(pRequest), pStep->pRequest, nLength));
}

static int flow_response_matches(xhttp_t *pResponse, const flow_step_t *pStep)
{
    if (pResponse->eType != XHTTP_RESPONSE || pResponse->nStatusCode != pStep->nStatus ||
        strcmp(pResponse->sVersion, "1.1")) return FLOW_BAD_STATUS;
    if (!flow_header(pResponse, "X-Request-ID", pStep->pID)) return FLOW_BAD_ID;
    if (!flow_header(pResponse, "Content-Type", pStep->nStatus == 204 ? NULL : "application/json")) return FLOW_BAD_TYPE;
    size_t nExpected = strlen(pStep->pResponse);
    if (pResponse->nContentLength != nExpected || XHTTP_GetBodySize(pResponse) != nExpected ||
        (nExpected && memcmp(XHTTP_GetBody(pResponse), pStep->pResponse, nExpected))) return FLOW_BAD_BODY;
    if (!flow_header(pResponse, "Connection", "keep-alive")) return FLOW_BAD_TYPE;
    return FLOW_VALID;
}

static int flow_application(flow_t *pTest, xhttp_t *pRequest, char *pBody, size_t nSize)
{
    int nStatus = 404;
    xstrncpy(pBody, nSize, "{\"error\":\"not found\"}");
    xbool_t bItem = !strcmp(pRequest->sUri, "/items/1");
    if (pRequest->eMethod == XHTTP_GET && bItem && pTest->bPresent) nStatus = 200;
    else if (pRequest->eMethod == XHTTP_DELETE && bItem && pTest->bPresent)
    {
        pTest->bPresent = XFALSE;
        pBody[0] = 0;
        return 204;
    }
    else if ((pRequest->eMethod == XHTTP_POST && !strcmp(pRequest->sUri, "/items")) ||
        (pRequest->eMethod == XHTTP_PUT && bItem && pTest->bPresent))
    {
        xjson_t json;
        int nParsed = XJSON_Parse(&json, NULL, (const char*)XHTTP_GetBody(pRequest), pRequest->nContentLength);
        xjson_obj_t *pName = nParsed ? XJSON_GetObject(json.pRootObj, "name") : NULL;
        xjson_obj_t *pValue = nParsed ? XJSON_GetObject(json.pRootObj, "value") : NULL;
        if (pName && pName->nType == XJSON_TYPE_STRING && pValue && pValue->nType == XJSON_TYPE_NUMBER)
        {
            xstrncpy(pTest->sName, sizeof(pTest->sName), XJSON_GetString(pName));
            pTest->nValue = XJSON_GetInt(pValue);
            pTest->bPresent = XTRUE;
            nStatus = pRequest->eMethod == XHTTP_POST ? 201 : 200;
        }
        else nStatus = 400;
        XJSON_Destroy(&json);
    }
    else if (pRequest->eMethod == XHTTP_POST && !strcmp(pRequest->sUri, "/sum"))
    {
        xjson_t json;
        int nParsed = XJSON_Parse(&json, NULL, (const char*)XHTTP_GetBody(pRequest), pRequest->nContentLength);
        xjson_obj_t *pLeft = nParsed ? XJSON_GetObject(json.pRootObj, "left") : NULL;
        xjson_obj_t *pRight = nParsed ? XJSON_GetObject(json.pRootObj, "right") : NULL;
        nStatus = 400;
        if (pLeft && pLeft->nType == XJSON_TYPE_NUMBER && pRight && pRight->nType == XJSON_TYPE_NUMBER)
        {
            snprintf(pBody, nSize, "{\"result\":%d}", XJSON_GetInt(pLeft) + XJSON_GetInt(pRight));
            nStatus = 200;
        }
        XJSON_Destroy(&json);
        if (nStatus == 200) return nStatus;
    }
    if (nStatus == 200 || nStatus == 201)
        snprintf(pBody, nSize, "{\"id\":1,\"name\":\"%s\",\"value\":%d}", pTest->sName, pTest->nValue);
    else if (nStatus == 400) xstrncpy(pBody, nSize, "{\"error\":\"invalid input\"}");
    return nStatus;
}

static int flow_answer(flow_t *pTest, xhttp_t *pRequest, xbyte_buffer_t *pOutput)
{
    if (pTest->nRequests >= sizeof(g_steps) / sizeof(*g_steps)) return XSTDERR;
    const flow_step_t *pStep = &g_steps[pTest->nRequests];
    xbool_t bMatches = flow_request_matches(pRequest, pStep);
    char sBody[256];
    int nStatus;
    if (!bMatches)
    {
        pTest->nRejectedRequests++;
        nStatus = 422;
        xstrncpy(sBody, sizeof(sBody), "{\"error\":\"request mismatch\"}");
    }
    else
    {
        nStatus = flow_application(pTest, pRequest, sBody, sizeof(sBody));
        pTest->nRequests++;
    }
    xbool_t bMutate = bMatches && pTest->nRequests == 2;
    if (bMutate && pTest->nFault == FLOW_BAD_STATUS) nStatus = 500;
    if (bMutate && pTest->nFault == FLOW_BAD_BODY) sBody[strlen(sBody) / 2] ^= 1;
    const char *pType = bMutate && pTest->nFault == FLOW_BAD_TYPE ? "text/plain" : "application/json";
    const char *pID = bMutate && pTest->nFault == FLOW_BAD_ID ? "unrelated-request" : pStep->pID;
    xhttp_t response;
    XHTTP_InitResponse(&response, (uint16_t)nStatus, "1.1");
    int nResult = XSTDERR;
    if (XHTTP_AddHeader(&response, "Connection", "keep-alive") > 0 &&
        XHTTP_AddHeader(&response, "X-Request-ID", "%s", pID) > 0 &&
        (nStatus == 204 || XHTTP_AddHeader(&response, "Content-Type", "%s", pType) > 0))
    {
        xbyte_buffer_t *pWire = XHTTP_Assemble(&response, (const uint8_t*)sBody, strlen(sBody));
        if (pWire && XByteBuffer_AddBuff(pOutput, pWire) > 0) nResult = XSTDOK;
    }
    XHTTP_Clear(&response);
    return nResult;
}

static int flow_api_cb(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    flow_t *pTest = pCtx->pApi->pUserCtx;
    if (pCtx->eCbType == XAPI_CB_ERROR) { pTest->nErrors++; return XAPI_DISCONNECT; }
    if (pCtx->eCbType == XAPI_CB_CLOSED && pSession->eRole == XAPI_PEER) pTest->nClosed++;
    if (pCtx->eCbType == XAPI_CB_ACCEPTED)
    {
        pTest->nAccepted++;
        return XAPI_SetEvents(pSession, XPOLLIN);
    }
    if (pCtx->eCbType == XAPI_CB_READ)
    {
        if (flow_answer(pTest, pSession->pPacket, &pSession->txBuffer) < 0)
        {
            pTest->nErrors++;
            return XAPI_DISCONNECT;
        }
        return XAPI_EnableEvent(pSession, XPOLLOUT);
    }
    return XAPI_CONTINUE;
}

static int flow_accept(flow_t *pTest)
{
    if (XSock_Accept(&pTest->listener, &pTest->peer) == XSOCK_INVALID) return XSTDERR;
    pTest->nAccepted++;
    if (pTest->nLayer == 1 &&
        !XEvents_RegisterEvent(&pTest->events, NULL, pTest->peer.nFD, XPOLLIN, XEVENT_TYPE_PEER)) return XSTDERR;
    return XSTDOK;
}

static int flow_read(flow_t *pTest)
{
    uint8_t bytes[19];
    int nRead = XSock_Read(&pTest->peer, bytes, sizeof(bytes));
    if (nRead < 0 && pTest->peer.eStatus == XSOCK_WANT_READ) return XSTDOK;
    if (nRead <= 0 || XByteBuffer_Add(&pTest->rx, bytes, (size_t)nRead) <= 0) return XSTDERR;
    while (pTest->rx.nUsed)
    {
        xhttp_t request;
        xhttp_status_t eStatus = XHTTP_ParseData(&request, pTest->rx.pData, pTest->rx.nUsed);
        int nStatus = XSTDOK;
        size_t nUsed = 0;
        if (eStatus == XHTTP_COMPLETE)
        {
            nUsed = XHTTP_GetPacketSize(&request);
            nStatus = flow_answer(pTest, &request, &pTest->tx);
        }
        else if (eStatus != XHTTP_INCOMPLETE && eStatus != XHTTP_PARSED) nStatus = XSTDERR;
        XHTTP_Clear(&request);
        if (nStatus < 0) return XSTDERR;
        if (!nUsed) break;
        XByteBuffer_Advance(&pTest->rx, nUsed);
    }
    return XSTDOK;
}

static int flow_write(flow_t *pTest)
{
    if (!pTest->tx.nUsed) return XSTDOK;
    int nSent = XSock_Write(&pTest->peer, pTest->tx.pData, XSTD_MIN(pTest->tx.nUsed, 13));
    if (nSent < 0 && pTest->peer.eStatus == XSOCK_WANT_WRITE) return XSTDOK;
    if (nSent <= 0) return XSTDERR;
    XByteBuffer_Advance(&pTest->tx, (size_t)nSent);
    return XSTDOK;
}

static int flow_event_cb(void *pLoop, void *pData, XSOCKET nFD, xevent_cb_type_t eReason)
{
    xevents_t *pEvents = pLoop;
    flow_t *pTest = pEvents->pUserSpace;
    xevent_data_t *pEvent = pData;
    (void)nFD;
    if (!pEvent) return XEVENTS_CONTINUE;
    if (eReason == XEVENT_CB_CLEAR)
    {
        if (pEvent->nType == XEVENT_TYPE_SERVER) XSock_Close(&pTest->listener);
        else { XSock_Close(&pTest->peer); pTest->nClosed++; }
        return XEVENTS_CONTINUE;
    }
    if (eReason == XEVENT_CB_READ && pEvent->nType == XEVENT_TYPE_SERVER)
    {
        if (flow_accept(pTest) > 0) return XEVENTS_ACCEPT;
    }
    else if (eReason == XEVENT_CB_READ || eReason == XEVENT_CB_WRITE)
    {
        int nStatus = eReason == XEVENT_CB_READ ? flow_read(pTest) : flow_write(pTest);
        if (nStatus > 0 && XEvents_Modify(pEvents, pEvent, XPOLLIN | (pTest->tx.nUsed ? XPOLLOUT : 0)) == XEVENTS_SUCCESS)
            return XEVENTS_CONTINUE;
    }
    else if (eReason != XEVENT_CB_ERROR && eReason != XEVENT_CB_CLOSED && eReason != XEVENT_CB_HUNGED)
        return XEVENTS_CONTINUE;
    pTest->nErrors++;
    return XEVENTS_DISCONNECT;
}

static void *flow_serve(void *pContext)
{
    flow_t *pTest = pContext;
    while (!XSYNC_ATOMIC_GET(&pTest->nStop) && !pTest->nErrors)
    {
        if (!pTest->nLayer)
        {
            if (XAPI_Service(&pTest->api, 10) != XEVENTS_SUCCESS) pTest->nErrors++;
        }
        else if (pTest->nLayer == 1)
        {
            if (XEvents_Service(&pTest->events, 10) != XEVENTS_SUCCESS) pTest->nErrors++;
        }
        else
        {
            xbool_t bListening = pTest->peer.nFD == XSOCK_INVALID;
            struct pollfd descriptor = {bListening ? pTest->listener.nFD : pTest->peer.nFD,
                POLLIN | (pTest->tx.nUsed ? POLLOUT : 0), 0};
            int nReady = poll(&descriptor, 1, 10);
            if (nReady < 0) { pTest->nErrors++; break; }
            if (!nReady) continue;
            if (bListening)
            {
                if (flow_accept(pTest) < 0) pTest->nErrors++;
            }
            else
            {
                if ((descriptor.revents & POLLIN) && flow_read(pTest) < 0) pTest->nErrors++;
                if ((descriptor.revents & POLLOUT) && flow_write(pTest) < 0) pTest->nErrors++;
            }
        }
    }
    return NULL;
}

static int flow_run(int nLayer, int nFault)
{
    flow_t test = {0};
    test.nLayer = nLayer;
    test.nFault = nFault;
    XSock_Init(&test.listener, XSOCK_TCP_SERVER | XSOCK_NB, XSOCK_INVALID);
    XSock_Init(&test.peer, XSOCK_TCP_PEER, XSOCK_INVALID);
    XByteBuffer_Init(&test.rx, 0, XTRUE);
    XByteBuffer_Init(&test.tx, 0, XTRUE);
    xsock_t client;
    XSock_Init(&client, XSOCK_TCP_CLIENT, XSOCK_INVALID);
    xthread_t thread;
    xbool_t bStarted = XFALSE;
    size_t nComplete = 0, nValidated = 0;
    int nRejected = FLOW_VALID, nStatus = XSTDERR;
    int nLastStatus = 0;
    size_t nFaultStep = nFault == FLOW_BAD_REQUEST_BODY || nFault == FLOW_BAD_REQUEST_TYPE ? 2 : 1;

    do
    {
        test.listener.nFD = socket(AF_INET, SOCK_STREAM, 0);
        if (test.listener.nFD == XSOCK_INVALID) break;
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t nAddrSize = sizeof(addr);
        if (bind(test.listener.nFD, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(test.listener.nFD, 4) < 0 ||
            getsockname(test.listener.nFD, (struct sockaddr*)&addr, &nAddrSize) < 0 ||
            XSock_NonBlock(&test.listener, XTRUE) == XSOCK_INVALID) break;
        if (!nLayer)
        {
            if (XAPI_Init(&test.api, flow_api_cb, &test) != XSTDOK) break;
            test.bInitialized = XTRUE;
            xapi_endpoint_t endpoint;
            XAPI_InitEndpoint(&endpoint);
            endpoint.eType = XAPI_HTTP;
            endpoint.eRole = XAPI_SERVER;
            endpoint.nFD = test.listener.nFD;
            endpoint.nEvents = XPOLLIN;
            test.listener.nFD = XSOCK_INVALID;
            if (XAPI_AddEvent(&test.api, &endpoint) != XSTDOK) break;
            test.api.events.nEventMax = 4;
        }
        else if (nLayer == 1)
        {
            if (XEvents_Create(&test.events, 4, &test, flow_event_cb, XTRUE) != XEVENTS_SUCCESS) break;
            test.bInitialized = XTRUE;
            if (!XEvents_RegisterEvent(&test.events, NULL, test.listener.nFD, XPOLLIN, XEVENT_TYPE_SERVER)) break;
        }
        if (XThread_Create(&thread, flow_serve, &test, 0) != XSTDOK) break;
        bStarted = XTRUE;
        if (XSock_Create(&client, XSOCK_TCP_CLIENT, "127.0.0.1", ntohs(addr.sin_port)) == XSOCK_INVALID) break;
        XSock_TimeOutR(&client, 10, 0);
        XSock_TimeOutS(&client, 10, 0);
        nStatus = XSTDOK;
        for (size_t i = 0; i < sizeof(g_steps) / sizeof(*g_steps); i++)
        {
            const flow_step_t *pStep = &g_steps[i];
            xbool_t bMutate = i == nFaultStep;
            const char *pUri = bMutate && nFault == FLOW_BAD_REQUEST ? "/wrong-request" : pStep->pUri;
            const char *pID = bMutate && nFault == FLOW_BAD_REQUEST_ID ? "unrelated-request" : pStep->pID;
            const char *pType = bMutate && nFault == FLOW_BAD_REQUEST_TYPE ? "text/plain" : "application/json";
            const char *pBody = bMutate && nFault == FLOW_BAD_REQUEST_BODY ? "{\"name\":\"beta\",\"value\":43}" : pStep->pRequest;
            xhttp_method_t eMethod = bMutate && nFault == FLOW_BAD_REQUEST_METHOD ? XHTTP_DELETE : pStep->eMethod;
            xhttp_t request, response;
            XHTTP_InitRequest(&request, eMethod, pUri, "1.1");
            XHTTP_Init(&response, XHTTP_DUMMY, 0);
            XHTTP_AddHeader(&request, "Host", "localhost");
            XHTTP_AddHeader(&request, "Connection", "keep-alive");
            XHTTP_AddHeader(&request, "X-Request-ID", "%s", pID);
            if (pBody) XHTTP_AddHeader(&request, "Content-Type", "%s", pType);
            size_t nLength = pBody ? strlen(pBody) : 0;
            if (!XHTTP_Assemble(&request, (const uint8_t*)pBody, nLength) ||
                XHTTP_Exchange(&request, &response, &client) != XHTTP_COMPLETE) nStatus = XSTDERR;
            else
            {
                nComplete++;
                nLastStatus = response.nStatusCode;
                nRejected = flow_response_matches(&response, pStep);
                if (nRejected == FLOW_VALID) nValidated++;
            }
            XHTTP_Clear(&response);
            XHTTP_Clear(&request);
            if (nStatus < 0 || nRejected != FLOW_VALID) break;
        }
    } while (0);

    XSYNC_ATOMIC_SET(&test.nStop, 1);
    if (bStarted) XThread_Join(&thread);
    if (test.bInitialized)
    {
        if (!nLayer) XAPI_Destroy(&test.api);
        else XEvents_Destroy(&test.events);
    }
    if (nLayer == 2 && test.peer.nFD != XSOCK_INVALID) test.nClosed++;
    XSock_Close(&test.peer);
    XSock_Close(&test.listener);
    XSock_Close(&client);
    XByteBuffer_Clear(&test.rx);
    XByteBuffer_Clear(&test.tx);

    CHECK(nStatus == XSTDOK && !test.nErrors, "Every attempted HTTP exchange completed without a transport or parser error");
    CHECK(test.nAccepted == 1 && test.nClosed == 1, "The complete flow uses and releases one persistent connection");
    if (nFault >= FLOW_BAD_REQUEST)
    {
        CHECK(test.nRequests == nFaultStep && test.nRejectedRequests == 1, "The server rejected the changed request");
        CHECK(nComplete == nFaultStep + 1 && nValidated == nFaultStep && nLastStatus == 422,
            "A completed exchange is not accepted as a valid response");
        CHECK(test.bPresent && test.nValue == 7 && !strcmp(test.sName, "alpha"), "Rejected requests preserve server state");
    }
    else if (nFault)
    {
        CHECK(test.nRequests == 2 && !test.nRejectedRequests, "The server verified both requests before corrupting its reply");
        CHECK(nComplete == 2 && nValidated == 1 && nRejected == nFault, "The client detected the specific invalid response");
        if (nFault == FLOW_BAD_STATUS) CHECK(nLastStatus == 500, "HTTP 500 completed the exchange but failed validation");
    }
    else
    {
        CHECK(test.nRequests == 9 && !test.nRejectedRequests, "The server verified every method, URI, header and request body");
        CHECK(nComplete == 9 && nValidated == 9, "The client verified every exact status, header and response body");
        CHECK(!test.bPresent && test.nValue == 42 && !strcmp(test.sName, "beta"), "CRUD operations changed server state");
    }
    return 0;
}

static int XTest_api_crud(void) { return flow_run(0, FLOW_VALID); }
static int XTest_event_crud(void) { return flow_run(1, FLOW_VALID); }
static int XTest_socket_crud(void) { return flow_run(2, FLOW_VALID); }

static int XTest_reject_status(void)
{
    for (int i = 0; i < 3; i++) CHECK(flow_run(i, FLOW_BAD_STATUS) == 0, "Reject HTTP 500 at every server layer");
    return 0;
}

static int XTest_reject_body(void)
{
    for (int i = 0; i < 3; i++) CHECK(flow_run(i, FLOW_BAD_BODY) == 0, "Reject unrelated response bytes at every server layer");
    return 0;
}

static int XTest_reject_type(void)
{
    for (int i = 0; i < 3; i++) CHECK(flow_run(i, FLOW_BAD_TYPE) == 0, "Reject an unexpected response content type");
    return 0;
}

static int XTest_reject_id(void)
{
    for (int i = 0; i < 3; i++) CHECK(flow_run(i, FLOW_BAD_ID) == 0, "Reject a response for another request");
    return 0;
}

static int XTest_reject_request(void)
{
    for (int i = 0; i < 3; i++)
        for (int nFault = FLOW_BAD_REQUEST; nFault <= FLOW_BAD_REQUEST_ID; nFault++)
            CHECK(flow_run(i, nFault) == 0, "The server checks incoming methods, targets, identifiers, types and body bytes");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(api_crud),
    XTEST_CASE(event_crud),
    XTEST_CASE(socket_crud),
    XTEST_CASE(reject_status),
    XTEST_CASE(reject_body),
    XTEST_CASE(reject_type),
    XTEST_CASE(reject_id),
    XTEST_CASE(reject_request)
)
