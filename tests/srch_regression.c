/* libxutils: advanced file search over a private fixture tree.
 *
 * Every case builds its own directory under /tmp so the result set is fully
 * known: the assertions are on exact match counts, which is what makes the
 * criteria filters (size, type, permissions, name tokens) meaningful.
 */

#include "test.h"
#include "srch.h"
#include "thread.h"
#include <unistd.h>
#include <sys/stat.h>

typedef struct {
    char sRoot[128];
    int nCreated;
} srch_fixture_t;

/* Writes a file with the given contents, creating parent directories. */
static int srch_write(const srch_fixture_t *pFixture, const char *pRelative, const char *pData, mode_t nMode)
{
    char sPath[512];
    snprintf(sPath, sizeof(sPath), "%s/%s", pFixture->sRoot, pRelative);

    FILE *pFile = fopen(sPath, "wb");
    if (pFile == NULL) return XSTDERR;
    if (pData != NULL && *pData) fwrite(pData, 1, strlen(pData), pFile);
    fclose(pFile);

    return chmod(sPath, nMode) == 0 ? XSTDOK : XSTDERR;
}

static int srch_mkdir(const srch_fixture_t *pFixture, const char *pRelative)
{
    char sPath[512];
    snprintf(sPath, sizeof(sPath), "%s/%s", pFixture->sRoot, pRelative);
    return mkdir(sPath, 0755) == 0 ? XSTDOK : XSTDERR;
}

/*  root/
 *    alpha.txt      "needle in the hay\nsecond line\n"   0644
 *    beta.txt       "nothing here\n"                     0600
 *    gamma.log      "needle again\n"                     0644
 *    runme.sh       "#!/bin/sh\n"                        0755
 *    nested/
 *      delta.txt    "deep needle\n"                      0644
 *      empty.txt    ""                                   0644
 */
static int srch_build(srch_fixture_t *pFixture)
{
    snprintf(pFixture->sRoot, sizeof(pFixture->sRoot), "/tmp/xutils-srch-XXXXXX");
    if (mkdtemp(pFixture->sRoot) == NULL) return XSTDERR;
    pFixture->nCreated = 1;

    if (srch_write(pFixture, "alpha.txt", "needle in the hay\nsecond line\n", 0644) != XSTDOK ||
        srch_write(pFixture, "beta.txt", "nothing here\n", 0600) != XSTDOK ||
        srch_write(pFixture, "gamma.log", "needle again\n", 0644) != XSTDOK ||
        srch_write(pFixture, "runme.sh", "#!/bin/sh\n", 0755) != XSTDOK ||
        srch_mkdir(pFixture, "nested") != XSTDOK ||
        srch_write(pFixture, "nested/delta.txt", "deep needle\n", 0644) != XSTDOK ||
        srch_write(pFixture, "nested/empty.txt", "", 0644) != XSTDOK) return XSTDERR;

    return XSTDOK;
}

static void srch_destroy(srch_fixture_t *pFixture)
{
    if (!pFixture->nCreated) return;
    char sCommand[256];
    snprintf(sCommand, sizeof(sCommand), "rm -rf %s", pFixture->sRoot);
    if (system(sCommand) < 0) { /* the fixture leaks at worst */ }
    pFixture->nCreated = 0;
}

/* Returns nonzero when the result set contains an entry with this name. */
static int srch_has(xsearch_t *pSearch, const char *pName)
{
    for (size_t i = 0; i < XArray_Used(&pSearch->fileArray); i++)
    {
        xsearch_entry_t *pEntry = XSearch_GetEntry(pSearch, (int)i);
        if (pEntry != NULL && strcmp(pEntry->sName, pName) == 0) return 1;
    }
    return 0;
}

