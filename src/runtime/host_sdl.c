/* SDL3 host: a resizable window (letterboxed 480x272, sharp pixels), keyboard and gamepads, audio.
 *
 * Keys (player 1):  arrows = d-pad    Z = cross   X = circle   A = square   S = triangle
 *                   Q = L   W = R      Enter = start   Backspace = select
 *                   F11 or Alt+Enter = full screen   F2 = back to the MODE MENU (no save)   Esc = quit
 * Gamepads: the first pad joins player 1 (alongside the keyboard), the next ones players 2-4.
 *   Mappings: SDL's own, SDL_GameControllerDB next to the exe, then <work>/gamecontrollerdb.txt.
 *   south = cross, east = circle, west = square, north = triangle, shoulders = L/R,
 *   d-pad or left stick = directions, start/back = start/select.
 */
#ifdef SFA3MR_SDL
#include "host.h"
#include <SDL3/SDL.h>
void rt_log(const char *fmt, ...);
#include <stdio.h>
#include <string.h>

static SDL_Window *win;
static SDL_GLContext glctx;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID adev;
static SDL_AudioStream *astream[8];
static SDL_Gamepad *pads_sdl[4];
static int headless_mode, quit_req, fullscreen, menu_req;

static void open_audio(void) {
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, 44100 };
    adev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (adev) {
        for (int i = 0; i < 8; i++) { astream[i] = SDL_CreateAudioStream(&spec, &spec); SDL_BindAudioStream(adev, astream[i]); }
    }
}

/* Gamepad mappings: SDL knows the common pads itself (and drives Switch, PlayStation and Xbox pads
 * directly); for the rest it reads the community database, SDL_GameControllerDB
 * (third_party/SDL_GameControllerDB, copied next to the exe), and then <work>/gamecontrollerdb.txt if
 * there is one, whose lines win, so a pad can be added or corrected without a rebuild. */
static char user_mappings[1100];
void host_set_work(const char *work) { snprintf(user_mappings, sizeof user_mappings, "%s/gamecontrollerdb.txt", work); }

int host_init(int headless, int want_gl) {
    headless_mode = headless;
    if (headless) return 1;
    char db[1200];
    const char *base = SDL_GetBasePath();
    snprintf(db, sizeof db, "%sgamecontrollerdb.txt", base ? base : "");
    SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG_FILE, db);
#ifdef _WIN32
    /* SDL's own Switch 2 driver needs libusb, which Windows does not have: let the pad come through as
       an ordinary controller, which the database maps */
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH2, "0");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) { SDL_Log("SDL_Init: %s", SDL_GetError()); return 0; }
    FILE *f = fopen(db, "rb");
    if (f) { fclose(f); rt_log("gamepad: mappings from %s", db); } else rt_log("gamepad: no %s (only SDL's own mappings)", db);
    if (user_mappings[0] && (f = fopen(user_mappings, "rb"))) {
        fclose(f);
        int n = SDL_AddGamepadMappingsFromFile(user_mappings);
        rt_log("gamepad: %d mapping(s) from %s", n, user_mappings);
    }
    /* default window: the largest multiple of 480x272 that fits comfortably on the monitor */
    int dh = host_display_height(), mult = dh ? (dh * 3 / 4) / 272 : 2;
    if (mult < 1) mult = 1;
    const char *title = "Street Fighter Alpha 3 MAX (SFA3MR)";
    if (want_gl) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        win = SDL_CreateWindow(title, 480 * mult, 272 * mult, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (win) glctx = SDL_GL_CreateContext(win);
        if (glctx) {
            SDL_GL_MakeCurrent(win, glctx);
            SDL_GL_SetSwapInterval(0);              /* the runtime paces itself to 59.94 Hz */
            open_audio();
            return 1;
        }
        SDL_Log("OpenGL 3.3 not available (%s): using the software renderer", SDL_GetError());
        if (win) SDL_DestroyWindow(win);
        win = NULL;
    }
    win = SDL_CreateWindow(title, 480 * mult, 272 * mult, SDL_WINDOW_RESIZABLE);
    if (!win) return 0;
    ren = SDL_CreateRenderer(win, NULL);
    if (!ren) return 0;
    SDL_SetRenderVSync(ren, 0);
    SDL_SetRenderLogicalPresentation(ren, 480, 272, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 480, 272);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    open_audio();
    return 1;
}

