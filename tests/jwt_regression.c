/*!
 * @file libxutils/tests/jwt_regression.c
 * @brief JWT signing, parsing and verification.
 *
 * A token is only as good as the checks around it, so the cases below pin
 * down what must be rejected as firmly as what must be accepted: a wrong
 * secret, a tampered segment, a fourth segment, trailing bytes after a
 * valid prefix, and an embedded NUL that would hide part of the input from
 * a length-unaware parser.
 */

#include "test.h"
#include "jwt.h"

static const uint8_t g_secret[] = "local-regression-test-key";
#define SECRET_LEN (sizeof(g_secret) - 1)

/* Signs a payload and hands the caller the token to free. */
static char *jwt_make(const char *pPayload, size_t *pLength)
{
    xjwt_t jwt;
    XJWT_Init(&jwt, XJWT_ALG_HS256);

    if (XJWT_AddPayload(&jwt, pPayload, strlen(pPayload), XFALSE) != XSTDOK)
    {
        XJWT_Destroy(&jwt);
        return NULL;
    }

    char *pToken = XJWT_Create(&jwt, g_secret, SECRET_LEN, pLength);
    XJWT_Destroy(&jwt);
    return pToken;
}

static int XTest_roundtrip(void)
{
    /* A token signed with a secret verifies with the same secret, and its
     * payload and header come back as they went in. */
    size_t nLength = 0;
    char *pToken = jwt_make("{\"sub\":\"device\",\"exp\":1700000000}", &nLength);
    CHECK(pToken != NULL && nLength > 0, "A token is signed");

    /* The wire form is three dot separated segments. */
    int nDots = 0;
    for (size_t i = 0; i < nLength; i++) if (pToken[i] == '.') nDots++;
    CHECK(nDots == 2, "A token has exactly three segments");

    xjwt_t jwt;
    CHECK(XJWT_Parse(&jwt, pToken, nLength, g_secret, SECRET_LEN) == XSTDOK, "The token parses");
    CHECK(jwt.bVerified == XTRUE, "The token verifies against its own secret");
    CHECK(XJWT_GetAlgorithm(&jwt) == XJWT_ALG_HS256, "The algorithm is read back from the header");

    size_t nPayloadLen = 0;
    char *pPayload = XJWT_GetPayload(&jwt, XTRUE, &nPayloadLen);
    CHECK(pPayload != NULL, "The payload is available");
    CHECK(strcmp(pPayload, "{\"sub\":\"device\",\"exp\":1700000000}") == 0, "The payload survives the round trip");
    CHECK(nPayloadLen == strlen(pPayload), "The reported payload length matches");
    free(pPayload);

    size_t nHeaderLen = 0;
    char *pHeader = XJWT_GetHeader(&jwt, XTRUE, &nHeaderLen);
    CHECK(pHeader != NULL, "The header is available");
    CHECK(strstr(pHeader, "\"alg\":\"HS256\"") != NULL, "The header names the algorithm");
    CHECK(strstr(pHeader, "\"typ\":\"JWT\"") != NULL, "The header names the token type");
    free(pHeader);

    /* The parsed segments are also available as JSON objects. */
    xjson_obj_t *pPayloadObj = XJWT_GetPayloadObj(&jwt);
    CHECK(pPayloadObj != NULL, "The payload is available as an object");
    CHECK(XJSON_GetObject(pPayloadObj, "sub") != NULL, "The payload object carries its fields");

    xjson_obj_t *pHeaderObj = XJWT_GetHeaderObj(&jwt);
    CHECK(pHeaderObj != NULL, "The header is available as an object");
    CHECK(XJSON_GetObject(pHeaderObj, "alg") != NULL, "The header object carries its fields");

    XJWT_Destroy(&jwt);
    free(pToken);
    return 0;
}

static int XTest_wrong_secret(void)
{
    /* The whole point of the signature: another secret must not verify. */
    size_t nLength = 0;
    char *pToken = jwt_make("{\"sub\":\"device\"}", &nLength);
    CHECK(pToken != NULL, "A token is signed");

    const char *pWrong[] = {
        "wrong-key", "", "local-regression-test-ke", "local-regression-test-key2",
        "LOCAL-REGRESSION-TEST-KEY"
    };

    for (size_t i = 0; i < sizeof(pWrong) / sizeof(*pWrong); i++)
    {
        xjwt_t jwt;
        XSTATUS nStatus = XJWT_Parse(&jwt, pToken, nLength,
            (const uint8_t*)pWrong[i], strlen(pWrong[i]));
        CHECK(nStatus != XSTDOK || jwt.bVerified == XFALSE, "Another secret never verifies");
        XJWT_Destroy(&jwt);
    }

    /* Parsing without a secret reads the token but claims no verification. */
    xjwt_t jwt;
    CHECK(XJWT_Parse(&jwt, pToken, nLength, NULL, 0) == XSTDOK, "A token parses without a secret");
    CHECK(jwt.bVerified == XFALSE, "A token parsed without a secret is not verified");
    XJWT_Destroy(&jwt);

    free(pToken);
    return 0;
}

