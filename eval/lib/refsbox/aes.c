/* AES-128 encrypt: S-box (byte lookups) and T-table (Te0..Te3) variants.
 * State is 4 big-endian 32-bit words, word c = column c, byte row0 = MSB.
 * This matches the reference in gen_tables.py, verified against FIPS-197. */
#include "aes.h"
#include "tables.h"

/* RAM copies of the tables (.bss -> SRAM) and the active-table pointers used by
 * the timed encrypt paths. aes_use_ram() flips between flash and RAM tables so
 * the same firmware can measure both - the tables-in-RAM control. */
static uint8_t  sbox_ram[256];
static uint32_t te0_ram[256], te1_ram[256], te2_ram[256], te3_ram[256];
static const uint8_t  *SB = SBOX;
static const uint32_t *T0 = Te0, *T1 = Te1, *T2 = Te2, *T3 = Te3;

void aes_init_ram_tables(void) {
    for (int i = 0; i < 256; i++) {
        sbox_ram[i] = SBOX[i];
        te0_ram[i] = Te0[i]; te1_ram[i] = Te1[i]; te2_ram[i] = Te2[i]; te3_ram[i] = Te3[i];
    }
}
void aes_use_ram(int on) {
    if (on) { SB = sbox_ram; T0 = te0_ram; T1 = te1_ram; T2 = te2_ram; T3 = te3_ram; }
    else    { SB = SBOX;     T0 = Te0;     T1 = Te1;     T2 = Te2;     T3 = Te3; }
}

/* Round-1 S-box lookups only, repeated `reps` times, to isolate the round-1
 * access pattern (addresses = SBOX + (in_i ^ key_i)) from key-schedule diffusion
 * into later rounds. Returns a sink so the loop isn't optimized away. */
uint8_t aes_round1_bench(const uint8_t in[16], const uint8_t key[16], int reps) {
    uint8_t sink = 0;
    for (int r = 0; r < reps; r++)
        for (int i = 0; i < 16; i++)
            sink ^= SB[(uint8_t)(in[i] ^ key[i])];
    return sink;
}

static inline uint8_t xt(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }

void aes_key_schedule(const uint8_t key[16], uint32_t w[44]) {
    for (int i = 0; i < 4; i++)
        w[i] = ((uint32_t)key[4*i] << 24) | ((uint32_t)key[4*i+1] << 16)
             | ((uint32_t)key[4*i+2] << 8) | key[4*i+3];
    uint32_t rcon = 0x01000000u;
    for (int i = 4; i < 44; i++) {
        uint32_t t = w[i-1];
        if ((i & 3) == 0) {
            t = (t << 8) | (t >> 24);                                  /* RotWord */
            t = ((uint32_t)SBOX[(t>>24)&0xff] << 24) | ((uint32_t)SBOX[(t>>16)&0xff] << 16)
              | ((uint32_t)SBOX[(t>>8)&0xff] << 8) | SBOX[t&0xff];      /* SubWord */
            t ^= rcon;
            uint8_t rb = (uint8_t)(rcon >> 24);
            rcon = (uint32_t)xt(rb) << 24;
        }
        w[i] = w[i-4] ^ t;
    }
}

static inline uint32_t subword(uint32_t x) {   /* uses active S-box (flash or RAM) */
    return ((uint32_t)SB[(x>>24)&0xff] << 24) | ((uint32_t)SB[(x>>16)&0xff] << 16)
         | ((uint32_t)SB[(x>>8)&0xff] << 8) | SB[x&0xff];
}

static inline uint32_t mixcolumn(uint32_t c) {
    uint8_t a0=c>>24, a1=c>>16, a2=c>>8, a3=c;
    uint8_t r0 = (uint8_t)(xt(a0) ^ (xt(a1)^a1) ^ a2 ^ a3);
    uint8_t r1 = (uint8_t)(a0 ^ xt(a1) ^ (xt(a2)^a2) ^ a3);
    uint8_t r2 = (uint8_t)(a0 ^ a1 ^ xt(a2) ^ (xt(a3)^a3));
    uint8_t r3 = (uint8_t)((xt(a0)^a0) ^ a1 ^ a2 ^ xt(a3));
    return ((uint32_t)r0<<24)|((uint32_t)r1<<16)|((uint32_t)r2<<8)|r3;
}