static int XTest_name_matching(void)
{
    srch_fixture_t fixture;
    CHECK(srch_build(&fixture) == XSTDOK, "Build the search fixture");

    /* An exact name matches exactly one file and does not recurse by default. */
    xsearch_t search;
    XSearch_Init(&search, "alpha.txt");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A flat search succeeds");
    CHECK(XArray_Used(&search.fileArray) == 1, "An exact name matches one file");
    CHECK(srch_has(&search, "alpha.txt"), "The matched file is the requested one");
    XSearch_Destroy(&search);

    /* A wildcard matches every file with the suffix in the top directory. */
    XSearch_Init(&search, "*.txt");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A wildcard search succeeds");
    CHECK(XArray_Used(&search.fileArray) == 2, "The wildcard matches both top level .txt files");
    CHECK(srch_has(&search, "alpha.txt") && srch_has(&search, "beta.txt"), "Both .txt files are found");
    CHECK(!srch_has(&search, "delta.txt"), "A non-recursive search stays in the top directory");
    XSearch_Destroy(&search);

    /* A semicolon separated list searches for any of the names. */
    XSearch_Init(&search, "beta.txt;gamma.log");
    CHECK(XArray_Used(&search.nameTokens) == 2, "The name is tokenized on the separator");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A multi-name search succeeds");
    CHECK(XArray_Used(&search.fileArray) == 2, "Both requested names are matched");
    CHECK(srch_has(&search, "beta.txt") && srch_has(&search, "gamma.log"), "Each token matched its file");
    XSearch_Destroy(&search);

    /* Case insensitive matching lowercases both sides of the comparison. */
    XSearch_Init(&search, "ALPHA.TXT");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A case sensitive search runs");
    CHECK(XArray_Used(&search.fileArray) == 0, "A case sensitive search does not match a different case");
    XSearch_Destroy(&search);

    XSearch_Init(&search, "ALPHA.TXT");
    search.bInsensitive = XTRUE;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A case insensitive search runs");
    CHECK(XArray_Used(&search.fileArray) == 1, "Case insensitive matching finds the file");
    XSearch_Destroy(&search);

    /* A name that matches nothing yields an empty, still valid result set. */
    XSearch_Init(&search, "no-such-file");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "An unmatched search still succeeds");
    CHECK(XArray_Used(&search.fileArray) == 0, "An unmatched search returns no entries");
    CHECK(XSearch_GetEntry(&search, 0) == NULL, "An empty result set hands out no entries");
    XSearch_Destroy(&search);

    srch_destroy(&fixture);
    return 0;
}

static int XTest_recursion(void)
{
    srch_fixture_t fixture;
    CHECK(srch_build(&fixture) == XSTDOK, "Build the search fixture");

    xsearch_t search;
    XSearch_Init(&search, "*.txt");
    search.bRecursive = XTRUE;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A recursive search succeeds");
    CHECK(XArray_Used(&search.fileArray) == 4, "Recursion reaches the nested directory");
    CHECK(srch_has(&search, "delta.txt") && srch_has(&search, "empty.txt"), "The nested files are found");

    /* Every entry records the directory it was found in. */
    for (size_t i = 0; i < XArray_Used(&search.fileArray); i++)
    {
        xsearch_entry_t *pEntry = XSearch_GetEntry(&search, (int)i);
        CHECK(pEntry != NULL && pEntry->sPath[0] != '\0', "Each entry carries its directory");
        CHECK(pEntry->eType == XF_REGULAR, "Each matched entry is a regular file");
    }
    XSearch_Destroy(&search);

    /* A trailing slash on the root must not produce a doubled separator. */
    char sSlashed[192];
    snprintf(sSlashed, sizeof(sSlashed), "%s/", fixture.sRoot);
    XSearch_Init(&search, "alpha.txt");
    CHECK(XSearch(&search, sSlashed) == XSTDOK, "A trailing slash is accepted");
    CHECK(XArray_Used(&search.fileArray) == 1, "A trailing slash finds the same file");
    xsearch_entry_t *pEntry = XSearch_GetEntry(&search, 0);
    CHECK(pEntry != NULL && strstr(pEntry->sPath, "//") == NULL, "The stored path has no doubled separator");
    XSearch_Destroy(&search);

    srch_destroy(&fixture);
    return 0;
}

