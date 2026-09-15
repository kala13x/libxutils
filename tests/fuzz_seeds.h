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
        "\x00\x00\x00\x09" "\x00\x00\x02\x00" "\x00\x02" "hi"),
    XFUZZ_SEED("unix-url", XFUZZ_TARGET_UNIX_URL, "unix:///var/run/app.sock:/v1/status"),
    XFUZZ_SEED("unix-url-query", XFUZZ_TARGET_UNIX_URL, "/tmp/x.sock?key=value"),
    /* The first byte splits the rest into a pattern and a subject. */
    XFUZZ_SEED("glob", XFUZZ_TARGET_GLOB, "\x06" "*.tar.gzlibxutils-2.8.tar.gz"),
    XFUZZ_SEED("glob-exact", XFUZZ_TARGET_GLOB, "\x05" "adminadministrator"),

    /* The stream targets take eight chunk sizes first, then the payload, so
     * the split points are fuzzed along with the bytes. */
    XFUZZ_SEED("http-stream", XFUZZ_TARGET_HTTP_STREAM,
        "\x04\x01\x20\x02\x08\x03\x10\x05"
        "GET / HTTP/1.1\r\nHost: x\r\nContent-Length: 2\r\n\r\nhi"
        "GET /second HTTP/1.1\r\nHost: x\r\n\r\n"),
    XFUZZ_SEED("mdtp-stream", XFUZZ_TARGET_MDTP_STREAM,
        "\x03\x01\x07\x02\x05\x09\x04\x06"
        "\x24\x00\x00\x00" "{\"version\":\"1.0\",\"packetType\":\"data\"}"
        "\x24\x00\x00\x00" "{\"version\":\"1.0\",\"packetType\":\"ping\"}"),
    XFUZZ_SEED("ws-stream", XFUZZ_TARGET_WS_STREAM,
        "\x02\x01\x03\x05\x01\x02\x04\x01"
        "\x82\x03" "a\0b" "\x81\x02" "hi"),

    XFUZZ_SEED("str-ops", XFUZZ_TARGET_STR_OPS, "\x01\x10" "one,two,three,four"),
    XFUZZ_SEED("str-ops-split", XFUZZ_TARGET_STR_OPS, "\x04\x00" "a,b,,c,"),

    XFUZZ_SEED("time-compact", XFUZZ_TARGET_TIME, "\x00" "2024031514304525"),
    XFUZZ_SEED("time-human", XFUZZ_TARGET_TIME, "\x01" "2024.03.15-14:30:45.25"),
    XFUZZ_SEED("time-iso", XFUZZ_TARGET_TIME, "\x04" "2024-03-15T14:30:45"),

    XFUZZ_SEED("path", XFUZZ_TARGET_PATH, "/one/two/three/file.tar.gz"),
    XFUZZ_SEED("hex", XFUZZ_TARGET_HEX, "\x00" "binary bytes to hex and back"),
    XFUZZ_SEED("json-roundtrip", XFUZZ_TARGET_JSON_ROUNDTRIP,
        "{\"a\":[1,2,3],\"b\":{\"c\":true,\"d\":null},\"e\":\"text\"}")
};

#endif