static inline uint8_t rowb(uint32_t w, int row) { return (uint8_t)(w >> (24 - 8*row)); }

void aes_sbox_encrypt_nr(const uint8_t in[16], uint8_t out[16], const uint32_t w[44], int nr) {
    uint32_t s[4], t[4];
    for (int c = 0; c < 4; c++)
        s[c] = (((uint32_t)in[4*c]<<24)|((uint32_t)in[4*c+1]<<16)
              |((uint32_t)in[4*c+2]<<8)|in[4*c+3]) ^ w[c];
    for (int r = 1; r <= nr; r++) {
        for (int c = 0; c < 4; c++) s[c] = subword(s[c]);            /* SubBytes */
        for (int c = 0; c < 4; c++)                                  /* ShiftRows */
            t[c] = ((uint32_t)rowb(s[c],0)<<24) | ((uint32_t)rowb(s[(c+1)&3],1)<<16)
                 | ((uint32_t)rowb(s[(c+2)&3],2)<<8) | rowb(s[(c+3)&3],3);
        if (r < 10) for (int c = 0; c < 4; c++) t[c] = mixcolumn(t[c]);
        for (int c = 0; c < 4; c++) s[c] = t[c] ^ w[4*r+c];          /* AddRoundKey */
    }
    for (int c = 0; c < 4; c++) {
        out[4*c]=s[c]>>24; out[4*c+1]=s[c]>>16; out[4*c+2]=s[c]>>8; out[4*c+3]=s[c];
    }
}

void aes_sbox_encrypt(const uint8_t in[16], uint8_t out[16], const uint32_t w[44]) {
    aes_sbox_encrypt_nr(in, out, w, 10);
}

/* Same cipher, but the four S-box reads in each column are issued in the order
 * byte1, byte0, byte3, byte2 - so the first<->last collision now exposes the
 * (row1,row2) byte pair. Models a different compiler/implementation; used to
 * recover the last-round-key bytes the natural order leaves hidden. */
static inline uint32_t subword_alt(uint32_t x) {
    uint8_t s1 = SB[(x >> 16) & 0xff];      /* read byte1 first  */
    uint8_t s0 = SB[(x >> 24) & 0xff];      /*      byte0        */
    uint8_t s3 = SB[x & 0xff];              /*      byte3        */
    uint8_t s2 = SB[(x >> 8) & 0xff];       /* read byte2 last   */
    return ((uint32_t)s0<<24)|((uint32_t)s1<<16)|((uint32_t)s2<<8)|s3;
}

void aes_sbox_encrypt_alt(const uint8_t in[16], uint8_t out[16], const uint32_t w[44]) {
    uint32_t s[4], t[4];
    for (int c = 0; c < 4; c++)
        s[c] = (((uint32_t)in[4*c]<<24)|((uint32_t)in[4*c+1]<<16)
              |((uint32_t)in[4*c+2]<<8)|in[4*c+3]) ^ w[c];
    for (int r = 1; r <= 10; r++) {
        for (int c = 0; c < 4; c++) s[c] = subword_alt(s[c]);
        for (int c = 0; c < 4; c++)
            t[c] = ((uint32_t)rowb(s[c],0)<<24) | ((uint32_t)rowb(s[(c+1)&3],1)<<16)
                 | ((uint32_t)rowb(s[(c+2)&3],2)<<8) | rowb(s[(c+3)&3],3);
        if (r < 10) for (int c = 0; c < 4; c++) t[c] = mixcolumn(t[c]);
        for (int c = 0; c < 4; c++) s[c] = t[c] ^ w[4*r+c];
    }
    for (int c = 0; c < 4; c++) {
        out[4*c]=s[c]>>24; out[4*c+1]=s[c]>>16; out[4*c+2]=s[c]>>8; out[4*c+3]=s[c];
    }
}

