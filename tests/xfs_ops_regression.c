/* libxutils: file handles, line readers, path helpers and directory walks.
 *
 * Everything runs inside a private temporary directory so the assertions
 * can be on exact contents and exact sizes. The line readers in particular
 * are checked against a file whose last line has no terminator, which is
 * where an off-by-one in the line count would show up.
 */

#include "test.h"
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <unistd.h>
#include "xfs.h"
#include "buf.h"
#include "str.h"
#include <unistd.h>
#include <sys/stat.h>

typedef struct {
    char sRoot[64];
    int nCreated;
} xfs_fixture_t;

static int xfs_begin(xfs_fixture_t *pFixture)
{
    snprintf(pFixture->sRoot, sizeof(pFixture->sRoot), "/tmp/xutils-xfs-XXXXXX");
    if (mkdtemp(pFixture->sRoot) == NULL) return XSTDERR;
    pFixture->nCreated = 1;
    return XSTDOK;
}

static void xfs_end(xfs_fixture_t *pFixture)
{
    if (!pFixture->nCreated) return;
    char sCommand[128];
    snprintf(sCommand, sizeof(sCommand), "rm -rf %s", pFixture->sRoot);
    if (system(sCommand) < 0) { /* the fixture leaks at worst */ }
    pFixture->nCreated = 0;
}

static void xfs_path(const xfs_fixture_t *pFixture, char *pDst, size_t nSize, const char *pName)
{
    snprintf(pDst, nSize, "%s/%s", pFixture->sRoot, pName);
}

static int XTest_file_io(void)
{
    xfs_fixture_t fixture;
    CHECK(xfs_begin(&fixture) == XSTDOK, "Create the filesystem fixture");

    char sPath[320];
    xfs_path(&fixture, sPath, sizeof(sPath), "io.bin");

    /* Writing, then reading back byte for byte including a NUL. */
    const uint8_t payload[] = {'h', 'e', 'a', 'd', 0x00, 0xff, 't', 'a', 'i', 'l'};
    xfile_t file;
    CHECK(XFile_OpenM(&file, sPath, "cwt", 0644) >= 0, "A file opens for writing");
    CHECK(XFile_IsOpen(&file) == XTRUE, "The handle reports itself open");
    CHECK(XFile_Write(&file, payload, sizeof(payload)) == (int)sizeof(payload), "Every byte is written");
    XFile_Close(&file);
    CHECK(XFile_IsOpen(&file) == XFALSE, "A closed handle reports itself closed");

    CHECK(XPath_GetSize(sPath) == (long)sizeof(payload), "The file is exactly as long as what was written");

    uint8_t sRead[32];
    CHECK(XFile_Open(&file, sPath, "r", NULL) >= 0, "The file opens for reading");
    CHECK(XFile_Read(&file, sRead, sizeof(sRead)) == (int)sizeof(payload), "Every byte is read back");
    CHECK(memcmp(sRead, payload, sizeof(payload)) == 0, "The bytes survive the round trip");

    /* Seeking back to the start re-reads the same bytes. */
    CHECK(XFile_Seek(&file, 0, SEEK_SET) == 0, "The handle seeks back to the start");
    memset(sRead, 0, sizeof(sRead));
    CHECK(XFile_Read(&file, sRead, 4) == 4, "A partial read returns what was asked for");
    CHECK(memcmp(sRead, "head", 4) == 0, "The partial read starts at the seek position");

    /* Seeking into the middle. */
    CHECK(XFile_Seek(&file, 6, SEEK_SET) == 6, "The handle seeks into the middle");
    memset(sRead, 0, sizeof(sRead));
    CHECK(XFile_Read(&file, sRead, 4) == 4, "The read after a seek returns the rest");
    CHECK(memcmp(sRead, "tail", 4) == 0, "The read after a seek starts where it was told");
    XFile_Close(&file);

    /* Appending adds to the end rather than truncating. The append flag on
     * its own leaves the handle read only, so the write flag comes too. */
    CHECK(XFile_Open(&file, sPath, "aw", NULL) >= 0, "The file opens for appending");
    CHECK(XFile_Write(&file, (const uint8_t*)"more", 4) == 4, "The append is written");
    XFile_Close(&file);
    CHECK(XPath_GetSize(sPath) == (long)sizeof(payload) + 4, "The append grew the file");

    /* Truncating starts over. */
    CHECK(XFile_OpenM(&file, sPath, "cwt", 0644) >= 0, "The file reopens truncating");
    XFile_Close(&file);
    CHECK(XPath_GetSize(sPath) == 0, "The truncating open emptied the file");

    /* Formatted output. */
    CHECK(XFile_Open(&file, sPath, "w", NULL) >= 0, "The file opens for formatted writing");
    CHECK(XFile_Print(&file, "value=%d name=%s\n", 42, "test") > 0, "The formatted line is written");
    XFile_Close(&file);

    size_t nLoaded = 0;
    uint8_t *pLoaded = XPath_Load(sPath, &nLoaded);
    CHECK(pLoaded != NULL, "The formatted file loads");
    CHECK(strcmp((char*)pLoaded, "value=42 name=test\n") == 0, "The formatted line is what was asked for");
    free(pLoaded);

    /* A file that does not exist cannot be opened or measured. */
    char sMissing[320];
    xfs_path(&fixture, sMissing, sizeof(sMissing), "absent");
    CHECK(XFile_Open(&file, sMissing, "r", NULL) < 0, "A missing file does not open");
    CHECK(XPath_GetSize(sMissing) < 0, "A missing file has no size");
    CHECK(XPath_Load(sMissing, &nLoaded) == NULL, "A missing file does not load");
    CHECK(XPath_Exists(sMissing) == XFALSE, "A missing file does not exist");
    CHECK(XPath_Exists(sPath) == XTRUE, "A written file exists");

    xfs_end(&fixture);
    return 0;
}

