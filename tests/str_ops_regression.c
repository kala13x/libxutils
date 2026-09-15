/* libxutils: the string helper families the other regressions do not reach.
 *
 * Splitting, matching, cutting, searching, case conversion and colour
 * formatting are the routines config parsing and log rendering are built
 * on, so each is checked on its boundaries: empty inputs, delimiters at
 * the very start and end, patterns that match nothing, and destinations
 * one byte too small.
 */

#include "test.h"
#include "str.h"
#include "array.h"
#include <ctype.h>

static int XTest_split(void)
{
    /* A plain split drops the delimiters and keeps every field. */
    xarray_t *pTokens = xstrsplit("a,b,c", ",");
    CHECK(pTokens != NULL && XArray_Used(pTokens) == 3, "A plain split yields one token per field");
    CHECK(strcmp((char*)XArray_GetData(pTokens, 0), "a") == 0, "The first field is intact");
    CHECK(strcmp((char*)XArray_GetData(pTokens, 2), "c") == 0, "The last field is intact");
    XArray_Destroy(pTokens);

    /* A string with no delimiter is a single field, not a failure. */
    pTokens = xstrsplit("single", ",");
    CHECK(pTokens != NULL && XArray_Used(pTokens) == 1, "A string without the delimiter is one field");
    CHECK(strcmp((char*)XArray_GetData(pTokens, 0), "single") == 0, "The single field is the whole string");
    XArray_Destroy(pTokens);

    CHECK(xstrsplit("", ",") == NULL, "An empty string does not split");
    CHECK(xstrsplit(NULL, ",") == NULL, "A missing string does not split");

    /* Leading, trailing and repeated delimiters. */
    pTokens = xstrsplit(",a,,b,", ",");
    CHECK(pTokens != NULL, "A string with empty fields splits");
    size_t nUsed = XArray_Used(pTokens);
    CHECK(nUsed == 2, "Empty fields are dropped by default");
    CHECK(strcmp((char*)XArray_GetData(pTokens, 0), "a") == 0, "The leading delimiter is consumed");
    CHECK(strcmp((char*)XArray_GetData(pTokens, 1), "b") == 0, "Repeated delimiters are consumed");
    XArray_Destroy(pTokens);

    /* The explicit form can keep empty fields and the delimiters. */
    xarray_t tokens;
    XArray_InitPool(&tokens, 0, 0, XFALSE);
    CHECK(xstrsplita("a,,b", ",", &tokens, XFALSE, XTRUE) > 0, "An empty-preserving split runs");
    CHECK(XArray_Used(&tokens) == 3, "Empty fields are preserved when asked for");
    CHECK(strcmp((char*)XArray_GetData(&tokens, 1), "") == 0, "The empty field is empty");
    XArray_Destroy(&tokens);

    XArray_InitPool(&tokens, 0, 0, XFALSE);
    CHECK(xstrsplita("a,b", ",", &tokens, XTRUE, XFALSE) > 0, "A delimiter-preserving split runs");
    CHECK(XArray_Used(&tokens) == 3, "The delimiter becomes a token of its own");
    CHECK(strcmp((char*)XArray_GetData(&tokens, 0), "a") == 0, "The first field is intact");
    CHECK(strcmp((char*)XArray_GetData(&tokens, 1), ",") == 0, "The delimiter is kept as its own token");
    CHECK(strcmp((char*)XArray_GetData(&tokens, 2), "b") == 0, "The second field is intact");
    XArray_Destroy(&tokens);

    /* A multi character delimiter is matched as a unit. */
    pTokens = xstrsplit("a::b::c", "::");
    CHECK(pTokens != NULL && XArray_Used(pTokens) == 3, "A multi character delimiter splits");
    CHECK(strcmp((char*)XArray_GetData(pTokens, 1), "b") == 0, "The middle field is intact");
    XArray_Destroy(pTokens);

    /* Splitting on newlines is what the line oriented callers use. */
    pTokens = xstrsplite("one\ntwo\nthree", "\n");
    CHECK(pTokens != NULL && XArray_Used(pTokens) == 3, "Lines split on the newline");
    XArray_Destroy(pTokens);

    /* The delimiter-keeping split interleaves the separators as tokens. */
    pTokens = xstrsplitd("x-y-z", "-");
    CHECK(pTokens != NULL && XArray_Used(pTokens) == 5, "The fields and delimiters are all tokens");
    CHECK(strcmp((char*)XArray_GetData(pTokens, 1), "-") == 0, "A delimiter sits between two fields");
    XArray_Destroy(pTokens);
    return 0;
}