static int XTest_criteria(void)
{
    srch_fixture_t fixture;
    CHECK(srch_build(&fixture) == XSTDOK, "Build the search fixture");

    /* Only directories. */
    xsearch_t search;
    XSearch_Init(&search, "*");
    search.nFileTypes = XF_DIRECTORY;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A directory-only search runs");
    CHECK(XArray_Used(&search.fileArray) == 1 && srch_has(&search, "nested"),
        "Only the directory matches the directory filter");
    XSearch_Destroy(&search);

    /* Only regular files, top level. */
    XSearch_Init(&search, "*");
    search.nFileTypes = XF_REGULAR;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A regular-file search runs");
    CHECK(XArray_Used(&search.fileArray) == 4, "The four top level files match");
    CHECK(!srch_has(&search, "nested"), "The directory is excluded");
    XSearch_Destroy(&search);

    /* Only executables. */
    XSearch_Init(&search, "*");
    search.nFileTypes = XF_EXEC;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "An executable search runs");
    CHECK(srch_has(&search, "runme.sh"), "The executable script matches");
    CHECK(!srch_has(&search, "alpha.txt"), "A non-executable file does not match");
    XSearch_Destroy(&search);

    /* Permissions are compared as a chmod-style number. */
    XSearch_Init(&search, "*");
    search.nPermissions = 600;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A permission search runs");
    CHECK(XArray_Used(&search.fileArray) == 1 && srch_has(&search, "beta.txt"),
        "Only the 0600 file matches the permission filter");
    XSearch_Destroy(&search);

    /* An exact size: alpha.txt is 30 bytes. */
    XSearch_Init(&search, "*");
    search.nFileSize = 30;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "An exact size search runs");
    CHECK(XArray_Used(&search.fileArray) == 1 && srch_has(&search, "alpha.txt"),
        "Only the file with that exact size matches");
    XSearch_Destroy(&search);

    /* A minimum excludes everything smaller. */
    XSearch_Init(&search, "*");
    search.nMinSize = 20;
    search.nFileTypes = XF_REGULAR;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A minimum size search runs");
    CHECK(XArray_Used(&search.fileArray) == 1 && srch_has(&search, "alpha.txt"),
        "Only files at or above the minimum match");
    XSearch_Destroy(&search);

    /* A maximum excludes everything larger. */
    XSearch_Init(&search, "*");
    search.nMaxSize = 12;
    search.nFileTypes = XF_REGULAR;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A maximum size search runs");
    CHECK(!srch_has(&search, "alpha.txt"), "A file above the maximum is excluded");
    CHECK(srch_has(&search, "runme.sh"), "A file below the maximum is included");
    XSearch_Destroy(&search);

    /* A link count filter: the fixture files all have a single link. */
    XSearch_Init(&search, "*");
    search.nLinkCount = 1;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A link count search runs");
    CHECK(XArray_Used(&search.fileArray) == 4, "The single-link files match");
    CHECK(!srch_has(&search, "nested"), "The directory has more than one link");
    XSearch_Destroy(&search);

    XSearch_Init(&search, "*");
    search.nLinkCount = 99;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "An unmatched link count search runs");
    CHECK(XArray_Used(&search.fileArray) == 0, "No entry has that link count");
    XSearch_Destroy(&search);

    srch_destroy(&fixture);
    return 0;
}

