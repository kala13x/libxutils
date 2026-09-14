/*!
 * @file libxutils/tests/jwt_regression.c
 * @brief JWT parsing must validate the complete, length-delimited token.
 */

#include "jwt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "jwt_regression: %s\n", msg); return 1; } } while (0)

int main(void)
{
    const uint8_t sSecret[] = "local-regression-test-key";
    xjwt_t jwt;
    XJWT_Init(&jwt, XJWT_ALG_HS256);
    CHECK(XJWT_AddPayload(&jwt, "{\"sub\":\"device\"}", 16, XFALSE) == XSTDOK, "create a test payload");
    size_t nLength = 0;
    char *pToken = XJWT_Create(&jwt, sSecret, sizeof(sSecret) - 1, &nLength);
    CHECK(pToken != NULL, "create a signed token");
    XJWT_Destroy(&jwt);
    CHECK(XJWT_Parse(&jwt, pToken, nLength, sSecret, sizeof(sSecret) - 1) == XSTDOK && jwt.bVerified,
        "an existing valid HS256 token must remain accepted");
    XJWT_Destroy(&jwt);

    char *pExtended = (char*)malloc(nLength + 8);
    CHECK(pExtended != NULL, "allocate a token with trailing bytes");
    memcpy(pExtended, pToken, nLength);
    memcpy(pExtended + nLength, "suffix", 7);
    CHECK(XJWT_Parse(&jwt, pExtended, nLength + 6, sSecret, sizeof(sSecret) - 1) != XSTDOK && !jwt.bVerified,
        "a matching signature prefix must not accept trailing bytes");
    XJWT_Destroy(&jwt);
    memcpy(pExtended + nLength, ".extra", 7);
    CHECK(XJWT_Parse(&jwt, pExtended, nLength + 6, sSecret, sizeof(sSecret) - 1) != XSTDOK && !jwt.bVerified,
        "a fourth token segment must be rejected");
    XJWT_Destroy(&jwt);
    free(pExtended);

    char *pSlice = (char*)malloc(nLength);
    CHECK(pSlice != NULL, "allocate a token slice without a NUL terminator");
    memcpy(pSlice, pToken, nLength);
    CHECK(XJWT_Parse(&jwt, pSlice, nLength, sSecret, sizeof(sSecret) - 1) == XSTDOK && jwt.bVerified,
        "parsing must respect the supplied length without reading beyond it");
    XJWT_Destroy(&jwt);
    pSlice[nLength / 2] = '\0';
    CHECK(XJWT_Parse(&jwt, pSlice, nLength, sSecret, sizeof(sSecret) - 1) != XSTDOK && !jwt.bVerified,
        "embedded NUL bytes must not hide part of a token");
    XJWT_Destroy(&jwt);
    free(pSlice);
    free(pToken);

    CHECK(XJWT_GetAlg("HS256unexpected") == XJWT_ALG_INVALID, "algorithm names must match in full");
    CHECK(XJWT_GetAlg("RS256unexpected") == XJWT_ALG_INVALID, "RSA algorithm names must match in full");
    puts("jwt_regression: OK");
    return 0;
}
