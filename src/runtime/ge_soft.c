/* Software renderer for the PSP GE, covering what this game uses:
 * through-mode (2D) triangles, strips, fans and sprites; CLUT4/8/16/32 and 16/32-bit textures
 * (linear or swizzled); texture functions; alpha test; blending; colour masks; scissor; clear mode;
 * and block transfers. Pixels go straight into guest VRAM, exactly where the PSP would put them,
 * so the game can read back or copy its own frame buffers.
 *
 * It is the reference renderer: an upscaling GPU renderer comes later and is checked against it.
 */
#include "rt.h"
#include "ge.h"
#include <string.h>

static uint32_t R[256];          /* last value of every GE register (24-bit argument) */
static uint32_t clut[1024];      /* loaded palette, as raw 32-bit words (16-bit entries packed two per word) */
static uint32_t ge_offset_addr;
uint64_t ge_prims_drawn, ge_pixels_drawn;

/* ---------------------------------------------------------------- colour helpers (ABGR8888 = R in low byte) */
typedef struct { int r, g, b, a; } Col;

static Col unpack(uint32_t v, int fmt) {
    Col c;
    switch (fmt) {
    case 0: c.r = (v & 31) << 3 | (v & 31) >> 2; c.g = ((v >> 5) & 63) << 2 | ((v >> 5) & 63) >> 4;
            c.b = ((v >> 11) & 31) << 3 | ((v >> 11) & 31) >> 2; c.a = 255; break;               /* 565 */
    case 1: c.r = (v & 31) << 3 | (v & 31) >> 2; c.g = ((v >> 5) & 31) << 3 | ((v >> 5) & 31) >> 2;
            c.b = ((v >> 10) & 31) << 3 | ((v >> 10) & 31) >> 2; c.a = (v >> 15) & 1 ? 255 : 0; break;  /* 5551 */
    case 2: c.r = (v & 15) * 17; c.g = ((v >> 4) & 15) * 17; c.b = ((v >> 8) & 15) * 17; c.a = ((v >> 12) & 15) * 17; break;  /* 4444 */
    default: c.r = v & 255; c.g = (v >> 8) & 255; c.b = (v >> 16) & 255; c.a = v >> 24; break;   /* 8888 */
    }
    return c;
}
static uint32_t pack(Col c, int fmt) {
    switch (fmt) {
    case 0: return (uint32_t)((c.r >> 3) | (c.g >> 2) << 5 | (c.b >> 3) << 11);
    case 1: return (uint32_t)((c.r >> 3) | (c.g >> 3) << 5 | (c.b >> 3) << 10 | (c.a >= 128) << 15);
    case 2: return (uint32_t)((c.r >> 4) | (c.g >> 4) << 4 | (c.b >> 4) << 8 | (c.a >> 4) << 12);
    default: return (uint32_t)(c.r | c.g << 8 | c.b << 16 | (uint32_t)c.a << 24);
    }
}
static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* ---------------------------------------------------------------- addresses */
static uint32_t rel_addr(uint32_t arg) { return ((((R[0x10] & 0x0F0000) << 8) | (arg & 0xFFFFFF)) + ge_offset_addr) & 0x0FFFFFFF; }
static uint32_t fb_base(void) { return 0x04000000u | (R[0x9C] & 0x1FFFF0); }
static int fb_stride(void) { return (int)(R[0x9D] & 0x7FC); }
static int fb_fmt(void) { return (int)(R[0xD2] & 3); }

/* ---------------------------------------------------------------- textures */
static uint32_t tex_addr(void) { return ((R[0xA0] & 0xFFFFF0) | ((R[0xA8] << 8) & 0x0F000000)); }
static int tex_stride(void) { return (int)(R[0xA8] & 0x7FF); }
static int tex_w(void) { return 1 << (R[0xB8] & 0xF); }
static int tex_h(void) { return 1 << ((R[0xB8] >> 8) & 0xF); }