static int XTest_handles(void)
{
    xfs_fixture_t fixture;
    CHECK(xfs_begin(&fixture) == XSTDOK, "Create the filesystem fixture");

    char sPath[320];
    xfs_path(&fixture, sPath, sizeof(sPath), "handle.txt");

    /* The heap handle owns itself and clears the caller pointer. */
    xfile_t *pFile = XFile_Alloc(sPath, "cwt", NULL);
    CHECK(pFile != NULL, "A heap handle is allocated");
    CHECK(XFile_IsOpen(pFile) == XTRUE, "The heap handle is open");
    CHECK(XFile_Print(pFile, "line\n") > 0, "The heap handle writes");
    XFile_Free(&pFile);
    CHECK(pFile == NULL, "Freeing clears the caller pointer");

    /* Reopening swaps the underlying file without leaking the old one. */
    char sSecond[320];
    xfs_path(&fixture, sSecond, sizeof(sSecond), "second.txt");

    xfile_t file;
    CHECK(XFile_Open(&file, sPath, "r", NULL) >= 0, "The first file opens");
    CHECK(XFile_Reopen(&file, sSecond, "cwt", NULL) >= 0, "The handle reopens on another file");
    CHECK(XFile_Print(&file, "second\n") > 0, "The reopened handle writes to the new file");
    XFile_Close(&file);

    CHECK(XPath_GetSize(sSecond) == 7, "The new file has the new contents");
    CHECK(XPath_GetSize(sPath) == 5, "The old file is untouched");

    /* Closing twice and destroying a closed handle are both safe. */
    XFile_Close(&file);
    XFile_Destroy(&file);
    CHECK(XFile_IsOpen(&file) == XFALSE, "A repeatedly closed handle stays closed");

    XFile_Free(NULL);
    CHECK(XFile_IsOpen(NULL) == XFALSE, "A missing handle is not open");

    xfs_end(&fixture);
    return 0;
}

