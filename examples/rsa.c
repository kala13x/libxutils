/*!
 *  @file libxutils/examples/rsa.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2023  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Example file of working with RSA
 */

#include <xutils/xstd.h>
#include <xutils/xlog.h>
#include <xutils/xfs.h>
#include <xutils/crypt.h>

#define XKEY_PRIV   "rsa_priv.pem"
#define XKEY_PUB    "rsa_pub.pem"

int main()
{
    xlog_defaults();

#ifdef XCRYPT_USE_SSL
    size_t nCryptedLen = 0;

    /* Generate keys */
    xrsa_ctx_t pair;
    XRSA_Init(&pair);
    XRSA_GenerateKeys(&pair, XRSA_KEY_SIZE, XRSA_PUB_EXP);

    xlog("Generated keys:\n\n%s\n%s", pair.pPrivateKey, pair.pPublicKey);

    /* Save generated keys in the files (just for testing) */
    XPath_Write(XKEY_PRIV, "cwt", (uint8_t*)pair.pPrivateKey, pair.nPrivKeyLen);
    XPath_Write(XKEY_PUB, "cwt", (uint8_t*)pair.pPublicKey, pair.nPubKeyLen);
    XRSA_Destroy(&pair);

    /* Load  private/public keys from the file */
    xrsa_ctx_t keyPair;
    XRSA_Init(&keyPair);
    XRSA_LoadKeyFiles(&keyPair, XKEY_PRIV, XKEY_PUB);

    /* Crypt and decrypt message with private/public key pair */
    uint8_t *pCrypted = XRSA_Crypt(&keyPair, (uint8_t*)"Hello, World!", 13, &nCryptedLen);
    char *pDecrypted = (char*)XRSA_Decrypt(&keyPair, pCrypted, nCryptedLen, NULL);

    xlog("Decrypted message: %s", pDecrypted);

    /* Cleanup */
    free(pDecrypted);
    free(pCrypted);

    /* Set keys to another key structure (just for testing) */
    xrsa_ctx_t anotherKey;
    XRSA_Init(&anotherKey);
    XRSA_SetPubKey(&anotherKey, keyPair.pPublicKey, keyPair.nPubKeyLen);
    XRSA_SetPrivKey(&anotherKey, keyPair.pPrivateKey, keyPair.nPrivKeyLen);
    XRSA_Destroy(&keyPair);

    /* Crypt and decrypt message with private/public key pair */
    pCrypted = XRSA_Crypt(&anotherKey, (uint8_t*)"It's me again.", 14, &nCryptedLen);
    pDecrypted = (char*)XRSA_Decrypt(&anotherKey, pCrypted, nCryptedLen, NULL);

    xlog("Decrypted message: %s", pDecrypted);

    /* Cleanup */
    XRSA_Destroy(&anotherKey);
    free(pDecrypted);
    free(pCrypted);

    /* Remove temp files */
    remove(XKEY_PRIV);
    remove(XKEY_PUB);

    return XSTDNON;
#endif

    xloge("No SSL support (probably OpenSSL is not installed in the system).");
    return XSTDERR;
}