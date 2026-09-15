/* libxutils: real TLS records, private trust anchors and hostname verification. */
#include "test.h"
#include "api.h"
#include "xtime.h"
#include <openssl/x509v3.h>

typedef struct xtest_tls_ {
    xapi_t api;
    xapi_session_t *pSession;
    xsock_t remote;
    SSL_CTX *pServerCtx;
    SSL *pServer;
    EVP_PKEY *pKey;
    X509 *pCertificate;
    xbyte_buffer_t received;
    int nReads;
} xtest_tls_t;

static int XTest_Callback(xapi_ctx_t *pContext, xapi_session_t *pSession)
{
    xtest_tls_t *pTest = (xtest_tls_t*)pContext->pApi->pUserCtx;
    if (pContext->eCbType == XAPI_CB_REGISTERED) pTest->pSession = pSession;
    else if (pContext->eCbType == XAPI_CB_READ)
    {
        xbyte_buffer_t *pBuffer = (xbyte_buffer_t*)pSession->pPacket;
        if (!pBuffer || XByteBuffer_AddBuff(&pTest->received, pBuffer) <= 0) return XAPI_DISCONNECT;
        pTest->nReads++;
    }
    return XAPI_CONTINUE;
}

static int XTest_Open(xtest_tls_t *pTest, const char *pHost, xbool_t bTrust)
{
    XByteBuffer_Init(&pTest->received, 0, XTRUE);
    EVP_PKEY_CTX *pKeyCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    CHECK(pKeyCtx && EVP_PKEY_keygen_init(pKeyCtx) == 1, "Initialize an ephemeral EC test key");
    CHECK(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pKeyCtx, NID_X9_62_prime256v1) == 1, "Choose P-256 for fast local fixtures");
    CHECK(EVP_PKEY_keygen(pKeyCtx, &pTest->pKey) == 1, "Generate test key");
    EVP_PKEY_CTX_free(pKeyCtx);
    pTest->pCertificate = X509_new();
    CHECK(pTest->pCertificate != NULL, "Allocate a test-only certificate");
    CHECK(X509_set_version(pTest->pCertificate, 2) == 1, "Use an X509 v3 certificate");
    CHECK(ASN1_INTEGER_set(X509_get_serialNumber(pTest->pCertificate), 1) == 1, "Set certificate serial");
    CHECK(X509_gmtime_adj(X509_getm_notBefore(pTest->pCertificate), -60) != NULL, "Set recent validity start");
    CHECK(X509_gmtime_adj(X509_getm_notAfter(pTest->pCertificate), 3600) != NULL, "Set bounded certificate validity");
    CHECK(X509_set_pubkey(pTest->pCertificate, pTest->pKey) == 1, "Attach certificate public key");
    X509_NAME *pName = X509_get_subject_name(pTest->pCertificate);
    CHECK(X509_NAME_add_entry_by_txt(pName, "CN", MBSTRING_ASC, (const unsigned char*)"localhost", -1, -1, 0) == 1,
        "Bind test certificate to localhost");
    CHECK(X509_set_issuer_name(pTest->pCertificate, pName) == 1, "Self-sign the private trust anchor");
    CHECK(X509_sign(pTest->pCertificate, pTest->pKey, EVP_sha256()) > 0, "Sign certificate");
    pTest->pServerCtx = SSL_CTX_new(TLS_server_method());
    CHECK(pTest->pServerCtx != NULL, "Initialize private TLS server context");
    CHECK(SSL_CTX_use_certificate(pTest->pServerCtx, pTest->pCertificate) == 1, "Configure server certificate");
    CHECK(SSL_CTX_use_PrivateKey(pTest->pServerCtx, pTest->pKey) == 1, "Configure server private key");
    XSOCKET pair[2];
    CHECK(XSock_CreatePair(pair) == XSTDOK, "Create TLS socket pair without a listening port");
    CHECK(XSock_Init(&pTest->remote, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "Own the remote descriptor");
    CHECK(XSock_NonBlock(&pTest->remote, XTRUE) != XSOCK_INVALID, "TLS handshake must not block");
    pTest->pServer = SSL_new(pTest->pServerCtx);
    CHECK(pTest->pServer && SSL_set_fd(pTest->pServer, (int)pair[0]) == 1, "Bind private server to socket pair");
    SSL_set_accept_state(pTest->pServer);
    CHECK(XAPI_Init(&pTest->api, XTest_Callback, pTest) == XSTDOK, "Initialize API receiver");
    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_PEER;
    endpoint.nFD = pair[1];
    endpoint.nEvents = XPOLLIN;
    CHECK(XAPI_AddEvent(&pTest->api, &endpoint) == XSTDOK && pTest->pSession, "Register TLS receiver");
    xsock_t *pClient = &pTest->pSession->sock;
    CHECK(XSock_Init(pClient, XSOCK_TCP_CLIENT | XSOCK_SSL | XSOCK_NB, pair[1]) != XSOCK_ERROR,
        "Initialize the adopted descriptor as a TLS client");
    CHECK(XSock_NonBlock(pClient, XTRUE) != XSOCK_INVALID, "Set library side nonblocking");
    CHECK(XSock_InitSSLClient(pClient, pHost) != XSOCK_INVALID, "Start library TLS client handshake");
    SSL *pSSL = XSock_GetSSL(pClient);
    CHECK(pSSL && (SSL_get_verify_mode(pSSL) & SSL_VERIFY_PEER), "Client verification remains enabled");
    if (bTrust)
    {
        CHECK(X509_STORE_add_cert(SSL_CTX_get_cert_store(XSock_GetSSLCTX(pClient)), pTest->pCertificate) == 1,
            "Trust only the generated certificate in this private client context");
    }
    return 0;
}

