/* The game's saves, kept in SFA3R's own two files instead of the PSP memory stick.
 *
 * The game builds its saves as PSP files in memory and hands them to the OS (sceUtilitySavedata*):
 *   STFALPH3.SYS (system data, 0x3600 bytes, save ULUS100620000)
 *   STFALPH3.WTR (World Tour, 0x1A00 bytes, save ULUS100620001)
 * Both are a header (0x3399 at +0x280, zeros elsewhere) followed by pieces of the game's memory:
 *
 *   SYS  +0x284  0x208   0x08A058A0  options: game / screen settings, button configs, unlocks, ...
 *        +0x48C  0x17C0  0x08A05AA8
 *        +0x1C4C 0x3C0   0x08A07268
 *        +0x200C 0x140   0x08A07628
 *        +0x214C 0x13F8  0x09519588
 *        +0x3544 4       0x08A05894
 *   WTR  +0x284  0x174C  0x09517E38  World Tour
 *
 * Here (hle_sys.c) every load and save of those two files goes to this module instead:
 *   <work>/sfa3r.dat  the pieces, as named blocks of hex (all of them, so nothing is lost)
 *   <work>/sfa3r.cfg  the settings in the options piece, by name (settings.c); they win over the
 *                     same bytes in sfa3r.dat, so the file can be edited by hand
 * A save made by the game writes both files. The settings are also written when they change on our
 * own screens and when the program closes (taken from the game's memory, saved or not).
 *
 * On the first run with no sfa3r.dat, a save on the memory stick (<work>/ms0/PSP/SAVEDATA, as the
 * earlier versions wrote it) is imported. With nothing there either, the game's defaults are taken
 * (sysdata.c) the first time the game asks for its system data.
 */
#include "rt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "psp_savecrypt.h"

#define SYS_SIZE 0x3600u
#define WTR_SIZE 0x1A00u
#define MAGIC_AT 0x280u
#define MAGIC    0x3399u

typedef struct { const char *name; uint32_t off, len, mem; } Piece;
static const Piece sys_pieces[] = {
    { "options",  0x284,  0x208,  0x08A058A0u }, { "data_08A05AA8", 0x48C, 0x17C0, 0x08A05AA8u },
    { "data_08A07268", 0x1C4C, 0x3C0, 0x08A07268u }, { "data_08A07628", 0x200C, 0x140, 0x08A07628u },
    { "data_09519588", 0x214C, 0x13F8, 0x09519588u }, { "data_08A05894", 0x3544, 4, 0x08A05894u },
};
static const Piece wtr_pieces[] = { { "world_tour", 0x284, 0x174C, 0x09517E38u } };

uint8_t store_sys[SYS_SIZE], store_wtr[WTR_SIZE];
int store_have_sys, store_have_wtr;
static int live;                                     /* the game has loaded its system data */
void store_sync_live(void);

extern char rt_save_root[1024];                      /* <work>/ms0 */
void store_path(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s", rt_save_root);
    char *s = strrchr(out, '/');
    if (s && !strcmp(s, "/ms0")) *s = 0; else snprintf(out, n, ".");
    size_t l = strlen(out);
    snprintf(out + l, n - l, "/%s", name);
}

/* ---------------------------------------------------------------- the PSP files <-> our images */
static void header(uint8_t *img, uint32_t size) { memset(img, 0, size); img[MAGIC_AT] = MAGIC & 0xFF; img[MAGIC_AT + 1] = MAGIC >> 8; }
int store_is_ours(const char *file) { return !strcmp(file, "STFALPH3.SYS") || !strcmp(file, "STFALPH3.WTR"); }
static int is_sys(const char *file) { return !strcmp(file, "STFALPH3.SYS"); }

