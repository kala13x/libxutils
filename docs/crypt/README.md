# Crypt Modules

This section documents `xutils/src/crypt/*`.

## Files

- [aes.md](aes.md): AES-CBC, XBC and SIV implementations plus context initialization rules
- [base64.md](base64.md): standard and URL-safe Base64 encoders/decoders
- [crc32.md](crc32.md): CRC32 calculators
- [crypt.md](crypt.md): generic cipher-chain wrapper over hashes, codecs and symmetric/asymmetric transforms
- [hmac.md](hmac.md): HMAC-SHA256 and HMAC-MD5 helpers
- [md5.md](md5.md): MD5 digest helpers
- [rsa.md](rsa.md): RSA key management, encrypt/decrypt and RS256-style signing helpers
- [sha1.md](sha1.md): SHA-1 digest helpers
- [sha256.md](sha256.md): SHA-256 digest helpers

## Common Rules

- Most functions that return a pointer allocate memory with `malloc`/`calloc`. The caller owns the result.
- Binary outputs often include an extra trailing `NUL`, but callers must still use the returned length for transport and file I/O.
- The generic `crypt.c` layer mixes reversible transforms, hashes and text encodings. Chaining works, but only reversible stages are valid on the decrypt path.
