/* Real filesystem operations under allocation, metadata and I/O failures. */
#include "test.h"
#include "xfs.h"
#include <fcntl.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

enum { FS_FAULT_NONE, FS_FAULT_ALLOC, FS_FAULT_OPEN, FS_FAULT_STAT, FS_FAULT_READ, FS_FAULT_WRITE };
static int g_nFault;
static int g_nFailAt;
static int g_nCalls;
static int g_nHits;
static int g_nFD = -1;
static xbool_t g_bPersistent;
static size_t g_nReadLimit;

static void fs_arm(int nFault, int nFailAt)
{
    g_nFault = nFault;
    g_nFailAt = nFailAt;
    g_nCalls = g_nHits = 0;
    g_bPersistent = XFALSE;
    g_nReadLimit = 0;
}

static int fs_fail(int nFault)
{
    if (g_nFault != nFault) return 0;
    g_nCalls++;
    if (g_nCalls < g_nFailAt || (!g_bPersistent && g_nCalls != g_nFailAt)) return 0;
    g_nHits++;
    errno = nFault == FS_FAULT_ALLOC ? ENOMEM : EIO;
    return 1;
}

void *__real_malloc(size_t);
int __real_open(const char*, int, ...);
int __real_fstat(int, struct stat*);
ssize_t __real_read(int, void*, size_t);
ssize_t __real_write(int, const void*, size_t);

void *__wrap_malloc(size_t nSize) { return fs_fail(FS_FAULT_ALLOC) ? NULL : __real_malloc(nSize); }

int __wrap_open(const char *pPath, int nFlags, ...)
{
    mode_t nMode = 0;
    if (nFlags & O_CREAT)
    {
        va_list args;
        va_start(args, nFlags);
        nMode = va_arg(args, mode_t);
        va_end(args);
    }
    if (fs_fail(FS_FAULT_OPEN)) return -1;
    return g_nFD = __real_open(pPath, nFlags, nMode);
}

int __wrap_fstat(int nFD, struct stat *pStat) { return fs_fail(FS_FAULT_STAT) ? -1 : __real_fstat(nFD, pStat); }

ssize_t __wrap_read(int nFD, void *pData, size_t nSize)
{
    if (fs_fail(FS_FAULT_READ)) return -1;
    if (g_nReadLimit) nSize = XSTD_MIN(nSize, g_nReadLimit);
    return __real_read(nFD, pData, nSize);
}

ssize_t __wrap_write(int nFD, const void *pData, size_t nSize)
{
    return fs_fail(FS_FAULT_WRITE) ? -1 : __real_write(nFD, pData, nSize);
}

static int XTest_metadata(void)
{
    char root[] = "/tmp/xutils-fs-meta-XXXXXX", path[256], missing[256];
    CHECK(mkdtemp(root) != NULL, "Create the metadata fixture");
    snprintf(path, sizeof(path), "%s/file", root);
    snprintf(missing, sizeof(missing), "%s/absent", root);
    CHECK(XPath_Write(path, (const uint8_t*)"file", 4, "cwt") == 4 && chmod(path, 0640) == 0, "Create known file metadata");
    char perms[16] = "sentinel";
    int nMissing = XPath_GetPerm(perms, sizeof(perms), missing);
    xbool_t bUnchanged = !strcmp(perms, "sentinel");
    CHECK(XPath_GetPerm(perms, sizeof(perms), path) == 9 && !strcmp(perms, "rw-r-----"), "Existing permissions match the mode");
    fs_arm(FS_FAULT_STAT, 1);
    long nSize = XPath_GetSize(path);
    int nHits = g_nHits;
    xbool_t bClosed = fcntl(g_nFD, F_GETFD) < 0 && errno == EBADF;
    fs_arm(FS_FAULT_NONE, 0);
    CHECK(XPath_GetSize(path) == 4, "A metadata retry reads the actual size");
    unlink(path);
    rmdir(root);
    CHECK(nMissing < 0 && bUnchanged, "Missing path permissions report failure without fabricating an empty mode");
    CHECK(nSize < 0 && nHits == 1 && bClosed, "A failed fstat reports no file size and releases the opened descriptor");
    return 0;
}

static int XTest_load(void)
{
    char path[] = "/tmp/xutils-fs-load-XXXXXX";
    int nFD = mkstemp(path);
    const uint8_t data[] = {'f', 'i', 'l', 'e', 0, 0xff, 'p', 'a', 'y', 'l', 'o', 'a', 'd'};
    CHECK(nFD >= 0 && write(nFD, data, sizeof(data)) == sizeof(data), "Create the binary load fixture");
    close(nFD);
    xbool_t bRejected = XTRUE;
    for (int nRead = 1; nRead <= 2; nRead++)
    {
        xfile_t file;
        CHECK(XFile_Open(&file, path, "r", NULL) >= 0, "Open the complete input");
        fs_arm(FS_FAULT_READ, nRead);
        g_nReadLimit = 4;
        size_t nSize = 19;
        uint8_t *pData = XFile_Load(&file, &nSize);
        if (pData || nSize || g_nHits != 1) bRejected = XFALSE;
        fs_arm(FS_FAULT_NONE, 0);
        free(pData);
        XFile_Close(&file);
    }
    size_t nSize = 0;
    uint8_t *pData = XPath_Load(path, &nSize);
    xbool_t bValid = pData && nSize == sizeof(data) && !memcmp(pData, data, sizeof(data)) && !pData[nSize];
    free(pData);
    unlink(path);
    CHECK(bRejected, "A read error before or after a partial read cannot return a successful truncated load");
    CHECK(bValid, "Loading recovers and returns the exact binary content with its terminator");
    return 0;
}