/* ---------------------------------------------------------------- sfa3r.dat */
static void write_block(FILE *f, const char *what, const Piece *p, const uint8_t *img) {
    fprintf(f, "\n[%s %s %08X %X]\n", what, p->name, p->mem, p->len);
    for (uint32_t i = 0; i < p->len; i++) fprintf(f, "%02X%s", img[p->off + i], (i % 32 == 31 || i + 1 == p->len) ? "\n" : "");
}
static int write_atomic(const char *path, void (*body)(FILE *)) {
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { rt_log("store: cannot write %s", tmp); return 0; }
    body(f);
    int ok = !ferror(f); fclose(f);
    if (!ok) { remove(tmp); return 0; }
    remove(path);
    if (rename(tmp, path) != 0) { rt_log("store: cannot replace %s", path); return 0; }
    return 1;
}
static void dat_body(FILE *f) {
    fprintf(f, "# SFA3R game data: the pieces of the PSP save files, in hex. The settings among them\n"
               "# (game options, screen, button configs, unlocks) are also in sfa3r.cfg, which wins.\n"
               "# [file block address length]\n");
    if (store_have_sys) for (size_t i = 0; i < sizeof sys_pieces / sizeof *sys_pieces; i++) write_block(f, "system", &sys_pieces[i], store_sys);
    if (store_have_wtr) write_block(f, "worldtour", &wtr_pieces[0], store_wtr);
}
static int dat_write(void) { char p[1200]; store_path(p, sizeof p, "sfa3r.dat"); return write_atomic(p, dat_body); }

static int hexval(int ch) { return isdigit(ch) ? ch - '0' : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : -1; }
static int dat_read(void) {
    char p[1200]; store_path(p, sizeof p, "sfa3r.dat");
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    char line[512];
    uint8_t *img = NULL; const Piece *cur = NULL; uint32_t at = 0;
    int got_sys = 0, got_wtr = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (line[0] == '[') {
            char what[32], name[64]; cur = NULL;
            if (sscanf(line, "[%31s %63s", what, name) != 2) continue;
            const Piece *list = !strcmp(what, "system") ? sys_pieces : !strcmp(what, "worldtour") ? wtr_pieces : NULL;
            size_t n = list == sys_pieces ? sizeof sys_pieces / sizeof *sys_pieces : 1;
            if (!list) continue;
            for (size_t i = 0; i < n; i++) if (!strcmp(list[i].name, name)) cur = &list[i];
            if (!cur) { rt_log("store: unknown block %s %s in sfa3r.dat", what, name); continue; }
            if (list == sys_pieces) { if (!got_sys++) header(store_sys, SYS_SIZE); img = store_sys; }
            else { if (!got_wtr++) header(store_wtr, WTR_SIZE); img = store_wtr; }
            at = 0;
            continue;
        }
        if (!cur) continue;
        for (char *s = line; s[0] && s[1]; ) {
            int hi = hexval(s[0]), lo = hexval(s[1]);
            if (hi < 0 || lo < 0) { s++; continue; }
            if (at < cur->len) img[cur->off + at++] = (uint8_t)(hi << 4 | lo);
            s += 2;
        }
    }
    fclose(f);
    store_have_sys = got_sys > 0; store_have_wtr = got_wtr > 0;
    rt_log("store: read sfa3r.dat (system data %s, World Tour %s)", store_have_sys ? "yes" : "no", store_have_wtr ? "yes" : "no");
    return 1;
}

/* ---------------------------------------------------------------- PSP save folders: IMPORT / EXPORT */
/* A PSP (or PPSSPP) keeps each save in SAVEDATA/<game><save>/: the data file, encrypted with the
 * game's key (crypt mode 3, psp_savecrypt.c), PARAM.SFO (titles, the file's hash, its own hashes) and
 * ICON0.PNG. The memory stick folder of the earlier SFA3R versions has the data files unencrypted
 * and no PARAM.SFO; both kinds are read. */
static const uint8_t game_key[16] = { 0x00, 0x6F, 0xAF, 0x01, 0xD5, 0x70, 0x00, 0x02, 0xC7, 0x12, 0x24, 0x2E, 0xBA, 0x83, 0xFF, 0xE1 };
#define CRYPT_MODE 3                                 /* SDK 2.5, secure version 0, with a key */
#define PARENTAL 5
#define ICON0_AT 0x08AD51D0u                         /* the save icon the game passes (in its program image) */
typedef struct { const char *dir, *file, *title; uint8_t *img; uint32_t size; int *have; } Save;
static Save saves[2] = {
    { "ULUS100620000", "STFALPH3.SYS", "System Data\nOption, Ranking, Entry Data, etc", store_sys, SYS_SIZE, &store_have_sys },
    { "ULUS100620001", "STFALPH3.WTR", "World Tour Data\nWorld Tour Mode and Edit Mode Character Data", store_wtr, WTR_SIZE, &store_have_wtr },
};