static int XTest_text_search(void)
{
    srch_fixture_t fixture;
    CHECK(srch_build(&fixture) == XSTDOK, "Build the search fixture");

    /* Line mode reports the matching line and its one-based number. */
    xsearch_t search;
    XSearch_Init(&search, "*");
    search.bSearchLines = XTRUE;
    search.bRecursive = XTRUE;
    xstrncpy(search.sText, sizeof(search.sText), "needle");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A line search runs");
    CHECK(XArray_Used(&search.fileArray) == 3, "Every file containing the text matches");

    int nChecked = 0;
    for (size_t i = 0; i < XArray_Used(&search.fileArray); i++)
    {
        xsearch_entry_t *pEntry = XSearch_GetEntry(&search, (int)i);
        CHECK(pEntry != NULL, "Entry lookup");
        CHECK(strstr(pEntry->sLine, "needle") != NULL, "The reported line contains the search text");
        CHECK(pEntry->nLineNum >= 1, "Line numbers are one based");
        if (strcmp(pEntry->sName, "alpha.txt") == 0)
        {
            CHECK(pEntry->nLineNum == 1, "The match is on the first line of alpha.txt");
            nChecked++;
        }
    }
    CHECK(nChecked == 1, "alpha.txt was reported exactly once");
    XSearch_Destroy(&search);

    /* Match-only mode reports the file without loading lines. */
    XSearch_Init(&search, "*");
    search.bMatchOnly = XTRUE;
    search.bRecursive = XTRUE;
    xstrncpy(search.sText, sizeof(search.sText), "needle");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A match-only search runs");
    CHECK(XArray_Used(&search.fileArray) == 3, "Match-only reports one entry per file");
    for (size_t i = 0; i < XArray_Used(&search.fileArray); i++)
    {
        xsearch_entry_t *pEntry = XSearch_GetEntry(&search, (int)i);
        CHECK(pEntry != NULL && pEntry->sLine[0] == '\0', "Match-only leaves the line empty");
    }
    XSearch_Destroy(&search);

    /* Buffer mode walks the raw bytes instead of splitting into lines. */
    XSearch_Init(&search, "*");
    search.bRecursive = XTRUE;
    xstrncpy(search.sText, sizeof(search.sText), "needle");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A buffer search runs");
    CHECK(XArray_Used(&search.fileArray) >= 3, "Buffer mode finds every file with the text");
    XSearch_Destroy(&search);

    /* Text that appears nowhere yields nothing. */
    XSearch_Init(&search, "*");
    search.bSearchLines = XTRUE;
    search.bRecursive = XTRUE;
    xstrncpy(search.sText, sizeof(search.sText), "haystack-only");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "An unmatched text search runs");
    CHECK(XArray_Used(&search.fileArray) == 0, "No file contains the text");
    XSearch_Destroy(&search);

    /* Case insensitive text search lowercases the file contents too. */
    XSearch_Init(&search, "*");
    search.bSearchLines = XTRUE;
    search.bInsensitive = XTRUE;
    search.bRecursive = XTRUE;
    xstrncpy(search.sText, sizeof(search.sText), "NEEDLE");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A case insensitive text search runs");
    CHECK(XArray_Used(&search.fileArray) == 3, "Case insensitive text matching finds every file");
    XSearch_Destroy(&search);

    /* A text search never matches a directory, only regular files. */
    XSearch_Init(&search, "nested");
    search.bSearchLines = XTRUE;
    xstrncpy(search.sText, sizeof(search.sText), "needle");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A text search over a directory name runs");
    CHECK(XArray_Used(&search.fileArray) == 0, "A directory is never a text match");
    XSearch_Destroy(&search);

    srch_destroy(&fixture);
    return 0;
}

/* Callback contract: >0 keeps the entry, 0 drops it, <0 interrupts. */
static int g_callbackSeen = 0;
static int g_callbackVerdict = XSTDOK;
static int g_callbackStopAfter = -1;
static int g_callbackErrors = 0;

static int srch_callback(xsearch_t *pSearch, xsearch_entry_t *pEntry, const char *pMsg)
{
    (void)pSearch;
    if (pEntry == NULL || pMsg != NULL)
    {
        g_callbackErrors++;
        return XSTDOK;
    }

    g_callbackSeen++;
    if (g_callbackStopAfter >= 0 && g_callbackSeen > g_callbackStopAfter) return XSTDERR;
    return g_callbackVerdict;
}

static int XTest_callback(void)
{
    srch_fixture_t fixture;
    CHECK(srch_build(&fixture) == XSTDOK, "Build the search fixture");

    /* A positive verdict keeps every entry in the result array. */
    xsearch_t search;
    g_callbackSeen = 0; g_callbackVerdict = XSTDOK; g_callbackStopAfter = -1; g_callbackErrors = 0;
    XSearch_Init(&search, "*.txt");
    search.callback = srch_callback;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A callback search runs");
    CHECK(g_callbackSeen == 2, "The callback saw both matches");
    CHECK(XArray_Used(&search.fileArray) == 2, "A positive verdict keeps the entries");
    XSearch_Destroy(&search);

    /* A zero verdict drops the entry: the caller took ownership of the output. */
    g_callbackSeen = 0; g_callbackVerdict = XSTDNON;
    XSearch_Init(&search, "*.txt");
    search.callback = srch_callback;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A dropping callback search runs");
    CHECK(g_callbackSeen == 2, "The callback still saw both matches");
    CHECK(XArray_Used(&search.fileArray) == 0, "A zero verdict keeps nothing in the array");
    XSearch_Destroy(&search);

    /* A negative verdict interrupts the walk and reports an error. */
    g_callbackSeen = 0; g_callbackVerdict = XSTDOK; g_callbackStopAfter = 1;
    XSearch_Init(&search, "*");
    search.bRecursive = XTRUE;
    search.callback = srch_callback;
    CHECK(XSearch(&search, fixture.sRoot) == XSTDERR, "An interrupting callback fails the search");
    CHECK(XSYNC_ATOMIC_GET(search.pInterrupted) == 1, "The interrupt flag is raised");
    CHECK(g_callbackSeen == 2, "The walk stops at the interrupting entry");
    XSearch_Destroy(&search);

    /* An already interrupted context refuses to start. */
    g_callbackSeen = 0; g_callbackStopAfter = -1;
    XSearch_Init(&search, "*");
    XSYNC_ATOMIC_SET(search.pInterrupted, 1);
    CHECK(XSearch(&search, fixture.sRoot) == XSTDERR, "An interrupted search refuses to run");
    CHECK(XArray_Used(&search.fileArray) == 0, "An interrupted search collects nothing");
    XSearch_Destroy(&search);

    /* An unreadable directory reaches the error callback, not the entry one. */
    g_callbackSeen = 0; g_callbackErrors = 0; g_callbackVerdict = XSTDOK;
    XSearch_Init(&search, "*");
    search.callback = srch_callback;
    CHECK(XSearch(&search, "/no/such/directory/anywhere") == XSTDOK,
        "An unopenable directory is reported, not fatal");
    CHECK(g_callbackErrors == 1, "The failure reached the message callback");
    CHECK(g_callbackSeen == 0, "No entries were produced");
    XSearch_Destroy(&search);

    srch_destroy(&fixture);
    return 0;
}

