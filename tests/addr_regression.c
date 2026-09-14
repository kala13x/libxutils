/* Length and port validation must not silently change a configured endpoint. */
#include "addr.h"
#include "xstd.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "addr_regression: %s\n", msg); return 1; } } while (0)

int main(void)
{
    xlink_t link;
    const char *pInvalid[] = {
        "ws://localhost:65536/", "ws://localhost:-1/", "ws://localhost:80oops/",
        "ws://localhost:18446744073709551616/", "ws://localhost:/"
    };
    for (size_t i = 0; i < sizeof(pInvalid) / sizeof(*pInvalid); i++)
        CHECK(XLink_Parse(&link, pInvalid[i]) == XSTDERR, "reject invalid port without narrowing or using a default");
    CHECK(XLink_Parse(&link, "ws://localhost:0/") == XSTDOK && link.nPort == 0, "preserve an explicit zero port");
    CHECK(XLink_Parse(&link, "wss://localhost/") == XSTDOK && link.nPort == 443, "preserve default TLS port");
    CHECK(XLink_Parse(&link, "redis://default:p%40ss%3Aword@localhost/3") == XSTDOK &&
        strcmp(link.sPass, "p@ss:word") == 0 && strcmp(link.sFile, "3") == 0, "preserve Redis ACL and escaped password");
    char sLong[4096];
    memset(sLong, 'x', sizeof(sLong) - 1);
    sLong[sizeof(sLong) - 1] = '\0';
    memcpy(sLong, "redis://:", 9);
    memcpy(sLong + 3000, "@localhost/0", 13);
    CHECK(XLink_Parse(&link, sLong) == XSTDERR, "reject oversized credentials without reading past the user buffer");
    memset(sLong, 'x', sizeof(sLong) - 1);
    memcpy(sLong, "ws://", 5);
    sLong[sizeof(sLong) - 1] = '\0';
    CHECK(XLink_Parse(&link, sLong) == XSTDERR, "reject oversized hostname");
    memcpy(sLong, "unix:///", 8);
    CHECK(XLink_Parse(&link, sLong) == XSTDERR, "reject truncated Unix socket path");
    puts("addr_regression: OK");
    return 0;
}