static int XTest_tampering(void)
{
    /* Changing any byte of any segment must invalidate the signature. */
    size_t nLength = 0;
    char *pToken = jwt_make("{\"sub\":\"device\",\"admin\":false}", &nLength);
    CHECK(pToken != NULL, "A token is signed");

    char *pCopy = (char*)malloc(nLength + 1);
    CHECK(pCopy != NULL, "A working copy is made");

    for (size_t i = 0; i < nLength; i++)
    {
        if (pToken[i] == '.') continue;

        memcpy(pCopy, pToken, nLength);
        pCopy[nLength] = '\0';
        /* Flip to another character from the base64url alphabet. */
        pCopy[i] = (pToken[i] == 'A') ? 'B' : 'A';

        xjwt_t jwt;
        XSTATUS nStatus = XJWT_Parse(&jwt, pCopy, nLength, g_secret, SECRET_LEN);
        CHECK(nStatus != XSTDOK || jwt.bVerified == XFALSE, "A tampered byte never verifies");
        XJWT_Destroy(&jwt);
    }

    /* Truncating the token at any point must also fail. */
    for (size_t n = 1; n < nLength; n++)
    {
        xjwt_t jwt;
        XSTATUS nStatus = XJWT_Parse(&jwt, pToken, n, g_secret, SECRET_LEN);
        CHECK(nStatus != XSTDOK || jwt.bVerified == XFALSE, "A truncated token never verifies");
        XJWT_Destroy(&jwt);
    }

    free(pCopy);
    free(pToken);
    return 0;
}

static int XTest_boundaries(void)
{
    /* The parser is length delimited: it must not read past what it was
     * given, and must not accept anything past the third segment. */
    size_t nLength = 0;
    char *pToken = jwt_make("{\"sub\":\"device\"}", &nLength);
    CHECK(pToken != NULL, "A token is signed");

    xjwt_t jwt;
    CHECK(XJWT_Parse(&jwt, pToken, nLength, g_secret, SECRET_LEN) == XSTDOK, "The token parses");
    CHECK(jwt.bVerified, "The token verifies");
    XJWT_Destroy(&jwt);

    /* Trailing bytes after a valid token are not part of it. */
    char *pExtended = (char*)malloc(nLength + 8);
    CHECK(pExtended != NULL, "A token with a suffix is built");
    memcpy(pExtended, pToken, nLength);
    memcpy(&pExtended[nLength], "suffix", 7);

    CHECK(XJWT_Parse(&jwt, pExtended, nLength + 6, g_secret, SECRET_LEN) != XSTDOK,
        "A valid prefix with trailing bytes is rejected");
    CHECK(!jwt.bVerified, "A token with trailing bytes is not verified");
    XJWT_Destroy(&jwt);

    /* A fourth segment is not a token. */
    memcpy(&pExtended[nLength], ".extra", 7);
    CHECK(XJWT_Parse(&jwt, pExtended, nLength + 6, g_secret, SECRET_LEN) != XSTDOK,
        "A fourth segment is rejected");
    CHECK(!jwt.bVerified, "A four segment token is not verified");
    XJWT_Destroy(&jwt);
    free(pExtended);

    /* A slice with no terminator parses on its length alone. */
    char *pSlice = (char*)malloc(nLength);
    CHECK(pSlice != NULL, "An unterminated slice is built");
    memcpy(pSlice, pToken, nLength);

    CHECK(XJWT_Parse(&jwt, pSlice, nLength, g_secret, SECRET_LEN) == XSTDOK,
        "An unterminated slice parses on its length");
    CHECK(jwt.bVerified, "An unterminated slice verifies");
    XJWT_Destroy(&jwt);

    /* An embedded NUL must not hide the rest of the input. */
    pSlice[nLength / 2] = '\0';
    CHECK(XJWT_Parse(&jwt, pSlice, nLength, g_secret, SECRET_LEN) != XSTDOK,
        "An embedded NUL does not hide part of the token");
    CHECK(!jwt.bVerified, "A token with an embedded NUL is not verified");
    XJWT_Destroy(&jwt);
    free(pSlice);

    free(pToken);
    return 0;
}