static uint32_t clut_entry(uint32_t index) {
    uint32_t cf = R[0xC5];
    int shift = (cf >> 2) & 31, mask = (cf >> 8) & 255, start = ((cf >> 16) & 31) << 4;
    int cfmt = cf & 3;
    uint32_t i = (((index >> shift) & (uint32_t)mask) | (uint32_t)start);
    if (cfmt == 3) return clut[i & 255];
    i &= 511;
    uint32_t w = clut[i >> 1];
    return (i & 1) ? w >> 16 : w & 0xFFFF;
}

static Col texel(int u, int v) {
    int w = tex_w(), h = tex_h();
    if (R[0xC7] & 1) u = u < 0 ? 0 : u >= w ? w - 1 : u; else u &= w - 1;
    if (R[0xC7] & 0x100) v = v < 0 ? 0 : v >= h ? h - 1 : v; else v &= h - 1;
    int tfmt = (int)(R[0xC3] & 0xF);
    int bpp4 = tfmt == 4 ? 1 : 0;                      /* 4-bit texels */
    int bytes = (tfmt == 3 || tfmt == 7) ? 4 : (tfmt <= 2 || tfmt == 6) ? 2 : 1;
    uint32_t base = tex_addr();
    int stride = tex_stride();
    uint32_t off;
    int xb2 = bpp4 ? u : u * bytes * 2;                 /* x in half-bytes */
    if (R[0xC2] & 1) {                                  /* swizzled: 16-byte x 8-row blocks */
        int rowb = (bpp4 ? stride / 2 : stride * bytes);
        int xb = xb2 / 2;
        off = (uint32_t)(((v >> 3) * (rowb >> 4) + (xb >> 4)) * 128 + (v & 7) * 16 + (xb & 15));
    } else {
        off = (uint32_t)(v * (bpp4 ? stride / 2 : stride * bytes) + xb2 / 2);
    }
    uint32_t a = base + off, raw;
    int cfmt = (int)(R[0xC5] & 3);
    switch (tfmt) {
    case 0: case 1: case 2: return unpack(RH(a), tfmt);
    case 3: return unpack(RW(a), 3);
    case 4: raw = (RB(a) >> ((u & 1) * 4)) & 15; return unpack(clut_entry(raw), cfmt);
    case 5: return unpack(clut_entry(RB(a)), cfmt);
    case 6: return unpack(clut_entry(RH(a)), cfmt);
    case 7: return unpack(clut_entry(RW(a)), cfmt);
    default: { Col m = { 255, 0, 255, 255 }; return m; }      /* DXT: not used by this game */
    }
}

/* ---------------------------------------------------------------- per-pixel pipeline */
static int alpha_test(int a) {
    if (!(R[0x22] & 1)) return 1;
    uint32_t t = R[0xDB]; int ref = (int)((t >> 8) & 255), mask = (int)((t >> 16) & 255);
    int x = a & mask; ref &= mask;
    switch (t & 7) {
    case 0: return 0; case 1: return 1; case 2: return x == ref; case 3: return x != ref;
    case 4: return x < ref; case 5: return x <= ref; case 6: return x > ref; default: return x >= ref;
    }
}

static Col tex_func(Col tex, Col col) {
    uint32_t tf = R[0xC9];
    int rgba = (tf >> 8) & 1, dbl = (tf >> 16) & 1;
    Col o;
    switch (tf & 7) {
    case 0: o.r = tex.r * col.r / 255; o.g = tex.g * col.g / 255; o.b = tex.b * col.b / 255;
            o.a = rgba ? tex.a * col.a / 255 : col.a; break;                                       /* modulate */
    case 1: o.r = (tex.r * tex.a + col.r * (255 - tex.a)) / 255; o.g = (tex.g * tex.a + col.g * (255 - tex.a)) / 255;
            o.b = (tex.b * tex.a + col.b * (255 - tex.a)) / 255; o.a = col.a; break;                /* decal */
    case 2: { Col e = unpack(R[0xCA] | 0xFF000000u, 3);                                            /* blend */
            o.r = (col.r * (255 - tex.r) + e.r * tex.r) / 255; o.g = (col.g * (255 - tex.g) + e.g * tex.g) / 255;
            o.b = (col.b * (255 - tex.b) + e.b * tex.b) / 255; o.a = rgba ? tex.a * col.a / 255 : col.a; break; }
    case 3: o.r = tex.r; o.g = tex.g; o.b = tex.b; o.a = rgba ? tex.a : col.a; break;              /* replace */
    default: o.r = tex.r + col.r; o.g = tex.g + col.g; o.b = tex.b + col.b;                       /* add */
            o.a = rgba ? tex.a * col.a / 255 : col.a; break;
    }
    if (dbl && (tf & 7) != 3) { o.r *= 2; o.g *= 2; o.b *= 2; }
    o.r = clamp255(o.r); o.g = clamp255(o.g); o.b = clamp255(o.b); o.a = clamp255(o.a);
    return o;
}

