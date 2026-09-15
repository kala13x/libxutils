/* libxutils: known AES block answer, padding boundaries and authenticated tamper rejection. */
#include "test.h"
#include "aes.h"

static int XTest_ecb_vector(void)
{
    const uint8_t key[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const uint8_t plain[16] = {0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    const uint8_t expected[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 4, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    uint8_t block[16];
    xaes_key_t aesKey;
    xaes_t aes;
    XAES_InitKey(&aesKey, key, 128, NULL, XFALSE);
    CHECK(XAES_Init(&aes, &aesKey, XAES_MODE_CBC) > 0, "Initialize AES-128");
    memcpy(block, plain, sizeof(block));
    XAES_ECB_Crypt(&aes, block);
    CHECK(memcmp(block, expected, sizeof(block)) == 0, "AES-128 encryption matches the FIPS 197 block vector");
    XAES_ECB_Decrypt(&aes, block);
    CHECK(memcmp(block, plain, sizeof(block)) == 0, "AES inverse recovers the published plaintext");
    return 0;
}

static int XTest_modes(void)
{
    uint8_t key[32], ctr[32], iv[16], data[97];
    memset(key, 0x31, sizeof(key));
    memset(ctr, 0xa9, sizeof(ctr));
    memset(iv, 0x47, sizeof(iv));
    for (size_t i = 0; i < sizeof(data); i++) data[i] = (uint8_t)i;
    const size_t lengths[] = {1, 15, 16, 17, 31, 32, 33, 97};
    for (int nMode = XAES_MODE_CBC; nMode <= XAES_MODE_SIV_NONCE; nMode++)
        for (size_t nBits = 128; nBits <= 256; nBits += 64)
            for (size_t i = 0; i < sizeof(lengths) / sizeof(*lengths); i++)
            {
                xaes_key_t aesKey;
                xaes_t aes;
                if (nMode >= XAES_MODE_SIV)
                    XAES_InitSIVKey(&aesKey, key, ctr, nBits);
                else
                    XAES_InitKey(&aesKey, key, nBits, iv, XFALSE);
                CHECK(XAES_Init(&aes, &aesKey, (xaes_mode_t)nMode) > 0, "Initialize each supported mode and key size");
                if (nMode == XAES_MODE_SIV_NONCE) XAES_SetSIVNonce(&aes, iv, sizeof(iv));
                size_t nLength = lengths[i];
                uint8_t *pCipher = XAES_Encrypt(&aes, data, &nLength);
                CHECK(pCipher != NULL && nLength >= lengths[i], "Encrypt across padding and block boundaries");
                CHECK(XAES_Init(&aes, &aesKey, (xaes_mode_t)nMode) > 0, "Reset the IV for independent decryption");
                if (nMode == XAES_MODE_SIV_NONCE) XAES_SetSIVNonce(&aes, iv, sizeof(iv));
                size_t nPlain = nLength;
                uint8_t *pPlain = XAES_Decrypt(&aes, pCipher, &nPlain);
                CHECK(pPlain && nPlain == lengths[i] && memcmp(pPlain, data, nPlain) == 0,
                    "Decrypt retains binary data and exact length");
                free(pPlain);
                free(pCipher);
            }
    return 0;
}

static int XTest_siv_tampering(void)
{
    uint8_t key[32] = {1}, ctr[32] = {2}, nonce[16] = {3};
    xaes_key_t aesKey;
    xaes_t aes;
    XAES_InitSIVKey(&aesKey, key, ctr, 256);
    CHECK(XAES_Init(&aes, &aesKey, XAES_MODE_SIV_NONCE) > 0, "Initialize authenticated mode");
    XAES_SetSIVNonce(&aes, nonce, sizeof(nonce));
    size_t nLength = 7;
    uint8_t *pCipher = XAES_Encrypt(&aes, (const uint8_t*)"payload", &nLength);
    CHECK(pCipher && nLength == 23, "SIV emits one tag followed by the ciphertext");
    for (size_t i = 0; i < nLength; i++)
    {
        pCipher[i] ^= 1;
        size_t nPlain = nLength;
        uint8_t *pPlain = XAES_Decrypt(&aes, pCipher, &nPlain);
        CHECK(pPlain == NULL, "Changing any tag or ciphertext byte must reject the message");
        pCipher[i] ^= 1;
    }
    nonce[0] ^= 1;
    XAES_SetSIVNonce(&aes, nonce, sizeof(nonce));
    size_t nPlain = nLength;
    CHECK(XAES_Decrypt(&aes, pCipher, &nPlain) == NULL, "A different associated nonce must reject authentication");
    free(pCipher);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(ecb_vector),
    XTEST_CASE(modes),
    XTEST_CASE(siv_tampering)
)
