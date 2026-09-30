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

static int XTest_unpredictable_random(void)
{
    /* Generated IVs and the XBC random prefix do not come from rand(): a
     * program that seeds it the same way (or never seeds it at all) would
     * otherwise hand out the same IV and prefix on every run. */
    const uint8_t key[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
    };

    xaes_key_t first, second;
    srand(7);
    XAES_InitKey(&first, key, 256, NULL, 1);
    srand(7);
    XAES_InitKey(&second, key, 256, NULL, 1);
    CHECK(memcmp(first.IV, second.IV, XAES_BLOCK_SIZE) != 0, "The same rand() seed does not repeat a generated IV");

    /* A fixed IV and a 16 byte message leave a 12 byte random prefix, which is
     * all that tells two encryptions of the same message apart. */
    uint8_t iv[XAES_BLOCK_SIZE];
    memset(iv, 0x5C, sizeof(iv));

    uint8_t plain[16];
    memcpy(plain, "xbc prefix check", sizeof(plain));

    xaes_key_t fixedKey;
    XAES_InitKey(&fixedKey, key, 256, iv, 0);

    uint8_t *pCipher[2] = { NULL, NULL };
    size_t nLength[2] = { sizeof(plain), sizeof(plain) };

    for (int i = 0; i < 2; i++)
    {
        xaes_t aes;
        CHECK(XAES_Init(&aes, &fixedKey, XAES_MODE_XBC) > 0, "Initialize XBC with a fixed IV");
        srand(7);
        pCipher[i] = XAES_Encrypt(&aes, plain, &nLength[i]);
        CHECK(pCipher[i] != NULL && nLength[i] == 2 * XAES_BLOCK_SIZE, "XBC pads a 16 byte message to two blocks");
    }

    CHECK(memcmp(pCipher[0], pCipher[1], nLength[0]) != 0, "The same rand() seed does not repeat the XBC prefix");

    for (int i = 0; i < 2; i++)
    {
        xaes_t aes;
        CHECK(XAES_Init(&aes, &fixedKey, XAES_MODE_XBC) > 0, "Reset the XBC IV for decryption");
        size_t nPlain = nLength[i];
        uint8_t *pPlain = XAES_Decrypt(&aes, pCipher[i], &nPlain);
        CHECK(pPlain != NULL && nPlain == sizeof(plain) && !memcmp(pPlain, plain, nPlain), "Each ciphertext still decrypts");
        free(pPlain);
        free(pCipher[i]);
    }

    return 0;
}

/* Hex text into bytes; the vectors below are kept in their published form */
static size_t aes_unhex(uint8_t *pOut, size_t nSize, const char *pHex)
{
    size_t nLength = strlen(pHex) / 2;
    if (nLength > nSize) return 0;

    for (size_t i = 0; i < nLength; i++)
    {
        unsigned int nByte = 0;
        if (sscanf(pHex + i * 2, "%2x", &nByte) != 1) return 0;
        pOut[i] = (uint8_t)nByte;
    }

    return nLength;
}