static int XTest_lines(void)
{
    xfs_fixture_t fixture;
    CHECK(xfs_begin(&fixture) == XSTDOK, "Create the filesystem fixture");

    char sPath[320];
    xfs_path(&fixture, sPath, sizeof(sPath), "lines.txt");

    /* Three lines, the last one without a terminator: this is where a line
     * counter that counts newlines rather than lines goes wrong. */
    FILE *pRaw = fopen(sPath, "wb");
    CHECK(pRaw != NULL, "Write the line fixture");
    fputs("first line\nsecond line\nthird line", pRaw);
    fclose(pRaw);

    xfile_t file;
    CHECK(XFile_Open(&file, sPath, "r", NULL) >= 0, "The line file opens");
    int nLines = XFile_GetLineCount(&file);
    CHECK(nLines == 3, "The unterminated last line is still counted");
    XFile_Close(&file);

    /* Reading the lines out one at a time. */
    char sLine[128];
    CHECK(XFile_Open(&file, sPath, "r", NULL) >= 0, "The line file reopens");
    CHECK(XFile_GetLine(&file, sLine, sizeof(sLine)) > 0, "The first line reads");
    CHECK(strncmp(sLine, "first line", 10) == 0, "The first line is the first one");
    CHECK(XFile_GetLine(&file, sLine, sizeof(sLine)) > 0, "The second line reads");
    CHECK(strncmp(sLine, "second line", 11) == 0, "The second line follows the first");
    XFile_Close(&file);

    /* Reading a line by its number. */
    for (size_t i = 1; i <= 3; i++)
    {
        CHECK(XFile_Open(&file, sPath, "r", NULL) >= 0, "The line file reopens");
        CHECK(XFile_ReadLine(&file, sLine, sizeof(sLine), i) > 0, "The numbered line reads");
        XFile_Close(&file);

        const char *pExpected = i == 1 ? "first" : (i == 2 ? "second" : "third");
        CHECK(strstr(sLine, pExpected) != NULL, "Each numbered line is the one that was asked for");
    }

    /* A line past the end is not there. */
    CHECK(XFile_Open(&file, sPath, "r", NULL) >= 0, "The line file reopens");
    CHECK(XFile_ReadLine(&file, sLine, sizeof(sLine), 99) <= 0, "A line past the end is not found");
    XFile_Close(&file);

    /* An empty file has no lines at all. */
    char sEmpty[320];
    xfs_path(&fixture, sEmpty, sizeof(sEmpty), "empty.txt");
    CHECK(XFile_OpenM(&file, sEmpty, "cwt", 0644) >= 0, "The empty file is created");
    XFile_Close(&file);

    /* The counter gates on the file having a size, so a zero length file
     * reports an error rather than a count of zero. Either way there is no
     * line to hand back. */
    CHECK(XFile_Open(&file, sEmpty, "r", NULL) >= 0, "The empty file opens");
    CHECK(XFile_GetLineCount(&file) <= 0, "An empty file yields no lines");
    CHECK(XFile_GetLine(&file, sLine, sizeof(sLine)) <= 0, "An empty file has no line to read");
    XFile_Close(&file);

    /* A file that is one empty line is a line. */
    char sNewline[320];
    xfs_path(&fixture, sNewline, sizeof(sNewline), "newline.txt");
    CHECK(XPath_Write(sNewline, (const uint8_t*)"\n", 1, "cwt") > 0, "A single newline is written");
    CHECK(XFile_Open(&file, sNewline, "r", NULL) >= 0, "The newline file opens");
    CHECK(XFile_GetLineCount(&file) == 1, "A lone newline is one line");
    XFile_Close(&file);

    xfs_end(&fixture);
    return 0;
}

