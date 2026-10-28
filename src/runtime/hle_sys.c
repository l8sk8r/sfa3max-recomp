/* System libraries: memory, modules, power, utilities, display, controller, audio, savedata, font, network.
 * Most are simple; the ones still stubbed say so in the trace. */
#include "rt.h"
#include "host.h"
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

/* ---------------------------------------------------------------- SysMemUserForUser */
typedef struct { uint32_t id, addr; } MemBlock;
static MemBlock mblocks[256];
void hle_sceKernelAllocPartitionMemory(Cpu *c) {
    uint32_t part = ARG(0), type = ARG(2), size = ARG(3), addr = ARG(4);
    const char *name = rt_str(ARG(1));
    uint32_t a = 0;
    if (type == 2) a = rt_alloc_at(addr, size, name);
    else if (type == 3 || type == 4) a = rt_alloc(size, type == 4, addr ? addr : 256, name);
    else a = rt_alloc(size, type == 1, 256, name);
    TRACE("AllocPartitionMemory part=%u %s type=%u size=%X -> %08X", part, name ? name : "", type, size, a);
    if (!a) RET(ERR_NO_MEMORY);
    for (int i = 0; i < 256; i++) if (!mblocks[i].id) { mblocks[i].id = rt_new_uid(); mblocks[i].addr = a; RET(mblocks[i].id); }
    RET(ERR_NO_MEMORY);
}
void hle_sceKernelFreePartitionMemory(Cpu *c) {
    for (int i = 0; i < 256; i++) if (mblocks[i].id == ARG(0)) { rt_free(mblocks[i].addr); mblocks[i].id = 0; RET(0); }
    RET(ERR_UNKNOWN_UID);
}
void hle_sceKernelGetBlockHeadAddr(Cpu *c) {
    for (int i = 0; i < 256; i++) if (mblocks[i].id == ARG(0)) RET(mblocks[i].addr);
    RET(0);
}
void hle_sceKernelSetCompiledSdkVersion(Cpu *c) { rt_log("game built with SDK %08X", ARG(0));   /* 02050010: save encryption mode 3 (savestore.c) */ RET(0); }
void hle_sceKernelSetCompilerVersion(Cpu *c) { RET(0); }
void hle_sceKernelPrintf(Cpu *c) { rt_log("[game printf] %s", rt_str(ARG(0))); RET(0); }

/* ---------------------------------------------------------------- modules: Sony libraries are native, so loading is a no-op */
#define MAIN_MODULE_ID 0x4D41494Eu
void hle_sceKernelGetModuleId(Cpu *c) { RET(MAIN_MODULE_ID); }
void hle_sceKernelGetModuleIdByAddress(Cpu *c) { RET(MAIN_MODULE_ID); }
void hle_sceKernelLoadModuleByID(Cpu *c) { uint32_t id = rt_new_uid(); TRACE("LoadModuleByID fd=%u -> %x (native)", ARG(0), id); RET(id); }
void hle_sceKernelStartModule(Cpu *c) { if (ARG(3)) WW(ARG(3), 0); RET(ARG(0)); }
void hle_sceKernelStopModule(Cpu *c) { if (ARG(3)) WW(ARG(3), 0); RET(0); }
void hle_sceKernelUnloadModule(Cpu *c) { RET(0); }
void hle_sceKernelSelfStopUnloadModule(Cpu *c) { (void)c; rt_log("game unloaded itself"); rt_quit = 1; hle_sceKernelExitThread(c); }
void hle_sceKernelExitGame(Cpu *c) { (void)c; rt_log("game exit"); rt_quit = 1; hle_sceKernelExitThread(c); }
void hle_sceKernelRegisterExitCallback(Cpu *c) { RET(0); }

