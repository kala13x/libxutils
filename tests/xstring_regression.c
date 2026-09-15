/* libxutils: the growable XString object.
 *
 * The plain C helpers are covered elsewhere; this is the object API that
 * config parsing and log rendering build on. Every case asserts on both the
 * contents and the recorded length, since a length that drifts from the
 * bytes is what turns a later append into a silent truncation.
 */

#include "test.h"
#include "str.h"
#include "array.h"

static int XTest_build(void)
{
    /* A string grows on demand and keeps its length in step with its bytes. */
    xstring_t str;
    CHECK(XString_Init(&str, 0, 0) >= 0, "A zero capacity string initializes");
    CHECK(str.nLength == 0 && str.pData == NULL, "A zero capacity string owns nothing yet");

    CHECK(XString_Add(&str, "hello", 5) == 5, "The first append reports the new length");
    CHECK(str.nLength == 5 && strcmp(str.pData, "hello") == 0, "The bytes and the length agree");
    CHECK(str.nSize > str.nLength, "The capacity leaves room for a terminator");

    CHECK(XString_Append(&str, "-%d-%s", 42, "x") == 10, "A formatted append reports the new length");
    CHECK(strcmp(str.pData, "hello-42-x") == 0, "The formatted append lands at the end");
    CHECK(str.nLength == strlen(str.pData), "The length still matches the bytes");

    /* Appending binary content keeps the embedded NUL. */
    const char binary[] = {'a', 0x00, 'b'};
    CHECK(XString_Add(&str, binary, sizeof(binary)) == 13, "A binary append reports the new length");
    CHECK(str.nLength == 13, "The embedded NUL is counted, not treated as the end");
    CHECK(str.pData[11] == 0x00 && str.pData[12] == 'b', "The bytes after the embedded NUL are kept");

    XString_Clear(&str);
    CHECK(str.nLength == 0 && str.pData == NULL, "Clearing releases the storage");

    /* Repeated growth across many appends. */
    CHECK(XString_Init(&str, 4, 0) >= 0, "A small string initializes");
    for (int i = 0; i < 500; i++)
        CHECK(XString_Add(&str, "x", 1) == i + 1, "Every append reports the running length");
    CHECK(str.nLength == 500, "Every appended byte is counted");
    for (size_t i = 0; i < str.nLength; i++)
        CHECK(str.pData[i] == 'x', "Growth preserved every byte");
    CHECK(str.pData[str.nLength] == '\0', "The grown string is still terminated");

    /* Resizing down to the length keeps the contents. */
    CHECK(XString_Resize(&str, str.nLength + 1) >= 0, "The string resizes down");
    CHECK(str.nLength == 500, "Resizing down kept the length");

    /* Increasing adds capacity without changing the contents. */
    size_t nBefore = str.nLength;
    CHECK(XString_Increase(&str, 1024) >= 0, "The string increases its capacity");
    CHECK(str.nLength == nBefore, "Increasing does not change the length");
    CHECK(str.nSize >= nBefore + 1024, "The capacity grew by what was asked");

    XString_Clear(&str);
    XString_Clear(&str);
    return 0;
}