static int XTest_load(void)
{
    xfs_fixture_t fixture;
    CHECK(xfs_begin(&fixture) == XSTDOK, "Create the filesystem fixture");

    char sPath[320];
    xfs_path(&fixture, sPath, sizeof(sPath), "payload.bin");

    /* A payload with a NUL in the middle proves the loaders use lengths. */
    uint8_t payload[1024];
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)(i % 256);
    CHECK(XPath_Write(sPath, payload, sizeof(payload), "cwt") > 0, "The payload is written");
    CHECK(XPath_GetSize(sPath) == (long)sizeof(payload), "The payload is exactly as long as written");

    size_t nLoaded = 0;
    uint8_t *pLoaded = XPath_Load(sPath, &nLoaded);
    CHECK(pLoaded != NULL && nLoaded == sizeof(payload), "The whole payload loads");
    CHECK(memcmp(pLoaded, payload, sizeof(payload)) == 0, "Every payload byte survives");
    free(pLoaded);

    /* A capped load stops at the cap rather than reading the whole file. */
    pLoaded = XPath_LoadSize(sPath, 64, &nLoaded);
    CHECK(pLoaded != NULL && nLoaded == 64, "A capped load stops at its cap");
    CHECK(memcmp(pLoaded, payload, 64) == 0, "The capped load starts at the beginning");
    free(pLoaded);

    /* A cap larger than the file just loads the file. */
    pLoaded = XPath_LoadSize(sPath, sizeof(payload) * 4, &nLoaded);
    CHECK(pLoaded != NULL && nLoaded == sizeof(payload), "A cap past the end loads the whole file");
    free(pLoaded);

    /* The byte buffer loaders agree with the raw ones. */
    xbyte_buffer_t buffer;
    CHECK(XPath_LoadBuffer(sPath, &buffer) == sizeof(payload), "The buffer load reports the file size");
    CHECK(buffer.nUsed == sizeof(payload), "The buffer holds the whole file");
    CHECK(memcmp(buffer.pData, payload, sizeof(payload)) == 0, "The buffer holds every byte");
    XByteBuffer_Clear(&buffer);

    CHECK(XPath_LoadBufferSize(sPath, &buffer, 32) == 32, "The capped buffer load stops at its cap");
    CHECK(buffer.nUsed == 32, "The capped buffer holds only the cap");
    XByteBuffer_Clear(&buffer);

    /* Writing a buffer back out. */
    XByteBuffer_Init(&buffer, 0, 0);
    CHECK(XByteBuffer_Add(&buffer, (const uint8_t*)"buffered", 8), "Fill a buffer to write");

    char sOut[320];
    xfs_path(&fixture, sOut, sizeof(sOut), "buffered.bin");
    CHECK(XPath_WriteBuffer(sOut, &buffer, "cwt") > 0, "The buffer is written out");
    CHECK(XPath_GetSize(sOut) == 8, "The written file is the buffer length");
    XByteBuffer_Clear(&buffer);

    /* The fixed size read fills the caller buffer and reports what it got. */
    uint8_t sFixed[16];
    memset(sFixed, 0, sizeof(sFixed));
    CHECK(XPath_Read(sOut, sFixed, sizeof(sFixed)) == 8, "The fixed read reports the byte count");
    CHECK(memcmp(sFixed, "buffered", 8) == 0, "The fixed read returns the contents");

    /* Copying a file reproduces it exactly. */
    char sCopy[320];
    xfs_path(&fixture, sCopy, sizeof(sCopy), "copy.bin");
    CHECK(XPath_CopyFile(sPath, sCopy) >= 0, "The file is copied");
    CHECK(XPath_GetSize(sCopy) == (long)sizeof(payload), "The copy is the same length");

    pLoaded = XPath_Load(sCopy, &nLoaded);
    CHECK(pLoaded != NULL && memcmp(pLoaded, payload, sizeof(payload)) == 0, "The copy is byte for byte identical");
    free(pLoaded);

    xfs_end(&fixture);
    return 0;
}

