/* libxutils: CLI window rendering, progress bars and terminal input.
 *
 * Nothing here needs a real terminal. Standard output is redirected to a
 * private file so the rendered frames can be read back and asserted on, and
 * so the window size query falls through to the COLUMNS/LINES fallback,
 * which makes every geometry assertion deterministic.
 */

#include "test.h"
#include "cli.h"
#include "str.h"
#include <unistd.h>
#include <fcntl.h>

typedef struct {
    char sPath[64];
    int nSavedFD;
} cli_capture_t;

/* Redirects stdout into a private file until capture_end(). */
static int capture_begin(cli_capture_t *pCapture)
{
    snprintf(pCapture->sPath, sizeof(pCapture->sPath), "/tmp/xutils-cli-XXXXXX");
    int nFile = mkstemp(pCapture->sPath);
    if (nFile < 0) return XSTDERR;

    fflush(stdout);
    pCapture->nSavedFD = dup(STDOUT_FILENO);
    if (pCapture->nSavedFD < 0 || dup2(nFile, STDOUT_FILENO) < 0)
    {
        close(nFile);
        return XSTDERR;
    }

    close(nFile);
    return XSTDOK;
}

/* Restores stdout and returns the captured bytes, which the caller frees. */
static char *capture_end(cli_capture_t *pCapture, size_t *pLength)
{
    fflush(stdout);
    dup2(pCapture->nSavedFD, STDOUT_FILENO);
    close(pCapture->nSavedFD);

    FILE *pFile = fopen(pCapture->sPath, "rb");
    if (pFile == NULL) { unlink(pCapture->sPath); return NULL; }

    fseek(pFile, 0, SEEK_END);
    long nSize = ftell(pFile);
    fseek(pFile, 0, SEEK_SET);

    char *pData = (char*)malloc((size_t)nSize + 1);
    if (pData != NULL)
    {
        size_t nRead = fread(pData, 1, (size_t)nSize, pFile);
        pData[nRead] = '\0';
        if (pLength != NULL) *pLength = nRead;
    }

    fclose(pFile);
    unlink(pCapture->sPath);
    return pData;
}

/* Feeds the given text to the code under test as standard input. */
static int stdin_begin(cli_capture_t *pCapture, const char *pText)
{
    snprintf(pCapture->sPath, sizeof(pCapture->sPath), "/tmp/xutils-cin-XXXXXX");
    int nFile = mkstemp(pCapture->sPath);
    if (nFile < 0) return XSTDERR;

    if (pText != NULL && *pText)
    {
        if (write(nFile, pText, strlen(pText)) < 0) { close(nFile); return XSTDERR; }
    }
    lseek(nFile, 0, SEEK_SET);

    pCapture->nSavedFD = dup(STDIN_FILENO);
    if (pCapture->nSavedFD < 0 || dup2(nFile, STDIN_FILENO) < 0)
    {
        close(nFile);
        return XSTDERR;
    }

    close(nFile);

    /* The FILE for stdin survives the descriptor swap, keeping its
     * end-of-file flag and anything it read ahead. Unbuffered reads stop it
     * from consuming past what fgets needs, which is what lets the raw read
     * path below start at offset zero of the new file. */
    static int bUnbuffered = 0;
    if (!bUnbuffered) { setvbuf(stdin, NULL, _IONBF, 0); bUnbuffered = 1; }

    clearerr(stdin);
    lseek(STDIN_FILENO, 0, SEEK_SET);
    return XSTDOK;
}

static void stdin_end(cli_capture_t *pCapture)
{
    dup2(pCapture->nSavedFD, STDIN_FILENO);
    close(pCapture->nSavedFD);
    unlink(pCapture->sPath);
}