void aes_ttable_encrypt_nr(const uint8_t in[16], uint8_t out[16], const uint32_t w[44], int nr) {
    uint32_t s[4], t[4];
    for (int c = 0; c < 4; c++)
        s[c] = (((uint32_t)in[4*c]<<24)|((uint32_t)in[4*c+1]<<16)
              |((uint32_t)in[4*c+2]<<8)|in[4*c+3]) ^ w[c];
    int R = nr < 10 ? nr : 9;
    for (int r = 1; r <= R; r++) {
        for (int c = 0; c < 4; c++)
            t[c] = T0[(s[c]>>24)&0xff] ^ T1[(s[(c+1)&3]>>16)&0xff]
                 ^ T2[(s[(c+2)&3]>>8)&0xff] ^ T3[s[(c+3)&3]&0xff] ^ w[4*r+c];
        for (int c = 0; c < 4; c++) s[c] = t[c];
    }
    if (nr >= 10) {
        /* Final round, staged through t[]. Writing s[c] in place here let
         * columns 1-3 read state that earlier iterations had already
         * overwritten, so this computed the WRONG cipher - FIPS-197 C.1 came
         * back 69c4e0d86a7b0476d8cd46b37037c2a8. The staging changes no lookup
         * address, so the access pattern under measurement is unchanged. */
        for (int c = 0; c < 4; c++)
            t[c] = ((uint32_t)SB[(s[c]>>24)&0xff]<<24) | ((uint32_t)SB[(s[(c+1)&3]>>16)&0xff]<<16)
                 | ((uint32_t)SB[(s[(c+2)&3]>>8)&0xff]<<8) | SB[s[(c+3)&3]&0xff];
        for (int c = 0; c < 4; c++) s[c] = t[c] ^ w[40+c];
    }
    for (int c = 0; c < 4; c++) {
        out[4*c]=s[c]>>24; out[4*c+1]=s[c]>>16; out[4*c+2]=s[c]>>8; out[4*c+3]=s[c];
    }
}

void aes_ttable_encrypt(const uint8_t in[16], uint8_t out[16], const uint32_t w[44]) {
    aes_ttable_encrypt_nr(in, out, w, 10);
}

static inline uint32_t ror32(uint32_t x, int n);   /* defined below */

/* Compact single-table T-table for rounds 1..9, but a STANDARD within-column
 * S-box final round (SubBytes on each column, then ShiftRows, then AddRoundKey)
 * - the common way real T-table code does the last round. That final round
 * reads within-column bytes, which collide and leak, so the last-round attack
 * applies. `alt` picks the final-round S-box read order (row0,row3 vs row1,row2)
 * to expose both halves of k10. */
void aes_ttw_encrypt(const uint8_t in[16], uint8_t out[16], const uint32_t w[44], int alt) {
    uint32_t s[4], t[4];
    for (int c = 0; c < 4; c++)
        s[c] = (((uint32_t)in[4*c]<<24)|((uint32_t)in[4*c+1]<<16)
              |((uint32_t)in[4*c+2]<<8)|in[4*c+3]) ^ w[c];
    for (int r = 1; r <= 9; r++) {                              /* compact T-table rounds */
        for (int c = 0; c < 4; c++)
            t[c] = T0[(s[c]>>24)&0xff]
                 ^ ror32(T0[(s[(c+1)&3]>>16)&0xff], 8)
                 ^ ror32(T0[(s[(c+2)&3]>>8)&0xff], 16)
                 ^ ror32(T0[s[(c+3)&3]&0xff], 24) ^ w[4*r+c];
        for (int c = 0; c < 4; c++) s[c] = t[c];
    }
    for (int c = 0; c < 4; c++) s[c] = alt ? subword_alt(s[c]) : subword(s[c]);  /* final SubBytes */
    for (int c = 0; c < 4; c++)                                 /* ShiftRows */
        t[c] = ((uint32_t)rowb(s[c],0)<<24) | ((uint32_t)rowb(s[(c+1)&3],1)<<16)
             | ((uint32_t)rowb(s[(c+2)&3],2)<<8) | rowb(s[(c+3)&3],3);
    for (int c = 0; c < 4; c++) s[c] = t[c] ^ w[40+c];          /* AddRoundKey */
    for (int c = 0; c < 4; c++) {
        out[4*c]=s[c]>>24; out[4*c+1]=s[c]>>16; out[4*c+2]=s[c]>>8; out[4*c+3]=s[c];
    }
}