static int XTest_paths(void)
{
    xfs_fixture_t fixture;
    CHECK(xfs_begin(&fixture) == XSTDOK, "Create the filesystem fixture");

    char sPath[320];
    xfs_path(&fixture, sPath, sizeof(sPath), "perm.txt");
    CHECK(XPath_Write(sPath, (const uint8_t*)"x", 1, "cwt") > 0, "A file to inspect is written");

    /* Permissions round trip through the string form. */
    CHECK(XPath_SetPerm(sPath, "rw-r-----") > 0, "The permissions are set");

    char sPerm[16];
    CHECK(XPath_GetPerm(sPerm, sizeof(sPerm), sPath) > 0, "The permissions are read back");
    CHECK(strcmp(sPerm, "rw-r-----") == 0, "The permissions round trip");

    xmode_t nMode = 0;
    CHECK(XPath_PermToMode("rwxr-xr-x", &nMode) > 0, "A permission string converts to a mode");
    CHECK((nMode & 0777) == 0755, "The converted mode is the expected octal");

    char sBack[16];
    CHECK(XPath_ModeToPerm(sBack, sizeof(sBack), 0755) > 0, "A mode converts to a permission string");
    CHECK(strcmp(sBack, "rwxr-xr-x") == 0, "The mode round trips");

    char sChmod[16];
    CHECK(XPath_ModeToChmod(sChmod, sizeof(sChmod), 0644) > 0, "A mode converts to a chmod number");
    CHECK(atoi(sChmod) == 644, "The chmod number is the expected octal");

    /* Path parsing separates the directory, the name and the metadata. */
    xpath_t parsed;
    CHECK(XPath_Parse(&parsed, sPath, XTRUE) > 0, "The path parses with a stat");
    CHECK(strstr(parsed.sPath, fixture.sRoot) != NULL, "The directory is the fixture root");
    CHECK(strcmp(parsed.sFile, "perm.txt") == 0, "The file name is separated out");

    CHECK(XPath_Parse(&parsed, sPath, XFALSE) > 0, "The path parses without a stat");
    CHECK(strcmp(parsed.sFile, "perm.txt") == 0, "The file name is separated out either way");

    /* File type classification. */
    xstat_t statbuf;
    CHECK(xstat(sPath, &statbuf) == XSTDOK, "The file is stat-able");
    CHECK(XFile_GetType(statbuf.st_mode) == XF_REGULAR, "A regular file is classified as one");
    CHECK(XFile_IsExec(statbuf.st_mode) == XFALSE, "A non-executable file is not executable");
    CHECK(XPath_GetType(statbuf.st_mode) == '-', "A regular file renders as a dash");
    CHECK(XFile_GetTypeChar(XF_DIRECTORY) == 'd', "A directory renders as a d");
    CHECK(XFile_GetTypeChar(XF_SYMLINK) == 'l', "A symlink renders as an l");

    CHECK(xstat(fixture.sRoot, &statbuf) == XSTDOK, "The directory is stat-able");
    CHECK(XFile_GetType(statbuf.st_mode) == XF_DIRECTORY, "A directory is classified as one");
    CHECK(XPath_GetType(statbuf.st_mode) == 'd', "A directory renders as a d");

    CHECK(XPath_SetPerm(sPath, "rwxr-xr-x") > 0, "The file is made executable");
    CHECK(xstat(sPath, &statbuf) == XSTDOK, "The executable file is stat-able");
    CHECK(XFile_IsExec(statbuf.st_mode) == XTRUE, "An executable file is recognised");

    /* Links are distinguished from what they point at. */
    char sLink[320];
    xfs_path(&fixture, sLink, sizeof(sLink), "link.txt");
    if (symlink(sPath, sLink) == 0)
    {
        CHECK(XPath_IsLink(sLink) == XTRUE, "A symlink is recognised");
        CHECK(XPath_IsLink(sPath) == XFALSE, "A regular file is not a symlink");
        CHECK(XPath_Exists(sLink) == XTRUE, "A symlink to an existing file exists");
    }

    CHECK(XPath_IsLink("/no/such/path") == XFALSE, "A missing path is not a symlink");

    xfs_end(&fixture);
    return 0;
}