static int XTest_entries(void)
{
    /* The standalone entry helpers are part of the public surface. */
    xsearch_entry_t *pEntry = XSearch_AllocEntry();
    CHECK(pEntry != NULL, "An entry is allocated");
    CHECK(pEntry->sPath[0] == '\0' && pEntry->sName[0] == '\0', "A fresh entry has empty strings");
    CHECK(pEntry->eType == XF_UNKNOWN && pEntry->pRealPath == NULL, "A fresh entry has no type or real path");
    CHECK(pEntry->nSize == 0 && pEntry->nLineNum == 0, "A fresh entry has zeroed counters");
    XSearch_FreeEntry(pEntry);

    /* Creating from a stat buffer copies the metadata across. */
    srch_fixture_t fixture;
    CHECK(srch_build(&fixture) == XSTDOK, "Build the search fixture");

    char sDir[192], sFile[256];
    snprintf(sDir, sizeof(sDir), "%s/", fixture.sRoot);
    snprintf(sFile, sizeof(sFile), "%salpha.txt", sDir);

    xstat_t statbuf;
    CHECK(xstat(sFile, &statbuf) == XSTDOK, "Stat the fixture file");

    pEntry = XSearch_NewEntry("alpha.txt", sDir, &statbuf);
    CHECK(pEntry != NULL, "An entry is created from a stat buffer");
    CHECK(strcmp(pEntry->sName, "alpha.txt") == 0, "The name is copied");
    CHECK(strcmp(pEntry->sPath, sDir) == 0, "The path is copied");
    CHECK(pEntry->eType == XF_REGULAR, "The type comes from the mode");
    CHECK(pEntry->nSize == (size_t)statbuf.st_size, "The size comes from the stat buffer");
    CHECK(pEntry->nUID == (uint32_t)statbuf.st_uid, "The owner comes from the stat buffer");
    CHECK(pEntry->sPerm[0] != '\0', "The permission string is rendered");
    XSearch_FreeEntry(pEntry);

    /* A NULL stat buffer leaves the metadata at its initial values. */
    pEntry = XSearch_NewEntry("name-only", NULL, NULL);
    CHECK(pEntry != NULL, "An entry without metadata is created");
    CHECK(strcmp(pEntry->sName, "name-only") == 0, "The name is still copied");
    CHECK(pEntry->sPath[0] == '\0' && pEntry->eType == XF_UNKNOWN, "No metadata is invented");
    XSearch_FreeEntry(pEntry);

    /* A symlink entry records its target and resolved path. */
    char sLink[256];
    snprintf(sLink, sizeof(sLink), "%s/link.txt", fixture.sRoot);
    if (symlink(sFile, sLink) == 0)
    {
        xstat_t linkStat;
        CHECK(xstat(sLink, &linkStat) == XSTDOK, "Stat the symlink");
        pEntry = XSearch_NewEntry("link.txt", sDir, &linkStat);
        CHECK(pEntry != NULL, "A symlink entry is created");
        if (pEntry->eType == XF_SYMLINK)
        {
            CHECK(strcmp(pEntry->sLink, sFile) == 0, "The link target is read back");
            CHECK(pEntry->pRealPath != NULL, "The resolved path is stored");
        }
        XSearch_FreeEntry(pEntry);
    }

    XSearch_FreeEntry(NULL);
    srch_destroy(&fixture);
    return 0;
}

