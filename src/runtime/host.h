/* The host platform layer: window, input and sound. SDL3 on desktop; a null version for headless tests. */
#pragma once
#include <stdint.h>

void host_set_work(const char *work);                          /* before host_init: where a user gamecontrollerdb.txt may be */
int  host_init(int headless, int want_gl);                     /* 0 on failure */
typedef void *(*HostGetProc)(const char *);
HostGetProc host_gl_getproc(void);                             /* OpenGL 3.3 core context's loader, or NULL */
HostGetProc host_headless_gl(void);                            /* headless test context (SFA3MR_EGL builds), or NULL */
void host_gl_swap(void);
void host_window_size(int *w, int *h);                         /* drawable size in pixels */
int  host_display_height(void);                                /* the monitor's height in pixels (0 if unknown) */
void host_present(const uint8_t *rgba, int w, int h);          /* one finished frame (RGBA8) */
void host_poll(uint32_t pads[4], uint8_t sticks[4][2]);        /* PSP button bits per player */
int  host_quit_requested(void);
int  host_menu_requested(void);                 /* F2 pressed since the last call: back to the MODE MENU */
int  host_pad_connected(int i);                                /* gamepad i (0-3) present */
void host_audio(int ch, const int16_t *samples, int frames, int stereo, int lvol, int rvol);
void host_shutdown(void);

/* PSP button bits */
#define PSP_SELECT   0x0001u
#define PSP_START    0x0008u
#define PSP_UP       0x0010u
#define PSP_RIGHT    0x0020u
#define PSP_DOWN     0x0040u
#define PSP_LEFT     0x0080u
#define PSP_L        0x0100u
#define PSP_R        0x0200u
#define PSP_TRIANGLE 0x1000u
#define PSP_CIRCLE   0x2000u
#define PSP_CROSS    0x4000u
#define PSP_SQUARE   0x8000u
/* buttons a PSP does not have (modern pads): turned into PSP buttons by the player's config (playercfg.c) */
#define PAD_L2       0x40000000u
#define PAD_R2       0x80000000u