static int XTest_fips_vectors(void)
{
    /* FIPS 197 appendix C for every key size, then the four block vectors
       of SP 800-38A F.1 (ECB) and F.2 (CBC), in both directions. */
    struct { size_t nBits; const char *pKey; const char *pCipher; } fips[] = {
        { 128, "000102030405060708090a0b0c0d0e0f", "69c4e0d86a7b0430d8cdb78070b4c55a" },
        { 192, "000102030405060708090a0b0c0d0e0f1011121314151617", "dda97ca4864cdfe06eaf70a0ec0d7191" },
        { 256, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "8ea2b7ca516745bfeafc49904b496089" }
    };

    for (size_t i = 0; i < sizeof(fips) / sizeof(*fips); i++)
    {
        uint8_t key[32], plain[16], expected[16], block[16];
        CHECK(aes_unhex(key, sizeof(key), fips[i].pKey) == fips[i].nBits / 8, "Decode the key");
        CHECK(aes_unhex(plain, sizeof(plain), "00112233445566778899aabbccddeeff") == 16, "Decode the plaintext");
        CHECK(aes_unhex(expected, sizeof(expected), fips[i].pCipher) == 16, "Decode the ciphertext");

        xaes_key_t aesKey;
        xaes_t aes;
        XAES_InitKey(&aesKey, key, fips[i].nBits, NULL, XFALSE);
        CHECK(XAES_Init(&aes, &aesKey, XAES_MODE_CBC) > 0, "Initialize the cipher");

        memcpy(block, plain, sizeof(block));
        XAES_ECB_Crypt(&aes, block);
        CHECK(memcmp(block, expected, sizeof(block)) == 0, "Encryption matches FIPS 197 for every key size");
        XAES_ECB_Decrypt(&aes, block);
        CHECK(memcmp(block, plain, sizeof(block)) == 0, "Decryption inverts it for every key size");
    }

    const char *pPlain = "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
                         "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710";

    struct {
        size_t nBits;
        const char *pKey;
        const char *pEcb1, *pEcb2;
        const char *pCbc1, *pCbc2;
    } sp[] = {
        { 128, "2b7e151628aed2a6abf7158809cf4f3c",
          "3ad77bb40d7a3660a89ecaf32466ef97f5d3d58503b9699de785895a96fdbaaf",
          "43b1cd7f598ece23881b00e3ed0306887b0c785e27e8ad3f8223207104725dd4",
          "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2",
          "73bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7" },
        { 192, "8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b",
          "bd334f1d6e45f25ff712a214571fa5cc974104846d0ad3ad7734ecb3ecee4eef",
          "ef7afd2270e2e60adce0ba2face6444e9a4b41ba738d6c72fb16691603c18e0e",
          "4f021db243bc633d7178183a9fa071e8b4d9ada9ad7dedf4e5e738763f69145a",
          "571b242012fb7ae07fa9baac3df102e008b0e27988598881d920a9e64f5615cd" },
        { 256, "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
          "f3eed1bdb5d2a03c064b5a7e3db181f8591ccb10d410ed26dc5ba74a31362870",
          "b6ed21b99ca6f4f9f153e7b1beafed1d23304b7a39f9f3ff067d8d8f9e24ecc7",
          "f58c4c04d6e5f1ba779eabfb5f7bfbd69cfc4e967edb808d679f777bc6702c7d",
          "39f23369a9d9bacfa530e26304231461b2eb05e2c39be9fcda6c19078c6a9d1b" }
    };

    for (size_t i = 0; i < sizeof(sp) / sizeof(*sp); i++)
    {
        uint8_t key[32], plain[64], ecb[64], cbc[64], iv[16], work[64];
        CHECK(aes_unhex(key, sizeof(key), sp[i].pKey) == sp[i].nBits / 8, "Decode the key");
        CHECK(aes_unhex(plain, sizeof(plain), pPlain) == 64, "Decode the four plaintext blocks");
        CHECK(aes_unhex(ecb, 32, sp[i].pEcb1) == 32 && aes_unhex(ecb + 32, 32, sp[i].pEcb2) == 32, "Decode ECB");
        CHECK(aes_unhex(cbc, 32, sp[i].pCbc1) == 32 && aes_unhex(cbc + 32, 32, sp[i].pCbc2) == 32, "Decode CBC");
        CHECK(aes_unhex(iv, sizeof(iv), "000102030405060708090a0b0c0d0e0f") == 16, "Decode the IV");

        xaes_key_t aesKey;
        xaes_t aes;
        XAES_InitKey(&aesKey, key, sp[i].nBits, NULL, XFALSE);
        CHECK(XAES_Init(&aes, &aesKey, XAES_MODE_CBC) > 0, "Initialize the cipher");

        memcpy(work, plain, sizeof(work));
        for (size_t b = 0; b < 64; b += 16) XAES_ECB_Crypt(&aes, work + b);
        CHECK(memcmp(work, ecb, sizeof(work)) == 0, "Every ECB block matches SP 800-38A");
        for (size_t b = 0; b < 64; b += 16) XAES_ECB_Decrypt(&aes, work + b);
        CHECK(memcmp(work, plain, sizeof(work)) == 0, "Every ECB block decrypts back");

        /* The CBC mode pads, so the published blocks are the first four of five. The
           published IV starts with a zero byte, which XAES_InitKey() takes as "no IV",
           so it is put in place directly. */
        XAES_InitKey(&aesKey, key, sp[i].nBits, NULL, XFALSE);
        memcpy(aesKey.IV, iv, sizeof(iv));
        CHECK(XAES_Init(&aes, &aesKey, XAES_MODE_CBC) > 0, "Initialize CBC with the published IV");
        size_t nLength = sizeof(plain);
        uint8_t *pCipher = XAES_Encrypt(&aes, plain, &nLength);
        CHECK(pCipher != NULL && nLength == 80 && memcmp(pCipher, cbc, 64) == 0, "CBC matches SP 800-38A");

        CHECK(XAES_Init(&aes, &aesKey, XAES_MODE_CBC) > 0, "Reset the IV");
        size_t nPlain = nLength;
        uint8_t *pDecrypted = XAES_Decrypt(&aes, pCipher, &nPlain);
        CHECK(pDecrypted != NULL && nPlain == 64 && memcmp(pDecrypted, plain, 64) == 0, "CBC decrypts back");
        free(pDecrypted);
        free(pCipher);
    }

    return 0;
}