static uint8_t *slurp(const char *path, long *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 0x100000) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)n + 16);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f); *len = n; return b;
}

/* one save from DIR/<save dir>: into OUT (size bytes). 1 done, 0 not there, -1 unreadable */
static int import_one(const char *dir, const Save *sv, uint8_t *out) {
    char p[1300]; long n = 0, sn = 0;
    snprintf(p, sizeof p, "%s/%s/%s", dir, sv->dir, sv->file);
    uint8_t *data = slurp(p, &n);
    if (!data) return 0;
    snprintf(p, sizeof p, "%s/%s/PARAM.SFO", dir, sv->dir);
    uint8_t *sfo = slurp(p, &sn);
    int mode = 0; const uint8_t *hash = NULL;
    if (sfo) {
        uint32_t l; const uint8_t *prm = psp_sfo_find(sfo, (uint32_t)sn, "SAVEDATA_PARAMS", &l);
        if (prm && l) mode = prm[0] == 0x01 ? 1 : prm[0] == 0x21 ? 3 : prm[0] == 0x41 ? 5 : prm[0] ? 1 : 0;
        const uint8_t *list = psp_sfo_find(sfo, (uint32_t)sn, "SAVEDATA_FILE_LIST", &l);
        for (uint32_t i = 0; list && i + 32 <= l; i += 32) if (!strncmp((const char *)list + i, sv->file, 13)) { hash = list + i + 13; break; }
        if (hash) { int z = 1; for (int i = 0; i < 16; i++) z &= hash[i] == 0; if (z) hash = NULL; }
    }
    int ok = 0;
    if ((uint32_t)n == sv->size && !mode) { memcpy(out, data, sv->size); ok = 1; }
    else if (mode) {
        int m = psp_decrypt(mode, mode > 1 ? game_key : NULL, data, (int)n, hash);
        if (m == -8) { rt_log("store: %s: its hash does not match; read anyway", p); m = psp_decrypt(mode, mode > 1 ? game_key : NULL, data, (int)n, NULL); }
        if (m == (int)sv->size) { memcpy(out, data, sv->size); ok = 1; }
    } else if ((uint32_t)n == sv->size + 0x10) {         /* encrypted, PARAM.SFO missing: try the game's mode */
        int m = psp_decrypt(CRYPT_MODE, game_key, data, (int)n, NULL);
        if (m == (int)sv->size) { memcpy(out, data, sv->size); ok = 1; }
    }
    if (ok && (out[MAGIC_AT] | out[MAGIC_AT + 1] << 8) != MAGIC) ok = 0;
    rt_log("store: %s/%s/%s: %s%s", dir, sv->dir, sv->file, ok ? "read" : "not readable", mode ? " (encrypted)" : "");
    free(data); free(sfo);
    return ok ? 1 : -1;
}

/* the saves in DIR (a SAVEDATA folder, or one holding PSP/SAVEDATA or SAVEDATA): how many were read */
int store_import_dir(const char *dir) {
    static const char *const sub[3] = { "", "/SAVEDATA", "/PSP/SAVEDATA" };
    static uint8_t tmp[SYS_SIZE];
    int n = 0;
    for (int s = 0; s < 3 && !n; s++) {
        char d[1200]; snprintf(d, sizeof d, "%s%s", dir, sub[s]);
        for (int i = 0; i < 2; i++) if (import_one(d, &saves[i], tmp) == 1) { memcpy(saves[i].img, tmp, saves[i].size); *saves[i].have = 1; n++; }
    }
    return n;
}

static int put_file(const char *path, const uint8_t *b, uint32_t n) {
    FILE *f = fopen(path, "wb"); if (!f) return 0;
    int ok = fwrite(b, 1, n, f) == n; fclose(f); return ok;
}
static void make_dir(const char *p) {
#ifdef _WIN32
    _mkdir(p);
#else
    mkdir(p, 0755);
#endif
}
static uint32_t png_size(const uint8_t *b, uint32_t max) {   /* a PNG's length, through IEND; 0 if not one */
    if (max < 8 || memcmp(b, "\x89PNG\r\n\x1a\n", 8)) return 0;
    uint32_t at = 8;
    while (at + 12 <= max) {
        uint32_t l = (uint32_t)b[at] << 24 | (uint32_t)b[at + 1] << 16 | (uint32_t)b[at + 2] << 8 | b[at + 3];
        if (l > max - at - 12) return 0;
        int end = !memcmp(b + at + 4, "IEND", 4);
        at += 12 + l;
        if (end) return at;
    }
    return 0;
}

