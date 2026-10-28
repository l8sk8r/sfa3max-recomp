/* GPU renderer for the PSP GE (OpenGL 3.3 core), drawing at S x the PSP's resolution.
 *
 * Every frame-buffer address the game draws to gets an off-screen render target (FBO) of
 * 512*S x 272*S. When the game later uses one of those addresses as a texture (it composes each frame
 * at 0x04088000 and then stretches that onto the visible buffer), the upscaled FBO is sampled directly,
 * so detail survives the whole chain. Textures from guest memory are decoded once to RGBA8 and cached
 * by a hash of their bytes and palette.
 *
 * Coordinates: guest pixels with y down. FBO row r holds guest row r (GL's bottom-up origin is used as
 * guest row 0), so readback and texture sampling need no flips; only the window presentation flips.
 */
#include "rt.h"
#include "ge.h"
#include "gl_funcs.h"
#include <string.h>

#define GL_DEFINE(ret, name, args) PFN_##name p_##name;
GL_FUNCS(GL_DEFINE)
#undef GL_DEFINE

int gl_load(void *(*getproc)(const char *)) {
    int ok = 1;
#define GL_LOAD(ret, name, args) p_##name = (PFN_##name)getproc(#name); if (!p_##name) { rt_log("GL: missing %s", #name); ok = 0; }
    GL_FUNCS(GL_LOAD)
#undef GL_LOAD
    return ok;
}

#define LW 512          /* logical render-target size in guest pixels */
#define LH 272

static int S = 4;
static int ready;
static GLuint prog, vao, vbo, prog_present;
static struct {
    GLint fbsize, tex_on, texfunc, rgba, dbl, env, atest, aref, amask, afunc, clear, texsize, sampler;
} U;
static struct { GLint tex, src_size, out_size, sharp, uv_max; } UP;

/* ---------------------------------------------------------------- shaders */
static const char *vs_src =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos; layout(location=1) in vec2 a_uv; layout(location=2) in vec4 a_col;\n"
    "uniform vec2 u_fbsize;\n"
    "out vec2 v_uv; out vec4 v_col; flat out vec4 v_fcol;\n"
    "void main() {\n"
    "  gl_Position = vec4(a_pos.x / u_fbsize.x * 2.0 - 1.0, a_pos.y / u_fbsize.y * 2.0 - 1.0, 0.0, 1.0);\n"
    "  v_uv = a_uv; v_col = a_col; v_fcol = a_col;\n"
    "}\n";

static const char *fs_src =
    "#version 330 core\n"
    "in vec2 v_uv; in vec4 v_col; flat in vec4 v_fcol;\n"
    "uniform sampler2D u_tex; uniform vec2 u_texsize;\n"
    "uniform int u_tex_on, u_texfunc, u_rgba, u_dbl, u_atest, u_afunc, u_clear;\n"
    "uniform float u_aref, u_amask; uniform vec4 u_env;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec4 c = v_col;\n"
    "  if (u_clear != 0) { o = c; return; }\n"
    "  if (u_tex_on != 0) {\n"
    "    vec4 t = texture(u_tex, v_uv / u_texsize);\n"
    "    vec4 r;\n"
    "    if (u_texfunc == 0) { r.rgb = t.rgb * c.rgb; r.a = u_rgba != 0 ? t.a * c.a : c.a; }\n"
    "    else if (u_texfunc == 1) { r.rgb = mix(c.rgb, t.rgb, t.a); r.a = c.a; }\n"
    "    else if (u_texfunc == 2) { r.rgb = mix(c.rgb, u_env.rgb, t.rgb); r.a = u_rgba != 0 ? t.a * c.a : c.a; }\n"
    "    else if (u_texfunc == 3) { r.rgb = t.rgb; r.a = u_rgba != 0 ? t.a : c.a; }\n"
    "    else { r.rgb = t.rgb + c.rgb; r.a = u_rgba != 0 ? t.a * c.a : c.a; }\n"
    "    if (u_dbl != 0 && u_texfunc != 3) r.rgb *= 2.0;\n"
    "    c = clamp(r, 0.0, 1.0);\n"
    "  }\n"
    "  if (u_atest != 0) {\n"
    "    float a = floor(c.a * 255.0 + 0.5);\n"
    "    float x = float(int(a) & int(u_amask)), ref = float(int(u_aref) & int(u_amask));\n"
    "    bool pass = u_afunc == 1 || (u_afunc == 2 && x == ref) || (u_afunc == 3 && x != ref) || (u_afunc == 4 && x < ref) ||\n"
    "                (u_afunc == 5 && x <= ref) || (u_afunc == 6 && x > ref) || (u_afunc == 7 && x >= ref);\n"
    "    if (!pass) discard;\n"
    "  }\n"
    "  o = c;\n"
    "}\n";