static int XTest_window_size(void)
{
    CHECK(XCLI_GetWindowSize(NULL) == XSTDINV, "A missing size output is rejected");

    /* With stdout on a file the ioctl cannot answer, so the environment
     * fallback decides the geometry. */
    cli_capture_t capture;
    CHECK(capture_begin(&capture) == XSTDOK, "Redirect stdout");

    setenv("COLUMNS", "100", 1);
    setenv("LINES", "40", 1);
    xcli_size_t size;
    XSTATUS nExplicit = XCLI_GetWindowSize(&size);
    size_t nExplicitColumns = size.nColumns, nExplicitRows = size.nRows;

    /* Anything that is not a plain, bounded decimal falls back to the
     * defaults. A negative value especially: strtoul() would wrap it into a
     * huge unsigned width that becomes a fill loop bound further down. */
    const char *pBadValues[] = {"", "abc", "0", "12x", "-5", "  7", "7 ", "+9", "99999999999999999999"};
    size_t nBadColumns[9], nBadRows[9];
    for (size_t i = 0; i < sizeof(pBadValues) / sizeof(*pBadValues); i++)
    {
        setenv("COLUMNS", pBadValues[i], 1);
        setenv("LINES", pBadValues[i], 1);
        XCLI_GetWindowSize(&size);
        nBadColumns[i] = size.nColumns;
        nBadRows[i] = size.nRows;
    }

    unsetenv("COLUMNS");
    unsetenv("LINES");
    XCLI_GetWindowSize(&size);
    size_t nDefaultColumns = size.nColumns, nDefaultRows = size.nRows;

    char *pOutput = capture_end(&capture, NULL);
    free(pOutput);

    CHECK(nExplicit == XSTDOK, "A resolvable window size succeeds");
    CHECK(nExplicitColumns == 100 && nExplicitRows == 40, "COLUMNS and LINES set the geometry");
    CHECK(nDefaultColumns == 80 && nDefaultRows == 24, "An unset environment uses the 80x24 default");
    for (size_t i = 0; i < sizeof(pBadValues) / sizeof(*pBadValues); i++)
    {
        CHECK(nBadColumns[i] == 80, "Anything but a plain bounded decimal COLUMNS falls back to the default");
        CHECK(nBadRows[i] == 24, "Anything but a plain bounded decimal LINES falls back to the default");
    }
    return 0;
}

static int XTest_window_lines(void)
{
    cli_capture_t capture;
    CHECK(capture_begin(&capture) == XSTDOK, "Redirect stdout");
    setenv("COLUMNS", "40", 1);
    setenv("LINES", "4", 1);

    xcli_win_t win;
    XCLIWin_Init(&win, XTRUE);

    XSTATUS nAdd = XCLIWin_AddLineFmt(&win, "first %d", 1);
    XSTATUS nAdd2 = XCLIWin_AddLineFmt(&win, "second");
    XSTATUS nEmpty = XCLIWin_AddEmptyLine(&win);
    char sThird[] = "third";
    XSTATUS nAdd3 = XCLIWin_AddLine(&win, sThird, sizeof(sThird) - 1);
    /* The window holds four rows, so a fifth line is refused. */
    XSTATUS nOverflow = XCLIWin_AddLineFmt(&win, "overflow");
    size_t nUsed = XArray_Used(&win.lines);

    xbyte_buffer_t frame;
    XSTATUS nFrame = XCLIWin_GetFrame(&win, &frame);
    size_t nFrameUsed = frame.nUsed;
    char *pFrame = frame.nUsed ? strdup((const char*)frame.pData) : NULL;
    XByteBuffer_Clear(&frame);
    XCLIWin_Destroy(&win);

    char *pOutput = capture_end(&capture, NULL);
    free(pOutput);

    CHECK(nAdd == XSTDOK && nAdd2 == XSTDOK && nAdd3 == XSTDOK, "Lines are accepted while rows remain");
    CHECK(nEmpty == XSTDOK, "An empty line is accepted");
    CHECK(nOverflow == XSTDNON, "A line past the last row is refused, not an error");
    CHECK(nUsed == 4, "The window holds exactly as many lines as it has rows");
    CHECK(nFrame == XSTDOK && nFrameUsed > 0, "The frame renders");
    CHECK(pFrame != NULL, "The frame is a printable string");
    CHECK(strstr(pFrame, "first 1") != NULL, "A formatted line reaches the frame");
    CHECK(strstr(pFrame, "second") != NULL && strstr(pFrame, "third") != NULL, "Every line reaches the frame");
    CHECK(strstr(pFrame, "overflow") == NULL, "A refused line does not reach the frame");
    free(pFrame);
    return 0;
}

