/* Tiny PNG writer (uncompressed deflate blocks) for screenshots and frame dumps. */
#include "ge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n) {
    if (!crc_table[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t x = i; for (int k = 0; k < 8; k++) x = x & 1 ? 0xEDB88320u ^ (x >> 1) : x >> 1; crc_table[i] = x; }
    c = ~c; while (n--) c = crc_table[(c ^ *p++) & 255] ^ (c >> 8); return ~c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n) {
    uint8_t h[8]; be32(h, n); memcpy(h + 4, type, 4); fwrite(h, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    uint32_t c = crc(0, (const uint8_t *)type, 4); c = crc(c, data, n);
    uint8_t t[4]; be32(t, c); fwrite(t, 1, 4, f);
}
int png_write(const char *path, const uint8_t *rgba, int w, int h) {
    FILE *f = fopen(path, "wb"); if (!f) return 0;
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13]; be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h); ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    size_t row = (size_t)w * 4 + 1, raw_n = row * (size_t)h;
    uint8_t *raw = (uint8_t *)malloc(raw_n);
    for (int y = 0; y < h; y++) { raw[y * row] = 0; memcpy(raw + y * row + 1, rgba + (size_t)y * w * 4, (size_t)w * 4); }
    size_t blocks = (raw_n + 65534) / 65535, zn = 2 + raw_n + blocks * 5 + 4;
    uint8_t *z = (uint8_t *)malloc(zn), *q = z;
    *q++ = 0x78; *q++ = 0x01;
    uint32_t s1 = 1, s2 = 0;
    for (size_t off = 0; off < raw_n; off += 65535) {
        size_t n = raw_n - off < 65535 ? raw_n - off : 65535;
        *q++ = off + n >= raw_n ? 1 : 0;
        *q++ = (uint8_t)n; *q++ = (uint8_t)(n >> 8); *q++ = (uint8_t)~n; *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + off, n); q += n;
        for (size_t i = 0; i < n; i++) { s1 = (s1 + raw[off + i]) % 65521; s2 = (s2 + s1) % 65521; }
    }
    be32(q, (s2 << 16) | s1); q += 4;
    chunk(f, "IDAT", z, (uint32_t)(q - z));
    chunk(f, "IEND", NULL, 0);
    free(raw); free(z); fclose(f);
    return 1;
}