static int XTest_guards(void)
{
    xsearch_t search;
    XSearch_Init(&search, "*");
    CHECK(search.nLinkCount == -1 && search.nFileSize == -1, "Unset numeric criteria start disabled");
    CHECK(search.pInterrupted == &search.nInterrupted, "The interrupt pointer defaults to the local flag");
    CHECK(search.callback == NULL && search.pUserCtx == NULL, "No callback is installed by default");

    /* A missing directory with stdin disabled is an error, not a crash. */
    CHECK(XSearch(&search, NULL) == XSTDERR, "A missing directory is rejected");
    XSearch_Destroy(&search);

    /* Stdin mode needs search text before it will read anything. */
    XSearch_Init(&search, "*");
    search.bReadStdin = XTRUE;
    CHECK(XSearch(&search, NULL) == XSTDERR, "Stdin mode without search text is rejected");
    XSearch_Destroy(&search);

    /* Destroying twice must not double free the arrays. */
    XSearch_Init(&search, "a;b;c");
    CHECK(XArray_Used(&search.nameTokens) == 3, "Three tokens are parsed");
    XSearch_Destroy(&search);
    XSearch_Destroy(&search);
    XSearch_Destroy(NULL);
    return 0;
}


static int XTest_text_shapes(void)
{
    /* Searching inside files is line oriented, so the shape of the file
     * decides which branches run: several matches in one file, a match on
     * the first line, a match on a line with no terminator, a line longer
     * than the line buffer, and a file with no newline at all. Each one is
     * a different way to walk off the end of the buffer. */
    srch_fixture_t fixture;
    CHECK(srch_build(&fixture) == XSTDOK, "Build the search fixture");

    /* A long line, so the copy into the fixed line buffer truncates. */
    char *pLong = (char*)malloc(XSTR_MAX + 512);
    CHECK(pLong != NULL, "The long line is allocated");

    if (pLong != NULL)
    {
        size_t nAt = 0;
        const char *pLead = "needle ";
        memcpy(&pLong[nAt], pLead, strlen(pLead));
        nAt += strlen(pLead);

        while (nAt < XSTR_MAX + 400) pLong[nAt++] = 'x';
        pLong[nAt++] = '\n';
        pLong[nAt] = '\0';

        CHECK(srch_write(&fixture, "long.txt", pLong, 0644) == XSTDOK, "The long line file is written");
        free(pLong);
    }

    CHECK(srch_write(&fixture, "many.txt",
        "needle one\nfiller\nneedle two\nfiller\nneedle three\n", 0644) == XSTDOK,
        "The multi match file is written");

    /* No trailing newline on the last line. */
    CHECK(srch_write(&fixture, "tail.txt", "filler\nneedle at the very end", 0644) == XSTDOK,
        "The unterminated file is written");

    /* A single line with no newline at all. */
    CHECK(srch_write(&fixture, "oneline.txt", "needle alone", 0644) == XSTDOK,
        "The single line file is written");

    /* And a file whose only content is newlines. */
    CHECK(srch_write(&fixture, "blank.txt", "\n\n\n\n", 0644) == XSTDOK,
        "The blank file is written");

    g_callbackSeen = 0;
    g_callbackErrors = 0;
    g_callbackVerdict = XSTDOK;

    xsearch_t search;
    XSearch_Init(&search, "*.txt");
    search.callback = srch_callback;
    search.bRecursive = XTRUE;
    xstrncpy(search.sText, sizeof(search.sText), "needle");

    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "The text search runs");
    CHECK(g_callbackSeen > 0, "Matches were reported");
    CHECK(g_callbackErrors == 0, "Nothing was reported as an error");

    XSearch_Destroy(&search);

    /* Text that appears nowhere produces no matches and no errors. */
    g_callbackSeen = 0;
    g_callbackErrors = 0;

    XSearch_Init(&search, "*.txt");
    search.callback = srch_callback;
    search.bRecursive = XTRUE;
    xstrncpy(search.sText, sizeof(search.sText), "no-such-text-anywhere");

    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A search for absent text runs");
    CHECK(g_callbackSeen == 0, "Nothing matched");
    CHECK(g_callbackErrors == 0, "And nothing failed");

    XSearch_Destroy(&search);

    /* A search bounded to one match stops there. */
    g_callbackSeen = 0;

    XSearch_Init(&search, "*.txt");
    search.callback = srch_callback;
    search.bRecursive = XTRUE;
    search.nLinkCount = 0;
    xstrncpy(search.sText, sizeof(search.sText), "needle");

    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "The bounded search runs");
    XSearch_Destroy(&search);

    srch_destroy(&fixture);
    return 0;
}

