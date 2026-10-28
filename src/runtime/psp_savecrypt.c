/* PSP save data encryption and PARAM.SFO, for IMPORT / EXPORT of real PSP (or PPSSPP) saves.
 *
 * A PSP encrypts a game's save file when the game asks it to (SceUtilitySavedataParam's key). Street
 * Fighter Alpha 3 MAX does (SDK 2.5, secure version 0, its own key): crypt mode 3. The file is a
 * 16-byte IV followed by the data, AES-encrypted through the KIRK engine; PARAM.SFO lists the file
 * with a hash and carries hashes of itself in SAVEDATA_PARAMS (first byte 0x21 for mode 3).
 *
 * The algorithms are PPSSPP's (Core/HLE/sceChnnlsv.cpp, Core/Dialog/SavedataParam.cpp,
 * Core/ELF/ParamSFO.cpp; GPL 2.0 or later), ported to C; KIRK itself is libkirk (third_party/libkirk,
 * GPL 3.0 or later). Commands needing a console's own keys (the "fuse" commands, used by the first
 * PARAM.SFO hash only) are not available, as in PPSSPP, whose saves PSPs accept.
 */
#include "kirk_engine.h"
#include "psp_savecrypt.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct { int32_t mode; uint8_t result[16], key[16]; int32_t keyLength; } Mac;
typedef struct { int32_t mode, unkn; uint8_t cryptedData[0x92]; } Cipher;

static KirkState kirk;
static int kirk_ready;
static uint8_t dataBuf[2048 + 20];
static uint8_t *const dataBuf2 = dataBuf + 20;

static const uint8_t hash198C[16] = {0xFA, 0xAA, 0x50, 0xEC, 0x2F, 0xDE, 0x54, 0x93, 0xAD, 0x14, 0xB2, 0xCE, 0xA5, 0x30, 0x05, 0xDF};
static const uint8_t hash19BC[16] = {0xCB, 0x15, 0xF4, 0x07, 0xF9, 0x6A, 0x52, 0x3C, 0x04, 0xB9, 0xB2, 0xEE, 0x5C, 0x53, 0xFA, 0x86};
static const uint8_t key19CC[16]  = {0x70, 0x44, 0xA3, 0xAE, 0xEF, 0x5D, 0xA5, 0xF2, 0x85, 0x7F, 0xF2, 0xD6, 0x94, 0xF5, 0x36, 0x3B};
static const uint8_t key19DC[16]  = {0xEC, 0x6D, 0x29, 0x59, 0x26, 0x35, 0xA5, 0x7F, 0x97, 0x2A, 0x0D, 0xBC, 0xA3, 0x26, 0x33, 0x00};
static const uint8_t key199C[16]  = {0x36, 0xA5, 0x3E, 0xAC, 0xC5, 0x26, 0x9E, 0xA3, 0x83, 0xD9, 0xEC, 0x25, 0x6C, 0x48, 0x48, 0x72};
static const uint8_t key19AC[16]  = {0xD8, 0xC0, 0xB0, 0xF3, 0x3E, 0x6B, 0x76, 0x85, 0xFD, 0xFB, 0x4D, 0x7D, 0x45, 0x1E, 0x92, 0x03};