static int XTest_window_align(void)
{
    cli_capture_t capture;
    CHECK(capture_begin(&capture) == XSTDOK, "Redirect stdout");
    setenv("COLUMNS", "20", 1);
    setenv("LINES", "10", 1);

    /* Each alignment pads to the full window width; only the position of
     * the text inside that width differs. */
    char *pCentered = NULL, *pLeft = NULL, *pRight = NULL;
    XSTATUS nStatus[3];
    const uint8_t alignments[] = {XCLI_CENTER, XCLI_LEFT, XCLI_RIGHT};
    char **ppResults[] = {&pCentered, &pLeft, &pRight};

    for (size_t i = 0; i < 3; i++)
    {
        xcli_win_t win;
        XCLIWin_Init(&win, XTRUE);
        nStatus[i] = XCLIWin_AddAligned(&win, "abc", NULL, alignments[i]);

        xbyte_buffer_t frame;
        if (XCLIWin_GetFrame(&win, &frame) == XSTDOK && frame.nUsed)
            *ppResults[i] = strdup((const char*)frame.pData);
        XByteBuffer_Clear(&frame);
        XCLIWin_Destroy(&win);
    }

    /* An empty input has nothing to align. */
    xcli_win_t win;
    XCLIWin_Init(&win, XTRUE);
    XSTATUS nEmpty = XCLIWin_AddAligned(&win, "", NULL, XCLI_CENTER);
    /* A colour format wraps the padded text without changing its width. */
    XSTATUS nFormatted = XCLIWin_AddAligned(&win, "xyz", XSTR_FMT_BOLD, XCLI_LEFT);
    XCLIWin_Destroy(&win);

    char *pOutput = capture_end(&capture, NULL);
    free(pOutput);

    for (size_t i = 0; i < 3; i++) CHECK(nStatus[i] == XSTDOK, "Every alignment is accepted");
    CHECK(pCentered && pLeft && pRight, "Every alignment renders a frame");
    CHECK(strstr(pCentered, "abc") && strstr(pLeft, "abc") && strstr(pRight, "abc"),
        "The text survives every alignment");

    /* Left alignment starts at column zero; right alignment does not. */
    CHECK(pLeft[0] == 'a', "Left alignment puts the text at the start of the line");
    CHECK(pRight[0] == ' ' && strstr(pRight, " abc") != NULL, "Right alignment pads on the left");
    CHECK(pCentered[0] == ' ' && strstr(pCentered, " abc") != NULL, "Centring pads on both sides");
    CHECK(nEmpty == XSTDERR, "An empty input has nothing to align");
    CHECK(nFormatted == XSTDOK, "A format prefix is accepted");

    free(pCentered);
    free(pLeft);
    free(pRight);
    return 0;
}

static int XTest_window_display(void)
{
    cli_capture_t capture;
    CHECK(capture_begin(&capture) == XSTDOK, "Redirect stdout");
    setenv("COLUMNS", "30", 1);
    setenv("LINES", "5", 1);

    /* Frame mode renders everything in one write. */
    xcli_win_t win;
    XCLIWin_Init(&win, XTRUE);
    win.eType = XCLI_RENDER_FRAME;
    XCLIWin_AddLineFmt(&win, "frame-line");
    XSTATUS nFrameDisplay = XCLIWin_Display(&win);
    XCLIWin_Destroy(&win);

    /* Line mode writes each row separately. */
    XCLIWin_Init(&win, XTRUE);
    win.eType = XCLI_LINE_BY_LINE;
    XCLIWin_AddLineFmt(&win, "row-one");
    XCLIWin_AddLineFmt(&win, "row-two");
    XSTATUS nLineDisplay = XCLIWin_Display(&win);
    XCLIWin_Destroy(&win);

    /* Flush displays and then empties the line list. */
    XCLIWin_Init(&win, XTRUE);
    XCLIWin_AddLineFmt(&win, "flushed");
    XSTATUS nFlush = XCLIWin_Flush(&win);
    size_t nAfterFlush = XArray_Used(&win.lines);
    XCLIWin_Destroy(&win);

    /* An unknown display type renders nothing at all. */
    XCLIWin_Init(&win, XTRUE);
    win.eType = (xcli_disp_type_t)99;
    XCLIWin_AddLineFmt(&win, "never-printed");
    XSTATUS nUnknown = XCLIWin_Display(&win);
    XCLIWin_Destroy(&win);

    size_t nLength = 0;
    char *pOutput = capture_end(&capture, &nLength);
    CHECK(pOutput != NULL, "Output was captured");

    CHECK(nFrameDisplay == XSTDOK, "Frame mode displays");
    CHECK(nLineDisplay == XSTDOK, "Line mode displays");
    CHECK(nFlush == XSTDOK && nAfterFlush == 0, "Flushing displays and then clears the lines");
    CHECK(nUnknown == XSTDNON, "An unknown display type renders nothing");
    CHECK(strstr(pOutput, "frame-line") != NULL, "The frame reached standard output");
    CHECK(strstr(pOutput, "row-one") != NULL && strstr(pOutput, "row-two") != NULL,
        "Every line reached standard output");
    CHECK(strstr(pOutput, "flushed") != NULL, "The flushed line reached standard output");
    CHECK(strstr(pOutput, "never-printed") == NULL, "An unknown type printed nothing");
    free(pOutput);

    CHECK(XCLIWin_Display(NULL) == XSTDERR, "A missing window is rejected");
    CHECK(XCLIWin_GetFrame(NULL, NULL) == XSTDERR, "A missing window has no frame");
    return 0;
}