static int XTest_match(void)
{
    /* Wildcard matching: a star spans any run, a question mark one byte. */
    CHECK(xstrmatch("file.txt", 8, "*.txt") == XTRUE, "A trailing wildcard matches a suffix");
    CHECK(xstrmatch("file.txt", 8, "file.*") == XTRUE, "A leading wildcard matches a prefix");
    CHECK(xstrmatch("file.txt", 8, "*") == XTRUE, "A bare wildcard matches anything");
    CHECK(xstrmatch("file.txt", 8, "file.txt") == XTRUE, "An exact pattern matches itself");
    CHECK(xstrmatch("file.txt", 8, "*.log") == XFALSE, "A non-matching suffix is rejected");
    CHECK(xstrmatch("file.txt", 8, "file") == XFALSE, "A prefix alone is not a match");
    CHECK(xstrmatch("file.txt", 8, "f?le.txt") == XTRUE, "A single character wildcard matches one byte");
    CHECK(xstrmatch("file.txt", 8, "f?.txt") == XFALSE, "A single character wildcard does not span a run");
    CHECK(xstrmatch("", 0, "*") == XFALSE, "An empty subject matches nothing, not even a bare wildcard");
    CHECK(xstrmatch("abc", 3, "") == XFALSE, "An empty pattern matches nothing");

    /* A wildcard in the middle. */
    CHECK(xstrmatch("prefix-middle-suffix", 20, "prefix*suffix") == XTRUE, "A middle wildcard spans the gap");
    CHECK(xstrmatch("prefix-suffix", 13, "prefix*suffix") == XTRUE, "A middle wildcard can span nothing");
    CHECK(xstrmatch("prefixsuffixes", 14, "prefix*suffix") == XFALSE, "A pattern must reach the end");

    /* The bounded form respects its length instead of the terminator. */
    const char raw[] = {'a', 'b', 'c', 'd'};
    CHECK(xstrnmatch(raw, 3, "abc", 3) == XTRUE, "The bounded form matches its slice");
    CHECK(xstrnmatch(raw, 4, "abc", 3) == XFALSE, "The bounded form does not match a longer slice");

    /* The multi-pattern form takes a delimited list of patterns. */
    CHECK(xstrmatchm("notes.txt", 9, "*.log;*.txt", ";") == XTRUE, "Any pattern in the list can match");
    CHECK(xstrmatchm("notes.md", 8, "*.log;*.txt", ";") == XFALSE, "A list that matches nothing is rejected");
    CHECK(xstrmatchm("notes.txt", 9, "*.txt", ";") == XTRUE, "A single pattern list still matches");
    return 0;
}

static int XTest_compare(void)
{
    CHECK(xstrcmp("same", "same") == XTRUE, "Identical strings compare equal");
    CHECK(xstrcmp("same", "different") == XFALSE, "Different strings do not compare equal");
    CHECK(xstrcmp("", "") == XTRUE, "Empty strings compare equal");
    CHECK(xstrcmp(NULL, "x") == XFALSE, "A missing string never compares equal");
    CHECK(xstrcmp("x", NULL) == XFALSE, "A missing comparand never compares equal");

    CHECK(xstrncmp("prefix-and-more", "prefix", 6) == XTRUE, "A bounded compare matches a prefix");
    CHECK(xstrncmp("prefix", "prefiy", 6) == XFALSE, "A bounded compare sees the differing byte");
    /* A zero length is a rejected argument here, not a vacuous match. */
    CHECK(xstrncmp("abc", "abc", 0) == XFALSE, "A zero length compare is rejected");
    CHECK(xstrncmp(NULL, "abc", 3) == XFALSE, "A missing string never compares equal");
    CHECK(xstrncmp("abc", NULL, 3) == XFALSE, "A missing comparand never compares equal");
    CHECK(xstrncasecmp(NULL, "abc", 3) == XFALSE, "A missing string never compares equal ignoring case");

    CHECK(xstrncasecmp("MiXeD", "mixed", 5) == XTRUE, "A case insensitive compare ignores case");
    CHECK(xstrncasecmp("MiXeD", "mixes", 5) == XFALSE, "A case insensitive compare still sees a difference");

    /* The length-aware form compares both slices, not just the prefix. */
    CHECK(xstrncmpn("abc", 3, "abc", 3) == XTRUE, "Equal slices compare equal");
    CHECK(xstrncmpn("abc", 3, "ab", 2) == XFALSE, "A shorter slice is not equal");
    CHECK(xstrncmpn("ab", 2, "abc", 3) == XFALSE, "A longer slice is not equal");
    return 0;
}

static int XTest_search(void)
{
    const char haystack[] = "the quick brown fox jumps";

    CHECK(xstrsrc(haystack, "quick") == 4, "A hit reports its offset");
    CHECK(xstrsrc(haystack, "the") == 0, "A hit at the start reports zero");
    CHECK(xstrsrc(haystack, "jumps") == 20, "A hit at the end reports its offset");
    CHECK(xstrsrc(haystack, "missing") < 0, "A miss is reported as negative");
    CHECK(xstrsrc(haystack, "") < 0, "An empty needle is not a hit");
    CHECK(xstrsrc(NULL, "x") < 0, "A missing haystack is not a hit");

    /* The positional form starts its scan at the given offset. */
    const char repeated[] = "aXbXcX";
    CHECK(xstrsrcp(repeated, "X", 0) == 1, "The first hit is found from the start");
    CHECK(xstrsrcp(repeated, "X", 2) == 1, "A scan from an offset reports the hit relative to that offset");
    CHECK(xstrsrcp(repeated, "X", 6) < 0, "A scan past the end finds nothing");

    /* The bounded form does not run past its length. */
    const char unterminated[] = {'f', 'i', 'n', 'd', 'm', 'e'};
    CHECK(xstrsrcb(unterminated, sizeof(unterminated), "me") == 4, "The bounded search finds a hit inside its slice");
    CHECK(xstrsrcb(unterminated, 4, "me") < 0, "The bounded search does not look past its slice");
    CHECK(xstrnsrc(unterminated, sizeof(unterminated), "find", 0) == 0, "The bounded positional search finds a hit");
    CHECK(xstrnsrc(unterminated, sizeof(unterminated), "nope", 0) < 0, "The bounded positional search reports a miss");
    return 0;
}

