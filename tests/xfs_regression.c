/* libxutils: filesystem operations isolated in private temporary directories. */
#include "test.h"
#include "xfs.h"
#include <unistd.h>

static int XTest_copy(void)
{
    char directory[] = "/tmp/xutils-copy-XXXXXX", source[256], target[256];
    CHECK(mkdtemp(directory) != NULL, "Create private filesystem fixture");
    snprintf(source, sizeof(source), "%s/source", directory);
    snprintf(target, sizeof(target), "%s/target", directory);
    const size_t lengths[] = {0, 1, 4095, 4096, 4097, 131073};
    uint8_t *pData = (uint8_t*)malloc(131073);
    CHECK(pData != NULL, "Allocate copy payload");
    for (size_t i = 0; i < 131073; i++) pData[i] = (uint8_t)(i % 251);
    for (size_t i = 0; i < sizeof(lengths) / sizeof(*lengths); i++)
    {
        xfile_t file;
        CHECK(XFile_OpenM(&file, source, "cwt", 0600) >= 0, "Open source with private permissions");
        if (lengths[i]) CHECK(XFile_Write(&file, pData, lengths[i]) == (int)lengths[i], "Write binary source");
        XFile_Close(&file);
        CHECK(XPath_CopyFile(source, target) >= 0, "Copy empty and nonempty regular files");
        CHECK(XPath_GetSize(target) == (long)lengths[i], "Copy replaces old target length exactly");
        if (lengths[i])
        {
            size_t nLoaded = 0;
            uint8_t *pLoaded = XPath_Load(target, &nLoaded);
            CHECK(pLoaded && nLoaded == lengths[i] && memcmp(pLoaded, pData, nLoaded) == 0,
                "Loaded copy matches every source byte");
            free(pLoaded);
        }
    }
    free(pData);
    CHECK(unlink(source) == 0 && unlink(target) == 0 && rmdir(directory) == 0, "Remove private fixture");
    return 0;
}

static int XTest_permissions(void)
{
    for (unsigned int nMode = 0; nMode <= 0777; nMode++)
    {
        char permissions[16];
        xmode_t nParsed;
        CHECK(XPath_ModeToPerm(permissions, sizeof(permissions), nMode) > 0, "Format all permission combinations");
        CHECK(XPath_PermToMode(permissions, &nParsed) > 0 && (nParsed & 0777) == nMode, "Permission conversion is reversible");
    }
    return 0;
}

static int XTest_directories(void)
{
    char directory[] = "/tmp/xutils-dir-XXXXXX", nested[256], entry[256];
    CHECK(mkdtemp(directory) != NULL, "Create directory fixture");
    snprintf(nested, sizeof(nested), "%s/child", directory);
    CHECK(XDir_Create(nested, 0700) > 0, "Create child directory");
    xdir_t dir;
    CHECK(XDir_Open(&dir, directory) > 0, "Open directory iterator");
    int nChildren = 0;
    while (XDir_Read(&dir, entry, sizeof(entry)) > 0)
    {
        if (strcmp(entry, ".") && strcmp(entry, ".."))
        {
            CHECK(strcmp(entry, "child") == 0, "Directory enumeration returns only the owned fixture child");
            nChildren++;
        }
    }
    XDir_Close(&dir);
    CHECK(nChildren == 1, "Directory iterator reports each child once");
    CHECK(rmdir(nested) == 0 && rmdir(directory) == 0, "Remove directory fixture");
    return 0;
}

static int XTest_nonregular_source(void)
{
    char directory[] = "/tmp/xutils-fifo-XXXXXX", fifo[256], target[256];
    CHECK(mkdtemp(directory) != NULL, "Create FIFO fixture");
    snprintf(fifo, sizeof(fifo), "%s/source", directory);
    snprintf(target, sizeof(target), "%s/target", directory);
    CHECK(mkfifo(fifo, 0600) == 0, "Create a FIFO without a writer");
    CHECK(XPath_CopyFile(fifo, target) < 0 && !XPath_Exists(target), "Copy rejects a FIFO without blocking or creating a target");
    CHECK(unlink(fifo) == 0 && rmdir(directory) == 0, "Remove FIFO fixture");
    return 0;
}