static void *gl_proc(const char *name) { return (void *)SDL_GL_GetProcAddress(name); }
HostGetProc host_gl_getproc(void) { return glctx ? gl_proc : NULL; }
void host_gl_swap(void) { if (glctx) SDL_GL_SwapWindow(win); }
void host_window_size(int *w, int *h) { *w = 480; *h = 272; if (win) SDL_GetWindowSizeInPixels(win, w, h); }
int host_display_height(void) {
    const SDL_DisplayMode *m = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    return m ? (int)(m->h * m->pixel_density) : 0;
}

void host_present(const uint8_t *rgba, int w, int h) {
    if (headless_mode || !ren) return;   /* the OpenGL path presents through ge_gl */
    SDL_UpdateTexture(tex, NULL, rgba, w * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    (void)h;
}

/* Which player a gamepad is: the controller's own player number when it has one (the XInput slot,
 * the lit LED), so pad 1 is player 1 whatever order Windows lists the pads in; otherwise the first
 * free slot. A pad already in another slot is moved to its own. */
/* Pads are ordered by the player number each controller reports (its XInput slot / lit LED), then by
   when it was connected, with no gaps: a first pad that reports player 2 (slot 1 taken by a device that
   is not a gamepad, say) is still pad 1. */
static int pad_rank[4], pads_seen;
static void order_pads(void) {
    for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) {
        int ri = pads_sdl[i] ? pad_rank[i] : 1 << 30, rj = pads_sdl[j] ? pad_rank[j] : 1 << 30;
        if (rj < ri) { SDL_Gamepad *g = pads_sdl[i]; pads_sdl[i] = pads_sdl[j]; pads_sdl[j] = g; int r = pad_rank[i]; pad_rank[i] = pad_rank[j]; pad_rank[j] = r; }
    }
    for (int i = 0; i < 4 && pads_sdl[i]; i++) rt_log("gamepad: pad %d is %s", i + 1, SDL_GetGamepadName(pads_sdl[i]) ? SDL_GetGamepadName(pads_sdl[i]) : "?");
}
static void open_pad(SDL_JoystickID id) {
    SDL_Gamepad *g = SDL_OpenGamepad(id);
    if (!g) return;
    int want = SDL_GetGamepadPlayerIndex(g);
    char guid[64]; SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), guid, sizeof guid);
    char *map = SDL_GetGamepadMapping(g);
    const char *type = SDL_GetGamepadStringForType(SDL_GetGamepadType(g));
    rt_log("gamepad: %s (id %u), player index %d, type %s, guid %s", SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "?", (unsigned)id, want,
           type ? type : "?", guid);
    rt_log("gamepad: mapping %s", map ? map : "(none)");
    SDL_free(map);
    for (int i = 0; i < 4; i++) if (!pads_sdl[i]) { pads_sdl[i] = g; pad_rank[i] = (want >= 0 ? want : 8) * 100 + pads_seen++; order_pads(); return; }
    SDL_CloseGamepad(g);
}

/* The left stick as a d-pad, the way a fighting game wants it: nothing inside a dead zone, then one of
 * eight directions by angle (45 degrees each). The old per-axis test turned a stick pushed straight
 * left a little downwards into a diagonal, and so a crouch. */
#define STICK_DEAD 13000                            /* of 32767: about 40% */
static uint32_t stick_dir(int x, int y) {
    if ((long)x * x + (long)y * y < (long)STICK_DEAD * STICK_DEAD) return 0;
    static const uint32_t dir[8] = { PSP_RIGHT, PSP_RIGHT | PSP_DOWN, PSP_DOWN, PSP_DOWN | PSP_LEFT,
                                     PSP_LEFT, PSP_LEFT | PSP_UP, PSP_UP, PSP_UP | PSP_RIGHT };
    double a = SDL_atan2((double)y, (double)x);     /* y grows downwards */
    int sector = (int)SDL_floor((a + SDL_PI_D / 8) / (SDL_PI_D / 4));
    return dir[(sector % 8 + 8) % 8];
}