static int XTest_replace(void)
{
    /* Replacement builds a new string and leaves the original alone. */
    const char original[] = "one two one two";
    char *pResult = xstrrep(original, "one", "1");
    CHECK(pResult != NULL && strcmp(pResult, "1 two 1 two") == 0, "Every occurrence is replaced");
    CHECK(strcmp(original, "one two one two") == 0, "The original is left untouched");
    free(pResult);

    /* Growing and shrinking replacements. */
    pResult = xstrrep("aaa", "a", "bb");
    CHECK(pResult != NULL && strcmp(pResult, "bbbbbb") == 0, "A longer replacement grows the result");
    free(pResult);

    pResult = xstrrep("aaa", "aa", "a");
    CHECK(pResult != NULL && strcmp(pResult, "aa") == 0, "A shorter replacement shrinks the result");
    free(pResult);

    /* A replacement with nothing removes the needle. */
    pResult = xstrrep("keep-drop-keep", "drop", "");
    CHECK(pResult != NULL && strcmp(pResult, "keep--keep") == 0, "An empty replacement removes the needle");
    free(pResult);

    /* A needle that is not there leaves the string as it was. */
    pResult = xstrrep("unchanged", "absent", "x");
    CHECK(pResult != NULL && strcmp(pResult, "unchanged") == 0, "An absent needle changes nothing");
    free(pResult);

    CHECK(xstrrep(NULL, "a", "b") == NULL, "A missing source is rejected");
    CHECK(xstrrep("abc", NULL, "b") == NULL, "A missing needle is rejected");

    /* The bounded form writes into a caller buffer and truncates safely. */
    char sBuffer[32];
    CHECK(xstrnrep(sBuffer, sizeof(sBuffer), "one two", "one", "1") > 0, "The bounded replacement runs");
    CHECK(strcmp(sBuffer, "1 two") == 0, "The bounded replacement produces the same result");

    char sTiny[6];
    memset(sTiny, 0x5a, sizeof(sTiny));
    xstrnrep(sTiny, sizeof(sTiny), "aaaaaaaaaa", "a", "bb");
    CHECK(strlen(sTiny) < sizeof(sTiny), "A short destination truncates rather than overflowing");
    return 0;
}

static int XTest_cut(void)
{
    /* Cutting between two markers returns a pointer into the caller's own
     * buffer and terminates it there, so the subject is modified in place. */
    char subject[] = "prefix[payload]suffix";
    char *pCut = xstrcut(subject, "[", "]");
    CHECK(pCut != NULL && strcmp(pCut, "payload") == 0, "The text between the markers is returned");
    CHECK(pCut == &subject[7], "The cut points into the caller buffer rather than a copy");
    CHECK(strcmp(subject, "prefix[payload") == 0, "The end marker was overwritten with a terminator");

    /* A missing start marker means there is nothing to cut. */
    char missing[] = "prefix[payload]suffix";
    CHECK(xstrcut(missing, "{", "}") == NULL, "An absent start marker yields nothing");

    /* A missing end marker returns everything after the start marker. */
    char openEnded[] = "prefix[payload]suffix";
    char *pRest = xstrcut(openEnded, "[", "}");
    CHECK(pRest != NULL && strcmp(pRest, "payload]suffix") == 0, "An absent end marker cuts to the end");

    /* The bounded form writes into a caller buffer. */
    char sBuffer[32];
    char bounded[] = "prefix[payload]suffix";
    CHECK(xstrncuts(sBuffer, sizeof(sBuffer), bounded, "[", "]") == 7, "The bounded cut reports its length");
    CHECK(strcmp(sBuffer, "payload") == 0, "The bounded cut produces the same text");

    /* Cutting by position and length. */
    pCut = xstracut("0123456789", 3, 4);
    CHECK(pCut != NULL && strcmp(pCut, "3456") == 0, "A positional cut returns the requested slice");
    free(pCut);

    CHECK(xstracut("0123456789", 20, 4) == NULL, "A position past the end yields nothing");
    CHECK(xstracut("0123456789", 3, 0) == NULL, "A zero length cut yields nothing");

    CHECK(xstrncut(sBuffer, sizeof(sBuffer), "0123456789", 2, 3) == 3, "The bounded positional cut reports its length");
    CHECK(strcmp(sBuffer, "234") == 0, "The bounded positional cut returns the slice");

    /* A slice longer than what remains is clamped to the remainder. */
    CHECK(xstrncut(sBuffer, sizeof(sBuffer), "abc", 1, 100) > 0, "An oversized request is clamped");
    CHECK(strcmp(sBuffer, "bc") == 0, "The clamped slice is the remainder");
    return 0;
}

static int XTest_tokens(void)
{
    /* The reentrant tokenizer walks the fields in order. */
    char subject[] = "alpha:beta:gamma";
    char *pContext = NULL;
    char *pToken = xstrtok(subject, ":", &pContext);
    CHECK(pToken != NULL && strcmp(pToken, "alpha") == 0, "The first token is returned");
    pToken = xstrtok(NULL, ":", &pContext);
    CHECK(pToken != NULL && strcmp(pToken, "beta") == 0, "The second token is returned");
    pToken = xstrtok(NULL, ":", &pContext);
    CHECK(pToken != NULL && strcmp(pToken, "gamma") == 0, "The last token is returned");
    CHECK(xstrtok(NULL, ":", &pContext) == NULL, "There is nothing after the last token");

    /* The indexed form picks one field out without walking. */
    char sField[32];
    /* The offset is a byte position, and the return value is the position
     * just past the token that was read. */
    CHECK(xstrntok(sField, sizeof(sField), "a:b:c", 0, ":") == 2, "The first token ends at the delimiter");
    CHECK(strcmp(sField, "a") == 0, "The first token is correct");
    CHECK(xstrntok(sField, sizeof(sField), "a:b:c", 2, ":") == 4, "The next token ends at the next delimiter");
    CHECK(strcmp(sField, "b") == 0, "The token at that offset is correct");
    CHECK(xstrntok(sField, sizeof(sField), "a:b:c", 4, ":") == 0, "The last token reports no following delimiter");
    CHECK(strcmp(sField, "c") == 0, "The last token is still written out");
    CHECK(xstrntok(sField, sizeof(sField), "a:b:c", 5, ":") < 0, "An offset past the end reports a miss");
    return 0;
}