static int XTest_copy_failures(void)
{
    /* A copy that cannot be finished must say so. Reporting the bytes that
     * did land instead turns a full disk into a truncated file that looked
     * like a success, which is the failure mode that loses data quietly. */
    char sDir[] = "/tmp/xutils-copyfail-XXXXXX";
    CHECK(mkdtemp(sDir) != NULL, "Create private filesystem fixture");

    char sSource[256], sTarget[256];
    snprintf(sSource, sizeof(sSource), "%s/source", sDir);
    snprintf(sTarget, sizeof(sTarget), "%s/target", sDir);

    /* A source large enough that the copy loop runs more than once. */
    const size_t nSize = 300000;
    uint8_t *pData = (uint8_t*)malloc(nSize);
    CHECK(pData != NULL, "Allocate the payload");

    for (size_t i = 0; i < nSize; i++) pData[i] = (uint8_t)(i % 253);

    xfile_t file;
    CHECK(XFile_OpenM(&file, sSource, "cwt", 0600) >= 0, "Open the source");
    CHECK(XFile_Write(&file, pData, nSize) == (int)nSize, "Write the source");
    XFile_Close(&file);

    /* Copying into a destination that was never opened is refused before
     * anything is read. */
    xfile_t in, out;
    CHECK(XFile_Open(&in, sSource, "r", NULL) >= 0, "Open the source for reading");

    memset(&out, 0, sizeof(out));
    out.nFD = XSTDERR;
    CHECK(XFile_Copy(&in, &out) == XSTDERR, "Copying into an unopened destination is refused");
    XFile_Close(&in);

    /* And copying from one that was never opened. */
    CHECK(XFile_Open(&out, sTarget, "cwt", NULL) >= 0, "Open the destination");

    memset(&in, 0, sizeof(in));
    in.nFD = XSTDERR;
    CHECK(XFile_Copy(&in, &out) == XSTDERR, "Copying from an unopened source is refused");
    XFile_Close(&out);

    /* A destination opened read only cannot be written, and the copy has
     * to report that rather than returning the bytes it read. */
    CHECK(XFile_Open(&in, sSource, "r", NULL) >= 0, "Reopen the source");
    CHECK(XFile_Open(&out, sTarget, "r", NULL) >= 0, "Open the destination read only");

    int nCopied = XFile_Copy(&in, &out);
    CHECK(nCopied == XSTDERR, "A destination that cannot be written fails the copy");

    XFile_Close(&in);
    XFile_Close(&out);

    /* The honest copy still works and reports the whole length. */
    CHECK(XFile_Open(&in, sSource, "r", NULL) >= 0, "Reopen the source again");
    CHECK(XFile_Open(&out, sTarget, "cwt", NULL) >= 0, "Reopen the destination for writing");

    nCopied = XFile_Copy(&in, &out);
    CHECK(nCopied == (int)nSize, "A complete copy reports every byte");

    XFile_Close(&in);
    XFile_Close(&out);

    CHECK(XPath_GetSize(sTarget) == (long)nSize, "The destination is the source's length");

    size_t nLoaded = 0;
    uint8_t *pLoaded = XPath_Load(sTarget, &nLoaded);
    CHECK(pLoaded != NULL && nLoaded == nSize, "The destination loads back whole");
    CHECK(pLoaded == NULL || memcmp(pLoaded, pData, nLoaded) == 0, "Every byte survived the copy");
    free(pLoaded);

    /* An empty source is a valid copy of nothing, not an error. */
    char sEmpty[256], sEmptyTarget[256];
    snprintf(sEmpty, sizeof(sEmpty), "%s/empty", sDir);
    snprintf(sEmptyTarget, sizeof(sEmptyTarget), "%s/empty-copy", sDir);

    CHECK(XFile_OpenM(&file, sEmpty, "cwt", 0600) >= 0, "Create an empty source");
    XFile_Close(&file);

    CHECK(XFile_Open(&in, sEmpty, "r", NULL) >= 0, "Open the empty source");
    CHECK(XFile_Open(&out, sEmptyTarget, "cwt", NULL) >= 0, "Open its destination");
    CHECK(XFile_Copy(&in, &out) >= 0, "Copying an empty file succeeds");
    XFile_Close(&in);
    XFile_Close(&out);

    CHECK(XPath_GetSize(sEmptyTarget) == 0, "The copy of an empty file is empty");

    /* The argument guards. */
    CHECK(XFile_Copy(NULL, &out) == XSTDERR, "A missing source is refused");
    CHECK(XFile_Copy(&in, NULL) == XSTDERR, "A missing destination is refused");

    free(pData);
    unlink(sSource);
    unlink(sTarget);
    unlink(sEmpty);
    unlink(sEmptyTarget);
    CHECK(rmdir(sDir) == 0, "Remove the private fixture");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(copy),
    XTEST_CASE(permissions),
    XTEST_CASE(directories),
    XTEST_CASE(nonregular_source),
    XTEST_CASE(copy_failures)
)
