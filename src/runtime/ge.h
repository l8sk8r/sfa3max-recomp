/* GE (PSP GPU) interfaces shared by the list walker, the renderer and the display. */
#pragma once
#include <stdint.h>
#include <stddef.h>
void     ge_soft_cmd(uint32_t pc, uint32_t op, uint32_t arg);   /* every command goes through here */
uint32_t ge_soft_reladdr(uint32_t arg);                          /* BASE/offset-relative address */
void     ge_fb_to_rgba(uint32_t addr, int stride, int fmt, int w, int h, uint8_t *out);
int      png_write(const char *path, const uint8_t *rgba, int w, int h);
extern uint64_t ge_prims_drawn, ge_pixels_drawn;

/* ---- shared GE state for the GPU renderer (ge_soft.c owns the registers and vertex decoding) ---- */
typedef struct { float x, y, u, v; uint8_t r, g, b, a; } GeVtx;
extern int ge_use_gl;                               /* 1: primitives go to ge_gl_draw instead of the software rasteriser */
extern int ge_no_draw;                              /* 1: walk display lists but draw nothing (--renderer none / lazy) */
const uint32_t *ge_regs(void);                      /* last value of every GE register */
uint32_t ge_tex_addr(void);
int      ge_tex_w(void), ge_tex_h(void), ge_tex_stride(void);
uint32_t ge_tex_bytes(void);                        /* bytes of guest memory the current texture covers */
void     ge_decode_texture(uint8_t *rgba);          /* whole current texture (tex_w x tex_h) as RGBA8 */
uint64_t ge_clut_hash(void);
uint32_t ge_fb_base(void);
int      ge_fb_stride(void), ge_fb_fmt(void);
uint64_t ge_hash(const void *p, size_t n);

/* ---- GPU renderer (ge_gl.c) ---- */
int  ge_gl_init(void *(*getproc)(const char *), int scale);   /* 0 if OpenGL 3.3 is not available */
void ge_gl_draw(int type, const GeVtx *v, int n, int flat, int sx1, int sy1, int sx2, int sy2);
void ge_gl_list_start(void);                        /* a display list (or a continuation) starts: guest memory may have changed */
int  ge_gl_present(uint32_t fb_addr, int win_w, int win_h);   /* draw the displayed frame to the window; 0 if not GPU-rendered */
void ge_gl_present_rgba(const uint8_t *rgba, int w, int h, int win_w, int win_h);  /* software frames, sharp-bilinear */
int  ge_gl_read(uint32_t fb_addr, uint8_t *rgba, int *w, int *h);  /* full-resolution readback of a frame (480*S x 272*S) */
int  ge_gl_scale(void);
void ge_gl_frame_end(void);
