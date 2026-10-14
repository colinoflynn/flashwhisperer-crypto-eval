#ifndef AES_H
#define AES_H
#include <stdint.h>
/* AES-128 encryption, tables in flash. Round keys computed once into RAM. */
void aes_key_schedule(const uint8_t key[16], uint32_t w[44]);
uint8_t aes_round1_bench(const uint8_t in[16], const uint8_t key[16], int reps);
void aes_init_ram_tables(void);   /* populate the RAM table copies at startup */
void aes_use_ram(int on);         /* 1 = timed paths use RAM tables, 0 = flash */
void aes_sbox_encrypt(const uint8_t in[16], uint8_t out[16], const uint32_t w[44]);
void aes_sbox_encrypt_nr(const uint8_t in[16], uint8_t out[16], const uint32_t w[44], int nr);
void aes_sbox_encrypt_alt(const uint8_t in[16], uint8_t out[16], const uint32_t w[44]);
void aes_ttable_encrypt(const uint8_t in[16], uint8_t out[16], const uint32_t w[44]);
void aes_ttable_encrypt_nr(const uint8_t in[16], uint8_t out[16], const uint32_t w[44], int nr);
void aes_ttable1_encrypt_nr(const uint8_t in[16], uint8_t out[16], const uint32_t w[44], int nr);
void aes_ttw_encrypt(const uint8_t in[16], uint8_t out[16], const uint32_t w[44], int alt);
#endif
