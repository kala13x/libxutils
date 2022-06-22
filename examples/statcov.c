/*!
 *  @file libxutils/examples/statcov.c
 * 
 *  2020-2021  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Parse and print COVID-19 case 
 * statistics from https://stopcov.ge/
 */

#include <xutils/xstd.h>
#include <xutils/xver.h>
#include <xutils/sock.h>
#include <xutils/http.h>
#include <xutils/xstr.h>
#include <xutils/xlog.h>
#include <xutils/xtype.h>
#include <xutils/xjson.h>

#define STOPCOV_LINK        "https://stopcov.ge/"
#define XSPACE_CHAR         "&#160;"
#define XSTART_POS          "numver\">"
#define XEND_POS            "</span>"
#define XTAB_SIZE           4

typedef struct {
    int nQuarantined;
    int nSupervision;
    int nConfirmed;
    int nRecovered;
    int nDeaths;
} covid_cases_t;

int COVID_ParseCase(const char *pSource, size_t nLength, const char *pCase)
{
    int nPosit = xstrnsrc(pSource, nLength, pCase, 0);
    if (nPosit < 0) return XSTDNON;

    const char *pOffset = &pSource[nPosit];
    XCHAR(sNumber, XSTR_MIN);
    XCHAR(sToken, XSTR_MIN);

    size_t i, nLen = xstrncuts(XARG_SIZE(sToken), pOffset, XSTART_POS, XEND_POS);
    if (!nLen) return XSTDERR;

    xarray_t *pArr = xstrsplit(sToken, XSPACE_CHAR);
    if (pArr == NULL) return atoi(sToken);

    for (i = 0; i < pArr->nUsed; i++)
    {
        const char *pData = (const char*)XArray_GetData(pArr, i);
        if (pData != NULL) xstrncatf(sNumber, XSTR_AVAIL(sNumber), "%s", pData);
    }

    XArray_Destroy(pArr);
    return atoi(sNumber);
}

int COVID_ParseResponse(xhttp_t *pHttp, covid_cases_t *pCovCases)
{
    if (pHttp->nStatusCode != 200)
    {
        pHttp->dataRaw.pData[pHttp->nHeaderLength - 1] = '\0';
        xlogi("Response header:\n%s\n", pHttp->dataRaw.pData);
        return XSTDNON;
    }

    const char *pContent = (const char *)XHTTP_GetBody(pHttp);
    size_t nBodyLen = XHTTP_GetBodySize(pHttp);

    pCovCases->nConfirmed = COVID_ParseCase(pContent, nBodyLen, "დადასტურებული შემთხვევა");
    pCovCases->nRecovered = COVID_ParseCase(pContent, nBodyLen, "მათ შორის გამოჯანმრთელებული");
    pCovCases->nQuarantined = COVID_ParseCase(pContent, nBodyLen, "კარანტინის რეჟიმში");
    pCovCases->nSupervision = COVID_ParseCase(pContent, nBodyLen, "მეთვალყურეობის ქვეშ");
    pCovCases->nDeaths = COVID_ParseCase(pContent, nBodyLen, "მათ შორის გარდაცვლილი");
    return pCovCases->nConfirmed;
}

int COVID_PrintCases(covid_cases_t *pCovCases)
{
    xjson_obj_t *pRootObject = XJSON_NewObject(NULL, 0);
    if (pRootObject != NULL)
    {
        XJSON_AddInt(pRootObject, "confirmed", pCovCases->nConfirmed);
        XJSON_AddInt(pRootObject, "recovered", pCovCases->nRecovered);
        XJSON_AddInt(pRootObject, "quarantined", pCovCases->nQuarantined);
        XJSON_AddInt(pRootObject, "supervision", pCovCases->nSupervision);
        XJSON_AddInt(pRootObject, "deaths", pCovCases->nDeaths);

        xjson_writer_t linter;
        XJSON_InitWriter(&linter, NULL, XSTR_MIN);
        linter.nTabSize = XTAB_SIZE;

        if (XJSON_WriteObject(pRootObject, &linter)) xlog("%s", linter.pData);
        else xloge("Failed to write JSON object (%s)", strerror(errno));

        XJSON_DestroyWriter(&linter);
        XJSON_FreeObject(pRootObject);
        return XSTDOK;
    }

    xloge("Failed to allocate memory for JSON obj: %s", strerror(errno));
    return XSTDERR;
}

int main(int argc, char* argv[])
{
    xlog_defaults();
    xlog_setfl(XLOG_ALL);

    xhttp_status_t stat;
    covid_cases_t cases;
    xhttp_t hdr;

    stat = XHTTP_SoloPerform(&hdr, XHTTP_GET, STOPCOV_LINK, NULL, 0);
    if (stat != XHTTP_COMPLETE)
    {
        xloge("%s", XHTTP_GetStatusStr(stat));
        XHTTP_Clear(&hdr);
        return 1;
    }

    if (COVID_ParseResponse(&hdr, &cases) > 0) COVID_PrintCases(&cases);
    else xloge("Response does not contain COVID-19 case statistics");

    XHTTP_Clear(&hdr);
    XSock_DeinitSSL();
    return 0;
}