/* ---------------------------------------------------------------- UtilsForUser */
void hle_sceKernelDcacheInvalidateRange(Cpu *c) { RET(0); }
void hle_sceKernelDcacheWritebackAll(Cpu *c) { (void)c; }
void hle_sceKernelDcacheWritebackRange(Cpu *c) { (void)c; }
void hle_sceKernelLibcClock(Cpu *c) { RET((uint32_t)rt_now_us()); }
void hle_sceKernelLibcTime(Cpu *c) {
    uint32_t t = (uint32_t)time(NULL);
    if (ARG(0)) WW(ARG(0), t);
    RET(t);
}
void hle_sceKernelLibcGettimeofday(Cpu *c) {
    uint64_t us = (uint64_t)time(NULL) * 1000000u + rt_now_us() % 1000000u;
    if (ARG(0)) { WW(ARG(0), (uint32_t)(us / 1000000u)); WW(ARG(0) + 4, (uint32_t)(us % 1000000u)); }
    RET(0);
}

/* ---------------------------------------------------------------- power */
void hle_sceKernelPowerLock(Cpu *c) { RET(0); }
void hle_sceKernelPowerUnlock(Cpu *c) { RET(0); }
void hle_sceKernelPowerTick(Cpu *c) { RET(0); }
void hle_scePowerRegisterCallback(Cpu *c) { RET(0); }
void hle_scePowerUnregisterCallback(Cpu *c) { RET(0); }
void hle_sceDmacMemcpy(Cpu *c) { memmove(HOST(ARG(0)), HOST(ARG(1)), ARG(2)); RET(0); }

/* ---------------------------------------------------------------- display */
static uint32_t fb_addr, fb_width, fb_format, disp_mode, disp_w = 480, disp_h = 272;
void hle_sceDisplaySetMode(Cpu *c) { disp_mode = ARG(0); disp_w = ARG(1); disp_h = ARG(2); TRACE("DisplaySetMode %u %ux%u", disp_mode, disp_w, disp_h); RET(0); }
void hle_sceDisplaySetFrameBuf(Cpu *c) {
    if (fb_addr != ARG(0)) TRACE("SetFrameBuf %08X width=%u fmt=%u sync=%u", ARG(0), ARG(1), ARG(2), ARG(3));
    fb_addr = ARG(0); fb_width = ARG(1); fb_format = ARG(2);
    RET(0);
}
void hle_sceDisplayGetVcount(Cpu *c) { RET((uint32_t)rt_vblank_count()); }
void hle_sceDisplayWaitVblankStart(Cpu *c) { rt_wait_vblank(c); RET(0); }
void hle_sceDisplayWaitVblank(Cpu *c) { rt_wait_vblank(c); RET(0); }
void hle_sceDisplayGetCurrentHcount(Cpu *c) { RET((uint32_t)(rt_now_us() * 286 / 16683)); }
uint32_t rt_fb_addr(void) { return fb_addr; }
uint32_t rt_fb_width(void) { return fb_width; }
uint32_t rt_fb_format(void) { return fb_format; }

/* ---------------------------------------------------------------- controller */
uint32_t rt_pad_buttons[4];          /* filled by the host input layer; [0] = player 1 */
uint8_t  rt_pad_lx = 128, rt_pad_ly = 128;
static uint32_t ctrl_mode;
void hle_sceCtrlSetSamplingMode(Cpu *c) { uint32_t o = ctrl_mode; ctrl_mode = ARG(0); RET(o); }
void hle_sceCtrlReadBufferPositive(Cpu *c) {
    uint32_t buf = ARG(0), n = ARG(1);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t p = buf + i * 16;
        WW(p, (uint32_t)rt_now_us()); WW(p + 4, rt_pad_buttons[0]);
        WB(p + 8, rt_pad_lx); WB(p + 9, rt_pad_ly);
        for (int k = 10; k < 16; k++) WB(p + k, 0);
    }
    RET(n);
}

