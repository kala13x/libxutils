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

XTEST_MAIN(
    XTEST_CASE(copy),
    XTEST_CASE(permissions),
    XTEST_CASE(directories),
    XTEST_CASE(nonregular_source)
)