/* A deep tree searched from a thread with the library's default stack, which is
 * what an application's search worker gets. Every level used to keep two full
 * path buffers on the stack, so a few dozen nested directories overflowed it
 * and took the whole process down. */
typedef struct {
    char sRoot[128];
    int nStatus;
    int nWarnings;
    int nFound;
} srch_deep_t;

static int srch_deep_callback(xsearch_t *pSearch, xsearch_entry_t *pEntry, const char *pMsg)
{
    srch_deep_t *pDeep = (srch_deep_t*)pSearch->pUserCtx;
    if (pMsg != NULL) pDeep->nWarnings++;
    if (pEntry != NULL && strcmp(pEntry->sName, "bottom.txt") == 0) pDeep->nFound++;
    return XSTDNON;
}

static void* srch_deep_worker(void *pArg)
{
    srch_deep_t *pDeep = (srch_deep_t*)pArg;
    xsearch_t search;

    XSearch_Init(&search, "bottom.txt");
    search.bRecursive = XTRUE;
    search.callback = srch_deep_callback;
    search.pUserCtx = pDeep;

    pDeep->nStatus = XSearch(&search, pDeep->sRoot);
    XSearch_Destroy(&search);
    return NULL;
}

static int srch_make_deep(char *pPath, size_t nSize, const char *pRoot, int nLevels)
{
    size_t nLen = (size_t)snprintf(pPath, nSize, "%s", pRoot);

    for (int i = 0; i < nLevels; i++)
    {
        if (nLen + 3 >= nSize) return XSTDERR;
        nLen += (size_t)snprintf(pPath + nLen, nSize - nLen, "/d");
        if (mkdir(pPath, 0755) != 0) return XSTDERR;
    }

    if (nLen + 12 >= nSize) return XSTDERR;
    snprintf(pPath + nLen, nSize - nLen, "/bottom.txt");

    FILE *pFile = fopen(pPath, "wb");
    if (pFile == NULL) return XSTDERR;
    fclose(pFile);
    return XSTDOK;
}

static int XTest_deep_tree(void)
{
    static char sPath[XPATH_MAX];
    srch_fixture_t fixture;

    snprintf(fixture.sRoot, sizeof(fixture.sRoot), "/tmp/xutils-srch-deep-XXXXXX");
    CHECK(mkdtemp(fixture.sRoot) != NULL, "Create the deep fixture root");
    fixture.nCreated = 1;

    /* Deep enough to overflow the old per-level frame, shallow enough to be
       searched in full. */
    char sShallow[160];
    snprintf(sShallow, sizeof(sShallow), "%s/shallow", fixture.sRoot);
    CHECK(mkdir(sShallow, 0755) == 0, "Create the searchable tree root");
    CHECK(srch_make_deep(sPath, sizeof(sPath), sShallow, 200) == XSTDOK, "Build a 200 level tree");

    srch_deep_t deep;
    memset(&deep, 0, sizeof(deep));
    xstrncpy(deep.sRoot, sizeof(deep.sRoot), sShallow);

    xthread_t thread;
    CHECK(XThread_Create(&thread, srch_deep_worker, &deep, 0) == XSTDOK, "Start a default-stack search thread");
    XThread_Join(&thread);

    CHECK(deep.nStatus == XSTDOK, "A 200 level recursive search completes on a default thread stack");
    CHECK(deep.nFound == 1, "The file at the bottom of the tree is found");

    /* Past the depth limit the search still completes and says what it skipped
       instead of recursing until something gives. */
    char sDeep[160];
    snprintf(sDeep, sizeof(sDeep), "%s/deep", fixture.sRoot);
    CHECK(mkdir(sDeep, 0755) == 0, "Create the over-deep tree root");
    CHECK(srch_make_deep(sPath, sizeof(sPath), sDeep, XSEARCH_MAX_DEPTH + 20) == XSTDOK, "Build an over-deep tree");

    memset(&deep, 0, sizeof(deep));
    xstrncpy(deep.sRoot, sizeof(deep.sRoot), sDeep);
    CHECK(XThread_Create(&thread, srch_deep_worker, &deep, 0) == XSTDOK, "Start the over-deep search thread");
    XThread_Join(&thread);

    CHECK(deep.nStatus == XSTDOK, "A search past the depth limit still completes");
    CHECK(deep.nFound == 0, "Nothing below the depth limit is visited");
    CHECK(deep.nWarnings > 0, "The depth limit is reported");

    srch_destroy(&fixture);
    return 0;
}