/* ---------------------------------------------------------------- audio output (silent for now; paced in virtual time) */
static uint64_t ch_busy_until[8];
static int ch_used[8];
static uint32_t ch_samples[8], ch_format[8];
void hle_sceAudioChReserve(Cpu *c) {
    int ch = (int)ARG(0);
    if (ch < 0 || ch >= 8) { for (ch = 0; ch < 8 && ch_used[ch]; ch++) {} }
    if (ch >= 8) RET(0x80260002);
    ch_used[ch] = 1; ch_samples[ch] = ARG(1); ch_format[ch] = ARG(2);
    TRACE("AudioChReserve %d samples=%u fmt=%u", ch, ARG(1), ARG(2));
    RET(ch);
}
void hle_sceAudioChRelease(Cpu *c) { if (ARG(0) < 8) ch_used[ARG(0)] = 0; RET(0); }
/* --record-audio DIR: every channel's output (after volume) is appended to DIR/chN.wav */
const char *rt_record_audio_dir;
static FILE *rec[8];
static uint32_t rec_bytes[8];
static void wav_header(FILE *f, uint32_t bytes) {
    uint8_t h[44]; memcpy(h, "RIFF", 4); uint32_t v = bytes + 36; memcpy(h + 4, &v, 4); memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4); uint16_t w = 1; memcpy(h + 20, &w, 2); w = 2; memcpy(h + 22, &w, 2);
    v = 44100; memcpy(h + 24, &v, 4); v = 44100 * 4; memcpy(h + 28, &v, 4); w = 4; memcpy(h + 32, &w, 2); w = 16; memcpy(h + 34, &w, 2);
    memcpy(h + 36, "data", 4); memcpy(h + 40, &bytes, 4);
    fseek(f, 0, SEEK_SET); fwrite(h, 1, 44, f); fseek(f, 0, SEEK_END);
}
static void record(uint32_t ch, uint32_t buf, uint32_t n, int stereo, uint32_t lv, uint32_t rv) {
    if (!rt_record_audio_dir || ch >= 8) return;
    if (!rec[ch]) { char p[1100]; snprintf(p, sizeof p, "%s/ch%u.wav", rt_record_audio_dir, ch); rec[ch] = fopen(p, "wb"); if (!rec[ch]) return; wav_header(rec[ch], 0); }
    const int16_t *s = (const int16_t *)HOST(buf);
    for (uint32_t i = 0; i < n; i++) {
        int l = stereo ? s[2 * i] : s[i], r = stereo ? s[2 * i + 1] : s[i];
        int16_t o[2] = { (int16_t)(l * (int)lv / 0x8000), (int16_t)(r * (int)rv / 0x8000) };
        fwrite(o, 2, 2, rec[ch]);
    }
    rec_bytes[ch] += n * 4;
    wav_header(rec[ch], rec_bytes[ch]);
}
static void audio_out(Cpu *c, uint32_t ch, int blocking, uint32_t lv, uint32_t rv, uint32_t buf) {
    if (ch >= 8) RET(0x80260003);
    if (buf) record(ch, buf, ch_samples[ch], ch_format[ch] == 0, lv, rv);
    if (buf) host_audio((int)ch, (const int16_t *)HOST(buf), (int)ch_samples[ch], ch_format[ch] == 0, (int)lv, (int)rv);
    uint64_t now = rt_now_us(), len = (uint64_t)ch_samples[ch] * 1000000u / 44100u;
    if (blocking && ch_busy_until[ch] > now) rt_delay(c, ch_busy_until[ch] - now);
    now = rt_now_us();
    ch_busy_until[ch] = (ch_busy_until[ch] > now ? ch_busy_until[ch] : now) + len;
    RET(ch_samples[ch]);
}
void hle_sceAudioOutputBlocking(Cpu *c) { audio_out(c, ARG(0), 1, ARG(1), ARG(1), ARG(2)); }
void hle_sceAudioOutputPannedBlocking(Cpu *c) { audio_out(c, ARG(0), 1, ARG(1), ARG(2), ARG(3)); }
void hle_sceAudioOutputPanned(Cpu *c) { audio_out(c, ARG(0), 0, ARG(1), ARG(2), ARG(3)); }
void hle_sceAudioGetChannelRestLength(Cpu *c) { RET(0); }
void hle_sceAudioSetChannelDataLen(Cpu *c) { if (ARG(0) < 8) ch_samples[ARG(0)] = ARG(1); RET(0); }
void hle_sceAudioChangeChannelConfig(Cpu *c) { RET(0); }
void hle_sceAudioChangeChannelVolume(Cpu *c) { RET(0); }