static int XTest_casing(void)
{
    /* In place conversion covers the whole string. */
    char sMixed[] = "MiXeD 123!";
    CHECK(xstrcase(sMixed, XSTR_LOWER) == strlen("MiXeD 123!"), "Lowercasing reports the converted length");
    CHECK(strcmp(sMixed, "mixed 123!") == 0, "Letters lowercase and everything else is left alone");

    CHECK(xstrcase(sMixed, XSTR_UPPER) > 0, "Uppercasing runs");
    CHECK(strcmp(sMixed, "MIXED 123!") == 0, "Letters uppercase and everything else is left alone");

    char sEmpty[] = "";
    CHECK(xstrcase(sEmpty, XSTR_LOWER) == 0, "An empty string converts to nothing");
    CHECK(xstrcase(NULL, XSTR_LOWER) == 0, "A missing string converts to nothing");

    /* The copying forms leave the source alone. */
    const char source[] = "Source Text";
    char sDest[32];
    CHECK(xstrncase(sDest, sizeof(sDest), XSTR_LOWER, source) > 0, "The copying form runs");
    CHECK(strcmp(sDest, "source text") == 0, "The copy is converted");
    CHECK(strcmp(source, "Source Text") == 0, "The source is untouched");

    CHECK(xstrncases(sDest, sizeof(sDest), XSTR_UPPER, source, 6) == 6, "The bounded copying form reports its length");
    CHECK(strcmp(sDest, "SOURCE") == 0, "The bounded copy stops at its length");

    /* A destination one byte too small truncates. */
    char sTiny[4];
    CHECK(xstrncase(sTiny, sizeof(sTiny), XSTR_LOWER, source) == 3, "A short destination truncates");
    CHECK(strcmp(sTiny, "sou") == 0, "The truncated copy is still terminated");

    /* The allocating forms hand back a converted duplicate. */
    char *pLower = xstracase(source, XSTR_LOWER);
    CHECK(pLower != NULL && strcmp(pLower, "source text") == 0, "The allocating form converts");
    free(pLower);

    char *pUpper = xstracasen(source, XSTR_UPPER, 6);
    CHECK(pUpper != NULL && strcmp(pUpper, "SOURCE") == 0, "The bounded allocating form converts its slice");
    free(pUpper);
    return 0;
}

static int XTest_fill_and_trim(void)
{
    /* Filling writes exactly the requested run and terminates it. */
    char sBuffer[16];
    memset(sBuffer, 0x5a, sizeof(sBuffer));
    CHECK(xstrnfill(sBuffer, sizeof(sBuffer), 5, '-') == 5, "The fill reports its length");
    CHECK(strcmp(sBuffer, "-----") == 0, "The fill writes the requested run");
    CHECK(sBuffer[6] == 0x5a, "The fill does not write past its terminator");

    /* A fill longer than the buffer is clamped. */
    CHECK(xstrnfill(sBuffer, sizeof(sBuffer), 100, '*') == sizeof(sBuffer) - 1, "An oversized fill is clamped");
    CHECK(strlen(sBuffer) == sizeof(sBuffer) - 1, "The clamped fill is still terminated");
    CHECK(xstrnfill(NULL, 8, 4, '-') == 0, "A missing destination fills nothing");
    CHECK(xstrnfill(sBuffer, 0, 4, '-') == 0, "A zero sized destination fills nothing");

    /* The pointer-returning form writes into static storage the caller
     * borrows rather than owns, so it is never freed and never reentrant. */
    char *pFilled = xstrfill(6, '#');
    CHECK(pFilled != NULL && strcmp(pFilled, "######") == 0, "The fill honours the requested character");
    CHECK(xstrfill(3, '=') == pFilled, "The buffer is static storage, not a fresh allocation");
    CHECK(strcmp(pFilled, "===") == 0, "A second call overwrites the first result");
    CHECK(xstrfill(0, '#')[0] == '\0', "A zero length fill yields an empty string");

    /* Removing a slice closes the gap. */
    char sSubject[] = "keep-cut-keep";
    CHECK(xstrnrm(sSubject, 4, 4) > 0, "The removal runs");
    CHECK(strcmp(sSubject, "keep-keep") == 0, "The removed slice is gone and the gap is closed");

    /* Nulling a string empties it without touching its capacity. */
    char sNull[16] = "content";
    xstrnul(sNull);
    CHECK(sNull[0] == '\0', "Nulling empties the string");
    CHECK(!xstrused(sNull), "An emptied string is not usable");

    char sRange[16];
    memset(sRange, 'x', sizeof(sRange));
    xstrnull(sRange, sizeof(sRange));
    for (size_t i = 0; i < sizeof(sRange); i++) CHECK(sRange[i] == '\0', "Nulling a range clears every byte");

    CHECK(xstrused("x") == XTRUE, "A non-empty string is usable");
    CHECK(xstrused("") == XFALSE, "An empty string is not usable");
    CHECK(xstrused(NULL) == XFALSE, "A missing string is not usable");
    return 0;
}