/* the saves, as a PSP keeps them, into DIR/<save dir>/: how many were written */
int store_export_dir(const char *dir) {
    store_sync_live();
    make_dir(dir);
    int n = 0;
    uint32_t icon_len = png_size(HOST(ICON0_AT), 0x10000);
    for (int i = 0; i < 2; i++) {
        const Save *sv = &saves[i];
        if (!*sv->have) continue;
        char d[1200], p[1300]; snprintf(d, sizeof d, "%s/%s", dir, sv->dir); make_dir(d);
        uint8_t hash[16]; int len = 0;
        static uint8_t img[SYS_SIZE > WTR_SIZE ? SYS_SIZE : WTR_SIZE];
        memcpy(img, sv->img, sv->size);
        extern int rt_widescreen;                        /* widescreen.c: WIDE FORCED is WIDE to the PSP */
        if (i == 0 && rt_widescreen) img[sys_pieces[0].off + 0x58] = img[sys_pieces[0].off + 0x62] = 1;
        uint8_t *enc = psp_encrypt(CRYPT_MODE, game_key, img, (int)sv->size, &len, hash);
        if (!enc) { rt_log("store: could not encrypt %s", sv->file); continue; }
        static uint8_t list[99 * 32]; memset(list, 0, sizeof list);
        memcpy(list, sv->file, strlen(sv->file)); memcpy(list + 13, hash, 16);
        static uint8_t params[128]; memset(params, 0, sizeof params);
        char title[64], detail[128]; const char *nl = strchr(sv->title, '\n');
        snprintf(title, sizeof title, "%.*s", (int)(nl - sv->title), sv->title); snprintf(detail, sizeof detail, "%s", nl + 1);
        SfoValue v[] = {                                  /* sorted by key */
            { "CATEGORY", 0, 0, "MS", NULL, 0, 4 }, { "PARENTAL_LEVEL", 1, PARENTAL, NULL, NULL, 0, 4 },
            { "SAVEDATA_DETAIL", 0, 0, detail, NULL, 0, 1024 }, { "SAVEDATA_DIRECTORY", 0, 0, sv->dir, NULL, 0, 64 },
            { "SAVEDATA_FILE_LIST", 0, 0, NULL, list, sizeof list, sizeof list }, { "SAVEDATA_PARAMS", 0, 0, NULL, params, 128, 128 },
            { "SAVEDATA_TITLE", 0, 0, title, NULL, 0, 128 }, { "TITLE", 0, 0, "STREET FIGHTER ALPHA3 MAX", NULL, 0, 128 },
        };
        uint32_t sfo_len; uint8_t *sfo = psp_sfo_write(v, (int)(sizeof v / sizeof *v), CRYPT_MODE, &sfo_len);
        snprintf(p, sizeof p, "%s/%s", d, sv->file); int ok = put_file(p, enc, (uint32_t)len);
        snprintf(p, sizeof p, "%s/PARAM.SFO", d); ok &= sfo && put_file(p, sfo, sfo_len);
        if (icon_len) { snprintf(p, sizeof p, "%s/ICON0.PNG", d); put_file(p, HOST(ICON0_AT), icon_len); }
        free(enc); free(sfo);
        if (ok) n++;
        rt_log("store: exported %s to %s%s", sv->file, d, ok ? "" : " (failed)");
    }
    return n;
}

void settings_write(void);
int store_import(const char *dir) {                  /* IMPORT: read and keep (the game then LOADs it: exsetting.c) */
    int n = store_import_dir(dir);
    if (n) { dat_write(); settings_write(); }
    return n;
}

/* ---------------------------------------------------------------- start up, and the settings in the options piece */
uint8_t *store_options(void) { return store_have_sys ? store_sys + sys_pieces[0].off : NULL; }
void store_sync_live(void) {                         /* the game's current options into our image */
    if (live && store_have_sys) memcpy(store_sys + sys_pieces[0].off, HOST(sys_pieces[0].mem), sys_pieces[0].len);
}
int store_is_live(void) { return live; }