static Col blend(Col s, Col d) {
    uint32_t bm = R[0xDF];
    int sf = bm & 15, df = (bm >> 4) & 15, eq = (bm >> 8) & 15;
    Col fa = unpack(R[0xE0] | 0xFF000000u, 3), fb = unpack(R[0xE1] | 0xFF000000u, 3);
    Col A, B;
    switch (sf) {
    case 0: A = d; break;
    case 1: A.r = 255 - d.r; A.g = 255 - d.g; A.b = 255 - d.b; break;
    case 2: A.r = A.g = A.b = s.a; break;
    case 3: A.r = A.g = A.b = 255 - s.a; break;
    case 4: A.r = A.g = A.b = d.a; break;
    case 5: A.r = A.g = A.b = 255 - d.a; break;
    case 6: A.r = A.g = A.b = clamp255(2 * s.a); break;
    case 7: A.r = A.g = A.b = clamp255(255 - 2 * s.a); break;
    case 8: A.r = A.g = A.b = clamp255(2 * d.a); break;
    case 9: A.r = A.g = A.b = clamp255(255 - 2 * d.a); break;
    default: A = fa; break;
    }
    switch (df) {
    case 0: B = s; break;
    case 1: B.r = 255 - s.r; B.g = 255 - s.g; B.b = 255 - s.b; break;
    case 2: B.r = B.g = B.b = s.a; break;
    case 3: B.r = B.g = B.b = 255 - s.a; break;
    case 4: B.r = B.g = B.b = d.a; break;
    case 5: B.r = B.g = B.b = 255 - d.a; break;
    case 6: B.r = B.g = B.b = clamp255(2 * s.a); break;
    case 7: B.r = B.g = B.b = clamp255(255 - 2 * s.a); break;
    case 8: B.r = B.g = B.b = clamp255(2 * d.a); break;
    case 9: B.r = B.g = B.b = clamp255(255 - 2 * d.a); break;
    default: B = fb; break;
    }
    Col o;
#define CH(ch) do { int x = s.ch * A.ch / 255, y = d.ch * B.ch / 255; \
        switch (eq) { case 0: o.ch = x + y; break; case 1: o.ch = x - y; break; case 2: o.ch = y - x; break; \
        case 3: o.ch = s.ch < d.ch ? s.ch : d.ch; break; case 4: o.ch = s.ch > d.ch ? s.ch : d.ch; break; \
        default: o.ch = s.ch > d.ch ? s.ch - d.ch : d.ch - s.ch; break; } o.ch = clamp255(o.ch); } while (0)
    CH(r); CH(g); CH(b);
#undef CH
    o.a = s.a;
    return o;
}

static int sc_x1, sc_y1, sc_x2, sc_y2;   /* inclusive scissor */

