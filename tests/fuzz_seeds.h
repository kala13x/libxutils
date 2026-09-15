/* libxutils: fuzz seeds kept as source, so the corpus is generated, not stored. */
#ifndef XUTILS_FUZZ_SEEDS_H
#define XUTILS_FUZZ_SEEDS_H

#include "fuzz_parsers.h"

typedef struct xfuzz_seed_ {
    const char *pName;
    xfuzz_target_t eTarget;
    const char *pData;
    size_t nSize;
} xfuzz_seed_t;

/* Literals carry embedded zeroes, so the length comes from sizeof() and never
 * from strlen(): a binary frame must reach the parser whole. */
#define XFUZZ_SEED(name, target, data) {name, target, data, sizeof(data) - 1}

static const xfuzz_seed_t g_fuzzSeeds[] = {
    XFUZZ_SEED("json", XFUZZ_TARGET_JSON, "{\"key\":[1,true,\"text\"]}"),
    XFUZZ_SEED("http", XFUZZ_TARGET_HTTP,
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"),
    XFUZZ_SEED("http-empty", XFUZZ_TARGET_HTTP,
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nContent-Type: application/json\r\n\r\n"),
    XFUZZ_SEED("ws", XFUZZ_TARGET_WS, "\x82\x03" "a\0b"),
    XFUZZ_SEED("base64", XFUZZ_TARGET_BASE64, "YWJj"),
    XFUZZ_SEED("jwt", XFUZZ_TARGET_JWT, "eyJhbGciOiJIUzI1NiJ9.e30.signature"),
    XFUZZ_SEED("url", XFUZZ_TARGET_URL, "https://user:password@localhost:443/path?query=1")
};

#endif