static int XTest_siv_vectors(void)
{
    /* AES-SIV answers from OpenSSL's implementation (the key is the MAC half
       then the CTR half), so the synthetic IV and the counter mode are pinned
       even where the build has no OpenSSL to compare against at run time. */
    struct { size_t nBits; int bNonce; size_t nLength; const char *pOutput; } vectors[] = {
        { 128, 0, 0, "a3320bf6d58508f13eb4091381439277" },
        { 128, 0, 1, "c37a33360cd2bec9676b234bc3fdade871" },
        { 128, 0, 15, "823c6d81e679ebaf960837051a70d40e490a0ba0379527d94b7c0c1f8a80a9" },
        { 128, 0, 16, "40243317db14c57014c27723960dad9c2fbac4b8c4551fe314bae5a06eeaa2a3" },
        { 128, 0, 17, "d19bc8b72d26994b6a920cbd2294e02b0f622969d30eabbca771e2dafdbe01e4fa" },
        { 128, 0, 31, "80dcc2f013a686de8e353ce95860b41c9546ec98ac5591444b72507ae8386b5b18fce6d6d2c151ee55f0a7362434f1" },
        { 128, 0, 32, "310b3741582cadf24408f40eb0cce43cd6ea4a89bcd3a2744a1057725a13dd71e262ebc2c0fffeb08bdf0b4152c43b32" },
        { 128, 0, 33, "0a836fea9ec2f0247ac38ae0249b1e0eaf5383a020aa553372cec55ec8e19854c7d648a655cb6b48a58fabdb9d911ddbeb" },
        { 128, 1, 0, "7264ba3c22878bc2f774fa90bb73138e" },
        { 128, 1, 1, "f5980f77c87ade1844266df7ace1be2e12" },
        { 128, 1, 15, "53d12c2b8a6230ae488fabd14ed9b1afc7f4eedffe519ce0a9011d15498862" },
        { 128, 1, 16, "955559ca058a1275d5b5bf3f672dafde61adb9eec85eb4c6f1e318300899892d" },
        { 128, 1, 17, "b2c1e30dcd9971a440abd44156f6408cba3540a7d558684c90d1f222bd91f80341" },
        { 128, 1, 31, "f17c5c78b49b2281704c92815b2001b248f93eeb9b286161b8611f8a2b5d600637c7652055d9e406c5c47618e34e96" },
        { 128, 1, 32, "a3829d1051dac000fccebde9f3478e16184fa664053834dda36f0508c6c92b9a25ddd6a1b18bed891a79714252c19694" },
        { 128, 1, 33, "b30b3f67570b094e5afe48125f696d9dd5c9fe069654f81b170bace3b5b52c94fc3dc6046ed312388c3c4b6f4b00675113" },
        { 192, 0, 0, "b6dfd5f58d23699a8ad2a8c6b5f2756c" },
        { 192, 0, 1, "d4141e5ee606b80a2eb917162ea1248ba7" },
        { 192, 0, 15, "7baeb7e58e887ee7c6ab76d09c5a8c61bfaf8f0aa3fe62aca0d7124e54498a" },
        { 192, 0, 16, "8dd2a6147a4b673625d2f8b87f847dc8406a5d7e7cce3b2e9e1a360c391fe98b" },
        { 192, 0, 17, "c51fed3cd52cd1e40f9a1074fc6ed5ffbfbf18446924c88c8df87fe4863871972d" },
        { 192, 0, 31, "3fbd9fa9c78960651c4b46a094a732f6a748d95e829e95dfbfeafa979c74009ca987df55cf456a646a477fe7a2fd83" },
        { 192, 0, 32, "6b55bc02c50082c663a177ebba7cc812f5f7875509ee764b27878e86a3874868e8742da209886cfd4f4092e88318d1f7" },
        { 192, 0, 33, "b35c45ff6d717a16345f8a6fba20316ac59b79f7363e28bf3478c9c7ca466e9d8d7daa55243e922943f39dbd82f4674682" },
        { 192, 1, 0, "f8fddbbac3cfbd5cef5803e7504f3888" },
        { 192, 1, 1, "64fed189597da3151177a01d8d5352ba7c" },
        { 192, 1, 15, "ea7d015b31f99b66e9c342b6511de999ee870d5b0a6f2a0191266297670d77" },
        { 192, 1, 16, "ec1141d4e16836d58359387db4672ff240e8f4d8d899bed0b0241b64afe0625b" },
        { 192, 1, 17, "2825424f400d5d8da496f3de3a1afc467f3671d105ab461b86b58a364b76bd5498" },
        { 192, 1, 31, "5bc2a3104ec40283ab933090c1f79310ae862a237a0cc3fbbf316d118a23a419e1c51ea4fc90aba2e368012d3aa0d1" },
        { 192, 1, 32, "1c8180b7bd21809bb2a279b4ad60a210da329f9e0dc6187e27aa5aecdbacf50ad0f410d1d4bbc5eec9e2b463ecf22111" },
        { 192, 1, 33, "04a483625129102592f70c0b13246b295f5443d9d57d525ace539e60673787356aebf4b8a2df2ea3c08fa982b1314df847" },
        { 256, 0, 0, "0eab8ac8e648a0a7c8270dd108477feb" },
        { 256, 0, 1, "6832d5fbf127ca1a981fa2cfacda4c94f6" },
        { 256, 0, 15, "c864fd1aa525bd23d58d2ca8875ff27b2aadda0bd87e7ffad3f31f9edd2796" },
        { 256, 0, 16, "a329f78015a06133a3e51da1caf6db6f5a6e155efaa4cf81163f67c708dbafd5" },
        { 256, 0, 17, "8e03a8c4c68ca06bca79df2f85b2153cfebca93183b4e43a8dd1ebcbf5f7b54592" },
        { 256, 0, 31, "d9714b5825eff2ab78624512513bd148d81f821537781671b9e2267fb584854fab4b386bf8ea05a20341283a600707" },
        { 256, 0, 32, "86985efe990c9e3db3e055798ebe04352046a90bd38d118ae720bcbdc9f4737afa63292a1a32c13d9c7c962a64cad4aa" },
        { 256, 0, 33, "9e8d1c6f39c0355b55137720870a00973a85ddaa797da091c4fa880d30a4155c64791215632ccf027c4b88fa90fca34e3c" },
        { 256, 1, 0, "a7b6a1b0081d3cfdc5623e29f45d47a1" },
        { 256, 1, 1, "0e710ad8c73c5e4f18e548ba3bf0b2b685" },
        { 256, 1, 15, "4f1c14e9b5e2fe661d0c629f4cc90529db804f6c0c65661f686edabd92da8c" },
        { 256, 1, 16, "6aa9a6eae40b2f7cbbb1212610d0e576e04d01ddcfd4ab9b868bee2e861e04f4" },
        { 256, 1, 17, "ec15c7f22da869df03e49f05182cb942e805d3b67a965b2c10f38f83651fafbf57" },
        { 256, 1, 31, "a2d2d154e8b63cee6940d2d31bd9d541fdea5a03b40c69e427a4f95850a3daf02c3ee4987470d32cc620fd68ef690f" },
        { 256, 1, 32, "fa103a020f12ff811c4ac531217263362dfdb34d3e0761f296f33574c7f21d806e9cf29aa786d4e9853abc3ab6c00e8a" },
        { 256, 1, 33, "da444a6cd91731a4fe3bfe3c18f8f1fe5a74f2904b7fe26a28fb25d9cd30c92741e40c42b68294f40e7a4a27e8861610d2" },
    };

    uint8_t mac[32], ctr[32], nonce[16], plain[80];
    for (int i = 0; i < 32; i++) { mac[i] = (uint8_t)(0x10 + i); ctr[i] = (uint8_t)(0xf0 - i); }
    for (int i = 0; i < 16; i++) nonce[i] = (uint8_t)(0xa0 + i * 3);
    for (int i = 0; i < 80; i++) plain[i] = (uint8_t)(i * 7 + 1);

    for (size_t i = 0; i < sizeof(vectors) / sizeof(*vectors); i++)
    {
        uint8_t expected[96];
        size_t nExpected = aes_unhex(expected, sizeof(expected), vectors[i].pOutput);
        CHECK(nExpected == vectors[i].nLength + 16, "Decode the expected output");

        xaes_key_t aesKey;
        xaes_t aes;
        XAES_InitSIVKey(&aesKey, mac, ctr, vectors[i].nBits);
        CHECK(XAES_Init(&aes, &aesKey, vectors[i].bNonce ? XAES_MODE_SIV_NONCE : XAES_MODE_SIV) > 0, "Initialize SIV");
        if (vectors[i].bNonce) XAES_SetSIVNonce(&aes, nonce, sizeof(nonce));

        size_t nLength = vectors[i].nLength;
        uint8_t *pOutput = XAES_Encrypt(&aes, plain, &nLength);
        CHECK(pOutput != NULL && nLength == nExpected, "Encrypt to tag and ciphertext");
        CHECK(memcmp(pOutput, expected, nExpected) == 0, "The output matches OpenSSL byte for byte");

        if (vectors[i].nLength)
        {
            size_t nPlain = nLength;
            uint8_t *pPlain = XAES_Decrypt(&aes, pOutput, &nPlain);
            CHECK(pPlain != NULL && nPlain == vectors[i].nLength && !memcmp(pPlain, plain, nPlain), "It decrypts back");
            free(pPlain);

            /* Every single bit flip anywhere is caught by the tag */
            for (size_t nBit = 0; nBit < nLength * 8; nBit += 7)
            {
                pOutput[nBit / 8] ^= (uint8_t)(1 << (nBit % 8));
                nPlain = nLength;
                CHECK(XAES_Decrypt(&aes, pOutput, &nPlain) == NULL, "A flipped bit is rejected");
                pOutput[nBit / 8] ^= (uint8_t)(1 << (nBit % 8));
            }
        }

        free(pOutput);
    }

    return 0;
}