static void init(void) { if (!kirk_ready) { kirk_init(&kirk); kirk_ready = 1; } }
static void memxor(void *dest, const void *src, size_t n) { uint8_t *d = dest; const uint8_t *s = src; while (n--) *d++ ^= *s++; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static int numFromMode(int mode) {
    switch (mode) { case 1: return 3; case 2: return 5; case 3: return 12; case 4: return 13; case 6: return 17; default: return 16; }
}
static int numFromMode2(int mode) { return mode == 1 ? 4 : mode == 3 ? 14 : 18; }
static int typeFromMode(int mode) { return (mode == 1 || mode == 2) ? 83 : (mode == 3 || mode == 4) ? 87 : 100; }

static int kirkSendCmd(uint8_t *data, int length, int num, int encrypt) {
    put32(data, encrypt ? KIRK_MODE_ENCRYPT_CBC : KIRK_MODE_DECRYPT_CBC); put32(data + 4, 0); put32(data + 8, 0);
    put32(data + 12, (uint32_t)num); put32(data + 16, (uint32_t)length);
    if (kirk_sceUtilsBufferCopyWithRange(&kirk, data, length + 20, data, length + 20, encrypt ? KIRK_CMD_ENCRYPT_IV_0 : KIRK_CMD_DECRYPT_IV_0)) return -257;
    return 0;
}
static int kirkSendFuseCmd(uint8_t *data, int length, int encrypt) {
    put32(data, encrypt ? KIRK_MODE_ENCRYPT_CBC : KIRK_MODE_DECRYPT_CBC); put32(data + 4, 0); put32(data + 8, 0);
    put32(data + 12, 256); put32(data + 16, (uint32_t)length);
    if (kirk_sceUtilsBufferCopyWithRange(&kirk, data, length + 20, data, length + 20, encrypt ? KIRK_CMD_ENCRYPT_IV_FUSE : KIRK_CMD_DECRYPT_IV_FUSE)) return -258;
    return 0;
}
static int sub_15B0(uint8_t *data, int alignedLen, uint8_t *buf, int val) {
    uint8_t sp0[16]; memcpy(sp0, data + alignedLen + 4, 16);
    int res = kirkSendCmd(data, alignedLen, val, 0);
    if (res) return res;
    memxor(data, buf, 16); memcpy(buf, sp0, 16);
    return 0;
}
static int sub_0000(uint8_t *data_out, uint8_t *data, int alignedLen, const uint8_t *data2, int *data3, int mode) {
    memcpy(data_out + 20, data2, 16);
    int type = typeFromMode(mode), res;
    if (type == 87) memxor(data_out + 20, key19AC, 16); else if (type == 100) memxor(data_out + 20, key19DC, 16);
    if (mode == 2 || mode == 4 || mode == 6) res = kirkSendFuseCmd(data_out, 16, 0);
    else res = kirkSendCmd(data_out, 16, numFromMode2(mode), 0);
    if (type == 87) memxor(data_out, key199C, 16); else if (type == 100) memxor(data_out, key19CC, 16);
    if (res) return res;
    uint8_t sp0[16], sp16[16];
    memcpy(sp16, data_out, 16);
    if (*data3 == 1) memset(sp0, 0, 16);
    else { memcpy(sp0, sp16, 12); put32(sp0 + 12, (uint32_t)(*data3 - 1)); }
    for (int i = 20; i < alignedLen + 20; i += 16) { memcpy(data_out + i, sp16, 12); put32(data_out + 12 + i, (uint32_t)*data3); (*data3)++; }
    res = sub_15B0(data_out, alignedLen, sp0, type);
    if (res) return res;
    if (alignedLen > 0) memxor(data, data_out, (size_t)alignedLen);
    return 0;
}
static int sub_1510(uint8_t *data, int size, uint8_t *result, int num) {
    memxor(data + 20, result, 16);
    int res = kirkSendCmd(data, size, num, 1);
    if (res) return res;
    memcpy(result, data + size + 4, 16);
    return 0;
}

static int mac_init(Mac *ctx, int mode) { ctx->mode = mode; memset(ctx->result, 0, 16); memset(ctx->key, 0, 16); ctx->keyLength = 0; return 0; }
static int mac_update(Mac *ctx, const uint8_t *data, int length) {
    if (ctx->keyLength >= 17) return -1026;
    if (ctx->keyLength + length < 17) { memcpy(ctx->key + ctx->keyLength, data, (size_t)length); ctx->keyLength += length; return 0; }
    int num = numFromMode(ctx->mode);
    memset(dataBuf2, 0, 2048);
    memcpy(dataBuf2, ctx->key, (size_t)ctx->keyLength);
    int len = (ctx->keyLength + length) & 0xF; if (len == 0) len = 16;
    int newSize = ctx->keyLength;
    ctx->keyLength = len;
    int diff = length - len;
    memcpy(ctx->key, data + diff, (size_t)len);
    for (int i = 0; i < diff; i++) {
        if (newSize == 2048) { int res = sub_1510(dataBuf, 2048, ctx->result, num); if (res) return res; newSize = 0; }
        dataBuf2[newSize++] = data[i];
    }
    if (newSize) sub_1510(dataBuf, newSize, ctx->result, num);
    return 0;
}
static void shift1(uint8_t *d) {
    int t = (d[0] & 0x80) ? 135 : 0;
    for (int i = 0; i < 15; i++) d[i] = (uint8_t)(d[i] << 1 | d[i + 1] >> 7);
    d[15] = (uint8_t)((d[15] << 1) ^ t);
}
static int mac_final(Mac *ctx, uint8_t *hash, const uint8_t *key) {
    if (ctx->keyLength >= 17) return -1026;
    int num = numFromMode(ctx->mode);
    memset(dataBuf2, 0, 16);
    int res = kirkSendCmd(dataBuf, 16, num, 1);
    if (res) return res;
    uint8_t data1[16], data2[16];
    memcpy(data1, dataBuf2, 16);
    shift1(data1);
    if (ctx->keyLength < 16) {
        shift1(data1);
        ctx->key[ctx->keyLength] = 0x80;
        if (ctx->keyLength + 1 < 16) memset(ctx->key + ctx->keyLength + 1, 0, (size_t)(16 - ctx->keyLength - 1));
    }
    memxor(ctx->key, data1, 16);
    memcpy(dataBuf2, ctx->key, 16);
    memcpy(data2, ctx->result, 16);
    res = sub_1510(dataBuf, 16, data2, num);
    if (res) return res;
    if (ctx->mode == 3 || ctx->mode == 4) memxor(data2, hash198C, 16);
    else if (ctx->mode == 5 || ctx->mode == 6) memxor(data2, hash19BC, 16);
    if (ctx->mode == 2 || ctx->mode == 4 || ctx->mode == 6) {
        memcpy(dataBuf2, data2, 16);
        res = kirkSendFuseCmd(dataBuf, 16, 1); if (res) return res;
        res = kirkSendCmd(dataBuf, 16, num, 1); if (res) return res;
        memcpy(data2, dataBuf2, 16);
    }
    if (key) {
        for (int i = 0; i < 16; i++) data2[i] ^= key[i];
        memcpy(dataBuf2, data2, 16);
        res = kirkSendCmd(dataBuf, 16, num, 1); if (res) return res;
        memcpy(data2, dataBuf2, 16);
    }
    memcpy(hash, data2, 16);
    mac_init(ctx, 0);
    return 0;
}
static int cipher_init(Cipher *c, int mode, int how, uint8_t *data, const uint8_t *key) {
    c->mode = mode; c->unkn = 1;
    if (how == 2) { memcpy(c->cryptedData, data, 16); if (key) memxor(c->cryptedData, key, 16); return 0; }
    uint8_t hdr[37]; uint8_t *kd = hdr + 20;
    if (kirk_sceUtilsBufferCopyWithRange(&kirk, hdr, 20, 0, 0, 14) != 0) return -261;     /* random IV */
    memcpy(hdr + 20, hdr, 16); memset(hdr + 32, 0, 4);
    int type = typeFromMode(mode), res;
    if (type == 87) memxor(kd, key199C, 16); else if (type == 100) memxor(kd, key19CC, 16);
    if (mode == 2 || mode == 4 || mode == 6) res = kirkSendFuseCmd(hdr, 16, 1);
    else res = kirkSendCmd(hdr, 16, numFromMode2(mode), 1);
    if (type == 87) memxor(kd, key19AC, 16); else if (type == 100) memxor(kd, key19DC, 16);
    if (res) return res;
    memcpy(c->cryptedData, kd, 16); memcpy(data, kd, 16);
    if (key) memxor(c->cryptedData, key, 16);
    return 0;
}
static int cipher_update(Cipher *c, uint8_t *data, int alignedLen) {
    if (alignedLen == 0) return 0;
    if (alignedLen & 0xF) return -1025;
    static uint8_t kd[20 + 2048];
    int i = 0;
    for (; alignedLen >= 2048; i += 2048, alignedLen -= 2048) {
        int res = sub_0000(kd, data + i, 2048, c->cryptedData, &c->unkn, c->mode);
        if (res) return res;
    }
    if (!alignedLen) return 0;
    return sub_0000(kd, data + i, alignedLen, c->cryptedData, &c->unkn, c->mode);
}

/* ---------------------------------------------------------------- data files */
/* encrypt DATA (LEN bytes) with MODE / KEY: returns a new buffer (IV + data, *out_len bytes) and the
   file's hash for PARAM.SFO, or NULL */
uint8_t *psp_encrypt(int mode, const uint8_t *key, const uint8_t *data, int len, int *out_len, uint8_t hash[16]) {
    init();
    int aligned = (len + 15) & ~15;
    uint8_t *buf = calloc(1, (size_t)aligned + 0x10);
    if (!buf) return NULL;
    memcpy(buf + 0x10, data, (size_t)len);
    Mac m; Cipher c; memset(&m, 0, sizeof m); memset(&c, 0, sizeof c);
    memset(hash, 0, 16);
    int hasKey = mode > 1;
    if (cipher_init(&c, mode, 1, buf, hasKey ? key : NULL) < 0 || mac_init(&m, mode) < 0 || mac_update(&m, buf, 0x10) < 0 ||
        cipher_update(&c, buf + 0x10, aligned) < 0) { free(buf); return NULL; }
    memset(buf + 0x10 + len, 0, (size_t)(aligned - len));
    if (mac_update(&m, buf + 0x10, aligned) < 0 || mac_final(&m, hash, hasKey ? key : NULL) < 0) { free(buf); return NULL; }
    *out_len = len + 0x10;
    return buf;
}
/* decrypt FILE (LEN bytes: IV + data) in place: returns the data's length, or < 0. EXPECT: its hash
   from PARAM.SFO, or NULL to skip the check */
int psp_decrypt(int mode, const uint8_t *key, uint8_t *file, int len, const uint8_t *expect) {
    init();
    int aligned = (len + 15) & ~15;
    if (aligned <= 0x10) return -1;
    int dlen = len - 0x10; aligned -= 0x10;
    uint8_t *buf = calloc(1, (size_t)aligned + 0x10);
    if (!buf) return -1;
    memcpy(buf, file, (size_t)len);
    Mac m; Cipher c; memset(&m, 0, sizeof m); memset(&c, 0, sizeof c);
    int hasKey = mode > 1, ok = 0;
    if (mac_init(&m, mode) >= 0 && cipher_init(&c, mode, 2, buf, hasKey ? key : NULL) >= 0 && mac_update(&m, buf, 0x10) >= 0 &&
        mac_update(&m, buf + 0x10, aligned) >= 0 && cipher_update(&c, buf + 0x10, aligned) >= 0) {
        ok = 1;
        if (expect) { uint8_t h[16]; if (mac_final(&m, h, hasKey ? key : NULL) < 0 || memcmp(h, expect, 16)) ok = -8; }
    }
    if (ok == 1) memcpy(file, buf + 0x10, (size_t)dlen);
    free(buf);
    return ok == 1 ? dlen : ok == -8 ? -8 : -2;
}

/* ---------------------------------------------------------------- PARAM.SFO */

static int build_hash(uint8_t *out, const uint8_t *data, int aligned, int mode) {
    Mac m; memset(&m, 0, sizeof m); memset(out, 0, 16);
    if (mac_init(&m, mode & 0xFF) < 0) return -1;
    if (mac_update(&m, data, aligned) < 0) return -2;
    if (mac_final(&m, out, NULL) < 0) { memset(out, 1, 16); return 0; }   /* fuse command missing, as PPSSPP */
    return 0;
}

/* a save's PARAM.SFO (VALUES sorted by key) with SAVEDATA_PARAMS hashed for MODE (0: none): returns a
   new buffer of *size bytes */
uint8_t *psp_sfo_write(const SfoValue *values, int n, int mode, uint32_t *size) {
    init();
    uint32_t keys = 0, data = 0;
    for (int i = 0; i < n; i++) { keys += (uint32_t)strlen(values[i].key) + 1; data += values[i].max; }
    while (keys % 4) keys++;
    uint32_t key_start = 20 + 16 * (uint32_t)n, data_start = key_start + keys, total = data_start + data;
    uint32_t aligned = (total + 15) & ~15u;
    uint8_t *b = calloc(1, aligned);
    if (!b) return NULL;
    put32(b, 0x46535000); put32(b + 4, 0x101); put32(b + 8, key_start); put32(b + 12, data_start); put32(b + 16, (uint32_t)n);
    uint32_t ko = 0, dof = 0, params_at = 0;
    for (int i = 0; i < n; i++) {
        const SfoValue *v = &values[i];
        uint8_t *ix = b + 20 + 16 * i;
        ix[0] = (uint8_t)ko; ix[1] = (uint8_t)(ko >> 8);
        uint16_t fmt = v->is_int ? 0x0404 : v->bytes ? 0x0004 : 0x0204;
        ix[2] = (uint8_t)fmt; ix[3] = (uint8_t)(fmt >> 8);
        uint32_t len;
        if (v->is_int) { len = 4; put32(b + data_start + dof, v->ival); }
        else if (v->bytes) { len = v->len < v->max ? v->len : v->max; memcpy(b + data_start + dof, v->bytes, len); }
        else { uint32_t l = (uint32_t)strlen(v->str); if (l > v->max - 1) l = v->max - 1; memcpy(b + data_start + dof, v->str, l); len = l + 1; }
        put32(ix + 4, len); put32(ix + 8, v->max); put32(ix + 12, dof);
        if (!strcmp(v->key, "SAVEDATA_PARAMS")) params_at = data_start + dof;
        memcpy(b + key_start + ko, v->key, strlen(v->key));
        ko += (uint32_t)strlen(v->key) + 1; dof += v->max;
    }
    if (mode && params_at) {                                    /* PPSSPP's UpdateHash */
        uint8_t h[16];
        int first = mode & 2 ? 4 : 2, second = mode & 2 ? 3 : 0;
        if (mode & 4) { first = 6; second = 5; }
        memset(b + params_at, 0, 128);
        build_hash(h, b, (int)aligned, first); memcpy(b + params_at + 0x20, h, 16); b[params_at] |= 1;
        if (mode & 6) { b[params_at] |= (uint8_t)((mode & 6) << 4); build_hash(h, b, (int)aligned, second); memcpy(b + params_at + 0x70, h, 16); }
        build_hash(h, b, (int)aligned, 1); memcpy(b + params_at + 0x10, h, 16);
    }
    *size = total;
    return b;
}

/* find KEY's data in a PARAM.SFO: pointer and length, or NULL */
const uint8_t *psp_sfo_find(const uint8_t *sfo, uint32_t size, const char *key, uint32_t *len) {
    if (size < 20 || sfo[0] != 0 || sfo[1] != 'P' || sfo[2] != 'S' || sfo[3] != 'F') return NULL;
    uint32_t ks = sfo[8] | sfo[9] << 8 | (uint32_t)sfo[10] << 16 | (uint32_t)sfo[11] << 24;
    uint32_t ds = sfo[12] | sfo[13] << 8 | (uint32_t)sfo[14] << 16 | (uint32_t)sfo[15] << 24;
    uint32_t n = sfo[16] | sfo[17] << 8;
    for (uint32_t i = 0; i < n && 20 + 16 * i + 16 <= size; i++) {
        const uint8_t *ix = sfo + 20 + 16 * i;
        uint32_t ko = ix[0] | ix[1] << 8, l = ix[4] | ix[5] << 8 | (uint32_t)ix[6] << 16, o = ix[12] | ix[13] << 8 | (uint32_t)ix[14] << 16;
        if (ks + ko >= size || ds + o + l > size) continue;
        if (!strncmp((const char *)sfo + ks + ko, key, size - ks - ko) ) { if (len) *len = l; return sfo + ds + o; }
    }
    return NULL;
}
