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


static int XTest_generated_iv(void)
{
    /* When a key says it carries its own IV and none is supplied, one is
     * generated. Two keys made the same way must not come out with the same
     * IV: a repeated IV in CBC leaks whether two messages share a prefix. */
    const uint8_t key[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
    };

    const uint8_t zeroIV[XAES_BLOCK_SIZE] = {0};

    xaes_key_t first, second;
    XAES_InitKey(&first, key, 256, NULL, 1);
    XAES_InitKey(&second, key, 256, NULL, 1);

    CHECK(memcmp(first.IV, zeroIV, sizeof(zeroIV)) != 0, "A generated IV is not all zeroes");
    CHECK(first.nContainIV == 1, "The key records that it carries its IV");

    /* Not a statistical test, just the one failure that would be fatal:
     * the generator handing back the same block every time. */
    int nDiffering = 0;
    for (int i = 0; i < 16; i++)
    {
        xaes_key_t again;
        XAES_InitKey(&again, key, 256, NULL, 1);
        if (memcmp(again.IV, first.IV, XAES_BLOCK_SIZE) != 0) nDiffering++;
    }
    CHECK(nDiffering > 0, "Generated IVs are not all identical");

    /* Asking for no IV gives a deterministic zero one. */
    xaes_key_t plain;
    XAES_InitKey(&plain, key, 256, NULL, 0);
    CHECK(memcmp(plain.IV, zeroIV, sizeof(zeroIV)) == 0, "Without the flag the IV is zeroed");
    CHECK(plain.nContainIV == 0, "The key records that it carries no IV");

    /* An explicit IV always wins over both. */
    uint8_t given[XAES_BLOCK_SIZE];
    memset(given, 0xA7, sizeof(given));

    xaes_key_t explicitKey;
    XAES_InitKey(&explicitKey, key, 256, given, 1);
    CHECK(memcmp(explicitKey.IV, given, sizeof(given)) == 0, "An explicit IV is taken as given");

    /* And a generated IV still decrypts what it encrypted. */
    xaes_t aes;
    CHECK(XAES_Init(&aes, &first, XAES_MODE_CBC) > 0, "A generated IV initializes a cipher");

    const char *pText = "generated iv round trip";
    size_t nLength = strlen(pText);

    uint8_t *pCipher = XAES_Encrypt(&aes, (const uint8_t*)pText, &nLength);
    CHECK(pCipher != NULL, "The message encrypts");

    CHECK(XAES_Init(&aes, &first, XAES_MODE_CBC) > 0, "The cipher is reset with the same key");
    uint8_t *pPlain = XAES_Decrypt(&aes, pCipher, &nLength);

    CHECK(pPlain != NULL && nLength == strlen(pText), "The message decrypts to its own length");
    CHECK(pPlain == NULL || memcmp(pPlain, pText, nLength) == 0, "The message decrypts unchanged");

    free(pCipher);
    free(pPlain);

    /* A key with no destination is refused rather than written through. */
    XAES_InitKey(NULL, key, 256, NULL, 1);
    XAES_InitSIVKey(NULL, key, key, 256);

    /* A key size the cipher does not have leaves the size unset rather than
     * copying an arbitrary number of bytes out of the caller's buffer. */
    xaes_key_t odd;
    XAES_InitKey(&odd, key, 200, NULL, 0);
    CHECK(odd.nKeySize == 0, "An unsupported key size is not accepted");
    return 0;
}