/* ---------------------------------------------------------------- utility: save data, system params */
/* Save data: ms0:/PSP/SAVEDATA/<gameName><saveName>/<fileName>, plus ICON0/PIC1/PARAM as given.
 * Offsets are SceUtilitySavedataParam's. The PSP's dialog UI is skipped: every request completes at once. */
static uint32_t sd_status, sd_param;
void sysdata_loaded(const char *file);                                  /* sysdata.c */
int store_is_ours(const char *file);                                    /* savestore.c: the game's two saves */
long store_load(const char *file, uint32_t buf, uint32_t cap);
int store_save(const char *file, uint32_t buf, uint32_t n);
int store_exists(const char *name);
extern char rt_save_root[1024];
static void mkdirs(const char *path) {
    char tmp[1100]; strncpy(tmp, path, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0;
#ifdef _WIN32
        _mkdir(tmp);
#else
        mkdir(tmp, 0755);
#endif
        *p = '/'; }
}
static void sd_dir(uint32_t prm, const char *save, char *out, size_t n) {
    char game[14] = {0}; memcpy(game, HOST(prm + 0x3C), 13);
    snprintf(out, n, "%s/PSP/SAVEDATA/%s%s", rt_save_root, game, save);
}
/* The game sometimes passes a buffer size of 0xFFFFFFFF ("any size"). glibc's fread then just reads
 * the file; the Windows C library refuses a 4 GB read and returns nothing, which the game reported
 * as "Auto-load failed". So read no more than the file holds and guest memory has room for. */