static int XTest_duplicate(void)
{
    /* Duplication makes an independent copy. */
    const char source[] = "duplicate me";
    char *pCopy = xstrdup(source);
    CHECK(pCopy != NULL && strcmp(pCopy, source) == 0, "The duplicate matches the source");
    CHECK(pCopy != source, "The duplicate is a separate allocation");
    free(pCopy);

    CHECK(xstrdup(NULL) == NULL, "A missing source duplicates to nothing");

    pCopy = xstrdup("");
    CHECK(pCopy != NULL && pCopy[0] == '\0', "An empty source duplicates to a terminated empty string");
    free(pCopy);

    /* The formatting duplicates build their result from a format string. */
    pCopy = xstracpy("%s-%d", "value", 42);
    CHECK(pCopy != NULL && strcmp(pCopy, "value-42") == 0, "The formatting duplicate formats");
    free(pCopy);

    size_t nLength = 0;
    pCopy = xstracpyn(&nLength, "%s", "measured");
    CHECK(pCopy != NULL && nLength == strlen("measured"), "The measured duplicate reports its length");
    CHECK(strcmp(pCopy, "measured") == 0, "The measured duplicate has the right contents");
    free(pCopy);

    /* A plain allocation is zero initialised enough to be a valid string. */
    char *pAllocated = xstralloc(16);
    CHECK(pAllocated != NULL, "A raw allocation succeeds");
    CHECK(pAllocated[0] == '\0', "A raw allocation is already a valid empty string");
    free(pAllocated);

    /* The pointer-returning copy writes through the caller's pointer. */
    char *pTarget = NULL;
    CHECK(xstrxcpyf(&pTarget, "%s:%d", "port", 8080) > 0, "The indirect copy runs");
    CHECK(pTarget != NULL && strcmp(pTarget, "port:8080") == 0, "The indirect copy has the right contents");
    free(pTarget);

    pTarget = xstrxcpy("%d", 7);
    CHECK(pTarget != NULL && strcmp(pTarget, "7") == 0, "The allocating format copy works");
    free(pTarget);
    return 0;
}

static int XTest_concat(void)
{
    /* Concatenation appends and keeps the destination terminated. */
    char sBuffer[32];
    xstrnul(sBuffer);
    CHECK(xstrncat(sBuffer, sizeof(sBuffer), "%s", "first") > 0, "The first append runs");
    CHECK(xstrncat(sBuffer, sizeof(sBuffer), "-%s", "second") > 0, "The second append runs");
    CHECK(strcmp(sBuffer, "first-second") == 0, "The appends accumulate in order");

    /* Appending past the capacity truncates instead of overflowing. */
    char sTiny[8];
    xstrnul(sTiny);
    xstrncat(sTiny, sizeof(sTiny), "%s", "0123456789abcdef");
    CHECK(strlen(sTiny) < sizeof(sTiny), "An oversized append truncates");

    /* The available-space form takes the remaining room, not the total. */
    char sRoom[32];
    size_t nUsed = xstrncpy(sRoom, sizeof(sRoom), "base");
    CHECK(nUsed == 4, "The base is copied");
    CHECK(xstrncatf(&sRoom[nUsed], sizeof(sRoom) - nUsed, "%s", "-more") > 0, "The available-space append runs");
    CHECK(strcmp(sRoom, "base-more") == 0, "The available-space append lands after the base");
    return 0;
}

static int XTest_colors(void)
{
    /* Colour helpers build escape sequences of the documented shape. */
    char sColor[64];
    CHECK(xstrnrgb(sColor, sizeof(sColor), 255, 128, 0) > 0, "An rgb sequence is built");
    CHECK(sColor[0] == '\x1B', "The rgb sequence starts with an escape");
    CHECK(strstr(sColor, "255") && strstr(sColor, "128"), "The rgb components are present");

    CHECK(xstrnyuv(sColor, sizeof(sColor), 100, 50, 25) > 0, "A yuv sequence is built");
    CHECK(sColor[0] == '\x1B', "The yuv sequence starts with an escape");

    char *pAllocated = xstrrgb(1, 2, 3);
    CHECK(pAllocated != NULL && pAllocated[0] == '\x1B', "The allocating rgb helper builds a sequence");
    free(pAllocated);

    pAllocated = xstryuv(1, 2, 3);
    CHECK(pAllocated != NULL && pAllocated[0] == '\x1B', "The allocating yuv helper builds a sequence");
    free(pAllocated);

    /* The colouring wrapper wraps the text and resets afterwards. */
    CHECK(xstrnclr(sColor, sizeof(sColor), XSTR_CLR_RED, "%s", "alert") > 0, "The colour wrapper runs");
    CHECK(strstr(sColor, "alert") != NULL, "The wrapped text is present");
    CHECK(strstr(sColor, XSTR_FMT_RESET) != NULL, "The wrapper resets the formatting afterwards");

    /* Escape sequence measurement: the escapes do not count as characters. */
    size_t nChars = 0;
    size_t nExtra = xstrextra(sColor, strlen(sColor), 0, &nChars, NULL);
    CHECK(nExtra > 0, "The escape bytes are counted as extra");
    CHECK(nChars == strlen("alert"), "Only the visible characters are counted");

    /* A plain string has no extra bytes at all. */
    nChars = 0;
    CHECK(xstrextra("plain", 5, 0, &nChars, NULL) == 0, "A plain string has no escape bytes");
    CHECK(nChars == 5, "Every byte of a plain string is visible");
    CHECK(xstrextra(NULL, 5, 0, NULL, NULL) == 0, "A missing string measures nothing");
    CHECK(xstrextra("plain", 0, 0, NULL, NULL) == 0, "An empty string measures nothing");
    return 0;
}

