/* wolfSSL user_settings.h for the sstimer library victim: wolfCrypt only,
 * AES direct/ECB, bare metal. Enabled with -DWOLFSSL_USER_SETTINGS, which is
 * wolfSSL's own documented mechanism -- no library source is touched.
 *
 * Adapted from rigs/fw_f4_lib/user_settings.h, with the Cortex-M4 note
 * corrected: this victim is a Cortex-M3 (STM32L152RE).
 *
 * THE ONE AXIS IS WOLFSSL'S OWN OPTION. WOLFSSL_AES_SMALL_TABLES defaults OFF
 * -- wolfcrypt/src/aes.c says so in its own options comment -- which gives
 * `Te[4][256]` and `Td[4][256]`, four 1 KB word tables, `static const` and so
 * in flash. wolfSSL has NO ROM/RAM switch, so unlike Mbed-TLS its stock build
 * already has its tables in flash; the only residence axis available is the
 * rig's link-time twin. ON replaces them with a 256-byte `Tsbox`.
 */
#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define NO_FILESYSTEM
#define NO_WOLFSSL_DIR
#define WOLFSSL_NO_SOCK
#define WOLFSSL_USER_IO
#define WC_NO_ASYNC_CRYPT

/* AES: the direct one-block API the provider calls. */
#define WOLFSSL_AES_DIRECT
#define HAVE_AES_ECB
#define WOLFSSL_AES_128
#define NO_AES_CBC
#define NO_AES_DECRYPT

#if defined(SS_WOLFSSL_SMALL_TABLES)
#define WOLFSSL_AES_SMALL_TABLES
#endif

/* No RNG, big-int or heap is needed for a direct block call with a
 * caller-provided key. */
#define WC_NO_RNG
#define WC_NO_HASHDRBG
#define NO_DEV_RANDOM
#define WOLFSSL_NO_MALLOC
#define NO_WOLFSSL_MEMORY

/* Everything unrelated to AES off, to keep the dependency closure small. */
#define NO_RSA
#define NO_DH
#define NO_DSA
#define NO_ASN
#define NO_CERTS
#define NO_RC4
#define NO_DES3
#define NO_MD4
#define NO_MD5
#define NO_SHA
#define NO_SHA256
#define NO_SHA512
#define NO_HMAC
#define NO_PWDBASED
#define NO_PKCS8
#define NO_PKCS12
#define NO_CODING
#define NO_BASE64_ENCODE
#define NO_ERROR_STRINGS
#define NO_OLD_TLS
#define NO_SESSION_CACHE
#define WOLFSSL_SP_MATH
#define NO_SIG_WRAPPER

/* Cortex-M3, little-endian, 32-bit. */
#define WOLFSSL_GENERAL_ALIGNMENT 4
#define LITTLE_ENDIAN_ORDER

#endif /* USER_SETTINGS_H */