static int XTest_edit(void)
{
    /* Inserting shifts the tail; removing closes the gap. */
    xstring_t str;
    CHECK(XString_InitFrom(&str, "%s", "hello-42-x") > 0, "The string is built");

    CHECK(XString_Insert(&str, 5, "INS", 3) == 13, "An insert reports the new length");
    CHECK(strcmp(str.pData, "helloINS-42-x") == 0, "The insert shifts the tail out of the way");

    CHECK(XString_Remove(&str, 5, 3) == 10, "A removal reports the new length");
    CHECK(strcmp(str.pData, "hello-42-x") == 0, "The removal closed the gap");

    CHECK(XString_InsertFmt(&str, 0, "[%d]", 7) == 13, "A formatted insert reports the new length");
    CHECK(strcmp(str.pData, "[7]hello-42-x") == 0, "The formatted insert lands at its position");

    CHECK(XString_Delete(&str, 0, 3) == 10, "A delete reports the new length");
    CHECK(strcmp(str.pData, "hello-42-x") == 0, "The delete removed the leading bytes");

    CHECK(XString_Advance(&str, 2) == 8, "Advancing reports the new length");
    CHECK(strcmp(str.pData, "llo-42-x") == 0, "Advancing dropped the leading bytes");

    /* Inserting at the very end is an append. */
    size_t nLength = str.nLength;
    CHECK(XString_Insert(&str, nLength, "END", 3) > 0, "An insert at the end succeeds");
    CHECK(strcmp(str.pData, "llo-42-xEND") == 0, "The insert at the end appended");

    /* Out of range edits take nothing rather than guessing at a position.
     * Removing reports that it took no bytes; deleting reports the length
     * it left behind. */
    CHECK(XString_Remove(&str, 999, 1) == 0, "A removal past the end takes nothing");
    CHECK(XString_Delete(&str, 999, 1) == (int)str.nLength, "A delete past the end leaves the length alone");
    CHECK(strcmp(str.pData, "llo-42-xEND") == 0, "A refused edit changed nothing");

    /* A removal longer than the remainder is clamped to it. */
    CHECK(XString_Remove(&str, 8, 999) == 8, "An oversized removal is clamped to the remainder");
    CHECK(strcmp(str.pData, "llo-42-x") == 0, "The clamped removal took only what was there");

    /* Advancing past the end empties the string. */
    CHECK(XString_Advance(&str, 999) >= 0, "Advancing past the end is accepted");
    CHECK(str.nLength == 0, "Advancing past the end empties the string");

    XString_Clear(&str);
    return 0;
}

static int XTest_transform(void)
{
    /* Case changes apply to a range or to the whole string. */
    xstring_t str;
    CHECK(XString_InitFrom(&str, "%s", "MiXeD case 123") > 0, "The string is built");

    CHECK(XString_Case(&str, XSTR_UPPER, 0, 5) > 0, "A ranged case change succeeds");
    CHECK(strncmp(str.pData, "MIXED", 5) == 0, "The range was uppercased");
    CHECK(strcmp(&str.pData[5], " case 123") == 0, "The bytes outside the range were left alone");

    CHECK(XString_ChangeCase(&str, XSTR_LOWER) > 0, "A whole string case change succeeds");
    CHECK(strcmp(str.pData, "mixed case 123") == 0, "The whole string was lowercased");

    CHECK(XString_ChangeCase(&str, XSTR_UPPER) > 0, "The string uppercases");
    CHECK(strcmp(str.pData, "MIXED CASE 123") == 0, "Digits and spaces are left alone");

    /* Replacement rebuilds the string in place. */
    CHECK(XString_Replace(&str, "CASE", "text") > 0, "A replacement succeeds");
    CHECK(strcmp(str.pData, "MIXED text 123") == 0, "The replacement landed");
    CHECK(str.nLength == strlen(str.pData), "The length follows the replacement");

    /* A replacement that shortens and one that lengthens. */
    CHECK(XString_Replace(&str, "MIXED", "M") > 0, "A shortening replacement succeeds");
    CHECK(strcmp(str.pData, "M text 123") == 0, "The shortening replacement landed");

    CHECK(XString_Replace(&str, "M", "MMMMM") > 0, "A lengthening replacement succeeds");
    CHECK(strcmp(str.pData, "MMMMM text 123") == 0, "The lengthening replacement landed");

    /* A needle that is not there leaves the string alone. */
    size_t nBefore = str.nLength;
    XString_Replace(&str, "absent", "x");
    CHECK(str.nLength == nBefore, "An absent needle changed nothing");

    /* Searching reports a position relative to where the scan started,
     * not an absolute offset into the string. */
    CHECK(XString_Search(&str, 0, "text") == 6, "A hit from the start reports its absolute position");
    CHECK(XString_Search(&str, 0, "MMMMM") == 0, "A hit at the start reports zero");
    CHECK(XString_Search(&str, 5, "text") == 1, "A hit reports its offset from the scan position");
    CHECK(XString_Search(&str, 0, "absent") < 0, "A miss is reported as negative");
    CHECK(XString_Search(&str, 999, "text") < 0, "A search past the end finds nothing");

    XString_Clear(&str);
    return 0;
}