/* Presentation: draws a frame into the window. "sharp" mode does sharp-bilinear (integer pre-scale
 * then linear), which keeps pixels crisp at any window size without uneven pixel widths. */
static const char *vs_present =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos; layout(location=1) in vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); v_uv = a_uv; }\n";
static const char *fs_present =
    "#version 330 core\n"
    "in vec2 v_uv; uniform sampler2D u_tex; uniform vec2 u_src_size, u_out_size, u_uv_max; uniform int u_sharp;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec2 uv = v_uv;\n"
    "  if (u_sharp != 0) {\n"
    "    vec2 texel = uv * u_src_size;\n"
    "    vec2 scale = max(floor(u_out_size / u_src_size), vec2(1.0));\n"
    "    vec2 base = floor(texel), f = fract(texel) - 0.5;\n"
    "    vec2 range = 0.5 - 0.5 / scale;\n"
    "    f = (f - clamp(f, -range, range)) * scale + 0.5;\n"
    "    uv = (base + f) / u_src_size;\n"
    "  }\n"
    "  o = vec4(texture(u_tex, uv * u_uv_max).rgb, 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = p_glCreateShader(type);
    p_glShaderSource(s, 1, &src, NULL);
    p_glCompileShader(s);
    GLint ok = 0; p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; p_glGetShaderInfoLog(s, sizeof log, NULL, log); rt_log("GL shader error: %s", log); return 0; }
    return s;
}
static GLuint link(const char *vs, const char *fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = p_glCreateProgram();
    p_glAttachShader(p, v); p_glAttachShader(p, f);
    p_glLinkProgram(p);
    GLint ok = 0; p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; p_glGetProgramInfoLog(p, sizeof log, NULL, log); rt_log("GL link error: %s", log); return 0; }
    return p;
}

/* ---------------------------------------------------------------- render targets */
typedef struct { uint32_t addr; GLuint fbo, tex; int used; } Target;
static Target targets[16];
static int ntargets;
static Target *cur_target;

static Target *target_for(uint32_t addr, int create) {
    addr &= 0x0FFFFFFF;
    for (int i = 0; i < ntargets; i++) if (targets[i].addr == addr) return &targets[i];
    if (!create || ntargets >= 16) return NULL;
    Target *t = &targets[ntargets++];
    t->addr = addr;
    p_glGenTextures(1, &t->tex);
    p_glBindTexture(GL_TEXTURE_2D, t->tex);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, LW * S, LH * S, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    p_glGenFramebuffers(1, &t->fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) rt_log("GL: render target %08X incomplete", addr);
    p_glDisable(GL_SCISSOR_TEST);
    p_glColorMask(1, 1, 1, 1);
    p_glClearColor(0, 0, 0, 0);
    p_glClear(GL_COLOR_BUFFER_BIT);
    rt_log("GL: render target %08X (%dx%d)", addr, LW * S, LH * S);
    return t;
}