static int XTest_directories(void)
{
    xfs_fixture_t fixture;
    CHECK(xfs_begin(&fixture) == XSTDOK, "Create the filesystem fixture");

    /* Creating a directory, then an entire path of them. */
    char sDir[320];
    xfs_path(&fixture, sDir, sizeof(sDir), "sub");
    CHECK(XDir_Create(sDir, 0755) >= 0, "A directory is created");
    CHECK(XDir_Valid(sDir) > 0, "The created directory is valid");
    CHECK(XPath_Exists(sDir) == XTRUE, "The created directory exists");

    char sDeep[320];
    xfs_path(&fixture, sDeep, sizeof(sDeep), "a/b/c/");
    CHECK(XPath_EnsureDirectory(sDeep) >= 0, "A nested path is created");

    char sCheck[320];
    xfs_path(&fixture, sCheck, sizeof(sCheck), "a/b/c");
    CHECK(XDir_Valid(sCheck) > 0, "Every level of the nested path exists");

    CHECK(XDir_Valid("/no/such/directory") <= 0, "A missing directory is not valid");

    /* Walking a directory sees exactly the entries that are in it. */
    const char *pNames[] = {"one.txt", "two.txt", "three.txt"};
    for (size_t i = 0; i < 3; i++)
    {
        char sFile[512];
        snprintf(sFile, sizeof(sFile), "%s/%s", sDir, pNames[i]);
        CHECK(XPath_Write(sFile, (const uint8_t*)"x", 1, "cwt") > 0, "A directory entry is created");
    }

    xdir_t dir;
    CHECK(XDir_Open(&dir, sDir) >= 0, "The directory opens");

    int nSeen = 0, nFound[3] = {0, 0, 0};
    char sEntry[256];
    while (XDir_Read(&dir, sEntry, sizeof(sEntry)) > 0)
    {
        nSeen++;
        for (size_t i = 0; i < 3; i++)
            if (strcmp(sEntry, pNames[i]) == 0) nFound[i] = 1;
    }
    XDir_Close(&dir);

    CHECK(nSeen == 3, "The walk sees exactly the three entries");
    CHECK(nFound[0] && nFound[1] && nFound[2], "Every entry is seen once");

    /* The dot entries are never handed out. */
    CHECK(XDir_Open(&dir, sDir) >= 0, "The directory reopens");
    while (XDir_Read(&dir, sEntry, sizeof(sEntry)) > 0)
    {
        CHECK(strcmp(sEntry, ".") != 0, "The current directory entry is skipped");
        CHECK(strcmp(sEntry, "..") != 0, "The parent directory entry is skipped");
    }
    XDir_Close(&dir);

    CHECK(XDir_Open(&dir, "/no/such/directory") < 0, "A missing directory does not open");

    /* Removing entries and then the directory itself. */
    for (size_t i = 0; i < 3; i++)
    {
        char sFile[512];
        snprintf(sFile, sizeof(sFile), "%s/%s", sDir, pNames[i]);
        CHECK(XPath_Remove(sFile) >= 0, "A file is removed");
        CHECK(XPath_Exists(sFile) == XFALSE, "The removed file is gone");
    }

    CHECK(XDir_Remove(sDir) >= 0, "The emptied directory is removed");
    CHECK(XPath_Exists(sDir) == XFALSE, "The removed directory is gone");

    /* A whole tree is removed at once. */
    char sTree[320];
    xfs_path(&fixture, sTree, sizeof(sTree), "a");
    CHECK(XDir_Remove(sTree) >= 0, "A populated tree is removed");
    CHECK(XPath_Exists(sTree) == XFALSE, "The removed tree is gone");

    CHECK(XPath_Remove("/no/such/file") < 0, "Removing a missing path is an error");

    xfs_end(&fixture);
    return 0;
}


static int XTest_ownership(void)
{
    /* Changing a file's owner by name rather than by id. Only a privileged
     * process can hand a file to somebody else, so what is checked here is
     * the lookup and the refusals: a name that does not resolve must fail
     * before touching the file, not after. */
    char sPath[256];
    snprintf(sPath, sizeof(sPath), "/tmp/xutils-own-%d.txt", (int)getpid());
    unlink(sPath);

    CHECK(XPath_Write(sPath, (const uint8_t*)"x", 1, "cwt") > 0, "The fixture file is written");

    struct passwd *pSelf = getpwuid(getuid());
    struct group *pGroup = getgrgid(getgid());

    if (pSelf == NULL || pGroup == NULL)
    {
        unlink(sPath);
        printf("The current user has no name entry, skipping\n");
        return 77;
    }

    /* Handing a file to the user who already owns it is the one change any
     * process is allowed to make. */
    CHECK(xchown(sPath, pSelf->pw_name, pGroup->gr_name) == XSTDOK,
        "A file can be given to the user that already owns it");

    struct stat statbuf;
    CHECK(stat(sPath, &statbuf) == 0, "The file can be stat'ed");
    CHECK(statbuf.st_uid == getuid(), "The owner is unchanged");

    /* A name nobody has is refused, and so is a group nobody is in. The
     * file must be left exactly as it was. */
    CHECK(xchown(sPath, "xutils-no-such-user", pGroup->gr_name) == XSTDERR,
        "An unknown user is refused");
    CHECK(xchown(sPath, pSelf->pw_name, "xutils-no-such-group") == XSTDERR,
        "An unknown group is refused");

    CHECK(stat(sPath, &statbuf) == 0, "The file still exists");
    CHECK(statbuf.st_uid == getuid(), "A refused change left the owner alone");

    /* A path that does not exist cannot be given away either. */
    CHECK(xchown("/tmp/xutils-no-such-path-at-all", pSelf->pw_name, pGroup->gr_name) == XSTDERR,
        "A missing path is refused");

    unlink(sPath);
    return 0;
}