static uint32_t pad_bits(SDL_Gamepad *g, uint8_t stick[2]) {
    uint32_t b = 0;
    if (!g) return 0;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_SOUTH)) b |= PSP_CROSS;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_EAST)) b |= PSP_CIRCLE;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_WEST)) b |= PSP_SQUARE;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_NORTH)) b |= PSP_TRIANGLE;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) b |= PSP_L;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) b |= PSP_R;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START)) b |= PSP_START;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK)) b |= PSP_SELECT;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP)) b |= PSP_UP;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) b |= PSP_DOWN;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) b |= PSP_LEFT;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) b |= PSP_RIGHT;
    int lx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    stick[0] = (uint8_t)((lx + 32768) >> 8); stick[1] = (uint8_t)((ly + 32768) >> 8);
    b |= stick_dir(lx, ly);
    if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) b |= PAD_L2;
    if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) b |= PAD_R2;
    return b;
}

void host_poll(uint32_t pads[4], uint8_t sticks[4][2]) {
    if (headless_mode) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT: quit_req = 1; break;
        case SDL_EVENT_GAMEPAD_ADDED: open_pad(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            for (int i = 0; i < 4; i++) if (pads_sdl[i] && SDL_GetGamepadID(pads_sdl[i]) == e.gdevice.which) { SDL_CloseGamepad(pads_sdl[i]); pads_sdl[i] = NULL; }
            order_pads();
            break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.scancode == SDL_SCANCODE_ESCAPE) quit_req = 1;
            if (e.key.scancode == SDL_SCANCODE_F2 && !e.key.repeat) menu_req = 1;   /* keyboard only: no pad has it */
            if (e.key.scancode == SDL_SCANCODE_F11 || (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT))) {
                fullscreen = !fullscreen; SDL_SetWindowFullscreen(win, fullscreen);
            }
            break;
        default: break;
        }
    }
    const bool *k = SDL_GetKeyboardState(NULL);
    uint32_t kb = 0;
    if (k[SDL_SCANCODE_UP]) kb |= PSP_UP;
    if (k[SDL_SCANCODE_DOWN]) kb |= PSP_DOWN;
    if (k[SDL_SCANCODE_LEFT]) kb |= PSP_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) kb |= PSP_RIGHT;
    if (k[SDL_SCANCODE_Z]) kb |= PSP_CROSS;
    if (k[SDL_SCANCODE_X]) kb |= PSP_CIRCLE;
    if (k[SDL_SCANCODE_A]) kb |= PSP_SQUARE;
    if (k[SDL_SCANCODE_S]) kb |= PSP_TRIANGLE;
    if (k[SDL_SCANCODE_Q]) kb |= PSP_L;
    if (k[SDL_SCANCODE_W]) kb |= PSP_R;
    if (k[SDL_SCANCODE_E]) kb |= PAD_L2;
    if (k[SDL_SCANCODE_R]) kb |= PAD_R2;
    if (k[SDL_SCANCODE_RETURN] && !(SDL_GetModState() & SDL_KMOD_ALT)) kb |= PSP_START;
    if (k[SDL_SCANCODE_BACKSPACE]) kb |= PSP_SELECT;
    for (int i = 0; i < 4; i++) { sticks[i][0] = sticks[i][1] = 128; pads[i] = pad_bits(pads_sdl[i], sticks[i]); }
    pads[0] |= kb;
}

int host_quit_requested(void) { return quit_req; }
int host_menu_requested(void) { int r = menu_req; menu_req = 0; return r; }   /* F2, once per press */
/* Pad i is player i+1; player 1 also has the keyboard, so a single gamepad does not make player 2 human. */
int host_pad_connected(int i) { return i >= 0 && i < 4 && pads_sdl[i] != NULL; }

void host_audio(int ch, const int16_t *s, int frames, int stereo, int lvol, int rvol) {
    if (headless_mode || !adev || ch < 0 || ch >= 8 || !astream[ch]) return;
    if (SDL_GetAudioStreamQueued(astream[ch]) > 44100 * 4 / 4) return;   /* more than 250 ms queued: drop */
    static int16_t buf[8192 * 2];
    if (frames > 8192) frames = 8192;
    for (int i = 0; i < frames; i++) {
        int l = stereo ? s[2 * i] : s[i], r = stereo ? s[2 * i + 1] : s[i];
        buf[2 * i] = (int16_t)(l * lvol / 0x8000); buf[2 * i + 1] = (int16_t)(r * rvol / 0x8000);
    }
    SDL_PutAudioStreamData(astream[ch], buf, frames * 4);
}

void host_shutdown(void) { if (!headless_mode) SDL_Quit(); }
#endif
