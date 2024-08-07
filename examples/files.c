/*
 *  examples/files.c
 * 
 *  Copyleft (C) 2015  Sun Dro (a.k.a. kala13x)
 *
 * File and folder operations.
 */

#include "xstd.h"
#include <sys/xlog.h>
#include <sys/xfs.h>

int main(int argc, char *argv[]) 
{
    XLog_Init("test", XLOG_ALL, 0);

    /* Check args */
    if (argc < 3) 
    {
        printf("Usage: %s [source] [destination]\n", argv[0]);
        printf("Example: %s src.txt dest.txt\n", argv[0]);
        return 0;
    }

    xfile_t srcFile, dstFile;
    XFile_Open(&srcFile, argv[1], NULL, NULL);
    XFile_Open(&dstFile, argv[2], "cwt", NULL);

    int nRet = XFile_Copy(&srcFile, &dstFile);
    if (nRet < 0) XLog_Error("Can not copy file: %d", errno);

    XFile_Close(&srcFile);
    XFile_Close(&dstFile);

    XFile_Open(&srcFile, argv[1], NULL, NULL);
    XLog_Debug("Lines: %d", XFile_GetLineCount(&srcFile));

    XFile_Seek(&srcFile, 0, SEEK_SET);
    char sLine[2048];
    int nLineNo = 2;

    if (XFile_ReadLine(&srcFile, sLine, sizeof(sLine), nLineNo) > 0) 
        XLog_Debug("Line (%d): %s", nLineNo, sLine);

    XFile_Close(&srcFile);

    xdir_t dir;
    XDir_Open(&dir, "./");

    char sFile[256];
    while(XDir_Read(&dir, sFile, sizeof(sFile)) > 0)
    {
        XLog_Info("Found file: %s", sFile);
        sFile[0] = '\0';
    }

    XDir_Close(&dir);
    return 0; 
}