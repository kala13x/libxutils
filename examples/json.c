/*
 *  examples/json.c
 * 
 *  Copyleft (C) 2018  Sun Dro (a.k.a. kala13x)
 *
 * Example file for working with json.
 */

#include <xutils/xstd.h>
#include <xutils/xfs.h>
#include <xutils/xlog.h>
#include <xutils/xjson.h>

int main(int argc, char *argv[])
{
    xlog_defaults();
    xlog_cfg_t cfg;

    xlog_get(&cfg);
    cfg.bUseHeap = 1;
    cfg.bFlush = 1;
    xlog_set(&cfg);

    if (argc < 2)
    {
        xloge("Please specify json file");
        return 1;
    }

////////////////////////////////////////////////////////
// CREATE JSON FILE
////////////////////////////////////////////////////////

    xjson_obj_t *pRootArray = XJSON_NewArray(NULL, 0);
    if (pRootArray != NULL)
    {
        xjson_obj_t *pRootObject = XJSON_NewObject(NULL, 0);
        if (pRootObject != NULL)
        {
            xjson_obj_t *pBigObj = XJSON_NewObject("bigobj", 0);
            if (pBigObj != NULL)
            {
                xjson_obj_t *pTestObj1 = XJSON_NewObject("testobj1", 0);
                if (pTestObj1 != NULL)
                {
                    xjson_obj_t *pTestObj2 = XJSON_NewObject("testobj2", 0);
                    if (pTestObj2 != NULL)
                    {
                        XJSON_AddObject(pTestObj2, XJSON_NewBool("testbool1", 1));
                        XJSON_AddObject(pTestObj2, XJSON_NewString("emptyString", ""));
                        XJSON_AddObject(pTestObj2, XJSON_NewString("teststr1", "example string 1"));
                        XJSON_AddObject(pTestObj1, pTestObj2);
                    }

                    XJSON_AddObject(pTestObj1, XJSON_NewFloat("testfloat1", 60.900002));
                    XJSON_AddObject(pTestObj1, XJSON_NewInt("testint1", 66));
                    XJSON_AddObject(pBigObj, pTestObj1);
                }

                xjson_obj_t *pTestObj3 = XJSON_NewObject("testobj3", 0);
                if (pTestObj3 != NULL)
                {
                    xjson_obj_t *pTestArr2 = XJSON_NewArray("testarray2", 0);
                    if (pTestArr2 != NULL)
                    {
                        XJSON_AddObject(pTestArr2, XJSON_NewBool(NULL, 1));
                        XJSON_AddObject(pTestArr2, XJSON_NewInt(NULL, 69));
                        XJSON_AddObject(pTestArr2, XJSON_NewFloat(NULL, 69.900002));
                        XJSON_AddObject(pTestObj3, pTestArr2);
                    }

                    XJSON_AddObject(pTestObj3, XJSON_NewString("teststr2", "example string 2"));
                    XJSON_AddObject(pTestObj3, XJSON_NewBool("testbool2", 1));
                    XJSON_AddObject(pBigObj, pTestObj3);
                }

                xjson_obj_t *pTestArr1 = XJSON_NewArray("testarray1", 0);
                if (pTestArr1 != NULL)
                {
                    xjson_obj_t *pArrObj = XJSON_NewObject(NULL, 0);
                    if (pArrObj != NULL)
                    {
                        XJSON_AddObject(pArrObj, XJSON_NewFloat("testfloat2", 61.900002));
                        XJSON_AddObject(pArrObj, XJSON_NewInt("testint2", -67));
                        XJSON_AddObject(pTestArr1, pArrObj);
                    }

                    pArrObj = XJSON_NewObject(NULL, 0);
                    if (pArrObj != NULL)
                    {
                        XJSON_AddObject(pArrObj, XJSON_NewFloat("testfloat2", 62.900002));
                        XJSON_AddObject(pArrObj, XJSON_NewInt("testint2", 68));
                        XJSON_AddObject(pTestArr1, pArrObj);
                    }

                    pArrObj = XJSON_NewObject(NULL, 0);
                    if (pArrObj != NULL)
                    {
                        XJSON_AddObject(pArrObj, XJSON_NewFloat("testfloat2", 63.96969002));
                        XJSON_AddObject(pArrObj, XJSON_NewInt("testint2", -69));
                        XJSON_AddObject(pTestArr1, pArrObj);
                    }

                    XJSON_AddObject(pBigObj, pTestArr1);
                }

                XJSON_AddObject(pBigObj, XJSON_NewObject("emptyObject", 0));
                XJSON_AddObject(pBigObj, XJSON_NewArray("emptyArray", 0));
                XJSON_AddObject(pBigObj, XJSON_NewNull("nullItem"));

                xjson_obj_t *pArrObj = XJSON_NewArray("emptyArrObj", 0);
                if (pArrObj != NULL)
                {
                    XJSON_AddObject(pArrObj, XJSON_NewObject(NULL, 0));
                    XJSON_AddObject(pBigObj, pArrObj);
                }

                XJSON_AddObject(pRootObject, pBigObj);
            }

            XJSON_AddObject(pRootArray, pRootObject);
        }

        xjson_writer_t linter;
        XJSON_InitWriter(&linter, NULL, 1); // Dynamic allocation
        linter.nTabSize = 4; // Enable linter and set tab size (4 spaces)

        /* Dump objects directly */
        if (XJSON_WriteObject(pRootArray, &linter))
        {
            xfile_t *pFile = XFile_Alloc(argv[1], "w", NULL);
            if (pFile != NULL)
            {
                XFile_Write(pFile, linter.pData, linter.nLength);
                XFile_Free(&pFile);
            }

            XJSON_DestroyWriter(&linter);
        }

        XJSON_FreeObject(pRootArray);
    }

////////////////////////////////////////////////////////
// PARSE JSON FILE
////////////////////////////////////////////////////////

    xjson_t json;
    size_t nSize;

    char *pBuffer = (char*)XPath_Load(argv[1],  &nSize);
    if (pBuffer == NULL)
    {
        xloge("Can't read file: %s (%s)", 
            argv[1], XSTRERR);
        return 0;
    }

    if (!XJSON_Parse(&json, pBuffer, nSize))
    {
        char sError[XMSG_MID];
        XJSON_GetErrorStr(&json, sError, sizeof(sError));
        xloge("Failed to parse JSON: %s", sError);

        XJSON_Destroy(&json);
        free(pBuffer);
        return 0;
    }

    xjson_obj_t *pBigObj = XJSON_GetObject(XJSON_GetArrayItem(json.pRootObj, 0), "bigobj");
    if (pBigObj != NULL)
    {
        xjson_obj_t *pTestObj1 = XJSON_GetObject(pBigObj, "testobj1");
        if (pTestObj1 != NULL)
        {
            xjson_obj_t *pIntObj = XJSON_GetObject(pTestObj1, "testint1");
            if (pIntObj != NULL) xlog("testint1: %d", XJSON_GetInt(pIntObj));

            xjson_obj_t *pFloatObj = XJSON_GetObject(pTestObj1, "testfloat1");
            if (pFloatObj != NULL) xlog("testfloat1: %f", XJSON_GetFloat(pFloatObj));

            xjson_obj_t *pTestObj2 = XJSON_GetObject(pTestObj1, "testobj2");
            if (pTestObj2 != NULL)
            {
                xjson_obj_t *pBoolObj = XJSON_GetObject(pTestObj2, "testbool1");
                if (pBoolObj != NULL) xlog("testbool1: %s", XJSON_GetBool(pBoolObj) ? "true" : "false");

                xjson_obj_t *pStrObj = XJSON_GetObject(pTestObj2, "teststr1");
                if (pStrObj != NULL) xlog("teststr1: %s", XJSON_GetString(pStrObj));
            }
        }

        xjson_obj_t *pArrObj = XJSON_GetObject(pBigObj, "testarray1");
        if (pArrObj != NULL)
        {
            size_t i, nLength = XJSON_GetArrayLength(pArrObj);
            for (i = 0; i < nLength; i++)
            {
                xjson_obj_t *pArrItemObj = XJSON_GetArrayItem(pArrObj, i);
                if (pArrItemObj != NULL)
                {
                    xjson_obj_t *pArrItem = XJSON_GetObject(pArrItemObj, "testint2");
                    if (pArrItem != NULL) xlog("testint2: %d", XJSON_GetInt(pArrItem));

                    pArrItem = XJSON_GetObject(pArrItemObj, "testfloat2");
                    if (pArrItem != NULL) xlog("testfloat2: %f", XJSON_GetFloat(pArrItem));
                }
            }
        }

        xjson_obj_t *pTestObj3 = XJSON_GetObject(pBigObj, "testobj3");
        if (pTestObj3 != NULL)
        {
            xjson_obj_t *pArrObj = XJSON_GetObject(pTestObj3, "testarray2");
            if (pArrObj != NULL)
            {
                size_t i, nLength = XJSON_GetArrayLength(pArrObj);
                for (i = 0; i < nLength; i++)
                {
                    xjson_obj_t *pArrItemObj = XJSON_GetArrayItem(pArrObj, i);
                    if (pArrItemObj != NULL) xlog("testarray2: %s", (char*)pArrItemObj->pData);
                }
            }

            xjson_obj_t *pBoolObj = XJSON_GetObject(pTestObj3, "testbool2");
            if (pBoolObj != NULL) xlog("testbool2: %s", XJSON_GetBool(pBoolObj) ? "true" : "false");

            xjson_obj_t *pStringObj = XJSON_GetObject(pTestObj3, "teststr2");
            if (pStringObj != NULL) xlog("teststr2: %s", XJSON_GetString(pStringObj));
        }
    }

////////////////////////////////////////////////////////
// MINIFY JSON FILE
////////////////////////////////////////////////////////

    /* Dump json from xjson_t object */    
    if (XJSON_Write(&json, pBuffer, nSize))
        xlog("\nMinify:\n%s\n", pBuffer);

////////////////////////////////////////////////////////
// LINT JSON FILE
////////////////////////////////////////////////////////

    xjson_writer_t writer;
    XJSON_InitWriter(&writer, NULL, nSize); // Dynamic allocation
    writer.nTabSize = 4; // Enable linter and set tab size (4 spaces)

    /* Dump objects directly */
    if (XJSON_WriteObject(json.pRootObj, &writer))
    {
        xlog("Lint:\n%s\n", writer.pData);
        XJSON_DestroyWriter(&writer);
    }

    XJSON_Destroy(&json);
    free(pBuffer);
    return 0;
}