void cfg_apply_pending(void);                        /* settings.c: settings read before there was an image */
void settings_write(void);

/* called by settings_load, before the settings are read */
void store_load_files(void) {
    if (dat_read()) return;
    char p[1200]; snprintf(p, sizeof p, "%s/PSP/SAVEDATA", rt_save_root);
    int n = store_import_dir(p);
    if (n) { rt_log("store: imported %d save file(s) from %s", n, p); dat_write(); }
}

/* ---------------------------------------------------------------- the game's loads and saves (hle_sys.c) */
long sysdata_default(const char *file, uint32_t buf, uint32_t cap);   /* sysdata.c */

/* a load of FILE into BUF (room CAP): its size, or -1 (no such save) */
static uint8_t default_sys[SYS_SIZE];                /* the game's own defaults, for DELETE */
static int have_default;
extern int sysdata_quiet;
long store_load(const char *file, uint32_t buf, uint32_t cap) {
    int sys = is_sys(file);
    uint8_t *img = sys ? store_sys : store_wtr; uint32_t size = sys ? SYS_SIZE : WTR_SIZE;
    int have = sys ? store_have_sys : store_have_wtr;
    if (cap < size || buf < USER_LO || buf + size > USER_HI) return -1;
    if (sys && !have_default) {                       /* the first load: the game's memory still holds its defaults */
        sysdata_quiet = 1;
        if (sysdata_default(file, buf, cap) == (long)size) { memcpy(default_sys, HOST(buf), size); have_default = 1; }
        sysdata_quiet = 0;
    }
    if (!have) {
        long n = sysdata_default(file, buf, cap);   /* first start: the game's defaults */
        if (n != (long)size) return -1;
        memcpy(img, HOST(buf), size);
        if (sys) { store_have_sys = 1; cfg_apply_pending(); } else store_have_wtr = 1;
        dat_write();
        if (sys) settings_write();
    }
    memcpy(HOST(buf), img, size);
    if (sys) live = 1;
    return size;
}

/* a save of FILE from BUF (N bytes) */
int store_save(const char *file, uint32_t buf, uint32_t n) {
    int sys = is_sys(file);
    uint32_t size = sys ? SYS_SIZE : WTR_SIZE;
    if (n != size || buf < USER_LO || buf + size > USER_HI) { rt_log("store: %s of %u bytes not saved", file, n); return 0; }
    memcpy(sys ? store_sys : store_wtr, HOST(buf), size);
    if (sys) store_have_sys = 1; else store_have_wtr = 1;
    int ok = dat_write();
    if (sys) settings_write();
    rt_log("store: the game saved %s%s", file, ok ? "" : " (not written!)");
    return ok;
}

/* does save NAME (the part after the game's name: 0000 system data, 0001 World Tour) exist? -1: not ours */
int store_exists(const char *name) {
    size_t l = strlen(name);
    if (l >= 4 && !strcmp(name + l - 4, "0000")) return store_have_sys;
    if (l >= 4 && !strcmp(name + l - 4, "0001")) return store_have_wtr;
    return -1;                                       /* not one of ours */
}

/* SAVE LOAD > DELETE: the system data back to the game's defaults (World Tour kept), and SFA3R's own
   settings too; the game then LOADs it (exsetting.c) */
extern int rt_join_in[3], rt_couch_coop, rt_l2[4], rt_r2[4];
extern uint8_t rt_p4_config[20];
extern const uint8_t pcfg_default[20];
int store_delete_sys(void) {
    if (!have_default) return 0;
    memcpy(store_sys, default_sys, SYS_SIZE); store_have_sys = 1;
    rt_join_in[0] = rt_join_in[1] = rt_join_in[2] = 0; rt_couch_coop = 1;
    for (int p = 0; p < 4; p++) { rt_l2[p] = 6; rt_r2[p] = 7; }
    memcpy(rt_p4_config, pcfg_default, sizeof rt_p4_config);
    int ok = dat_write();
    settings_write();
    rt_log("store: system data deleted (back to the defaults)");
    return ok;
}

/* the program is closing: keep the settings as they are now */
void store_shutdown(void) { store_sync_live(); if (store_have_sys) dat_write(); settings_write(); }
