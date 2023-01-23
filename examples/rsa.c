/*!
 *  @file libxutils/examples/rsa.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2023  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Example file of working with RSA
 */

#include <xutils/xstd.h>
#include <xutils/xlog.h>
#include <xutils/xfs.h>

#define _XUTILS_USE_SSL
#include <xutils/crypt.h>

#define XKEY_PRIV   "rsa_priv.pem"
#define XKEY_PUB   "rsa_pub.pem"

int main()
{
    xlog_defaults();

    /* Generate keys */
    xrsa_key_t pair;
    XRSA_InitKey(&pair);
    XRSA_GenerateKeys(&pair, XRSA_KEY_SIZE, XRSA_PUB_EXP);
    xlog("\n%s\n%s", pair.pPrivateKey, pair.pPublicKey);

    /* Set generated keys to another key structure (just for testing) */
    xrsa_key_t anotherKey;
    XRSA_InitKey(&anotherKey);
    XRSA_SetPubKey(&anotherKey, pair.pPublicKey, pair.nPubKeyLen);
    XRSA_SetPrivKey(&anotherKey, pair.pPrivateKey, pair.nPrivKeyLen);
    XRSA_FreeKey(&pair);

    /* Crypt and decrypt message with private/public key pair */
    size_t nCryptedLen = 0, nDecryptedLen = 0;
    uint8_t *pCrypted = XRSA_Crypt(&anotherKey, (uint8_t*)"test-message", 12, &nCryptedLen);
    char *pDecrypted = (char*)XRSA_Decrypt(&anotherKey, pCrypted, nCryptedLen, &nDecryptedLen);
    xlog("Decrypted message: %s", pDecrypted);

    /* Cleanup */
    free(pDecrypted);
    free(pCrypted);

    /* Save generated keys in the files (just for testing) */
    xfile_t privFile, pubFile;
    XFile_Open(&privFile, XKEY_PRIV, "cw", NULL);
    XFile_Open(&pubFile, XKEY_PUB, "cw", NULL);
    XFile_Write(&privFile, (uint8_t*)anotherKey.pPrivateKey, anotherKey.nPrivKeyLen);
    XFile_Write(&pubFile, (uint8_t*)anotherKey.pPublicKey, anotherKey.nPubKeyLen);

    /* Cleanup */
    XRSA_FreeKey(&anotherKey);
    XFile_Close(&privFile);
    XFile_Close(&pubFile);

    /* Load  private/public keys from the file */
    xrsa_key_t keyPair;
    XRSA_InitKey(&keyPair);
    XRSA_LoadKeyFiles(&keyPair, XKEY_PRIV, XKEY_PUB);

    /* Crypt and decrypt message with private/public key pair */
    pCrypted = XRSA_Crypt(&keyPair, (uint8_t*)"another-message", 15, &nCryptedLen);
    pDecrypted = (char*)XRSA_Decrypt(&keyPair, pCrypted, nCryptedLen, &nDecryptedLen);
    xlog("Decrypted message: %s", pDecrypted);

    /* Cleanup */
    XRSA_FreeKey(&keyPair);
    free(pDecrypted);
    free(pCrypted);

    return 0;
}