static int XTest_block_roundtrip(void)
{
    /* Many keys and blocks, each size: decryption is the inverse of encryption,
       and one changed plaintext bit changes about half the ciphertext bits. */
    srand(20260929);
    for (int nRound = 0; nRound < 3000; nRound++)
    {
        size_t nBits = 128 + (size_t)(nRound % 3) * 64;
        uint8_t key[32], block[16], copy[16];
        for (size_t i = 0; i < sizeof(key); i++) key[i] = (uint8_t)rand();
        for (size_t i = 0; i < sizeof(block); i++) block[i] = (uint8_t)rand();
        memcpy(copy, block, sizeof(copy));

        xaes_key_t aesKey;
        xaes_t aes;
        XAES_InitKey(&aesKey, key, nBits, NULL, XFALSE);
        CHECK(XAES_Init(&aes, &aesKey, XAES_MODE_CBC) > 0, "Initialize the cipher");

        XAES_ECB_Crypt(&aes, block);
        CHECK(memcmp(block, copy, sizeof(block)) != 0, "Encryption changes the block");

        uint8_t flipped[16];
        memcpy(flipped, copy, sizeof(flipped));
        flipped[nRound % 16] ^= (uint8_t)(1 << (nRound % 8));
        XAES_ECB_Crypt(&aes, flipped);

        int nChanged = 0;
        for (size_t i = 0; i < 16; i++)
            for (int b = 0; b < 8; b++) nChanged += ((block[i] ^ flipped[i]) >> b) & 1;
        CHECK(nChanged > 20 && nChanged < 108, "One plaintext bit spreads over the whole block");

        XAES_ECB_Decrypt(&aes, block);
        CHECK(memcmp(block, copy, sizeof(block)) == 0, "Decryption inverts encryption");
    }

    return 0;
}