static void put_pixel(int x, int y, Col c) {
    if (x < sc_x1 || x > sc_x2 || y < sc_y1 || y > sc_y2) return;
    int fmt = fb_fmt(), bpp = fmt == 3 ? 4 : 2;
    uint32_t a = fb_base() + (uint32_t)((y * fb_stride() + x) * bpp);
    uint32_t old = bpp == 4 ? RW(a) : RH(a);
    Col d = unpack(old, fmt);
    int clear = R[0xD3] & 1;
    if (clear) {
        uint32_t cm = R[0xD3];
        Col o = d;
        if (cm & 0x100) { o.r = c.r; o.g = c.g; o.b = c.b; }
        if (cm & 0x200) o.a = c.a;
        uint32_t v = pack(o, fmt);
        if (bpp == 4) WW(a, v); else WH(a, v);
        ge_pixels_drawn++;
        return;
    }
    if (!alpha_test(c.a)) return;
    Col o = (R[0x21] & 1) ? blend(c, d) : c;
    o.a = d.a;                                   /* frame buffer alpha holds stencil; leave it alone */
    uint32_t v = pack(o, fmt);
    uint32_t keep = R[0xE8] & 0xFFFFFF;          /* MASKRGB: set bits are not written */
    if (keep) {
        uint32_t m = pack(unpack(keep | 0xFF000000u, 3), fmt) & (fmt == 3 ? 0x00FFFFFFu : 0xFFFFu);
        v = (v & ~m) | (old & m);
    }
    if (bpp == 4) WW(a, v); else WH(a, v);
    ge_pixels_drawn++;
}

/* ---------------------------------------------------------------- vertices */
typedef struct { float x, y, u, v; Col c; } Vtx;

static int vtx_decode(uint32_t addr, uint32_t vt, Vtx *out, int n) {
    int tc = vt & 3, cc = (vt >> 2) & 7, nc = (vt >> 5) & 3, pc = (vt >> 7) & 3, wc = (vt >> 9) & 3;
    int nw = ((vt >> 14) & 7) + 1;
    static const int tsz[4] = { 0, 1, 2, 4 }, nsz[4] = { 0, 1, 2, 4 }, psz[4] = { 0, 1, 2, 4 }, wsz[4] = { 0, 1, 2, 4 };
    static const int csz[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
    int off = 0, maxal = 1, ot, oc, on, op;
    (void)on;
    if (wc) { off += wsz[wc] * nw; maxal = wsz[wc]; }
    if (tc) { off = (off + tsz[tc] - 1) & ~(tsz[tc] - 1); ot = off; off += tsz[tc] * 2; if (tsz[tc] > maxal) maxal = tsz[tc]; } else ot = -1;
    if (cc) { off = (off + csz[cc] - 1) & ~(csz[cc] - 1); oc = off; off += csz[cc]; if (csz[cc] > maxal) maxal = csz[cc]; } else oc = -1;
    if (nc) { off = (off + nsz[nc] - 1) & ~(nsz[nc] - 1); on = off; off += nsz[nc] * 3; if (nsz[nc] > maxal) maxal = nsz[nc]; }
    off = (off + psz[pc] - 1) & ~(psz[pc] - 1); op = off; off += psz[pc] * 3; if (psz[pc] > maxal) maxal = psz[pc];
    int size = (off + maxal - 1) & ~(maxal - 1);
    Col mat = unpack(R[0x55] | (R[0x58] & 255) << 24, 3);   /* material colour when the vertex has none */
    for (int i = 0; i < n; i++) {
        uint32_t a = addr + (uint32_t)(i * size);
        Vtx *v = &out[i];
        if (ot >= 0) {
            if (tc == 1) { v->u = (float)RB(a + ot); v->v = (float)RB(a + ot + 1); }
            else if (tc == 2) { v->u = (float)RH(a + ot); v->v = (float)RH(a + ot + 2); }
            else { float f; uint32_t w = RW(a + ot); memcpy(&f, &w, 4); v->u = f; w = RW(a + ot + 4); memcpy(&f, &w, 4); v->v = f; }
        } else v->u = v->v = 0;
        if (oc >= 0) v->c = cc == 7 ? unpack(RW(a + oc), 3) : unpack(RH(a + oc), cc - 4);
        else v->c = mat;
        if (pc == 2) { v->x = (float)(int16_t)RH(a + op); v->y = (float)(int16_t)RH(a + op + 2); }
        else if (pc == 1) { v->x = (float)(int8_t)RB(a + op); v->y = (float)(int8_t)RB(a + op + 1); }
        else { float f; uint32_t w = RW(a + op); memcpy(&f, &w, 4); v->x = f; w = RW(a + op + 4); memcpy(&f, &w, 4); v->y = f; }
    }
    return size;
}

/* ---------------------------------------------------------------- rasterising */
static Col shade(const Vtx *v, float u, float vv, Col c) {
    if (R[0x1E] & 1) {
        Col t = texel((int)floorf(u), (int)floorf(vv));
        return tex_func(t, c);
    }
    return c;
}

static void sprite(const Vtx *a, const Vtx *b) {
    int x1 = (int)a->x, y1 = (int)a->y, x2 = (int)b->x, y2 = (int)b->y;
    float u1 = a->u, v1 = a->v, u2 = b->u, v2 = b->v;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; float f = u1; u1 = u2; u2 = f; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; float f = v1; v1 = v2; v2 = f; }
    if (x2 == x1 || y2 == y1) return;
    float du = (u2 - u1) / (float)(x2 - x1), dv = (v2 - v1) / (float)(y2 - y1);
    for (int y = y1; y < y2; y++) {
        if (y < sc_y1 || y > sc_y2) continue;
        float vv = v1 + ((float)(y - y1) + 0.5f) * dv;
        for (int x = x1; x < x2; x++) {
            if (x < sc_x1 || x > sc_x2) continue;
            float u = u1 + ((float)(x - x1) + 0.5f) * du;
            put_pixel(x, y, shade(b, u, vv, b->c));
        }
    }
}

static inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static void triangle(const Vtx *a, const Vtx *b, const Vtx *c, int flat) {
    float area = edge(a->x, a->y, b->x, b->y, c->x, c->y);
    if (area == 0) return;
    if (area < 0) { const Vtx *t = b; b = c; c = t; area = -area; }
    int minx = (int)floorf(fminf(a->x, fminf(b->x, c->x))), maxx = (int)ceilf(fmaxf(a->x, fmaxf(b->x, c->x)));
    int miny = (int)floorf(fminf(a->y, fminf(b->y, c->y))), maxy = (int)ceilf(fmaxf(a->y, fmaxf(b->y, c->y)));
    if (minx < sc_x1) minx = sc_x1;
    if (miny < sc_y1) miny = sc_y1;
    if (maxx > sc_x2 + 1) maxx = sc_x2 + 1;
    if (maxy > sc_y2 + 1) maxy = sc_y2 + 1;
    for (int y = miny; y < maxy; y++) {
        float py = (float)y + 0.5f;
        for (int x = minx; x < maxx; x++) {
            float px = (float)x + 0.5f;
            float w0 = edge(b->x, b->y, c->x, c->y, px, py);
            float w1 = edge(c->x, c->y, a->x, a->y, px, py);
            float w2 = edge(a->x, a->y, b->x, b->y, px, py);
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            /* top-left rule: drop pixels exactly on a right or bottom edge */
            if ((w0 == 0 && !((c->y == b->y && c->x < b->x) || c->y < b->y)) ||
                (w1 == 0 && !((a->y == c->y && a->x < c->x) || a->y < c->y)) ||
                (w2 == 0 && !((b->y == a->y && b->x < a->x) || b->y < a->y))) continue;
            w0 /= area; w1 /= area; w2 /= area;
            Col col;
            if (flat) col = c->c;
            else {
                col.r = (int)(a->c.r * w0 + b->c.r * w1 + c->c.r * w2 + 0.5f);
                col.g = (int)(a->c.g * w0 + b->c.g * w1 + c->c.g * w2 + 0.5f);
                col.b = (int)(a->c.b * w0 + b->c.b * w1 + c->c.b * w2 + 0.5f);
                col.a = (int)(a->c.a * w0 + b->c.a * w1 + c->c.a * w2 + 0.5f);
            }
            float u = a->u * w0 + b->u * w1 + c->u * w2, v = a->v * w0 + b->v * w1 + c->v * w2;
            put_pixel(x, y, shade(c, u, v, col));
        }
    }
}