static int XTest_progress_bounds(void)
{
    cli_capture_t capture;
    CHECK(capture_begin(&capture) == XSTDOK, "Redirect stdout");
    setenv("COLUMNS", "100", 1);
    setenv("LINES", "24", 1);

    xcli_bar_t bar;
    XProgBar_GetDefaults(&bar);
    size_t nDefaultColumns = bar.frame.nColumns;
    char cLoader = bar.cLoader, cStart = bar.cStart, cEnd = bar.cEnd;
    double fInitial = bar.fPercent;
    uint32_t nInterval = bar.nIntervalU;

    /* Out of range percentages are clamped, not wrapped. */
    bar.fPercent = 250.;
    XProgBar_CalculateBounds(&bar);
    double fHigh = bar.fPercent;
    size_t nFullUsed = bar.nBarUsed, nFullLength = bar.nBarLength;

    bar.fPercent = -12.;
    XProgBar_CalculateBounds(&bar);
    double fLow = bar.fPercent;
    size_t nEmptyUsed = bar.nBarUsed;

    /* Half way fills about half the bar. */
    bar.fPercent = 50.;
    XProgBar_CalculateBounds(&bar);
    size_t nHalfUsed = bar.nBarUsed, nHalfLength = bar.nBarLength;

    /* A prefix and suffix take their width out of the bar itself. */
    xstrncpy(bar.sPrefix, sizeof(bar.sPrefix), "Downloading ");
    xstrncpy(bar.sSuffix, sizeof(bar.sSuffix), " 12MB");
    XProgBar_CalculateBounds(&bar);
    size_t nShrunkLength = bar.nBarLength;

    /* A window narrower than the decorations leaves no bar at all. */
    bar.frame.nColumns = 4;
    XProgBar_CalculateBounds(&bar);
    size_t nNoLength = bar.nBarLength, nNoUsed = bar.nBarUsed;

    char *pOutput = capture_end(&capture, NULL);
    free(pOutput);

    CHECK(nDefaultColumns == 100, "The defaults pick up the window size");
    CHECK(cLoader == '=' && cStart == '[' && cEnd == ']', "The default bar characters are set");
    CHECK(fInitial == 0. && nInterval == XCLI_BAR_INTERVAL, "The defaults start empty at the default interval");
    CHECK(fHigh == 100., "A percentage above 100 is clamped");
    CHECK(fLow == 0., "A negative percentage is clamped to zero");
    CHECK(nFullUsed == nFullLength, "A full bar uses its whole length");
    CHECK(nEmptyUsed == 0, "An empty bar uses none of its length");
    CHECK(nHalfUsed == nHalfLength / 2, "Half way fills half the bar");
    CHECK(nShrunkLength < nFullLength, "A prefix and suffix shorten the bar");
    CHECK(nNoLength == 0 && nNoUsed == 0, "A window with no room leaves no bar");
    return 0;
}

