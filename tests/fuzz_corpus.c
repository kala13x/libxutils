/* libxutils: seed corpus generator, so a fuzz run needs no checked-in inputs. */
#include "xstd.h"
#include "xfs.h"
#include "fuzz_parsers.h"
#include "fuzz_seeds.h"
#include <stdio.h>

static int XFuzz_WriteSeed(const char *pDir, const xfuzz_seed_t *pSeed)
{
    char sPath[XPATH_MAX];
    xstrncpyf(sPath, sizeof(sPath), "%s/%s", pDir, pSeed->pName);

    FILE *pFile = fopen(sPath, "wb");
    if (pFile == NULL)
    {
        fprintf(stderr, "Can not create seed: %s\n", sPath);
        return XSTDERR;
    }

    /* The selector byte is prepended here rather than stored in the table:
     * the harness derives the parser from it and both sides share the enum. */
    uint8_t nTarget = (uint8_t)pSeed->eTarget;
    int nStatus = XSTDNON;

    if (fwrite(&nTarget, 1, 1, pFile) != 1 ||
        (pSeed->nSize && fwrite(pSeed->pData, 1, pSeed->nSize, pFile) != pSeed->nSize))
        nStatus = XSTDERR;

    if (fclose(pFile) != 0) nStatus = XSTDERR;
    if (nStatus < 0) fprintf(stderr, "Can not write seed: %s\n", sPath);

    return nStatus;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <corpus directory>\n", argv[0]);
        return 2;
    }

    const char *pDir = argv[1];
    if (XDir_Create(pDir, 0755) <= 0)
    {
        fprintf(stderr, "Can not create corpus directory: %s\n", pDir);
        return 1;
    }

    size_t i, nCount = sizeof(g_fuzzSeeds) / sizeof(*g_fuzzSeeds);
    for (i = 0; i < nCount; i++)
        if (XFuzz_WriteSeed(pDir, &g_fuzzSeeds[i]) < 0) return 1;

    printf("Generated %zu fuzz seeds in %s\n", nCount, pDir);
    return 0;
}