/* Is guest address a inside a render target? Returns it and the texel offset. */
static Target *target_containing(uint32_t a, int *ox, int *oy) {
    a &= 0x0FFFFFFF;
    for (int i = 0; i < ntargets; i++) {
        uint32_t base = targets[i].addr, bytes = LW * LH * 2u;
        if (a >= base && a < base + bytes) {
            uint32_t px = (a - base) / 2u;
            *ox = (int)(px % LW); *oy = (int)(px / LW);
            return &targets[i];
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------- texture cache */
typedef struct { uint64_t key; GLuint tex; int w, h; uint64_t last_used; } TexEntry;
#define TEX_CACHE 1024
static TexEntry tcache[TEX_CACHE];
static uint64_t frame_no;
static uint8_t decode_buf[1024 * 1024 * 4];
static struct { int valid; uint32_t addr, bytes, fmt, clutfmt, size, mode; uint64_t clut_hash; TexEntry *e; } last_tex;

void ge_gl_list_start(void) { last_tex.valid = 0; }

static TexEntry *texture_from_memory(const uint32_t *R) {
    uint32_t addr = ge_tex_addr(), bytes = ge_tex_bytes();
    uint64_t ch = ge_clut_hash();
    if (last_tex.valid && last_tex.addr == addr && last_tex.bytes == bytes && last_tex.fmt == R[0xC3] && last_tex.size == R[0xB8] &&
        last_tex.mode == R[0xC2] && last_tex.clut_hash == ch) { last_tex.e->last_used = frame_no; return last_tex.e; }
    int w = ge_tex_w(), h = ge_tex_h();
    if (w > 1024 || h > 1024 || bytes > 0x200000) return NULL;
    uint64_t key = ge_hash(HOST(addr), bytes) ^ (ch * 31) ^ ((uint64_t)R[0xC3] << 40) ^ ((uint64_t)R[0xB8] << 48) ^
                   ((uint64_t)(R[0xC2] & 1) << 63) ^ ((uint64_t)addr << 8) ^ ((uint64_t)ge_tex_stride() << 20);
    TexEntry *e = NULL, *victim = &tcache[0];
    for (int i = 0; i < TEX_CACHE; i++) {
        if (tcache[i].tex && tcache[i].key == key) { e = &tcache[i]; break; }
        if (!tcache[i].tex) { victim = &tcache[i]; }
        else if (victim->tex && tcache[i].last_used < victim->last_used) victim = &tcache[i];
    }
    if (!e) {
        e = victim;
        if (!e->tex) p_glGenTextures(1, &e->tex);
        ge_decode_texture(decode_buf);
        p_glBindTexture(GL_TEXTURE_2D, e->tex);
        p_glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, decode_buf);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        e->key = key; e->w = w; e->h = h;
    }
    e->last_used = frame_no;
    last_tex.valid = 1; last_tex.addr = addr; last_tex.bytes = bytes; last_tex.fmt = R[0xC3]; last_tex.size = R[0xB8];
    last_tex.mode = R[0xC2]; last_tex.clut_hash = ch; last_tex.e = e;
    return e;
}

/* ---------------------------------------------------------------- blending */
static GLenum blend_factor(int f, int src_side, uint32_t fixed, int *use_const, float cc[3]) {
    switch (f) {
    case 0: return src_side ? GL_DST_COLOR : GL_SRC_COLOR;
    case 1: return src_side ? GL_ONE_MINUS_DST_COLOR : GL_ONE_MINUS_SRC_COLOR;
    case 2: case 6: return GL_SRC_ALPHA;
    case 3: case 7: return GL_ONE_MINUS_SRC_ALPHA;
    case 4: case 8: return GL_DST_ALPHA;
    case 5: case 9: return GL_ONE_MINUS_DST_ALPHA;
    default:
        if ((fixed & 0xFFFFFF) == 0xFFFFFF) return GL_ONE;
        if ((fixed & 0xFFFFFF) == 0) return GL_ZERO;
        *use_const = 1;
        cc[0] = (float)(fixed & 255) / 255.f; cc[1] = (float)((fixed >> 8) & 255) / 255.f; cc[2] = (float)((fixed >> 16) & 255) / 255.f;
        return GL_CONSTANT_COLOR;
    }
}

static void apply_state(const uint32_t *R, int clear) {
    if (clear || !(R[0x21] & 1)) p_glDisable(GL_BLEND);
    else {
        uint32_t bm = R[0xDF];
        int uc = 0; float cc[3] = { 0, 0, 0 };
        GLenum sf = blend_factor((int)(bm & 15), 1, R[0xE0], &uc, cc);
        GLenum df = blend_factor((int)((bm >> 4) & 15), 0, R[0xE1], &uc, cc);
        static const GLenum eqs[6] = { GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX, GL_FUNC_ADD };
        p_glEnable(GL_BLEND);
        uint32_t eq = (bm >> 8) & 15;
        p_glBlendEquation(eqs[eq < 6 ? eq : 0]);
        p_glBlendFuncSeparate(sf, df, GL_ZERO, GL_ONE);          /* frame-buffer alpha is not blended */
        if (uc) p_glBlendColor(cc[0], cc[1], cc[2], 1.f);
    }
    if (clear) {
        uint32_t cm = R[0xD3];
        GLboolean c = (cm & 0x100) != 0, a = (cm & 0x200) != 0;
        p_glColorMask(c, c, c, a);
    } else {
        uint32_t m = R[0xE8];
        p_glColorMask((m & 0xFF) != 0xFF, ((m >> 8) & 0xFF) != 0xFF, ((m >> 16) & 0xFF) != 0xFF, 0);
    }
}

/* ---------------------------------------------------------------- drawing */
typedef struct { float x, y, u, v; uint8_t c[4]; } GlVtx;
static GlVtx vbuf[65536 * 3];

void ge_gl_draw(int type, const GeVtx *v, int n, int flat, int sx1, int sy1, int sx2, int sy2) {
    if (!ready) return;
    const uint32_t *R = ge_regs();
    Target *t = target_for(ge_fb_base(), 1);
    if (!t) return;
    int clear = (R[0xD3] & 1) != 0;

    /* triangles in guest pixels */
    int m = 0;
#define PUT(src) do { const GeVtx *s_ = (src); GlVtx *d_ = &vbuf[m++]; d_->x = s_->x; d_->y = s_->y; d_->u = s_->u; d_->v = s_->v; \
        d_->c[0] = s_->r; d_->c[1] = s_->g; d_->c[2] = s_->b; d_->c[3] = s_->a; } while (0)
    switch (type) {
    case 3: for (int i = 0; i + 2 < n; i += 3) { PUT(&v[i]); PUT(&v[i + 1]); PUT(&v[i + 2]); } break;
    case 4: for (int i = 0; i + 2 < n; i++) { PUT(&v[i]); PUT(&v[i + 1]); PUT(&v[i + 2]); } break;
    case 5: for (int i = 1; i + 1 < n; i++) { PUT(&v[0]); PUT(&v[i]); PUT(&v[i + 1]); } break;
    case 6:
        for (int i = 0; i + 1 < n; i += 2) {
            const GeVtx *a = &v[i], *b = &v[i + 1];
            GeVtx q[4] = { *b, *b, *b, *b };                /* sprites take the second vertex's colour */
            q[0].x = a->x; q[0].y = a->y; q[0].u = a->u; q[0].v = a->v;
            q[1].x = b->x; q[1].y = a->y; q[1].u = b->u; q[1].v = a->v;
            q[2].x = a->x; q[2].y = b->y; q[2].u = a->u; q[2].v = b->v;
            q[3].x = b->x; q[3].y = b->y; q[3].u = b->u; q[3].v = b->v;
            PUT(&q[0]); PUT(&q[1]); PUT(&q[2]); PUT(&q[1]); PUT(&q[3]); PUT(&q[2]);
        }
        flat = 1;
        break;
    default: return;
    }
#undef PUT
    if (!m) return;

    p_glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    p_glViewport(0, 0, LW * S, LH * S);
    p_glEnable(GL_SCISSOR_TEST);
    if (sx2 < sx1 || sy2 < sy1) return;
    p_glScissor(sx1 * S, sy1 * S, (sx2 - sx1 + 1) * S, (sy2 - sy1 + 1) * S);
    p_glUseProgram(prog);
    p_glUniform2f(U.fbsize, (float)LW, (float)LH);
    p_glUniform1i(U.clear, clear);
    int tex_on = !clear && (R[0x1E] & 1);
    p_glUniform1i(U.tex_on, tex_on);
    if (tex_on) {
        int ox = 0, oy = 0;
        Target *src = target_containing(ge_tex_addr(), &ox, &oy);
        int linear = (R[0xC6] >> 8) & 1;
        p_glActiveTexture(GL_TEXTURE0);
        if (src && src != t) {
            /* render-to-texture: sample the upscaled target; texel (u,v) is target pixel (ox+u, oy+v) */
            p_glBindTexture(GL_TEXTURE_2D, src->tex);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
            for (int i = 0; i < m; i++) { vbuf[i].u += (float)ox; vbuf[i].v += (float)oy; }
            p_glUniform2f(U.texsize, (float)LW, (float)LH);
        } else {
            TexEntry *e = texture_from_memory(R);
            if (!e) return;
            p_glBindTexture(GL_TEXTURE_2D, e->tex);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (R[0xC7] & 1) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (R[0xC7] & 0x100) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            p_glUniform2f(U.texsize, (float)e->w, (float)e->h);
        }
        uint32_t tf = R[0xC9];
        p_glUniform1i(U.texfunc, (int)(tf & 7));
        p_glUniform1i(U.rgba, (int)((tf >> 8) & 1));
        p_glUniform1i(U.dbl, (int)((tf >> 16) & 1));
        uint32_t env = R[0xCA];
        p_glUniform4f(U.env, (float)(env & 255) / 255.f, (float)((env >> 8) & 255) / 255.f, (float)((env >> 16) & 255) / 255.f, 1.f);
    }
    int at = !clear && (R[0x22] & 1);
    p_glUniform1i(U.atest, at);
    if (at) {
        uint32_t a = R[0xDB];
        p_glUniform1i(U.afunc, (int)(a & 7));
        p_glUniform1f(U.aref, (float)((a >> 8) & 255));
        p_glUniform1f(U.amask, (float)((a >> 16) & 255));
    }
    apply_state(R, clear);
    if (flat) {   /* flat shading: every corner gets the triangle's last vertex colour */
        for (int i = 0; i + 2 < m; i += 3) {
            memcpy(vbuf[i].c, vbuf[i + 2].c, 4); memcpy(vbuf[i + 1].c, vbuf[i + 2].c, 4);
        }
    }
    p_glBindVertexArray(vao);
    p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m * sizeof(GlVtx)), vbuf, GL_STREAM_DRAW);
    p_glDrawArrays(GL_TRIANGLES, 0, m);
    cur_target = t;
    t->used = 1;
}