static int XTest_progress_output(void)
{
    cli_capture_t capture;
    CHECK(capture_begin(&capture) == XSTDOK, "Redirect stdout");
    setenv("COLUMNS", "60", 1);
    setenv("LINES", "24", 1);

    xcli_bar_t bar;
    XProgBar_GetDefaults(&bar);
    bar.fPercent = 40.;

    char sPlain[XLINE_MAX];
    size_t nPlain = XProgBar_GetOutput(&bar, sPlain, sizeof(sPlain));

    /* The percentage inside the bar. */
    XProgBar_GetDefaults(&bar);
    bar.fPercent = 40.;
    bar.bInPercent = XTRUE;
    char sInPercent[XLINE_MAX];
    size_t nInPercent = XProgBar_GetOutput(&bar, sInPercent, sizeof(sInPercent));

    /* The suffix inside the bar, with the percentage after it. */
    XProgBar_GetDefaults(&bar);
    bar.fPercent = 40.;
    bar.bInSuffix = XTRUE;
    xstrncpy(bar.sSuffix, sizeof(bar.sSuffix), "ETA 3s");
    char sInSuffix[XLINE_MAX];
    size_t nInSuffix = XProgBar_GetOutput(&bar, sInSuffix, sizeof(sInSuffix));

    /* Both flags with a suffix hide the percentage entirely. */
    XProgBar_GetDefaults(&bar);
    bar.fPercent = 40.;
    bar.bInPercent = XTRUE;
    bar.bInSuffix = XTRUE;
    xstrncpy(bar.sSuffix, sizeof(bar.sSuffix), "ETA 3s");
    char sHidden[XLINE_MAX];
    size_t nHidden = XProgBar_GetOutput(&bar, sHidden, sizeof(sHidden));

    /* A caller supplied progress string replaces the generated one. */
    XProgBar_GetDefaults(&bar);
    bar.fPercent = 40.;
    XProgBar_CalculateBounds(&bar);
    char sCustomProgress[] = "####";
    char sCustom[XLINE_MAX];
    size_t nCustom = XProgBar_GetOutputAdv(&bar, sCustom, sizeof(sCustom), sCustomProgress, XFALSE);

    /* An empty and a full bar. */
    XProgBar_GetDefaults(&bar);
    bar.fPercent = 0.;
    char sEmpty[XLINE_MAX];
    XProgBar_GetOutput(&bar, sEmpty, sizeof(sEmpty));

    XProgBar_GetDefaults(&bar);
    bar.fPercent = 100.;
    char sFull[XLINE_MAX];
    XProgBar_GetOutput(&bar, sFull, sizeof(sFull));

    char *pOutput = capture_end(&capture, NULL);
    free(pOutput);

    CHECK(nPlain > 0 && strchr(sPlain, '[') && strchr(sPlain, ']'), "The bar is framed by its start and end characters");
    CHECK(strstr(sPlain, "40.0%") != NULL, "The percentage is rendered to one decimal");
    CHECK(strchr(sPlain, '=') != NULL, "A partly full bar contains loader characters");
    CHECK(strchr(sPlain, '>') != NULL, "A partly full bar shows the cursor");

    CHECK(nInPercent > 0 && strstr(sInPercent, "40.0%") != NULL, "The inline percentage is rendered");
    CHECK(nInSuffix > 0 && strstr(sInSuffix, "ETA 3s") != NULL, "The inline suffix is rendered");
    CHECK(nHidden > 0 && strstr(sHidden, "ETA 3s") != NULL, "The suffix survives when the percentage is hidden");
    CHECK(strstr(sHidden, "40.0%") == NULL, "The percentage is hidden when it would duplicate the suffix");

    CHECK(nCustom > 0 && strstr(sCustom, "####") != NULL, "A caller supplied progress string is used verbatim");
    CHECK(strchr(sEmpty, '=') == NULL, "An empty bar has no loader characters");
    CHECK(strchr(sEmpty, '>') == NULL, "An empty bar has no cursor");
    CHECK(strchr(sFull, '>') == NULL, "A full bar has no cursor left to draw");
    CHECK(strstr(sFull, "100.0%") != NULL, "A full bar reads one hundred percent");
    return 0;
}

