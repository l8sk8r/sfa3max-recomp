/* Headless OpenGL for tests (Linux, Mesa): a surfaceless EGL context, so the GPU renderer can run
 * and be checked without a window. Built only with -DSFA3R_EGL=ON. */
#include <stddef.h>
typedef void *(*GetProc)(const char *);
#ifdef SFA3R_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
static void *egl_proc(const char *name) { return (void *)eglGetProcAddress(name); }
GetProc host_headless_gl(void) {
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay d = gpd ? gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) { fprintf(stderr, "EGL: no display\n"); return NULL; }
    eglBindAPI(EGL_OPENGL_API);
    static const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
    EGLConfig cfg = NULL; EGLint n = 0;
    if (!eglChooseConfig(d, cfg_attr, &cfg, 1, &n) || n < 1) cfg = NULL;    /* surfaceless: no config needed */
    static const EGLint ctx_attr[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                                       EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) { fprintf(stderr, "EGL: no context\n"); return NULL; }
    return egl_proc;
}
#else
GetProc host_headless_gl(void) { return NULL; }
#endif