/* ---------------------------------------------------------------- presentation */
static GLuint upload_tex;
static int upload_w, upload_h;

static void draw_to_window(GLuint tex, float src_w, float src_h, float uv_w, float uv_h, int win_w, int win_h, int sharp) {
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    p_glDisable(GL_SCISSOR_TEST);
    p_glDisable(GL_BLEND);
    p_glColorMask(1, 1, 1, 1);
    p_glViewport(0, 0, win_w, win_h);
    p_glClearColor(0, 0, 0, 1);
    p_glClear(GL_COLOR_BUFFER_BIT);
    /* letterbox to the PSP's 480:272 */
    float sx = (float)win_w / 480.f, sy = (float)win_h / 272.f, sc = sx < sy ? sx : sy;
    float w = 480.f * sc, h = 272.f * sc;
    float x0 = ((float)win_w - w) / 2.f, y0 = ((float)win_h - h) / 2.f;
    float l = x0 / (float)win_w * 2.f - 1.f, r = (x0 + w) / (float)win_w * 2.f - 1.f;
    float b = y0 / (float)win_h * 2.f - 1.f, t = (y0 + h) / (float)win_h * 2.f - 1.f;
    /* guest row 0 is texture row 0, which must appear at the top */
    GlVtx q[6] = {
        { l, t, 0, 0, {0} }, { r, t, 1, 0, {0} }, { l, b, 0, 1, {0} },
        { r, t, 1, 0, {0} }, { r, b, 1, 1, {0} }, { l, b, 0, 1, {0} } };
    p_glUseProgram(prog_present);
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindTexture(GL_TEXTURE_2D, tex);
    p_glUniform1i(UP.tex, 0);
    p_glUniform2f(UP.src_size, src_w, src_h);
    p_glUniform2f(UP.out_size, w, h);
    p_glUniform2f(UP.uv_max, uv_w, uv_h);
    p_glUniform1i(UP.sharp, sharp);
    p_glBindVertexArray(vao);
    p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    p_glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    p_glDrawArrays(GL_TRIANGLES, 0, 6);
}