static int XTest_path_bounds(void)
{
    /* A path longer than the field it is split into has to be truncated
     * into it, terminated, and reported - not written one byte past the end
     * with the terminator landing in the next member. A server that builds
     * a path out of a request URI takes this straight from the network. */
    xpath_t path;

    /* Long enough that the accumulated directory part cannot fit. */
    size_t nLong = sizeof(path.sPath) + 512;
    char *pLong = (char*)malloc(nLong + 1);
    CHECK(pLong != NULL, "The long path is allocated");

    if (pLong == NULL) return 1;

    /* "/dir/dir/.../file.txt", sized so the file name lands exactly at the
     * end: stepping four at a time and then writing eight would overshoot
     * the allocation, so the last stretch is filled one byte at a time. */
    size_t nAt = 0;
    pLong[nAt++] = '/';

    while (nAt + 4 <= nLong - 8)
    {
        pLong[nAt++] = 'd';
        pLong[nAt++] = 'i';
        pLong[nAt++] = 'r';
        pLong[nAt++] = '/';
    }

    while (nAt < nLong - 9) pLong[nAt++] = 'd';

    /* A separator immediately before the name, so the last segment is
     * exactly "file.txt" rather than the filler run plus it. */
    pLong[nLong - 9] = '/';
    memcpy(&pLong[nLong - 8], "file.txt", 8);
    pLong[nLong] = '\0';

    /* Poisoned, so an unterminated field is visible rather than lucky. */
    memset(&path, 0x7f, sizeof(path));
    XPath_Parse(&path, pLong, XFALSE);

    CHECK(memchr(path.sPath, '\0', sizeof(path.sPath)) != NULL,
        "The directory part is terminated inside its own field");
    CHECK(memchr(path.sFile, '\0', sizeof(path.sFile)) != NULL,
        "The file part is terminated inside its own field");
    CHECK(strlen(path.sPath) < sizeof(path.sPath), "The directory part fits its field");
    CHECK(strlen(path.sFile) < sizeof(path.sFile), "The file part fits its field");
    CHECK(strcmp(path.sFile, "file.txt") == 0, "The file name is still the last segment");

    /* Every length around the boundary, so an off by one cannot hide. */
    for (size_t nLen = sizeof(path.sPath) - 8; nLen <= sizeof(path.sPath) + 8; nLen++)
    {
        char *pEdge = (char*)malloc(nLen + 1);
        if (pEdge == NULL) continue;

        memset(pEdge, 'a', nLen);
        pEdge[0] = '/';
        pEdge[nLen] = '\0';

        /* Turn it into real segments so the accumulating branch is taken. */
        for (size_t i = 8; i < nLen; i += 8) pEdge[i] = '/';

        memset(&path, 0x7f, sizeof(path));
        XPath_Parse(&path, pEdge, XFALSE);

        CHECK(memchr(path.sPath, '\0', sizeof(path.sPath)) != NULL,
            "Every length leaves the directory part terminated");
        CHECK(memchr(path.sFile, '\0', sizeof(path.sFile)) != NULL,
            "Every length leaves the file part terminated");

        free(pEdge);
    }

    free(pLong);

    /* A single segment longer than the file field is truncated into it. */
    char sLongFile[1024];
    memset(sLongFile, 'f', sizeof(sLongFile) - 1);
    sLongFile[0] = '/';
    sLongFile[sizeof(sLongFile) - 1] = '\0';

    memset(&path, 0x7f, sizeof(path));
    XPath_Parse(&path, sLongFile, XFALSE);

    CHECK(strlen(path.sFile) < sizeof(path.sFile), "An overlong file name is truncated into its field");

    /* And the guards. */
    CHECK(XPath_Parse(NULL, "/a/b", XFALSE) == XSTDINV, "A missing destination is rejected");
    CHECK(XPath_Parse(&path, NULL, XFALSE) == XSTDERR, "A missing path is rejected");
    CHECK(XPath_Parse(&path, "", XFALSE) == XSTDERR, "An empty path is rejected");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(file_io),
    XTEST_CASE(handles),
    XTEST_CASE(lines),
    XTEST_CASE(load),
    XTEST_CASE(paths),
    XTEST_CASE(directories),
    XTEST_CASE(ownership),
    XTEST_CASE(path_bounds)
)
