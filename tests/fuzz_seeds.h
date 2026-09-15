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
    XFUZZ_SEED("url", XFUZZ_TARGET_URL, "https://user:password@localhost:443/path?query=1"),
    XFUZZ_SEED("mdtp", XFUZZ_TARGET_MDTP,
        "\x24\x00\x00\x00" "{\"version\":\"1.0\",\"packetType\":\"data\"}"),
    XFUZZ_SEED("mdtp-payload", XFUZZ_TARGET_MDTP,
        "\x3a\x00\x00\x00" "{\"version\":\"1.0\",\"payload\":{\"payloadSize\":3}}" "abc"),
    XFUZZ_SEED("rtp", XFUZZ_TARGET_RTP,
        "\x80\x60\x00\x2a\x00\x00\x10\x00\x00\x00\x00\x07"
        "\x12\x34\x56\xa1" "\x00\x03" "abc"),
    XFUZZ_SEED("rtp-csrc", XFUZZ_TARGET_RTP,
        "\x81\x60\x00\x01\x00\x00\x00\x00\x00\x00\x00\x01"
        "\x00\x00\x00\x09" "\x00\x00\x02\x00" "\x00\x02" "hi")
};

#endif
