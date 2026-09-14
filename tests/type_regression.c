/* libxutils: exact float representation and concurrent conversion independence. */
#include "test.h"
#include "type.h"
#include "thread.h"

typedef struct xtest_type_ {
    uint32_t nBits;
    xbool_t bFailed;
} xtest_type_t;

static void *XTest_Convert(void *pContext)
{
    xtest_type_t *pTest = (xtest_type_t*)pContext;
    for (int i = 0; i < 100000; i++)
    {
        float fValue = XU32ToFloat(pTest->nBits);
        uint32_t nBits;
        memcpy(&nBits, &fValue, sizeof(nBits));
        if (nBits != pTest->nBits || XFloatToU32(fValue) != pTest->nBits) pTest->bFailed = XTRUE;
    }
    return NULL;
}

static int XTest_float_bits(void)
{
    const uint32_t bits[] = {0, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000, 1, 0x7f7fffff};
    for (size_t i = 0; i < sizeof(bits) / sizeof(*bits); i++)
    {
        float fValue = XU32ToFloat(bits[i]);
        uint32_t nActual;
        memcpy(&nActual, &fValue, sizeof(nActual));
        CHECK(nActual == bits[i] && XFloatToU32(fValue) == bits[i], "Conversions retain sign, subnormal and infinity bits");
    }
    return 0;
}

static int XTest_concurrent(void)
{
    xtest_type_t contexts[4] = {{0x3f800000, 0}, {0xbf800000, 0}, {0x41200000, 0}, {0xc1200000, 0}};
    xthread_t threads[4];
    for (int i = 0; i < 4; i++)
        CHECK(XThread_Create(&threads[i], XTest_Convert, &contexts[i], XFALSE) > 0, "Start conversion workers");
    for (int i = 0; i < 4; i++) XThread_Join(&threads[i]);
    for (int i = 0; i < 4; i++) CHECK(!contexts[i].bFailed, "Concurrent callers must not share conversion scratch storage");
    return 0;
}

XTEST_MAIN(XTEST_CASE(float_bits), XTEST_CASE(concurrent))
