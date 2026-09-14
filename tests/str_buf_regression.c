/* libxutils string helpers and byte buffers: the building blocks under
 * every config path, log line and protocol packet. Locks the bounded-copy
 * guarantees (truncation, NUL termination) and buffer growth/advance
 * semantics that the PTY TX queue depends on. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "str.h"
#include "buf.h"

#include "test.h"

static int XTest_boundaries(void)
{
    /* ---- bounded copies always NUL-terminate and never overflow ---- */
    char sSmall[8];
    memset(sSmall, 0x7f, sizeof(sSmall));

    size_t nLen = xstrncpy(sSmall, sizeof(sSmall), "0123456789abcdef");
    CHECK(nLen < sizeof(sSmall), "xstrncpy truncates");
    CHECK(sSmall[sizeof(sSmall) - 1] == '\0' || strlen(sSmall) < sizeof(sSmall), "xstrncpy terminates");
    CHECK(strncmp(sSmall, "0123456", 7) == 0, "xstrncpy content");

    CHECK(xstrncpy(sSmall, sizeof(sSmall), "") == 0, "xstrncpy empty");
    CHECK(sSmall[0] == '\0', "xstrncpy empty terminates");
    CHECK(xstrncpy(NULL, 0, "x") == 0, "xstrncpy NULL destination");

    nLen = xstrncpyf(sSmall, sizeof(sSmall), "%s-%d", "abc", 42);
    CHECK(nLen == 6 && strcmp(sSmall, "abc-42") == 0, "xstrncpyf format");

    nLen = xstrncpyf(sSmall, sizeof(sSmall), "%s", "0123456789");
    CHECK(strlen(sSmall) < sizeof(sSmall), "xstrncpyf truncates");

    /* Length-limited copy from a non-terminated source */
    char sFrom[4] = {'w', 'i', 'r', 'e'};
    char sTo[16];
    nLen = xstrncpys(sTo, sizeof(sTo), sFrom, sizeof(sFrom));
    CHECK(nLen == 4 && strcmp(sTo, "wire") == 0, "xstrncpys bounded source");

    /* ---- comparison helpers: TRUE means equal ---- */
    CHECK(xstrcmp("abc", "abc") == XTRUE, "xstrcmp equal");
    CHECK(xstrcmp("abc", "abd") == XFALSE, "xstrcmp differs");
    CHECK(xstrncmp("abcdef", "abc", 3) == XTRUE, "xstrncmp prefix");
    CHECK(xstrused("x") == XTRUE, "xstrused non-empty");
    CHECK(xstrused("") == XFALSE, "xstrused empty");
    CHECK(xstrused(NULL) == XFALSE, "xstrused NULL");

    /* ---- substring search ---- */
    CHECK(xstrsrc("wss://relay.example/ws?x=1", "?") == 22, "xstrsrc offset");
    CHECK(xstrsrc("plain", "?") < 0, "xstrsrc missing");
    CHECK(xstrsrc("aaaa", "aa") == 0, "xstrsrc first match");

    /* ---- tokenizer ---- */
    char sTok[32];
    xstrncpy(sTok, sizeof(sTok), "host:443:extra");
    char *pSave = NULL;
    char *pTok = xstrtok(sTok, ":", &pSave);
    CHECK(pTok != NULL && strcmp(pTok, "host") == 0, "first token");
    pTok = xstrtok(NULL, ":", &pSave);
    CHECK(pTok != NULL && strcmp(pTok, "443") == 0, "second token");
    pTok = xstrtok(NULL, ":", &pSave);
    CHECK(pTok != NULL && strcmp(pTok, "extra") == 0, "third token");
    CHECK(xstrtok(NULL, ":", &pSave) == NULL, "tokens exhausted");

    /* ---- byte buffers ---- */
    xbyte_buffer_t buf;
    CHECK(XByteBuffer_Init(&buf, XSTDNON, XFALSE) >= 0, "buffer init");
    CHECK(buf.nUsed == 0, "fresh buffer empty");
    CHECK(!XByteBuffer_HasData(&buf), "fresh buffer has no data");
    CHECK(XByteBuffer_Add(&buf, NULL, 1) == 0, "buffer rejects NULL add");
    CHECK(XByteBuffer_Add(&buf, (const uint8_t*)"x", 0) == 0, "buffer rejects empty add");

    CHECK(XByteBuffer_Add(&buf, (const uint8_t*)"hello", 5) > 0, "buffer add");
    CHECK(buf.nUsed == 5, "buffer used");
    CHECK(XByteBuffer_AddByte(&buf, '!') > 0, "buffer add byte");
    CHECK(buf.nUsed == 6 && memcmp(buf.pData, "hello!", 6) == 0, "buffer content");
    CHECK(XByteBuffer_GetByte(&buf, 0) == 'h' && XByteBuffer_GetByte(&buf, 99) == 0, "buffer byte access");
    CHECK(XByteBuffer_Insert(&buf, 5, (const uint8_t*)", world", 7) > 0, "buffer insert");
    CHECK(buf.nUsed == 13 && memcmp(buf.pData, "hello, world!", 13) == 0, "buffer inserted content");
    CHECK(XByteBuffer_Remove(&buf, 5, 7) == 7 && memcmp(buf.pData, "hello!", 6) == 0, "buffer remove middle");
    CHECK(XByteBuffer_Remove(&buf, 99, 1) == 0, "buffer remove out of range");
    CHECK(XByteBuffer_Terminate(&buf, 5) == XSTDOK && buf.nUsed == 5 && strcmp((char*)buf.pData, "hello") == 0,
        "buffer terminate");
    CHECK(XByteBuffer_AddFmt(&buf, "-%d", 42) > 0 && strcmp((char*)buf.pData, "hello-42") == 0, "buffer add format");

    /* Advance models partial PTY writes: drops consumed bytes from the head */
    CHECK(XByteBuffer_Advance(&buf, 2) > 0, "buffer advance");
    CHECK(buf.nUsed == 6 && memcmp(buf.pData, "llo-42", 6) == 0, "advance content");

    /* Advancing everything empties without invalidating the buffer */
    XByteBuffer_Advance(&buf, buf.nUsed);
    CHECK(buf.nUsed == 0, "advance to empty");
    CHECK(XByteBuffer_Add(&buf, (const uint8_t*)"again", 5) > 0, "reuse after drain");
    CHECK(buf.nUsed == 5, "reused size");

    /* Reset keeps the allocation, Clear releases it */
    XByteBuffer_Reset(&buf);
    CHECK(buf.nUsed == 0, "reset empties");
    CHECK(XByteBuffer_Add(&buf, (const uint8_t*)"x", 1) > 0, "add after reset");
    XByteBuffer_Clear(&buf);
    CHECK(buf.pData == NULL && buf.nUsed == 0, "clear releases");

    xbyte_buffer_t *pHeap = XByteBuffer_New(4, XTRUE);
    CHECK(pHeap != NULL, "heap buffer new");
    CHECK(XByteBuffer_Add(pHeap, (const uint8_t*)"owned", 5) > 0, "heap buffer add");
    XByteBuffer_Free(&pHeap);
    CHECK(pHeap == NULL, "heap buffer free clears pointer");

    uint8_t *pDup = XByteData_Dup((const uint8_t*)"abc", 3);
    CHECK(pDup != NULL && strcmp((char*)pDup, "abc") == 0, "byte data duplicate");
    free(pDup);
    CHECK(XByteData_Dup(NULL, 3) == NULL && XByteData_Dup((const uint8_t*)"x", 0) == NULL, "byte data duplicate invalid inputs");

    /* Growth: stream 256 KB through in odd-sized chunks, verify content */
    CHECK(XByteBuffer_Init(&buf, XSTDNON, XFALSE) >= 0, "growth buffer init");

    uint8_t sChunk[331];
    size_t nTotal = 0;
    while (nTotal < 256 * 1024)
    {
        for (size_t i = 0; i < sizeof(sChunk); i++) sChunk[i] = (uint8_t)((nTotal + i) % 251);

        CHECK(XByteBuffer_Add(&buf, sChunk, sizeof(sChunk)) > 0, "growth add");
        nTotal += sizeof(sChunk);
    }

    CHECK(buf.nUsed == nTotal, "growth size");
    for (size_t i = 0; i < nTotal; i += 7919) CHECK(buf.pData[i] == (uint8_t)(i % 251), "growth content spot check");

    XByteBuffer_Clear(&buf);

    /* Binary safety: NUL and 0x1A bytes (the CRLF/EOF hazards) survive */
    CHECK(XByteBuffer_Init(&buf, XSTDNON, XFALSE) >= 0, "binary buffer init");
    const uint8_t binary[] = {0x00, 0x1a, 0x0d, 0x0a, 0xff, 0x00};
    CHECK(XByteBuffer_Add(&buf, binary, sizeof(binary)) > 0, "binary add");
    CHECK(buf.nUsed == sizeof(binary) && memcmp(buf.pData, binary, sizeof(binary)) == 0, "binary bytes intact");
    XByteBuffer_Clear(&buf);

    xbyte_buffer_t src;
    xbyte_buffer_t dst;
    CHECK(XByteBuffer_Init(&src, 0, XFALSE) >= 0 && XByteBuffer_Init(&dst, 0, XFALSE) >= 0, "ownership buffers init");
    CHECK(XByteBuffer_Add(&src, (const uint8_t*)"move", 4) > 0, "ownership source add");
    CHECK(XByteBuffer_Own(&dst, &src) > 0, "buffer ownership move");
    CHECK(src.pData == NULL && dst.nUsed == 4 && memcmp(dst.pData, "move", 4) == 0, "buffer ownership state");
    XByteBuffer_Clear(&src);
    XByteBuffer_Clear(&dst);

    puts("str_buf_regression: OK");
    return 0;
}

XTEST_MAIN(XTEST_CASE(boundaries))