static long sd_read(const char *path, uint32_t buf, uint32_t cap) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size < 0) size = 0;
    if ((uint32_t)size < cap) cap = (uint32_t)size;
    if (buf < USER_LO || buf >= USER_HI) cap = 0; else if (cap > USER_HI - buf) cap = USER_HI - buf;
    long n = (long)fread(HOST(buf), 1, cap, f); fclose(f); return n;
}
static int sd_write(const char *path, uint32_t buf, uint32_t n) {
    FILE *f = fopen(path, "wb"); if (!f) return 0;
    fwrite(HOST(buf), 1, n, f); fclose(f); return 1;
}
static const char *sd_pick_name(uint32_t prm, int loading, char *buf) {
    char *own = (char *)HOST(prm + 0x4C);
    memset(buf, 0, 21); memcpy(buf, own, 20);
    uint32_t list = RW(prm + 0x60);
    if ((!buf[0] || buf[0] == '<') && list) {             /* list modes: first entry that exists (or the first) */
        for (int i = 0; i < 64; i++) {
            char *e = (char *)HOST(list + 20u * (uint32_t)i);
            if (!e[0]) break;
            char d[1100]; char nm[21] = {0}; memcpy(nm, e, 20);
            sd_dir(prm, nm, d, sizeof d);
            struct stat st;
            if (!loading || stat(d, &st) == 0 || i == 0) { memcpy(buf, nm, 21); if (!loading || stat(d, &st) == 0) break; }
        }
    }
    return buf;
}
void hle_sceUtilitySavedataInitStart(Cpu *c) {
    uint32_t prm = sd_param = ARG(0);
    uint32_t mode = RW(prm + 0x30), result = 0;
    char name[21], dir[1100], path[1200], file[14] = {0};
    memcpy(file, HOST(prm + 0x64), 13);
    switch (mode) {
    case 0: case 2: case 4: {                               /* AUTOLOAD, LOAD, LISTLOAD */
        long n;
        if (store_is_ours(file)) n = store_load(file, RW(prm + 0x74), RW(prm + 0x78));   /* sfa3r.dat / .cfg */
        else {
            sd_dir(prm, sd_pick_name(prm, 1, name), dir, sizeof dir);
            snprintf(path, sizeof path, "%s/%s", dir, file);
            n = sd_read(path, RW(prm + 0x74), RW(prm + 0x78));
        }
        if (n < 0) result = 0x80110307;                    /* no data */
        else { WW(prm + 0x7C, (uint32_t)n); sysdata_loaded(file); }
        break;
    }
    case 1: case 3: case 5: {                               /* AUTOSAVE, SAVE, LISTSAVE */
        if (store_is_ours(file)) { if (!store_save(file, RW(prm + 0x74), RW(prm + 0x7C))) result = 0x80110385; break; }
        sd_dir(prm, sd_pick_name(prm, 0, name), dir, sizeof dir);
        snprintf(path, sizeof path, "%s/%s", dir, file);
        mkdirs(path);
        if (!sd_write(path, RW(prm + 0x74), RW(prm + 0x7C))) result = 0x80110385;
        static const char *extra[4] = { "ICON0.PNG", "ICON1.PMF", "PIC1.PNG", "SND0.AT3" };
        for (int k = 0; k < 4; k++) {
            uint32_t fd = prm + 0x584 + 16u * (uint32_t)k, b = RW(fd), sz = RW(fd + 8);
            if (b && sz) { snprintf(path, sizeof path, "%s/%s", dir, extra[k]); sd_write(path, b, sz); }
        }
        snprintf(path, sizeof path, "%s/TITLE.TXT", dir);
        FILE *f = fopen(path, "w");
        if (f) { fprintf(f, "%.128s\n%.128s\n%.1024s\n", (char *)HOST(prm + 0x80), (char *)HOST(prm + 0x100), (char *)HOST(prm + 0x180)); fclose(f); }
        break;
    }
    case 8: {                                               /* SIZES: plenty of room; report whether the save exists */
        uint32_t fr = RW(prm + 0x5D0), md = RW(prm + 0x5D4), ud = RW(prm + 0x5D8);
        if (fr) { WW(fr, 0x8000); WW(fr + 4, 0x8000); WW(fr + 8, 0x100000); strcpy((char *)HOST(fr + 12), "1 GB"); }
        if (md) {
            char nm[21] = {0}; memcpy(nm, HOST(md + 16), 20);
            sd_dir(prm, nm, dir, sizeof dir);
            struct stat st;
            int exists = store_exists(nm);
            if (exists < 0) exists = stat(dir, &st) == 0;
            memset(HOST(md + 36), 0, 24);
            if (exists) { WW(md + 36, 1); WW(md + 40, 32); strcpy((char *)HOST(md + 44), "32 KB"); WW(md + 52, 32); strcpy((char *)HOST(md + 56), "32 KB"); }
            else result = 0x801103C7;                      /* no such save */
        }
        if (ud) { WW(ud, 1); WW(ud + 4, 32); strcpy((char *)HOST(ud + 8), "32 KB"); WW(ud + 16, 32); strcpy((char *)HOST(ud + 20), "32 KB"); }
        break;
    }
    default:
        rt_log("savedata mode %u not implemented", mode);
        break;
    }
    TRACE("Savedata mode=%u game=%.13s save=%.20s file=%s -> %08X", mode, (char *)HOST(prm + 0x3C), (char *)HOST(prm + 0x4C), file, result);
    WW(prm + 0x1C, result);
    sd_status = 3;
    RET(0);
}
void hle_sceUtilitySavedataShutdownStart(Cpu *c) { sd_status = 4; RET(0); }
void hle_sceUtilitySavedataUpdate(Cpu *c) { RET(0); }
void hle_sceUtilitySavedataGetStatus(Cpu *c) { uint32_t s = sd_status; if (sd_status == 4) sd_status = 0; RET(s); }
void hle_sceUtilityGetSystemParamString(Cpu *c) {
    if (ARG(0) == 1 && ARG(1)) { strncpy((char *)HOST(ARG(1)), "PLAYER1", ARG(2)); RET(0); }   /* nickname */
    RET(0x80110103);
}
void hle_sceUtilityLoadNetModule(Cpu *c) { RET(0); }

/* font: see hle_font.c */

/* ---------------------------------------------------------------- network (stub: no other PSPs around) */
/* ad-hoc wireless: see hle_net.c */
