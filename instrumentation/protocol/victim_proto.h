/* The victim wire protocol: the one definition, mirrored by the host in
 * instrumentation/host/mcueval/proto.py and checked against it by
 * instrumentation/host/tests/test_proto.py (no hardware, no toolchain).
 *
 * NO ASCII, for a measured reason rather than a stylistic one.
 * ChipWhisperer's simpleserial was the starting point and got dropped
 * because its `hex_decode` is a three-way if/else chain on each character:
 * '0'-'9' takes the first branch, 'a'-'f' the third, so a host sending %02x
 * makes the PARSE DURATION a function of the plaintext. In the arm that has
 * no trigger and times the UART edges, that term sits INSIDE the measured
 * interval and correlates with the plaintext in roughly the shape a
 * first-round attack looks for. It would manufacture a channel. Raw binary
 * has no such term.
 *
 * EVERY OPCODE'S PAYLOAD LENGTH IS DERIVABLE FROM THE OPCODE ALONE. There is
 * no frame marker and no length field, so after a reset or a lost byte the
 * two sides may disagree about where a command starts. A driver resyncs by
 * sending SS_V_ENC one byte at a time until a response appears: whatever
 * that response answered, the victim is then at a command boundary. The
 * filler has to be a VALID opcode so that wherever it lands it starts a
 * command the victim will finish, which is why it's SS_V_ENC and not zero.
 *
 * NO BANNER ON BOOT, for the same reason: an unsolicited byte stream after
 * reset lands in the middle of a resync and gets decoded as a response.
 */
#ifndef MCUEVAL_VICTIM_PROTO_H
#define MCUEVAL_VICTIM_PROTO_H

#include <stdint.h>

#define SS_PROTO_VERSION     1u

/* ---- requests ---------------------------------------------------------
 * The response opcode is the request with SS_V_RSP set. */
#define SS_V_ENC             0x01u   /* pt[16]  -> 0x11 ct[16]              */
#define SS_V_ENC_CYC         0x02u   /* pt[16]  -> 0x12 ct[16] cyc u32      */
#define SS_V_IDENT           0x03u   /* --      -> 0x13 ident[16]           */
#define SS_V_SET_ACR         0x04u   /* u32     -> 0x14 u32 read-back       */
#define SS_V_TRIG_CTL        0x05u   /* u8      -> 0x15 u8                  */
#define SS_V_LIBINFO         0x06u   /* --      -> 0x16 libinfo[24]         */

#define SS_V_RSP             0x10u
#define SS_V_RSP_ERR         0x1Fu   /* + the offending opcode              */

#define SS_V_PT_LEN          16u
#define SS_V_CT_LEN          16u
#define SS_V_IDENT_LEN       16u
#define SS_V_LIBINFO_LEN     24u
/* Sizes a driver's reply buffer. Raise it when an opcode's response grows. */
#define SS_V_MAX_RSP         25u

/* ---- SS_V_IDENT payload, 16 bytes -------------------------------------
 *     0   u8  protocol version
 *     1   u8  flags SS_VI_*
 *     2   u8  table layout tag, 's' / 't' / 'c' / '?'
 *     3   u8  reserved, 0
 *     4   u32 primary table runtime address  (0 = no table in flash)
 *     8   u32 the part's flash accelerator word, read back
 *    12   u32 core clock, Hz
 *
 * The table address is here because a trace count quoted without it is
 * UNDER-SPECIFIED: the same firmware at three link-time accidents has needed
 * 6 250, 12 500 and 25 000 traces. The firmware knows its own address better
 * than a human copying it out of a .map.
 *
 * Address 0 means NO TABLE IN FLASH, which is a real answer rather than a
 * missing one. Stock Mbed-TLS generates its tables into .bss at run time.
 */
#define SS_VI_TABLES_RAM     0x01u   /* the mandatory RAM twin build        */
#define SS_VI_REPORTS_CYC    0x02u   /* SS_V_ENC_CYC is implemented         */
#define SS_VI_HAS_TRIGGER    0x04u   /* a GPIO marker brackets the cipher   */
#define SS_VI_WARMUP         0x08u   /* a fixed plaintext runs first        */

/* ---- SS_V_LIBINFO payload, 24 bytes ------------------------------------
 *     0   u8  library id, SS_LIB_*
 *     1   u8  configuration bitmap, SS_LC_*
 *     2   u8  how many of the three table slots are populated
 *     3   u8  the library's own build byte, 0 if it has none
 *     4   u32 table slot 0: the byte S-box
 *     8   u32 table slot 1: the word T-tables
 *    12   u32 table slot 2: decryption tables
 *    16   u32 the encrypt entry point
 *    20   u32 the library's own version, (major<<16)|(minor<<8)|patch
 */
#define SS_LIB_REFSBOX       1u   /* this project's own byte S-box cipher    */
#define SS_LIB_MBEDTLS       2u
#define SS_LIB_WOLFSSL       3u
#define SS_LIB_STCRYPT       4u   /* ST STM32 Cryptographic Library, binary  */
#define SS_LIB_LIBTOMCRYPT   5u

/* The table residence twin: same code, same instruction stream, same
 * indices, tables relocated to RAM by the LINKER. The mandatory control. */
#define SS_LC_RAMTAB         0x01u
/* Mbed-TLS MBEDTLS_AES_ROM_TABLES. DEFAULT OFF, and off IS stock Mbed-TLS:
 * the tables are plain `static` arrays in .bss generated at run time, so the
 * flash read path never sees them and the build should be quiet. */
#define SS_LC_ROMTAB         0x02u
/* The reduced-table option: Mbed-TLS MBEDTLS_AES_FEWER_TABLES, wolfSSL
 * WOLFSSL_AES_SMALL_TABLES, LibTomCrypt LTC_SMALL_CODE. All default off. */
#define SS_LC_FEWER          0x04u
/* ST ships two archives per core; this is the time-optimised one. */
#define SS_LC_TIMEOPT        0x08u

#endif /* MCUEVAL_VICTIM_PROTO_H */