int ge_gl_present(uint32_t fb_addr, int win_w, int win_h) {
    if (!ready) return 0;
    Target *t = target_for(fb_addr, 0);
    if (!t || !t->used) return 0;
    p_glBindTexture(GL_TEXTURE_2D, t->tex);
    /* window smaller than the render: mip-map so the downscale is clean */
    if (win_h < 272 * S) {
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);
        p_glGenerateMipmap(GL_TEXTURE_2D);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    draw_to_window(t->tex, 480.f * S, 272.f * S, 480.f / LW, 272.f / LH, win_w, win_h, 1);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    return 1;
}

void ge_gl_present_rgba(const uint8_t *rgba, int w, int h, int win_w, int win_h) {
    if (!ready) return;
    if (!upload_tex || upload_w != w || upload_h != h) {
        if (!upload_tex) p_glGenTextures(1, &upload_tex);
        p_glBindTexture(GL_TEXTURE_2D, upload_tex);
        p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        upload_w = w; upload_h = h;
    }
    p_glBindTexture(GL_TEXTURE_2D, upload_tex);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    draw_to_window(upload_tex, (float)w, (float)h, 1.f, 1.f, win_w, win_h, 1);
}

int ge_gl_read(uint32_t fb_addr, uint8_t *rgba, int *w, int *h) {
    if (!ready) return 0;
    Target *t = target_for(fb_addr, 0);
    if (!t || !t->used) return 0;
    *w = 480 * S; *h = 272 * S;
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, t->fbo);
    p_glPixelStorei(GL_PACK_ALIGNMENT, 4);
    p_glReadPixels(0, 0, *w, *h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    for (int i = 0; i < *w * *h; i++) rgba[i * 4 + 3] = 255;
    return 1;
}