static int XTest_Handshake(xtest_tls_t *pTest)
{
    uint64_t nDeadline = XTime_GetMs() + 5000;
    xsock_t *pClient = &pTest->pSession->sock;
    while (XTime_GetMs() < nDeadline)
    {
        int nServer = SSL_accept(pTest->pServer);
        if (nServer != 1)
        {
            int nError = SSL_get_error(pTest->pServer, nServer);
            if (nError != SSL_ERROR_WANT_READ && nError != SSL_ERROR_WANT_WRITE) return 0;
        }
        if (XSock_SSLConnect(pClient) == XSOCK_INVALID) return 0;
        if (SSL_is_init_finished(pTest->pServer) && SSL_is_init_finished(XSock_GetSSL(pClient))) return 1;
    }
    return 0;
}

static void XTest_Close(xtest_tls_t *pTest)
{
    XAPI_Destroy(&pTest->api);
    SSL_free(pTest->pServer);
    SSL_CTX_free(pTest->pServerCtx);
    X509_free(pTest->pCertificate);
    EVP_PKEY_free(pTest->pKey);
    XSock_Close(&pTest->remote);
    XByteBuffer_Clear(&pTest->received);
}

static int XTest_pending_record(void)
{
    xtest_tls_t test = {0};
    CHECK(XTest_Open(&test, "localhost", XTRUE) == 0, "Create trusted TLS fixture");
    CHECK(XTest_Handshake(&test), "Complete a verified handshake");
    uint8_t data[16384];
    for (size_t i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(i % 251);
    CHECK(SSL_write(test.pServer, data, sizeof(data)) == sizeof(data), "Send one TLS record larger than the API read buffer");
    CHECK(XAPI_Service(&test.api, 100) == XEVENTS_SUCCESS, "Service one readable event");
    CHECK(test.received.nUsed == sizeof(data) && test.nReads > 1, "Drain decrypted bytes without requiring another socket event");
    CHECK(memcmp(test.received.pData, data, sizeof(data)) == 0, "Record draining retains all bytes in order");
    CHECK(XSock_Pending(&test.pSession->sock) == 0, "No plaintext remains stranded inside OpenSSL");
    XTest_Close(&test);
    return 0;
}

static int XTest_wrong_hostname(void)
{
    xtest_tls_t test = {0};
    CHECK(XTest_Open(&test, "wrong.example", XTRUE) == 0, "Create trusted certificate with a mismatched hostname");
    CHECK(!XTest_Handshake(&test) && test.pSession->sock.nFD == XSOCK_INVALID,
        "A trusted chain cannot bypass hostname verification");
    XTest_Close(&test);
    return 0;
}

static int XTest_untrusted(void)
{
    xtest_tls_t test = {0};
    CHECK(XTest_Open(&test, "localhost", XFALSE) == 0, "Create an untrusted certificate fixture");
    CHECK(!XTest_Handshake(&test) && test.pSession->sock.nFD == XSOCK_INVALID,
        "A matching hostname cannot bypass chain verification");
    XTest_Close(&test);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(pending_record),
    XTEST_CASE(wrong_hostname),
    XTEST_CASE(untrusted)
)