static int XTest_progress_animation(void)
{
    cli_capture_t capture;
    CHECK(capture_begin(&capture) == XSTDOK, "Redirect stdout");
    setenv("COLUMNS", "40", 1);
    setenv("LINES", "24", 1);

    /* With no interval the marquee advances on every call, and reverses
     * when it reaches the end instead of running off the bar. */
    xcli_bar_t bar;
    XProgBar_GetDefaults(&bar);
    bar.nIntervalU = 0;

    int nMaxPosition = 0, nReversals = 0;
    xbool_t bWasReverse = XFALSE;
    for (int i = 0; i < 200; i++)
    {
        XProgBar_MakeMove(&bar);
        if (bar.nPosition > nMaxPosition) nMaxPosition = bar.nPosition;
        if (bar.bReverse != bWasReverse) { nReversals++; bWasReverse = bar.bReverse; }
        CHECK(bar.nPosition >= 0, "The marquee never runs off the start of the bar");
        CHECK((size_t)bar.nPosition <= bar.nBarLength, "The marquee never runs off the end of the bar");
    }

    /* A negative percentage means "unknown", which drives the marquee. */
    XProgBar_GetDefaults(&bar);
    bar.fPercent = -1.;
    bar.nIntervalU = 0;
    XProgBar_Update(&bar);
    int nAdvanced = bar.nPosition;

    /* Reaching a hundred percent finishes the bar. */
    XProgBar_GetDefaults(&bar);
    bar.fPercent = 100.;
    XProgBar_Update(&bar);

    XProgBar_GetDefaults(&bar);
    bar.fPercent = 100.;
    bar.bKeepBar = XTRUE;
    XProgBar_Update(&bar);

    /* Finishing with an unknown percentage prints the N/A form. */
    XProgBar_GetDefaults(&bar);
    bar.fPercent = -1.;
    XProgBar_Finish(&bar);

    size_t nLength = 0;
    char *pOutput = capture_end(&capture, &nLength);
    CHECK(pOutput != NULL && nLength > 0, "The animation wrote to standard output");

    CHECK(nMaxPosition > 0, "The marquee advances");
    CHECK(nReversals >= 2, "The marquee turns around at both ends");
    CHECK(nAdvanced > 0, "An unknown percentage drives the marquee");
    CHECK(strstr(pOutput, "N/A") != NULL, "An unknown percentage renders as N/A");
    CHECK(strstr(pOutput, "100.0%") != NULL, "A finished bar reads one hundred percent");
    free(pOutput);
    return 0;
}