static int XTest_malformed_ciphertext(void)
{
    /* Ciphertext arrives from wherever the message came from, so every
     * length and every shape of it has to be refused rather than decrypted
     * into whatever the buffer happened to contain. Each mode has its own
     * framing, so each is checked on its own. */
    const uint8_t key[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
    };

    const uint8_t iv[XAES_BLOCK_SIZE] = {
        0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF
    };

    uint8_t sCipher[256];
    memset(sCipher, 0x5A, sizeof(sCipher));

    const xaes_mode_t modes[] = {XAES_MODE_CBC, XAES_MODE_XBC};

    for (size_t m = 0; m < sizeof(modes) / sizeof(*modes); m++)
    {
        /* With the IV carried in the message, anything at or below one
         * block is all IV and no ciphertext. */
        xaes_key_t carried;
        XAES_InitKey(&carried, key, 256, iv, 1);

        xaes_t aes;
        CHECK(XAES_Init(&aes, &carried, modes[m]) > 0, "The cipher initializes with a carried IV");

        for (size_t nLen = 1; nLen <= XAES_BLOCK_SIZE; nLen++)
        {
            size_t nLength = nLen;
            uint8_t *pPlain = XAES_Decrypt(&aes, sCipher, &nLength);
            CHECK(pPlain == NULL, "Nothing at or below one block can carry both an IV and data");
            free(pPlain);
        }

        /* And past that, only whole blocks of ciphertext are valid. */
        for (size_t nExtra = 1; nExtra < XAES_BLOCK_SIZE; nExtra++)
        {
            size_t nLength = XAES_BLOCK_SIZE + nExtra;
            uint8_t *pPlain = XAES_Decrypt(&aes, sCipher, &nLength);
            CHECK(pPlain == NULL, "A partial trailing block is refused");
            free(pPlain);
        }

        /* Without a carried IV the whole message must be whole blocks. */
        xaes_key_t plain;
        XAES_InitKey(&plain, key, 256, iv, 0);
        CHECK(XAES_Init(&aes, &plain, modes[m]) > 0, "The cipher initializes with a fixed IV");

        for (size_t nLen = 1; nLen < XAES_BLOCK_SIZE; nLen++)
        {
            size_t nLength = nLen;
            uint8_t *pPlain = XAES_Decrypt(&aes, sCipher, &nLength);
            CHECK(pPlain == NULL, "Less than one block is refused");
            free(pPlain);
        }

        for (size_t nExtra = 1; nExtra < XAES_BLOCK_SIZE; nExtra++)
        {
            size_t nLength = XAES_BLOCK_SIZE + nExtra;
            uint8_t *pPlain = XAES_Decrypt(&aes, sCipher, &nLength);
            CHECK(pPlain == NULL, "A message that is not whole blocks is refused");
            free(pPlain);
        }

        /* The argument guards. */
        size_t nLength = XAES_BLOCK_SIZE;
        CHECK(XAES_Decrypt(&aes, NULL, &nLength) == NULL, "Missing ciphertext is refused");
        CHECK(XAES_Decrypt(&aes, sCipher, NULL) == NULL, "A missing length is refused");

        nLength = 0;
        CHECK(XAES_Decrypt(&aes, sCipher, &nLength) == NULL, "A zero length is refused");

        nLength = XAES_BLOCK_SIZE;
        CHECK(XAES_Decrypt(NULL, sCipher, &nLength) == NULL, "A missing cipher is refused");
    }

    /* A valid message with one bit flipped decrypts to something, but not
     * to the original: silent corruption is the mode's contract, and what
     * matters is that it cannot be mistaken for the real plaintext. */
    xaes_key_t carried;
    XAES_InitKey(&carried, key, 256, iv, 1);

    xaes_t aes;
    CHECK(XAES_Init(&aes, &carried, XAES_MODE_CBC) > 0, "The cipher initializes");

    const char *pText = "a message worth exactly two blocks of aes.";
    size_t nLength = strlen(pText);

    uint8_t *pCipher = XAES_Encrypt(&aes, (const uint8_t*)pText, &nLength);
    CHECK(pCipher != NULL && nLength > XAES_BLOCK_SIZE, "The message encrypts");

    if (pCipher != NULL)
    {
        pCipher[nLength - 1] ^= 0x01;

        size_t nBack = nLength;
        CHECK(XAES_Init(&aes, &carried, XAES_MODE_CBC) > 0, "The cipher is reset");

        uint8_t *pPlain = XAES_Decrypt(&aes, pCipher, &nBack);
        CHECK(pPlain == NULL || nBack != strlen(pText) ||
              memcmp(pPlain, pText, nBack) != 0, "A flipped bit does not decrypt to the original");
        free(pPlain);
    }

    free(pCipher);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(ecb_vector),
    XTEST_CASE(modes),
    XTEST_CASE(siv_tampering),
    XTEST_CASE(generated_iv),
    XTEST_CASE(malformed_ciphertext)
)
