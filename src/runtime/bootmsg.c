/* The boot screen's save messages.
 *
 * At boot the game auto-loads the system data, showing "Auto-loading SYSTEM DATA... Please do not
 * turn off the PSP..." and then "Auto-load completed.". Messages are pictures, not text: each is a
 * row of 16x16 tiles unpacked (0x088D2B64) into 64x64 4-bit textures and drawn tile by tile. Both
 * are kept off screen (their textures are cleared); the load itself takes no time (sysdata.c). The
 * textures are recognised by their contents (FNV-1a of the 2 KB) at the addresses the boot screen
 * uses, and only in the first frames after boot. (SFA3R's version is shown on EX. SETTING.)
 */
#include "rt.h"
#include <string.h>

#define TEX_BYTES 0x800u                      /* 64 x 64, 4 bits */
static const struct { uint32_t addr, hash; } messages[] = {
    { 0x095FC300u, 0xDE5B66F0u }, { 0x095FCB80u, 0xB775E7F0u }, { 0x095FD400u, 0x4D9570C9u },   /* Auto-loading ... */
    { 0x095FDC80u, 0x54CD7B00u }, { 0x095FE500u, 0x1726ADB2u },
    { 0x095FF600u, 0x0E558E3Eu },                                                               /* Auto-load completed. */
};

static uint32_t fnv(uint32_t a, uint32_t n) {
    uint32_t h = 0x811C9DC5u; const uint8_t *p = HOST(a);
    for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

/* once a frame while the boot screen may be up */
void bootmsg_frame(void) {
    static int done;
    if (done) return;
    if ((long)rt_vblank_count() > 1800) { done = 1; return; }
    for (size_t i = 0; i < sizeof messages / sizeof *messages; i++)
        if (fnv(messages[i].addr, TEX_BYTES) == messages[i].hash) {
            memset(HOST(messages[i].addr), 0, TEX_BYTES);
            if (i == sizeof messages / sizeof *messages - 1) done = 1;
        }
}