int ge_gl_scale(void) { return S; }

void ge_gl_frame_end(void) {
    frame_no++;
    /* free textures unused for 10 seconds */
    if (frame_no % 600 == 0)
        for (int i = 0; i < TEX_CACHE; i++)
            if (tcache[i].tex && frame_no - tcache[i].last_used > 600) { p_glDeleteTextures(1, &tcache[i].tex); tcache[i].tex = 0; tcache[i].key = 0; }
}

/* ---------------------------------------------------------------- init */
int ge_gl_init(void *(*getproc)(const char *), int scale) {
    if (!gl_load(getproc)) return 0;
    S = scale < 1 ? 1 : scale > 8 ? 8 : scale;
    prog = link(vs_src, fs_src);
    prog_present = link(vs_present, fs_present);
    if (!prog || !prog_present) return 0;
#define UNI(f, name) U.f = p_glGetUniformLocation(prog, name)
    UNI(fbsize, "u_fbsize"); UNI(tex_on, "u_tex_on"); UNI(texfunc, "u_texfunc"); UNI(rgba, "u_rgba"); UNI(dbl, "u_dbl");
    UNI(env, "u_env"); UNI(atest, "u_atest"); UNI(aref, "u_aref"); UNI(amask, "u_amask"); UNI(afunc, "u_afunc");
    UNI(clear, "u_clear"); UNI(texsize, "u_texsize"); UNI(sampler, "u_tex");
#undef UNI
    UP.tex = p_glGetUniformLocation(prog_present, "u_tex");
    UP.src_size = p_glGetUniformLocation(prog_present, "u_src_size");
    UP.out_size = p_glGetUniformLocation(prog_present, "u_out_size");
    UP.uv_max = p_glGetUniformLocation(prog_present, "u_uv_max");
    UP.sharp = p_glGetUniformLocation(prog_present, "u_sharp");
    p_glUseProgram(prog);
    p_glUniform1i(U.sampler, 0);
    p_glGenVertexArrays(1, &vao);
    p_glBindVertexArray(vao);
    p_glGenBuffers(1, &vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GlVtx), (void *)0);
    p_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GlVtx), (void *)(2 * sizeof(float)));
    p_glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GlVtx), (void *)(4 * sizeof(float)));
    p_glEnableVertexAttribArray(0); p_glEnableVertexAttribArray(1); p_glEnableVertexAttribArray(2);
    p_glDisable(GL_DEPTH_TEST);
    p_glDisable(GL_CULL_FACE);
    ready = 1;
    rt_log("GL: %s / %s, internal resolution %dx%d (x%d)", (const char *)p_glGetString(GL_VERSION),
           (const char *)p_glGetString(GL_RENDERER), 480 * S, 272 * S, S);
    return 1;
}