/* Compact single-table T-table: one 1 KB Te0 + rotations (Te1=ror8, Te2=ror16,
 * Te3=ror24). ALL 16 lookups per round hit one table, so same-line collisions
 * return - the space-saving variant many real implementations use. */
static inline uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

/* The ONE divergence from fw_g4/aes.c: a build-time halfword pad, inert at the
 * default of 0.  It exists to move this function's final-round loop by an odd
 * multiple of 2 bytes relative to the 8-byte flash read word, which the linker
 * cannot do - ARM's `LDR (literal)` is based on Align(PC,4), so relocating
 * Thumb code by 2 desynchronises every literal pool (see link.ld).  Inserting
 * the halfwords in the source instead lets the assembler re-lay the pools, so
 * the build stays correct while the loop moves.  Nothing else in the file is
 * touched and the access pattern under measurement is unchanged. */
#ifndef CIPHER_NOPS
#define CIPHER_NOPS 0
#endif
#define _CN_STR2(x) #x
#define _CN_STR(x)  _CN_STR2(x)

void aes_ttable1_encrypt_nr(const uint8_t in[16], uint8_t out[16], const uint32_t w[44], int nr) {
#if CIPHER_NOPS > 0
    __asm volatile(".rept " _CN_STR(CIPHER_NOPS) "\n\tnop\n\t.endr\n");
#endif
    uint32_t s[4], t[4];
    for (int c = 0; c < 4; c++)
        s[c] = (((uint32_t)in[4*c]<<24)|((uint32_t)in[4*c+1]<<16)
              |((uint32_t)in[4*c+2]<<8)|in[4*c+3]) ^ w[c];
    int R = nr < 10 ? nr : 9;
    for (int r = 1; r <= R; r++) {
        for (int c = 0; c < 4; c++)
            t[c] = T0[(s[c]>>24)&0xff]
                 ^ ror32(T0[(s[(c+1)&3]>>16)&0xff], 8)
                 ^ ror32(T0[(s[(c+2)&3]>>8)&0xff], 16)
                 ^ ror32(T0[s[(c+3)&3]&0xff], 24) ^ w[4*r+c];
        for (int c = 0; c < 4; c++) s[c] = t[c];
    }
    if (nr >= 10) {
        /* Final round, staged through t[]. Writing s[c] in place here let
         * columns 1-3 read state that earlier iterations had already
         * overwritten, so this computed the WRONG cipher - FIPS-197 C.1 came
         * back 69c4e0d86a7b0476d8cd46b37037c2a8. The staging changes no lookup
         * address, so the access pattern under measurement is unchanged. */
        for (int c = 0; c < 4; c++)
            t[c] = ((uint32_t)SB[(s[c]>>24)&0xff]<<24) | ((uint32_t)SB[(s[(c+1)&3]>>16)&0xff]<<16)
                 | ((uint32_t)SB[(s[(c+2)&3]>>8)&0xff]<<8) | SB[s[(c+3)&3]&0xff];
        for (int c = 0; c < 4; c++) s[c] = t[c] ^ w[40+c];
    }
    for (int c = 0; c < 4; c++) {
        out[4*c]=s[c]>>24; out[4*c+1]=s[c]>>16; out[4*c+2]=s[c]>>8; out[4*c+3]=s[c];
    }
}