static int XTest_random(void)
{
    /* The random helper fills exactly the requested length from the
     * requested alphabet, and terminates what it wrote. */
    char sRandom[64];
    for (size_t nLength = 1; nLength <= 32; nLength++)
    {
        memset(sRandom, 0x5a, sizeof(sRandom));
        CHECK(xstrrand(sRandom, sizeof(sRandom), nLength, XTRUE, XTRUE) == nLength,
            "The random string is exactly as long as requested");
        CHECK(strlen(sRandom) == nLength, "The random string is terminated at its length");
        for (size_t i = 0; i < nLength; i++)
            CHECK(isalnum((unsigned char)sRandom[i]), "Every random byte is from the alphabet");
    }

    /* Letters only, and lower case only. */
    CHECK(xstrrand(sRandom, sizeof(sRandom), 24, XFALSE, XFALSE) == 24, "A letters-only string is built");
    for (size_t i = 0; i < 24; i++)
        CHECK(sRandom[i] >= 'a' && sRandom[i] <= 'z', "Lower case letters only");

    CHECK(xstrrand(sRandom, sizeof(sRandom), 24, XFALSE, XTRUE) == 24, "A digits and letters string is built");
    for (size_t i = 0; i < 24; i++)
        CHECK(isalnum((unsigned char)sRandom[i]) && !isupper((unsigned char)sRandom[i]),
            "No upper case letters when they were not asked for");

    /* Two runs must not be identical, or the generator is not random. */
    char sFirst[33], sSecond[33];
    xstrrand(sFirst, sizeof(sFirst), 32, XTRUE, XTRUE);
    xstrrand(sSecond, sizeof(sSecond), 32, XTRUE, XTRUE);
    CHECK(strcmp(sFirst, sSecond) != 0, "Two random strings differ");

    /* A length past the destination is clamped. */
    char sTiny[8];
    CHECK(xstrrand(sTiny, sizeof(sTiny), 100, XTRUE, XTRUE) == sizeof(sTiny) - 1, "An oversized request is clamped");
    CHECK(strlen(sTiny) == sizeof(sTiny) - 1, "The clamped string fills the destination");
    return 0;
}


static int XTest_glob(void)
{
    /* Glob style matching, with the star as the only wildcard. It is used
     * to pick names out of a list, so what has to be pinned down is which
     * strings do NOT match: a pattern that matched everything would quietly
     * widen whatever it is filtering. */
    const char *pName = "libxutils-2.8.39.tar.gz";
    size_t nLength = strlen(pName);

    CHECK(xstrregex(pName, nLength, "*") == XTRUE, "A bare star matches anything");
    CHECK(xstrregex(pName, nLength, "libxutils*") == XTRUE, "A leading literal matches a prefix");
    CHECK(xstrregex(pName, nLength, "*tar.gz") == XTRUE, "A trailing literal matches a suffix");
    CHECK(xstrregex(pName, nLength, "libxutils*tar.gz") == XTRUE, "Both ends match together");
    CHECK(xstrregex(pName, nLength, "*2.8*") == XTRUE, "An interior literal matches");
    CHECK(xstrregex(pName, nLength, "*x*t*g*") == XTRUE, "Several literals match in order");

    CHECK(xstrregex(pName, nLength, "*nosuchpart*") == XFALSE, "A literal that is absent does not match");
    CHECK(xstrregex(pName, nLength, "*.zip") == XFALSE, "A different suffix does not match");
    CHECK(xstrregex(pName, nLength, "other*") == XFALSE, "A different prefix does not match");

    /* Without a star the pattern is a plain comparison, anchored at both
     * ends. Anything else would make it unsafe to filter with: a list
     * holding "admin" must not also admit "administrator". */
    CHECK(xstrregex(pName, nLength, pName) == XTRUE, "An exact pattern matches itself");
    CHECK(xstrregex(pName, nLength, "libxutils") == XFALSE, "A shorter exact pattern does not match");
    CHECK(xstrregex("administrator", 13, "admin") == XFALSE, "A starless pattern is not a prefix match");
    CHECK(xstrregex("admin", 5, "admin") == XTRUE, "A starless pattern still matches exactly");
    CHECK(xstrregex("administrator", 13, "admin*") == XTRUE, "The star is what opens the end up");

    CHECK(xstrregex(NULL, 4, "*") == XFALSE, "A missing string matches nothing");
    CHECK(xstrregex(pName, nLength, NULL) == XFALSE, "A missing pattern matches nothing");
    CHECK(xstrregex("", 0, "*") == XTRUE, "An empty string still matches a bare star");
    return 0;
}