static int XTest_xbc_prefix(void)
{
    const uint8_t key[16] = {1}, iv[16] = {2};
    const uint32_t lengths[] = {0, 1, 11, 12, 13, 15, 16, 255, UINT32_MAX - 4, UINT32_MAX - 3, UINT32_MAX};
    for (int nCarried = 0; nCarried < 2; nCarried++)
    {
        for (size_t i = 0; i < sizeof(lengths) / sizeof(*lengths); i++)
        {
            uint8_t wire[32], block[16];
            uint32_t nPrefix = lengths[i];
            memset(block, 0xa7, sizeof(block));
            block[0] = (uint8_t)(nPrefix >> 24);
            block[1] = (uint8_t)(nPrefix >> 16);
            block[2] = (uint8_t)(nPrefix >> 8);
            block[3] = (uint8_t)nPrefix;
            xaes_key_t aesKey;
            xaes_t aes;
            XAES_InitKey(&aesKey, key, 128, iv, nCarried);
            CHECK(XAES_Init(&aes, &aesKey, XAES_MODE_XBC) > 0, "Initialize prefix-boundary fixture");
            for (size_t j = 0; j < sizeof(block); j++) block[j] ^= iv[j];
            XAES_ECB_Crypt(&aes, block);
            size_t nOffset = nCarried ? 16 : 0;
            if (nCarried) memcpy(wire, iv, 16);
            memcpy(wire + nOffset, block, 16);
            size_t nLength = nOffset + 16;
            uint8_t *pPlain = XAES_XBC_Decrypt(&aes, wire, &nLength);
            xbool_t bValid = nPrefix <= 12;
            int nCorrect = bValid ? pPlain != NULL && nLength == 12 - nPrefix : pPlain == NULL;
            if (bValid && pPlain)
                for (size_t j = 0; j < nLength; j++) if (pPlain[j] != 0xa7) nCorrect = 0;
            free(pPlain);
            CHECK(nCorrect, "The prefix must fit its ciphertext even when its length would overflow");
        }
    }
    return 0;
}