static int XTest_copy(void)
{
    char root[] = "/tmp/xutils-fs-copy-XXXXXX", source[256], target[256];
    CHECK(mkdtemp(root) != NULL, "Create the copy failure fixture");
    snprintf(source, sizeof(source), "%s/source", root);
    snprintf(target, sizeof(target), "%s/target", root);
    uint8_t data[8193];
    for (size_t i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(i * 131 + (i >> 8));
    CHECK(XPath_Write(source, data, sizeof(data), "cwt") == sizeof(data), "Create the exact copy source");
    const int faults[] = {FS_FAULT_OPEN, FS_FAULT_STAT, FS_FAULT_READ, FS_FAULT_WRITE};
    xbool_t bValid = XTRUE;
    for (int nExisting = 0; nExisting < 2; nExisting++)
        for (size_t i = 0; i < sizeof(faults) / sizeof(*faults); i++)
        {
            if (nExisting) CHECK(XPath_Write(target, (const uint8_t*)"previous", 8, "cwt") == 8, "Create an existing target");
            fs_arm(faults[i], faults[i] == FS_FAULT_OPEN ? 2 : 1);
            int nResult = XPath_CopyFile(source, target);
            int nError = errno, nHits = g_nHits;
            fs_arm(FS_FAULT_NONE, 0);
            if (nResult >= 0 || nError != EIO || nHits != 1 || XPath_Exists(target) != nExisting) bValid = XFALSE;
            CHECK(XPath_CopyFile(source, target) == sizeof(data), "A failed copy can be retried");
            size_t nSize = 0;
            uint8_t *pData = XPath_Load(target, &nSize);
            if (!pData || nSize != sizeof(data) || memcmp(pData, data, sizeof(data))) bValid = XFALSE;
            free(pData);
            unlink(target);
        }
    fs_arm(FS_FAULT_ALLOC, 1);
    int nCopied = XPath_CopyFile(source, target);
    int nHits = g_nHits;
    fs_arm(FS_FAULT_NONE, 0);
    size_t nSize = 0;
    uint8_t *pData = XPath_Load(target, &nSize);
    xbool_t bFallback = pData && nSize == sizeof(data) && !memcmp(pData, data, sizeof(data));
    free(pData);
    unlink(target);
    fs_arm(FS_FAULT_ALLOC, 1);
    g_bPersistent = XTRUE;
    int nFailed = XPath_CopyFile(source, target);
    int nFailedHits = g_nHits;
    fs_arm(FS_FAULT_NONE, 0);
    xbool_t bRemoved = !XPath_Exists(target);
    unlink(source);
    rmdir(root);
    CHECK(bValid, "A failed copy preserves its error and target ownership, and a retry copies every source byte");
    CHECK(nCopied == sizeof(data) && nHits == 1 && bFallback, "Copy falls back to a smaller buffer without losing data");
    CHECK(nFailed < 0 && nFailedHits == 2 && bRemoved, "Failure of both copy buffers leaves no newly created target");
    return 0;
}

static int XTest_remove(void)
{
    CHECK(XDir_Remove(NULL) < 0 && errno == EINVAL, "A missing directory argument fails without walking the filesystem");
    CHECK(XDir_Remove("") < 0 && errno == EINVAL, "An empty directory argument fails without walking the filesystem");
    for (int nFailAt = 1; nFailAt <= 2; nFailAt++)
    {
        char root[] = "/tmp/xutils-fs-remove-XXXXXX", path[256];
        CHECK(mkdtemp(root) != NULL, "Create an allocation failure tree");
        snprintf(path, sizeof(path), "%s/entry", root);
        CHECK(XPath_Write(path, (const uint8_t*)"retained", 8, "cwt") == 8, "Create the child file");
        fs_arm(FS_FAULT_ALLOC, nFailAt);
        int nStatus = XDir_Remove(root);
        int nError = errno, nHits = g_nHits;
        fs_arm(FS_FAULT_NONE, 0);
        xbool_t bRetained = XPath_Exists(root) && XPath_GetSize(path) == 8;
        CHECK(XDir_Remove(root) == XSTDOK && !XPath_Exists(root), "Removing retries after allocation failure");
        CHECK(nStatus < 0 && nError == ENOMEM && nHits == 1 && bRetained, "Failed removal leaves unprocessed entries intact");
    }
    char missing[] = "/tmp/xutils-fs-missing-XXXXXX";
    int nFD = mkstemp(missing);
    CHECK(nFD >= 0, "Reserve a unique nonexistent path");
    close(nFD);
    unlink(missing);
    CHECK(XDir_Remove(missing) < 0 && errno == ENOENT, "A missing directory retains the filesystem error");
    CHECK(XFile_Alloc(missing, "r", NULL) == NULL, "Failed heap file opening releases its allocated handle");
    return 0;
}

static int XTest_types(void)
{
    char root[] = "/tmp/xutils-fs-types-XXXXXX", path[256], fifo[256], link[256];
    CHECK(mkdtemp(root) != NULL, "Create the file type fixture");
    snprintf(path, sizeof(path), "%s/file", root);
    snprintf(fifo, sizeof(fifo), "%s/pipe", root);
    snprintf(link, sizeof(link), "%s/link", root);
    CHECK(XPath_Write(path, (const uint8_t*)"data", 4, "cwt") == 4 && mkfifo(fifo, 0600) == 0 && symlink(path, link) == 0,
        "Create regular, FIFO and symbolic link entries");
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/socket", root);
    int nSock = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(nSock >= 0 && bind(nSock, (struct sockaddr*)&addr, sizeof(addr)) == 0, "Create a filesystem socket entry");
    struct { const char *pPath; char cType; xfile_type_t eType; } cases[] = {
        {root, 'd', XF_DIRECTORY}, {path, '-', XF_REGULAR}, {fifo, 'p', XF_PIPE},
        {link, 'l', XF_SYMLINK}, {addr.sun_path, 's', XF_SOCKET}, {"/dev/null", 'c', XF_CHAR_DEVICE}
    };
    xbool_t bValid = XTRUE;
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
    {
        struct stat info;
        CHECK(lstat(cases[i].pPath, &info) == 0, "Read each entry type independently from the kernel");
        if (XPath_GetType(info.st_mode) != cases[i].cType || XFile_GetType(info.st_mode) != cases[i].eType ||
            XFile_GetTypeChar(cases[i].eType) != cases[i].cType) bValid = XFALSE;
    }
    CHECK(XPath_GetType(S_IFBLK | 0640) == 'b' && XFile_GetType(S_IFBLK | 0640) == XF_BLOCK_DEVICE &&
        XFile_GetTypeChar(XF_BLOCK_DEVICE) == 'b', "Block device modes are independent of their permission bits");
    CHECK(XPath_GetType(0) == '?' && XFile_GetType(0) == 0 && XFile_GetTypeChar((xfile_type_t)255) == '?',
        "Unknown modes and enum values cannot be mistaken for a known file type");
    close(nSock);
    unlink(addr.sun_path);
    unlink(link);
    unlink(fifo);
    unlink(path);
    rmdir(root);
    CHECK(bValid, "Every real filesystem entry is classified with its exact type and display character");
    return 0;
}

static int XTest_flags(void)
{
    char root[] = "/tmp/xutils-fs-flags-XXXXXX", path[256];
    CHECK(mkdtemp(root) != NULL, "Create the open flag fixture");
    snprintf(path, sizeof(path), "%s/file", root);
    xfile_t file;
    CHECK(XFile_OpenM(&file, path, "cxsei", 0600) >= 0, "Exclusively create a synchronous read/write file");
    int nFlags = fcntl(file.nFD, F_GETFL), nDescriptorFlags = fcntl(file.nFD, F_GETFD);
    CHECK((nFlags & O_ACCMODE) == O_RDWR && (nFlags & O_SYNC) == O_SYNC && (nDescriptorFlags & FD_CLOEXEC),
        "The kernel received read/write, synchronous and non-inheritable flags");
    CHECK(XFile_Write(&file, "a\0b", 3) == 3 && XFile_Seek(&file, 0, SEEK_SET) == 0, "The file accepts binary content");
    uint8_t data[4] = {0};
    CHECK(XFile_Read(&file, data, sizeof(data)) == 3 && !memcmp(data, "a\0b", 3), "The same handle reads the exact content");
    XFile_Close(&file);
    CHECK(XFile_OpenM(&file, path, "cxse", 0600) < 0 && errno == EEXIST, "Exclusive creation refuses an existing file");
    CHECK(XFile_Open(&file, path, "r?d", NULL) >= 0 && (fcntl(file.nFD, F_GETFL) & O_NONBLOCK),
        "Nonblocking open tolerates an unknown flag character");
    memset(data, 0, sizeof(data));
    CHECK(XFile_Read(&file, data, sizeof(data)) == 3 && !memcmp(data, "a\0b", 3),
        "Refused creation preserved the original bytes");
    XFile_Close(&file);
    unlink(path);
    rmdir(root);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(metadata),
    XTEST_CASE(load),
    XTEST_CASE(copy),
    XTEST_CASE(remove),
    XTEST_CASE(types),
    XTEST_CASE(flags)
)