static int XTest_field_formatting(void)
{
    /* Padding a formatted value out to a fixed width, from either side.
     * These build the columns of a rendered table, so what matters is that
     * the field is exactly the requested width and that a value too wide
     * for it is not silently padded out to the whole buffer. */
    char sBuffer[64];

    /* Right aligned: the fill goes in front of the text. */
    memset(sBuffer, 0, sizeof(sBuffer));
    size_t nLeft = xstrnlcpyf(sBuffer, sizeof(sBuffer), 10, ' ', "%d", 42);
    CHECK(nLeft == 10, "A right aligned field reports its width");
    CHECK(strlen(sBuffer) == 10, "The rendered field is the width asked for");
    CHECK(strcmp(sBuffer, "        42") == 0, "The value is pushed to the right");

    memset(sBuffer, 0, sizeof(sBuffer));
    xstrnlcpyf(sBuffer, sizeof(sBuffer), 8, '0', "%s", "7");
    CHECK(strcmp(sBuffer, "0000000" "7") == 0, "The fill character is the one asked for");

    /* Left aligned: the fill goes after the text. */
    memset(sBuffer, 0, sizeof(sBuffer));
    size_t nRight = xstrncpyfl(sBuffer, sizeof(sBuffer), 10, '.', "%d", 42);
    CHECK(nRight == 10, "A left aligned field reports its width");
    CHECK(strcmp(sBuffer, "42........") == 0, "The value stays on the left");

    /* A value wider than the field is written whole rather than truncated
     * to the field or padded out to the buffer. */
    memset(sBuffer, 0, sizeof(sBuffer));
    size_t nWide = xstrnlcpyf(sBuffer, sizeof(sBuffer), 4, ' ', "%s", "much-too-long");
    CHECK(nWide == strlen("much-too-long"), "An overwide value reports its own length");
    CHECK(strcmp(sBuffer, "much-too-long") == 0, "An overwide value is written whole");
    CHECK(strchr(sBuffer, ' ') == NULL, "An overwide value is not padded at all");

    memset(sBuffer, 0, sizeof(sBuffer));
    xstrncpyfl(sBuffer, sizeof(sBuffer), 4, ' ', "%s", "much-too-long");
    CHECK(strcmp(sBuffer, "much-too-long") == 0, "The left aligned form agrees");

    /* A field wider than the buffer is clamped to the buffer rather than
     * running past it. */
    char sSmall[8];
    memset(sSmall, 0, sizeof(sSmall));
    xstrnlcpyf(sSmall, sizeof(sSmall), 100, '-', "%s", "x");
    CHECK(strlen(sSmall) < sizeof(sSmall), "A field wider than the buffer is clamped");

    CHECK(xstrnlcpyf(NULL, 16, 4, ' ', "%d", 1) == 0, "A missing destination renders nothing");
    CHECK(xstrnlcpyf(sBuffer, 0, 4, ' ', "%d", 1) == 0, "A zero sized destination renders nothing");
    CHECK(xstrncpyfl(NULL, 16, 4, ' ', "%d", 1) == 0, "The left aligned form agrees on a missing destination");
    return 0;
}

static int XTest_append_remaining(void)
{
    /* Appending into the space a buffer has left, where the caller tracks
     * how much is left rather than measuring the string each time. The
     * count that comes back is what is still free afterwards, so a loop
     * that appends until it reaches zero must never write past the end. */
    char sBuffer[32];
    memset(sBuffer, 0, sizeof(sBuffer));

    size_t nAvail = sizeof(sBuffer);
    nAvail = xstrncatsf(sBuffer, sizeof(sBuffer), nAvail, "%s", "first");
    CHECK(nAvail == sizeof(sBuffer) - strlen("first"), "The remaining space drops by what was written");
    CHECK(strcmp(sBuffer, "first") == 0, "The first append lands at the start");

    nAvail = xstrncatsf(sBuffer, sizeof(sBuffer), nAvail, "-%s", "second");
    CHECK(strcmp(sBuffer, "first-second") == 0, "The second append lands after the first");
    CHECK(nAvail == sizeof(sBuffer) - strlen("first-second"), "The remaining space keeps dropping");

    /* Appending until there is no room left must stop rather than run on. */
    for (int i = 0; i < 20 && nAvail > 0; i++)
        nAvail = xstrncatsf(sBuffer, sizeof(sBuffer), nAvail, "%s", "xxxx");

    CHECK(strlen(sBuffer) < sizeof(sBuffer), "The buffer is still terminated inside itself");
    CHECK(strncmp(sBuffer, "first-second", 12) == 0, "What was already there was not overwritten");

    CHECK(xstrncatsf(NULL, 16, 16, "%d", 1) == 0, "A missing destination appends nothing");
    CHECK(xstrncatsf(sBuffer, 0, 16, "%d", 1) == 0, "A zero sized destination appends nothing");
    CHECK(xstrncatsf(sBuffer, sizeof(sBuffer), 0, "%d", 1) == 0, "No remaining space appends nothing");
    return 0;
}