static int XTest_input(void)
{
    /* fgets based input, with and without the trailing newline trimmed. */
    cli_capture_t out, in;
    CHECK(capture_begin(&out) == XSTDOK, "Redirect stdout");
    CHECK(stdin_begin(&in, "hello world\nsecond\n") == XSTDOK, "Redirect stdin");

    char sInput[64];
    XSTATUS nFirst = XCLI_GetInput("prompt: ", sInput, sizeof(sInput), XTRUE);
    int bTrimmed = strcmp(sInput, "hello world") == 0;

    XSTATUS nSecond = XCLI_GetInput(NULL, sInput, sizeof(sInput), XFALSE);
    int bKept = strcmp(sInput, "second\n") == 0;

    /* Past the end of the input there is nothing left to read. */
    XSTATUS nEOF = XCLI_GetInput(NULL, sInput, sizeof(sInput), XTRUE);
    stdin_end(&in);

    /* A short buffer truncates rather than overflowing. */
    CHECK(stdin_begin(&in, "0123456789abcdef\n") == XSTDOK, "Redirect stdin");
    char sShort[8];
    XSTATUS nShort = XCLI_GetInput(NULL, sShort, sizeof(sShort), XTRUE);
    int bShortLen = strlen(sShort) < sizeof(sShort);
    stdin_end(&in);

    /* The raw read path reports what it got and terminates the buffer. */
    CHECK(stdin_begin(&in, "raw-bytes") == XSTDOK, "Redirect stdin");
    char sRaw[32];
    XSTATUS nRaw = XCLI_ReadStdin(sRaw, sizeof(sRaw), XFALSE);
    int bRaw = (nRaw == 9) && strcmp(sRaw, "raw-bytes") == 0;
    stdin_end(&in);

    /* A single character read consumes exactly one byte. */
    CHECK(stdin_begin(&in, "AB") == XSTDOK, "Redirect stdin");
    char cChar = 0;
    XSTATUS nChar = XCLI_GetChar(&cChar, XFALSE);
    stdin_end(&in);

    /* Turning the echo off needs a terminal. With stdin on a file the POSIX
     * path declines rather than reading the password back in clear text;
     * either way it must never print what it read. */
    CHECK(stdin_begin(&in, "s3cret\n") == XSTDOK, "Redirect stdin");
    char sPass[32];
    memset(sPass, 0, sizeof(sPass));
    XSTATUS nPass = XCLI_GetPass("password: ", sPass, sizeof(sPass));
    int bPass = (nPass == XSTDERR) || (nPass == 6 && strcmp(sPass, "s3cret") == 0);
    stdin_end(&in);

    char *pOutput = capture_end(&out, NULL);
    CHECK(pOutput != NULL, "Output was captured");
    int bPrompted = strstr(pOutput, "prompt: ") != NULL;
    int bNoEcho = strstr(pOutput, "s3cret") == NULL;
    free(pOutput);

    CHECK(nFirst == XSTDOK && bTrimmed, "The trailing newline is cut when requested");
    CHECK(nSecond == XSTDOK && bKept, "The trailing newline is kept when not requested");
    CHECK(nEOF == XSTDERR, "Reading past the end of the input is an error");
    CHECK(nShort == XSTDOK && bShortLen, "A short buffer truncates the input");
    CHECK(bRaw, "The raw read returns the byte count and terminates the buffer");
    CHECK(nChar == 1 && cChar == 'A', "A single character read consumes one byte");
    CHECK(bPass, "A password is either declined or returned without its newline");
    CHECK(bPrompted, "The prompt was written to standard output");
    CHECK(bNoEcho, "The password is never echoed");

    CHECK(XCLI_GetInput(NULL, NULL, 16, XTRUE) == XSTDINV, "A missing input buffer is rejected");
    CHECK(XCLI_ReadStdin(NULL, 16, XFALSE) == XSTDINV, "A missing read buffer is rejected");
    CHECK(XCLI_ReadStdin(sRaw, 0, XFALSE) == XSTDINV, "A zero sized read buffer is rejected");
    CHECK(XCLI_GetChar(NULL, XFALSE) == XSTDERR, "A missing character output is rejected");
    CHECK(XCLI_GetPass(NULL, NULL, 16) == XSTDERR, "A missing password buffer is rejected");
    CHECK(XCLI_GetPass(NULL, sPass, 0) == XSTDERR, "A zero sized password buffer is rejected");
    return 0;
}

static int XTest_terminal_modes(void)
{
    /* Without a terminal on stdin the raw mode switch must decline rather
     * than corrupting the attributes of whatever fd 0 happens to be. */
    cli_capture_t in;
    CHECK(stdin_begin(&in, "not-a-terminal\n") == XSTDOK, "Redirect stdin to a file");

    char sAttributes[256];
    XSTATUS nSet = XCLI_SetInputMode(sAttributes);
    stdin_end(&in);

    CHECK(nSet == XSTDERR || nSet == XSTDNON, "Raw mode is declined when stdin is not a terminal");
    CHECK(XCLI_RestoreAttributes(NULL) == XSTDERR || XCLI_RestoreAttributes(NULL) == XSTDNON,
        "Restoring without saved attributes is rejected");

    /* Clearing the screen with the ascii escape writes the escape and
     * nothing else; it must never shell out. */
    cli_capture_t out;
    CHECK(capture_begin(&out) == XSTDOK, "Redirect stdout");
    int nCleared = XCLIWin_ClearScreen(XTRUE);
    size_t nLength = 0;
    char *pOutput = capture_end(&out, &nLength);

    CHECK(pOutput != NULL, "Output was captured");
    CHECK(nCleared == XSTDNON, "The ascii clear reports no child process");
    CHECK(nLength > 0 && pOutput[0] == '\x1B', "The ascii clear writes an escape sequence");
    free(pOutput);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(window_size),
    XTEST_CASE(window_lines),
    XTEST_CASE(window_align),
    XTEST_CASE(window_display),
    XTEST_CASE(progress_bounds),
    XTEST_CASE(progress_output),
    XTEST_CASE(progress_animation),
    XTEST_CASE(input),
    XTEST_CASE(terminal_modes)
)