static int XTest_malformed(void)
{
    /* Anything that is not three base64url segments is refused, without
     * reading past the length it was handed. */
    const char *pInvalid[] = {
        "", "a", "a.b", "....", "...", "a.b.c.d",
        "eyJhbGciOiJIUzI1NiJ9..signature",
        ".eyJzdWIiOiJ4In0.signature",
        "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiJ4In0.",
        "!!!.???.***",
        "\\x01\\x02.\\x03\\x04.\\x05\\x06"
    };

    for (size_t i = 0; i < sizeof(pInvalid) / sizeof(*pInvalid); i++)
    {
        xjwt_t jwt;
        XSTATUS nStatus = XJWT_Parse(&jwt, pInvalid[i], strlen(pInvalid[i]), g_secret, SECRET_LEN);
        CHECK(nStatus != XSTDOK, "A malformed token is refused");
        CHECK(jwt.bVerified == XFALSE, "A malformed token is never verified");
        XJWT_Destroy(&jwt);
    }

    /* Missing arguments are refused rather than dereferenced. */
    xjwt_t jwt;
    CHECK(XJWT_Parse(&jwt, NULL, 32, g_secret, SECRET_LEN) != XSTDOK, "A missing token is refused");
    XJWT_Destroy(&jwt);
    CHECK(XJWT_Parse(NULL, "a.b.c", 5, g_secret, SECRET_LEN) != XSTDOK, "A missing output is refused");
    return 0;
}

static int XTest_algorithms(void)
{
    /* The algorithm name and its enum value round trip. */
    CHECK(strcmp(XJWT_GetAlgStr(XJWT_ALG_HS256), "HS256") == 0, "The HMAC algorithm has its name");
    CHECK(XJWT_GetAlg("HS256") == XJWT_ALG_HS256, "The HMAC name parses back to its value");
    CHECK(XJWT_GetAlgStr(XJWT_ALG_INVALID) == NULL, "The invalid algorithm has no name");

    /* A name has to match in full: a prefix or a suffix is not the name. */
    CHECK(XJWT_GetAlg("HS256unexpected") == XJWT_ALG_INVALID, "A name with a suffix is rejected");
    CHECK(XJWT_GetAlg("RS256unexpected") == XJWT_ALG_INVALID, "An RSA name with a suffix is rejected");
    CHECK(XJWT_GetAlg("HS25") == XJWT_ALG_INVALID, "A truncated name is rejected");
    CHECK(XJWT_GetAlg("hs256") == XJWT_ALG_INVALID, "The name is case sensitive");
    CHECK(XJWT_GetAlg("none") == XJWT_ALG_INVALID, "The unsigned algorithm is not accepted");
    CHECK(XJWT_GetAlg("") == XJWT_ALG_INVALID, "An empty name is rejected");
    CHECK(XJWT_GetAlg(NULL) == XJWT_ALG_INVALID, "A missing name is rejected");

    /* A header built for an algorithm names it. */
    xjson_obj_t *pHeader = XJWT_CreateHeaderObj(XJWT_ALG_HS256);
    CHECK(pHeader != NULL, "A header object is built");
    CHECK(XJSON_GetObject(pHeader, "alg") != NULL, "The header names an algorithm");
    CHECK(XJSON_GetObject(pHeader, "typ") != NULL, "The header names a token type");
    XJSON_FreeObject(pHeader);

    /* A token whose header claims an algorithm the library does not
     * implement must not be accepted on that basis. */
    xjwt_t jwt;
    XJWT_Init(&jwt, XJWT_ALG_HS256);
    CHECK(XJWT_AddHeader(&jwt, "{\"typ\":\"JWT\",\"alg\":\"none\"}", 26, XFALSE) == XSTDOK,
        "A header claiming no algorithm is accepted as a field");
    CHECK(XJWT_AddPayload(&jwt, "{\"sub\":\"x\"}", 11, XFALSE) == XSTDOK, "A payload is added");

    size_t nLength = 0;
    char *pToken = XJWT_Create(&jwt, g_secret, SECRET_LEN, &nLength);
    XJWT_Destroy(&jwt);

    if (pToken != NULL)
    {
        XSTATUS nStatus = XJWT_Parse(&jwt, pToken, nLength, g_secret, SECRET_LEN);
        CHECK(nStatus != XSTDOK || jwt.bVerified == XFALSE,
            "A token claiming an unimplemented algorithm is not verified");
        XJWT_Destroy(&jwt);
        free(pToken);
    }
    return 0;
}