static int XTest_substrings(void)
{
    /* Substrings can be taken into a caller buffer, into another string,
     * or into a newly allocated one. */
    xstring_t str;
    CHECK(XString_InitFrom(&str, "%s", "abcdefghij") > 0, "The string is built");

    char sBuffer[32];
    CHECK(XString_Sub(&str, sBuffer, sizeof(sBuffer), 2, 3) == 3, "A substring reports its length");
    CHECK(strcmp(sBuffer, "cde") == 0, "The substring is the requested slice");

    xstring_t sub;
    CHECK(XString_SubStr(&str, &sub, 2, 3) == 3, "A substring object reports its length");
    CHECK(strcmp(sub.pData, "cde") == 0, "The substring object holds the slice");
    XString_Clear(&sub);

    xstring_t *pNew = XString_SubNew(&str, 2, 3);
    CHECK(pNew != NULL && strcmp(pNew->pData, "cde") == 0, "A new substring holds the slice");
    XString_Clear(pNew);

    /* The source is untouched by any of them. */
    CHECK(strcmp(str.pData, "abcdefghij") == 0, "The source survives every substring");

    /* A position past the end yields nothing and leaves the source alone. */
    CHECK(XString_Sub(&str, sBuffer, sizeof(sBuffer), 99, 3) < 0, "A substring past the end is refused");
    CHECK(XString_SubStr(&str, &sub, 99, 3) < 0, "A substring object past the end is refused");
    CHECK(XString_SubNew(&str, 99, 3) == NULL, "A new substring past the end is refused");
    CHECK(strcmp(str.pData, "abcdefghij") == 0, "A refused substring leaves the source intact");
    CHECK(str.nLength == 10, "A refused substring leaves the source length intact");

    /* Cutting between markers. */
    xstring_t marked;
    CHECK(XString_InitFrom(&marked, "%s", "pre[mid]post") > 0, "The marked string is built");

    CHECK(XString_Cut(&marked, sBuffer, sizeof(sBuffer), "[", "]") == 3, "A cut reports its length");
    CHECK(strcmp(sBuffer, "mid") == 0, "The cut is the text between the markers");

    xstring_t cut;
    CHECK(XString_CutSub(&marked, &cut, "[", "]") == 3, "A cut object reports its length");
    CHECK(strcmp(cut.pData, "mid") == 0, "The cut object holds the text");
    XString_Clear(&cut);

    xstring_t *pCutNew = XString_CutNew(&marked, "[", "]");
    CHECK(pCutNew != NULL && strcmp(pCutNew->pData, "mid") == 0, "A new cut holds the text");
    XString_Clear(pCutNew);

    /* Markers that are not there yield nothing. */
    CHECK(XString_Cut(&marked, sBuffer, sizeof(sBuffer), "{", "}") <= 0, "Absent markers cut nothing");
    CHECK(XString_CutNew(&marked, "{", "}") == NULL, "Absent markers make no new string");

    XString_Clear(&marked);
    XString_Clear(&str);
    return 0;
}