static void draw_prim(uint32_t arg) {
    int type = (int)((arg >> 16) & 7), n = (int)(arg & 0xFFFF);
    uint32_t vt = R[0x12];
    if (!n) return;
    if (!(vt & 0x800000)) {
        static int warned;
        if (!warned++) rt_log("GE: 3D (non-through) primitives not handled yet");
        return;
    }
    static Vtx v[65536];
    uint32_t addr = rel_addr(R[0x01]);
    int size;
    if (vt & 0x1800) {                                   /* indexed */
        Vtx tmp[1];
        size = vtx_decode(addr, vt, tmp, 0);
        uint32_t ia = rel_addr(R[0x02]);
        int isz = (vt >> 11) & 3;
        for (int i = 0; i < n; i++) {
            uint32_t idx = isz == 1 ? RB(ia + i) : isz == 2 ? RH(ia + 2 * i) : RW(ia + 4 * i);
            vtx_decode(addr + idx * (uint32_t)size, vt, &v[i], 1);
        }
        R[0x02] = (R[0x02] & 0xFF000000) | ((R[0x02] + (uint32_t)(n << (isz - 1))) & 0xFFFFFF);
    } else {
        size = vtx_decode(addr, vt, v, n);
        R[0x01] = (R[0x01] & 0xFF000000) | ((R[0x01] + (uint32_t)(n * size)) & 0xFFFFFF);
    }
    sc_x1 = (int)(R[0xD4] & 0x3FF); sc_y1 = (int)((R[0xD4] >> 10) & 0x3FF);
    sc_x2 = (int)(R[0xD5] & 0x3FF); sc_y2 = (int)((R[0xD5] >> 10) & 0x3FF);
    int flat = !(R[0x50] & 1);
    ge_prims_drawn++;
    if (ge_no_draw) return;
    if (ge_use_gl) {
        static GeVtx gv[65536];
        for (int i = 0; i < n; i++) {
            gv[i].x = v[i].x; gv[i].y = v[i].y; gv[i].u = v[i].u; gv[i].v = v[i].v;
            gv[i].r = (uint8_t)v[i].c.r; gv[i].g = (uint8_t)v[i].c.g; gv[i].b = (uint8_t)v[i].c.b; gv[i].a = (uint8_t)v[i].c.a;
        }
        ge_gl_draw(type, gv, n, flat, sc_x1, sc_y1, sc_x2, sc_y2);
        return;
    }
    switch (type) {
    case 3: for (int i = 0; i + 2 < n; i += 3) triangle(&v[i], &v[i + 1], &v[i + 2], flat); break;
    case 4: for (int i = 0; i + 2 < n; i++) triangle(&v[i], &v[i + 1], &v[i + 2], flat); break;
    case 5: for (int i = 1; i + 1 < n; i++) triangle(&v[0], &v[i], &v[i + 1], flat); break;
    case 6: for (int i = 0; i + 1 < n; i += 2) sprite(&v[i], &v[i + 1]); break;
    default: { static int w; if (!w++) rt_log("GE: primitive type %d not handled", type); } break;
    }
}