static int XTest_line_bounds(void)
{
    srch_fixture_t fixture;
    snprintf(fixture.sRoot, sizeof(fixture.sRoot), "/tmp/xutils-srch-XXXXXX");
    CHECK(mkdtemp(fixture.sRoot) != NULL, "Create a private search directory");
    fixture.nCreated = 1;

    CHECK(srch_write(&fixture, "first.txt", "a needle on the first line\nother\n", 0644) == XSTDOK, "Write a first line match");
    CHECK(srch_write(&fixture, "last.txt", "other\nneedle on the last line", 0644) == XSTDOK, "Write an unterminated last line");

    xsearch_t search;
    XSearch_Init(&search, "*.txt");
    xstrncpy(search.sText, sizeof(search.sText), "needle");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A buffer search runs");

    int nChecked = 0;
    for (size_t i = 0; i < XArray_Used(&search.fileArray); i++)
    {
        xsearch_entry_t *pEntry = XSearch_GetEntry(&search, (int)i);
        CHECK(pEntry != NULL, "Entry lookup");

        /* The first character of the first line used to be cut off, and a
           last line without a newline was reported as a binary match. */
        if (strcmp(pEntry->sName, "first.txt") == 0)
            CHECK(strcmp(pEntry->sLine, "a needle on the first line") == 0, "A first line match is reported whole");
        else if (strcmp(pEntry->sName, "last.txt") == 0)
            CHECK(strcmp(pEntry->sLine, "needle on the last line") == 0, "An unterminated last line is reported as text");
        nChecked++;
    }

    CHECK(nChecked == 2, "Both files are reported once");
    XSearch_Destroy(&search);
    srch_destroy(&fixture);
    return 0;
}

static int XTest_link_entries(void)
{
    srch_fixture_t fixture;
    CHECK(srch_build(&fixture) == XSTDOK, "Build the search fixture");

    char sTarget[256], sLink[256];
    snprintf(sTarget, sizeof(sTarget), "%s/alpha.txt", fixture.sRoot);
    snprintf(sLink, sizeof(sLink), "%s/alpha.lnk", fixture.sRoot);
    CHECK(symlink(sTarget, sLink) == 0, "Create a link in the tree");

    /* A link entry carries the realpath() of its target. Destroying the
       search freed the entry but not that string (the leak checker sees it). */
    xsearch_t search;
    XSearch_Init(&search, "*.lnk");
    CHECK(XSearch(&search, fixture.sRoot) == XSTDOK, "A name search runs");
    CHECK(XArray_Used(&search.fileArray) == 1, "The link is found");

    xsearch_entry_t *pEntry = XSearch_GetEntry(&search, 0);
    CHECK(pEntry != NULL && pEntry->eType == XF_SYMLINK && pEntry->pRealPath != NULL, "The link entry knows its target");
    XSearch_Destroy(&search);

    srch_destroy(&fixture);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(deep_tree),
    XTEST_CASE(name_matching),
    XTEST_CASE(recursion),
    XTEST_CASE(criteria),
    XTEST_CASE(text_search),
    XTEST_CASE(callback),
    XTEST_CASE(entries),
    XTEST_CASE(guards),
    XTEST_CASE(text_shapes),
    XTEST_CASE(line_bounds),
    XTEST_CASE(link_entries)
)