static int XTest_tokens(void)
{
    /* Tokenizing walks the string by returning the next scan position. */
    xstring_t str;
    CHECK(XString_InitFrom(&str, "%s", "alpha:beta:gamma") > 0, "The string is built");

    const char *pExpected[] = {"alpha", "beta", "gamma"};
    char sToken[32];
    int nPosit = 0;

    for (int i = 0; i < 3; i++)
    {
        int nNext = XString_Tokenize(&str, sToken, sizeof(sToken), (size_t)nPosit, ":");
        CHECK(nNext >= 0, "Every token is found");
        CHECK(strcmp(sToken, pExpected[i]) == 0, "Every token is the expected one");
        if (i < 2) CHECK(nNext > nPosit, "The scan position advances past each delimiter");
        else CHECK(nNext == 0, "The last token reports no delimiter after it");
        nPosit = nNext;
    }

    /* The object form fills an initialized destination. */
    xstring_t token;
    CHECK(XString_Init(&token, 0, 0) >= 0, "The token destination initializes");
    CHECK(XString_Token(&str, &token, 0, ":") > 0, "The first token is taken");
    CHECK(strcmp(token.pData, "alpha") == 0, "The token object holds the first field");
    XString_Clear(&token);

    /* Splitting yields every field at once. Unlike xstrsplit(), which
     * stores plain C strings, this stores string objects. */
    xarray_t *pFields = XString_SplitStr(&str, ":");
    CHECK(pFields != NULL && XArray_Used(pFields) == 3, "Splitting yields one entry per field");

    for (size_t i = 0; i < 3; i++)
    {
        xstring_t *pField = (xstring_t*)XArray_GetData(pFields, i);
        CHECK(pField != NULL && pField->pData != NULL, "Every split entry is a string object");
        CHECK(strcmp(pField->pData, pExpected[i]) == 0, "Every split field is correct");
        CHECK(pField->nLength == strlen(pExpected[i]), "Every split field has its own length");
    }
    XArray_Destroy(pFields);

    /* The C string form of the split agrees, and stores objects too. */
    pFields = XString_Split("alpha:beta:gamma", ":");
    CHECK(pFields != NULL && XArray_Used(pFields) == 3, "The C string split yields the same fields");
    for (size_t i = 0; i < 3; i++)
    {
        xstring_t *pField = (xstring_t*)XArray_GetData(pFields, i);
        CHECK(pField != NULL && strcmp(pField->pData, pExpected[i]) == 0,
            "The C string split yields the same field values");
    }
    XArray_Destroy(pFields);

    /* A string with no delimiter in it is a single field. */
    xstring_t single;
    CHECK(XString_InitFrom(&single, "%s", "nodelimiter") > 0, "A single field string is built");
    pFields = XString_SplitStr(&single, ":");
    CHECK(pFields != NULL && XArray_Used(pFields) == 1, "A string without the delimiter is one field");
    XArray_Destroy(pFields);
    XString_Clear(&single);

    /* A delimiter that is not there yields the whole string. */
    int nNext = XString_Tokenize(&str, sToken, sizeof(sToken), 0, "|");
    CHECK(nNext == 0, "A missing delimiter reports no next position");
    CHECK(strcmp(sToken, "alpha:beta:gamma") == 0, "A missing delimiter yields the whole string");

    /* A position past the end has nothing to tokenize. */
    CHECK(XString_Tokenize(&str, sToken, sizeof(sToken), 999, ":") < 0, "A position past the end is refused");

    XString_Clear(&str);
    return 0;
}