/* ---------------------------------------------------------------- state access for the GPU renderer */
int ge_use_gl;
int ge_no_draw;                                     /* background instances: walk lists, draw nothing */
const uint32_t *ge_regs(void) { return R; }
uint32_t ge_tex_addr(void) { return tex_addr(); }
int ge_tex_w(void) { return tex_w(); }
int ge_tex_h(void) { return tex_h(); }
int ge_tex_stride(void) { return tex_stride(); }
uint32_t ge_fb_base(void) { return fb_base(); }
int ge_fb_stride(void) { return fb_stride(); }
int ge_fb_fmt(void) { return fb_fmt(); }
uint32_t ge_tex_bytes(void) {
    int tfmt = (int)(R[0xC3] & 0xF), h = tex_h(), stride = tex_stride();
    uint32_t bits = tfmt == 4 ? 4 : tfmt == 5 ? 8 : (tfmt == 3 || tfmt == 7) ? 32 : 16;
    return (uint32_t)stride * (uint32_t)h * bits / 8;
}
void ge_decode_texture(uint8_t *rgba) {
    int w = tex_w(), h = tex_h();
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            Col c = texel(x, y);
            uint8_t *p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4;
            p[0] = (uint8_t)c.r; p[1] = (uint8_t)c.g; p[2] = (uint8_t)c.b; p[3] = (uint8_t)c.a;
        }
}
uint64_t ge_hash(const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data; uint64_t h = 0x9E3779B97F4A7C15ull ^ n;
    while (n >= 8) { uint64_t w; memcpy(&w, p, 8); h = (h ^ w) * 0xFF51AFD7ED558CCDull; h ^= h >> 32; p += 8; n -= 8; }
    while (n--) { h = (h ^ *p++) * 0x100000001B3ull; }
    return h;
}
uint64_t ge_clut_hash(void) {
    int tfmt = (int)(R[0xC3] & 0xF);
    if (tfmt < 4 || tfmt > 7) return 0;
    return ge_hash(clut, sizeof clut) ^ ((uint64_t)R[0xC5] << 32);
}

static void transfer(uint32_t arg) {
    uint32_t src = (R[0xB2] & 0xFFFFF0) | ((R[0xB3] << 8) & 0x0F000000);
    uint32_t dst = (R[0xB4] & 0xFFFFF0) | ((R[0xB5] << 8) & 0x0F000000);
    int sw = (int)(R[0xB3] & 0x7F8), dw = (int)(R[0xB5] & 0x7F8);
    int sx = (int)(R[0xEB] & 0x3FF), sy = (int)((R[0xEB] >> 10) & 0x3FF);
    int dx = (int)(R[0xEC] & 0x3FF), dy = (int)((R[0xEC] >> 10) & 0x3FF);
    int w = (int)(R[0xEE] & 0x3FF) + 1, h = (int)((R[0xEE] >> 10) & 0x3FF) + 1;
    int bpp = (arg & 1) ? 4 : 2;
    for (int y = 0; y < h; y++)
        memmove(HOST(dst + (uint32_t)(((dy + y) * dw + dx) * bpp)), HOST(src + (uint32_t)(((sy + y) * sw + sx) * bpp)), (size_t)(w * bpp));
}

/* ---------------------------------------------------------------- command entry */
void ge_soft_cmd(uint32_t pc, uint32_t op, uint32_t arg) {
    R[op] = arg;
    switch (op) {
    case 0x04: draw_prim(arg); break;
    case 0x13: ge_offset_addr = arg << 8; break;
    case 0x14: ge_offset_addr = pc & 0x0FFFFFFF; break;
    case 0xC4: {                                                          /* LOADCLUT */
        uint32_t a = (R[0xB0] & 0xFFFFF0) | ((R[0xB1] << 8) & 0x0F000000);
        int words = (int)(arg & 0x3F) * 8;
        for (int i = 0; i < words && i < 1024; i++) clut[i] = RW(a + 4u * (uint32_t)i);
        break;
    }
    case 0xEA: transfer(arg); break;
    default: break;
    }
}
uint32_t ge_soft_reladdr(uint32_t arg) { return rel_addr(arg); }
void ge_soft_set_base(uint32_t arg) { R[0x10] = arg; }

/* ---------------------------------------------------------------- frame buffer -> RGBA8 for display or screenshots */
void ge_fb_to_rgba(uint32_t addr, int stride, int fmt, int w, int h, uint8_t *out) {
    int bpp = fmt == 3 ? 4 : 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t a = addr + (uint32_t)((y * stride + x) * bpp);
            Col c = unpack(bpp == 4 ? RW(a) : RH(a), fmt);
            uint8_t *p = out + (y * w + x) * 4;
            p[0] = (uint8_t)c.r; p[1] = (uint8_t)c.g; p[2] = (uint8_t)c.b; p[3] = 255;
        }
}
