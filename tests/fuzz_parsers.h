/* libxutils: parser fuzz targets shared by the harness and the corpus generator. */
#ifndef XUTILS_FUZZ_PARSERS_H
#define XUTILS_FUZZ_PARSERS_H

/* The first byte of every input selects the parser, so seeds and harness must
 * agree on the order. Appending a target keeps older corpus entries valid. */
typedef enum {
    XFUZZ_TARGET_JSON = 0,
    XFUZZ_TARGET_HTTP,
    XFUZZ_TARGET_WS,
    XFUZZ_TARGET_BASE64,
    XFUZZ_TARGET_JWT,
    XFUZZ_TARGET_URL,
    XFUZZ_TARGET_COUNT
} xfuzz_target_t;

#endif