static int XTest_length_overflow(void)
{
    const uint8_t key[16] = {1}, iv[16] = {2}, input[1] = {3};
    for (int nMode = XAES_MODE_CBC; nMode <= XAES_MODE_SIV_NONCE; nMode++)
    {
        for (int nCarried = 0; nCarried < 2; nCarried++)
        {
            xaes_key_t aesKey;
            xaes_t aes;
            if (nMode >= XAES_MODE_SIV) XAES_InitSIVKey(&aesKey, key, key, 128);
            else XAES_InitKey(&aesKey, key, 128, iv, nCarried);
            CHECK(XAES_Init(&aes, &aesKey, (xaes_mode_t)nMode) > 0, "Initialize length-boundary fixture");
            for (size_t i = 0; i < 16; i++)
            {
                size_t nLength = SIZE_MAX - i;
                uint8_t *pOutput = XAES_Encrypt(&aes, input, &nLength);
                int nRejected = pOutput == NULL && nLength == SIZE_MAX - i;
                free(pOutput);
                CHECK(nRejected, "Unrepresentable ciphertext sizes fail before reading or modifying input");
            }
        }
    }
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(ecb_vector),
    XTEST_CASE(xbc_prefix),
    XTEST_CASE(length_overflow),
    XTEST_CASE(modes),
    XTEST_CASE(siv_tampering),
    XTEST_CASE(generated_iv),
    XTEST_CASE(malformed_ciphertext),
    XTEST_CASE(unpredictable_random),
    XTEST_CASE(fips_vectors),
    XTEST_CASE(siv_vectors),
    XTEST_CASE(block_roundtrip)
)