static int XTest_segments(void)
{
    /* Segments can be supplied already encoded or in the clear, and both
     * routes have to produce the same token. */
    xjwt_t plain, encoded;
    XJWT_Init(&plain, XJWT_ALG_HS256);
    CHECK(XJWT_AddPayload(&plain, "{\"sub\":\"x\"}", 11, XFALSE) == XSTDOK, "A clear payload is added");

    size_t nPlainLen = 0;
    char *pPlainToken = XJWT_Create(&plain, g_secret, SECRET_LEN, &nPlainLen);
    CHECK(pPlainToken != NULL, "The clear payload signs");

    /* Reading a segment is asymmetric: the decoded form is a fresh copy the
     * caller frees, while the encoded form is the token's own buffer and is
     * only borrowed until the token is destroyed. */
    size_t nEncodedLen = 0;
    char *pBorrowed = XJWT_GetPayload(&plain, XFALSE, &nEncodedLen);
    CHECK(pBorrowed != NULL && nEncodedLen > 0, "The encoded payload is available");
    CHECK(pBorrowed == plain.pPayload, "The encoded form is borrowed from the token, not copied");

    char *pEncoded = (char*)malloc(nEncodedLen + 1);
    CHECK(pEncoded != NULL, "The borrowed segment is copied before the token goes away");
    memcpy(pEncoded, pBorrowed, nEncodedLen);
    pEncoded[nEncodedLen] = '\0';

    XJWT_Init(&encoded, XJWT_ALG_HS256);
    CHECK(XJWT_AddPayload(&encoded, pEncoded, nEncodedLen, XTRUE) == XSTDOK, "An encoded payload is added");

    size_t nEncodedTokenLen = 0;
    char *pEncodedToken = XJWT_Create(&encoded, g_secret, SECRET_LEN, &nEncodedTokenLen);
    CHECK(pEncodedToken != NULL, "The encoded payload signs");

    CHECK(nPlainLen == nEncodedTokenLen, "Both routes produce the same token length");
    CHECK(memcmp(pPlainToken, pEncodedToken, nPlainLen) == 0, "Both routes produce the same token");

    free(pEncoded);
    free(pEncodedToken);
    free(pPlainToken);
    XJWT_Destroy(&encoded);
    XJWT_Destroy(&plain);

    /* A token with no payload at all is still a token. */
    xjwt_t empty;
    XJWT_Init(&empty, XJWT_ALG_HS256);
    CHECK(XJWT_AddPayload(&empty, "{}", 2, XFALSE) == XSTDOK, "An empty object payload is added");

    size_t nEmptyLen = 0;
    char *pEmptyToken = XJWT_Create(&empty, g_secret, SECRET_LEN, &nEmptyLen);
    CHECK(pEmptyToken != NULL, "An empty payload signs");
    XJWT_Destroy(&empty);

    xjwt_t parsed;
    CHECK(XJWT_Parse(&parsed, pEmptyToken, nEmptyLen, g_secret, SECRET_LEN) == XSTDOK,
        "The empty payload token parses");
    CHECK(parsed.bVerified, "The empty payload token verifies");
    XJWT_Destroy(&parsed);
    free(pEmptyToken);

    /* A large payload signs and verifies like any other. */
    char sBig[4096];
    size_t nUsed = (size_t)snprintf(sBig, sizeof(sBig), "{\"data\":\"");
    while (nUsed < sizeof(sBig) - 16) sBig[nUsed++] = 'p';
    nUsed += (size_t)snprintf(&sBig[nUsed], sizeof(sBig) - nUsed, "\"}");

    xjwt_t big;
    XJWT_Init(&big, XJWT_ALG_HS256);
    CHECK(XJWT_AddPayload(&big, sBig, nUsed, XFALSE) == XSTDOK, "A large payload is added");

    size_t nBigLen = 0;
    char *pBigToken = XJWT_Create(&big, g_secret, SECRET_LEN, &nBigLen);
    CHECK(pBigToken != NULL, "A large payload signs");
    XJWT_Destroy(&big);

    CHECK(XJWT_Parse(&parsed, pBigToken, nBigLen, g_secret, SECRET_LEN) == XSTDOK,
        "The large payload token parses");
    CHECK(parsed.bVerified, "The large payload token verifies");

    size_t nBackLen = 0;
    char *pBack = XJWT_GetPayload(&parsed, XTRUE, &nBackLen);
    CHECK(pBack != NULL && nBackLen == nUsed, "The large payload comes back at its own length");
    CHECK(memcmp(pBack, sBig, nUsed) == 0, "The large payload comes back unchanged");
    free(pBack);

    XJWT_Destroy(&parsed);
    free(pBigToken);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(roundtrip),
    XTEST_CASE(wrong_secret),
    XTEST_CASE(tampering),
    XTEST_CASE(boundaries),
    XTEST_CASE(malformed),
    XTEST_CASE(algorithms),
    XTEST_CASE(segments)
)
