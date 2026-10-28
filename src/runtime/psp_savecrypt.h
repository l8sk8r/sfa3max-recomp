/* PSP save data encryption and PARAM.SFO (psp_savecrypt.c) */
#pragma once
#include <stdint.h>
typedef struct { const char *key; int is_int; uint32_t ival; const char *str; const uint8_t *bytes; uint32_t len, max; } SfoValue;
uint8_t *psp_encrypt(int mode, const uint8_t *key, const uint8_t *data, int len, int *out_len, uint8_t hash[16]);
int psp_decrypt(int mode, const uint8_t *key, uint8_t *file, int len, const uint8_t *expect);
uint8_t *psp_sfo_write(const SfoValue *values, int n, int mode, uint32_t *size);
const uint8_t *psp_sfo_find(const uint8_t *sfo, uint32_t size, const char *key, uint32_t *len);