static int XTest_ansi_widths(void)
{
    /* Every escape the library can emit has to be recognised, and for its
     * exact byte width. The visible length of a coloured string is computed
     * by subtracting these, so one unrecognised sequence makes a progress
     * bar or a table column render at the wrong width - and only for the
     * colour nobody tested. */
    typedef struct { const char *pName; const char *pSeq; size_t nWidth; } ansi_t;

    const ansi_t escapes[] = {
        {"light red",     XSTR_CLR_LIGHT_RED,     7},
        {"light green",   XSTR_CLR_LIGHT_GREEN,   7},
        {"light yellow",  XSTR_CLR_LIGHT_YELLOW,  7},
        {"light blue",    XSTR_CLR_LIGHT_BLUE,    7},
        {"light magenta", XSTR_CLR_LIGHT_MAGENTA, 7},
        {"light cyan",    XSTR_CLR_LIGHT_CYAN,    7},
        {"light white",   XSTR_CLR_LIGHT_WHITE,   7},

        {"red",           XSTR_CLR_RED,           5},
        {"green",         XSTR_CLR_GREEN,         5},
        {"yellow",        XSTR_CLR_YELLOW,        5},
        {"blue",          XSTR_CLR_BLUE,          5},
        {"magenta",       XSTR_CLR_MAGENTA,       5},
        {"cyan",          XSTR_CLR_CYAN,          5},
        {"white",         XSTR_CLR_WHITE,         5},

        {"back black",    XSTR_BACK_BLACK,        5},
        {"back red",      XSTR_BACK_RED,          5},
        {"back green",    XSTR_BACK_GREEN,        5},
        {"back yellow",   XSTR_BACK_YELLOW,       5},
        {"back blue",     XSTR_BACK_BLUE,         5},
        {"back magenta",  XSTR_BACK_MAGENTA,      5},
        {"back cyan",     XSTR_BACK_CYAN,         5},
        {"back white",    XSTR_BACK_WHITE,        5},

        {"bold",          XSTR_FMT_BOLD,          4},
        {"dim",           XSTR_FMT_DIM,           4},
        {"italic",        XSTR_FMT_ITALIC,        4},
        {"underline",     XSTR_FMT_ULINE,         4},
        {"flicker",       XSTR_FMT_FLICK,         4},
        {"blink",         XSTR_FMT_BLINK,         4},
        {"highlight",     XSTR_FMT_HIGHLITE,      4},
        {"hide",          XSTR_FMT_HIDE,          4},
        {"cross",         XSTR_FMT_CROSS,         4},
        {"reset",         XSTR_FMT_RESET,         4},

        {"degree",        XSTR_DEGREE_SYMBOL,     1}
    };

    size_t nCount = sizeof(escapes) / sizeof(*escapes);

    for (size_t i = 0; i < nCount; i++)
    {
        size_t nFound = xstrisextra(escapes[i].pSeq);
        CHECK(nFound == escapes[i].nWidth, "Every escape is recognised at its own width");

        /* The degree symbol is two UTF-8 bytes rendering as one character,
         * so it is reported as one extra byte rather than as a sequence
         * that vanishes entirely. Everything else is pure escape. */
        if (escapes[i].nWidth == 1) continue;

        /* The visible length of text wrapped in it is the text alone. */
        char sLine[128];
        xstrncpyf(sLine, sizeof(sLine), "%svisible%s", escapes[i].pSeq, XSTR_FMT_RESET);

        size_t nPosit = 0, nChars = 0;
        size_t nExtra = xstrextra(sLine, strlen(sLine), 0, &nChars, &nPosit);

        CHECK(nChars == strlen("visible"), "The visible length excludes the escapes");
        CHECK(nExtra == strlen(sLine) - strlen("visible"), "Everything else is counted as escape bytes");
    }

    /* The degree symbol on its own: one visible character, two bytes. */
    {
        char sDegree[32];
        xstrncpyf(sDegree, sizeof(sDegree), "21%sC", XSTR_DEGREE_SYMBOL);

        size_t nPosit = 0, nChars = 0;
        size_t nExtra = xstrextra(sDegree, strlen(sDegree), 0, &nChars, &nPosit);

        CHECK(nExtra == 1, "The degree symbol counts as one extra byte");
        CHECK(nChars + nExtra == strlen(sDegree), "Its bytes are all accounted for");
    }

    /* Anything that is not one of them is not an escape, including the
     * prefixes that start like one. */
    CHECK(xstrisextra("plain text") == 0, "Plain text is not an escape");
    CHECK(xstrisextra("\x1B") == 0, "A lone escape byte is not a sequence");
    CHECK(xstrisextra("\x1B[") == 0, "An unfinished sequence is not one");
    CHECK(xstrisextra("\x1B[99m") == 0, "An unknown sequence is not recognised");
    CHECK(xstrisextra("") == 0, "An empty string holds no escape");

    /* Several in a row, which is how the library actually emits them. */
    char sMixed[256];
    xstrncpyf(sMixed, sizeof(sMixed), "%s%s%sboth%s",
        XSTR_FMT_BOLD, XSTR_CLR_LIGHT_CYAN, XSTR_BACK_RED, XSTR_FMT_RESET);

    size_t nPosit = 0, nChars = 0;
    xstrextra(sMixed, strlen(sMixed), 0, &nChars, &nPosit);
    CHECK(nChars == strlen("both"), "Stacked escapes all come off the visible length");

    /* The character limit stops counting where it is told to. */
    nPosit = 0; nChars = 0;
    xstrextra(sMixed, strlen(sMixed), 2, &nChars, &nPosit);
    CHECK(nChars == 2, "The character limit is honoured across escapes");
    CHECK(nPosit <= strlen(sMixed), "The reported position stays inside the string");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(split),
    XTEST_CASE(match),
    XTEST_CASE(compare),
    XTEST_CASE(search),
    XTEST_CASE(replace),
    XTEST_CASE(cut),
    XTEST_CASE(tokens),
    XTEST_CASE(casing),
    XTEST_CASE(fill_and_trim),
    XTEST_CASE(duplicate),
    XTEST_CASE(concat),
    XTEST_CASE(colors),
    XTEST_CASE(random),
    XTEST_CASE(glob),
    XTEST_CASE(field_formatting),
    XTEST_CASE(append_remaining),
    XTEST_CASE(ansi_widths)
)
