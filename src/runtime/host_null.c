/* Headless host: no window, no sound. Used for tests and when built without SDL. */
#include "host.h"
#ifndef SFA3MR_SDL
int  host_init(int headless, int want_gl) { (void)headless; (void)want_gl; return 1; }
HostGetProc host_gl_getproc(void) { return 0; }
void host_gl_swap(void) {}
void host_window_size(int *w, int *h) { *w = 480; *h = 272; }
int  host_display_height(void) { return 0; }
void host_present(const uint8_t *rgba, int w, int h) { (void)rgba; (void)w; (void)h; }
void host_poll(uint32_t pads[4], uint8_t sticks[4][2]) { (void)pads; (void)sticks; }
int  host_quit_requested(void) { return 0; }
int  host_menu_requested(void) { return 0; }
int  host_pad_connected(int i) { (void)i; return 0; }
void host_audio(int ch, const int16_t *s, int f, int st, int l, int r) { (void)ch; (void)s; (void)f; (void)st; (void)l; (void)r; }
void host_shutdown(void) {}
#endif