static int XTest_construction(void)
{
    /* Each constructor produces an independently owned string. */
    xstring_t *pNew = XString_New(64, 0);
    CHECK(pNew != NULL, "A sized string is allocated");
    CHECK(pNew->nSize >= 64, "The sized string has its capacity");
    CHECK(pNew->nLength == 0, "The sized string starts empty");
    CHECK(XString_Add(pNew, "in-new", 6) == 6, "The sized string accepts data");
    XString_Clear(pNew);

    xstring_t *pFrom = XString_From("from-data", 9);
    CHECK(pFrom != NULL && strcmp(pFrom->pData, "from-data") == 0, "A string is built from bytes");
    CHECK(pFrom->nLength == 9, "The built string has the right length");

    xstring_t *pFmt = XString_FromFmt("fmt-%d", 9);
    CHECK(pFmt != NULL && strcmp(pFmt->pData, "fmt-9") == 0, "A string is built from a format");

    xstring_t *pCopy = XString_FromStr(pFrom);
    CHECK(pCopy != NULL && strcmp(pCopy->pData, "from-data") == 0, "A string is copied from another");
    CHECK(pCopy->pData != pFrom->pData, "The copy owns its own storage");

    /* Clearing the source leaves the copy intact. */
    XString_Clear(pFrom);
    CHECK(strcmp(pCopy->pData, "from-data") == 0, "The copy outlives its source");
    XString_Clear(pCopy);
    XString_Clear(pFmt);

    /* Copying into an existing string replaces its contents. */
    xstring_t target, source;
    CHECK(XString_InitFrom(&target, "%s", "old") > 0, "The target is built");
    CHECK(XString_InitFrom(&source, "%s", "new-contents") > 0, "The source is built");

    CHECK(XString_Copy(&target, &source) > 0, "The copy succeeds");
    CHECK(strcmp(target.pData, "new-contents") == 0, "The target holds the source contents");
    CHECK(target.pData != source.pData, "The target owns its own storage");

    /* Appending one string onto another. */
    CHECK(XString_AddString(&target, &source) > 0, "One string appends onto another");
    CHECK(strcmp(target.pData, "new-contentsnew-contents") == 0, "The append lands at the end");

    /* Appending a string to itself must survive the reallocation. */
    CHECK(XString_AddString(&target, &target) > 0, "A string appends onto itself");
    CHECK(target.nLength == 48, "The self append doubled the length");

    XString_Clear(&source);
    XString_Clear(&target);

    /* Setting points the string at a caller buffer without taking it over:
     * the capacity stays zero, so clearing releases nothing and the caller
     * keeps responsibility for the memory. */
    char *pBorrowed = strdup("adopted");
    CHECK(pBorrowed != NULL, "A caller allocation is made");

    xstring_t borrowed;
    CHECK(XString_Init(&borrowed, 0, 0) >= 0, "The borrowing string initializes");
    CHECK(XString_Set(&borrowed, pBorrowed, 7) == 7, "The buffer is pointed at");
    CHECK(borrowed.pData == pBorrowed, "The string points at the caller buffer, not a copy");
    CHECK(borrowed.nSize == 0, "A borrowed buffer is not owned capacity");
    CHECK(strcmp(borrowed.pData, "adopted") == 0, "The borrowed bytes are readable");

    XString_Clear(&borrowed);
    CHECK(strcmp(pBorrowed, "adopted") == 0, "Clearing a borrowing string leaves the buffer alone");
    free(pBorrowed);
    return 0;
}

static int XTest_colors(void)
{
    /* Colouring wraps a range in escape sequences, which lengthens the
     * stored bytes while leaving the visible text alone. */
    xstring_t str;
    CHECK(XString_InitFrom(&str, "%s", "colorize") > 0, "The string is built");
    size_t nPlain = str.nLength;

    CHECK(XString_Color(&str, XSTR_CLR_RED, 0, 5) > 0, "A range is coloured");
    CHECK(str.nLength > nPlain, "Colouring lengthens the stored bytes");
    CHECK(strstr(str.pData, "color") != NULL, "The coloured text is still there");
    CHECK(str.pData[0] == '\x1B', "The colour starts with an escape");
    CHECK(strstr(str.pData, XSTR_FMT_RESET) != NULL, "The colour is reset afterwards");

    /* The visible character count is unchanged. */
    size_t nChars = 0;
    xstrextra(str.pData, str.nLength, 0, &nChars, NULL);
    CHECK(nChars == nPlain, "Colouring adds no visible characters");

    XString_Clear(&str);

    /* Colouring the whole string. */
    CHECK(XString_InitFrom(&str, "%s", "whole") > 0, "The string is rebuilt");
    nPlain = str.nLength;

    CHECK(XString_ChangeColor(&str, XSTR_CLR_GREEN) > 0, "The whole string is coloured");
    CHECK(str.nLength > nPlain, "Colouring the whole string lengthens it");

    nChars = 0;
    xstrextra(str.pData, str.nLength, 0, &nChars, NULL);
    CHECK(nChars == nPlain, "Colouring the whole string adds no visible characters");
    CHECK(strstr(str.pData, "whole") != NULL, "The text survives being coloured");

    XString_Clear(&str);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(build),
    XTEST_CASE(edit),
    XTEST_CASE(transform),
    XTEST_CASE(substrings),
    XTEST_CASE(tokens),
    XTEST_CASE(construction),
    XTEST_CASE(colors)
)
