/*!
 *  @file libxutils/examples/map.c
 *
 *  Copyleft (C) 2015  Sun Dro (a.k.a. kala13x)
 *
 * @brief Simple test/demo for xmap (hash map) API.
 */

#include "xstd.h"
#include "map.h"
#include "pool.h"

static void map_clear_cb(xmap_pair_t *pPair)
{
	/* Free keys/values we allocated in this example */
	if (pPair->pKey) free(pPair->pKey);
	if (pPair->pData) free(pPair->pData);
}

static int map_print_it(xmap_pair_t *pPair, void *pCtx)
{
	(void)pCtx;
	printf("  %s => %s\n", pPair->pKey, (char*)pPair->pData);
	return XMAP_OK;
}

static int map_put_dup(xmap_t *pMap, const char *pKey, const char *pVal)
{
	char *pKeyDup = strdup(pKey);
	char *pValDup = strdup(pVal);

	if (!pKeyDup || !pValDup)
	{
		free(pKeyDup);
		free(pValDup);
		return XMAP_OMEM;
	}

	int nStatus = XMap_Put(pMap, pKeyDup, pValDup);
	if (nStatus < 0)
	{
		free(pKeyDup);
		free(pValDup);
	}

	return nStatus;
}

static void print_used(xmap_t *pMap, const char *pMsg)
{
	printf("%s (used=%d, table=%zu)\n", pMsg, XMap_UsedSize(pMap), pMap->nTableSize);
}

int main(void)
{
	xpool_t pool;
	XPool_Init(&pool, 4096);

	xmap_t map;
	if (XMap_Init(&map, &pool, 4) != XMAP_OK)
	{
		fprintf(stderr, "Failed to init map\n");
		return 1;
	}

	map.clearCb = map_clear_cb;

	printf("=== Basic put/get ===\n");
	map_put_dup(&map, "alpha", "one");
	map_put_dup(&map, "beta", "two");
	map_put_dup(&map, "gamma", "three");
	print_used(&map, "After inserts");

	char *pVal = (char*)XMap_Get(&map, "beta");
	printf("beta -> %s\n", pVal ? pVal : "<missing>");

	printf("=== Update existing ===\n");
	xmap_pair_t *pPair = XMap_GetPair(&map, "beta");
	if (pPair)
	{
		free(pPair->pData);
		pPair->pData = strdup("updated-two");
	}
	XMap_Iterate(&map, map_print_it, NULL);

	printf("=== Remove key ===\n");
	XMap_Remove(&map, "gamma");
	print_used(&map, "After remove");

	printf("=== Rehash under load ===\n");
	for (int i = 0; i < 16; i++)
	{
		char key[32];
		char val[32];
		snprintf(key, sizeof(key), "key-%02d", i);
		snprintf(val, sizeof(val), "val-%02d", i);
		map_put_dup(&map, key, val);
	}
	print_used(&map, "After bulk insert");
	XMap_Iterate(&map, map_print_it, NULL);

	printf("=== Reset and destroy ===\n");
	XMap_Destroy(&map);
	XPool_Destroy(&pool);
	return 0;
}

