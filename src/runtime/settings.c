/* sfa3mr.cfg: every setting, by name, in sections. Written by the program (OPTION MODE screens, the
 * game's saves, closing); it can also be edited by hand while the program is closed.
 *
 *   [ex setting]     SFA3MR's own options (OPTION MODE > EX. SETTING; JOIN-IN: docs/PLAYERS.md)
 *   [game option]    OPTION MODE > GAME OPTION
 *   [screen setting] OPTION MODE > SCREEN SETTING
 *   [p1 config] .. [p4 config]   OPTION MODE > PLAYER CONFIG: each button's attack (names as on the
 *                    screen, or 0-54), COMMAND, TYPE and RAPID. L2 / R2 and player 4 are SFA3MR's own
 *   [progress]       unlock bits (all held on by sysdata.c)
 *
 * The game keeps the settings in its options block (0x08A058A0, the system data's first piece:
 * savestore.c); here each one is a byte (or two) in it:
 *   +0x3C difficulty 0-7   +0x3D time (30 60 99 INFINITE)   +0x3E rounds (1 3 5)
 *   +0x40 damage 0-3 (and +0x42)   +0x43 speed TURBO1-7 (0: NORMAL / FREE1 / FREE2 by +0x44)
 *   +0x45 S.C. gauge (0 MAX)   +0x58 screen size (NORMAL WIDE, and +0x62)   +0x1FC BGM volume 0-15
 *   +0x1FD S.E. volume   +0x1FE wallpaper (NONE 01-10)   +0x1FF player side (1P 2P)
 *   +0x204 mode select (OFF ON)   +0x52 unlocks   +0x200 ISM-Pluses   +20 * p: player p's buttons
 */
#include "rt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

int rt_vsync = 1;                             /* [screen setting] vsync: wait for the monitor (no tearing); ON when missing */
int rt_join_in[3] = { 0, 0, 0 }, rt_couch_coop = 1;   /* EX. SETTING: 1P / 2P / 3P JOIN-IN, HUMAN (1) or CPU (0): docs/PLAYERS.md */
extern int rt_l2[4], rt_r2[4];
extern uint8_t rt_p4_config[20];

void store_path(char *out, size_t n, const char *name);
void store_load_files(void);
uint8_t *store_options(void);
void store_sync_live(void);
int store_is_live(void);

/* ---------------------------------------------------------------- names */
static const char *const off_on[] = { "OFF", "ON", NULL };
static const char *const v_human[] = { "CPU", "HUMAN", NULL };
static const char *const v_time[] = { "30", "60", "99", "INFINITE", NULL };
static const char *const v_rounds[] = { "1", "3", "5", NULL };
static const char *const v_gauge[] = { "0", "MAX", NULL };
static const char *const v_side[] = { "1P", "2P", NULL };
static const char *const v_size[] = { "NORMAL", "WIDE", "WIDE FORCED", NULL };
static const char *const v_wall[] = { "NONE", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", NULL };
static const char *const v_speed[] = { "NORMAL", "FREE1", "FREE2", "TURBO1", "TURBO2", "TURBO3", "TURBO4", "TURBO5", "TURBO6", "TURBO7", NULL };
static const char *const v_command[] = { "ARCADE", "NORMAL", "LONG", NULL };
static const char *const v_type[] = { "NORMAL", "EASY", NULL };
#define N_ATTACKS 55
static char attack_name[N_ATTACKS][16];
static void make_names(void) {
    static const char *const first[15] = { "L PUNCH", "M PUNCH", "H PUNCH", "L KICK", "M KICK", "H KICK", "PPP", "KKK", "PP", "KK",
        "LP + LK", "MP + MK", "HP + HK", "NO USE", "TAUNT" };
    for (int i = 0; i < 15; i++) strcpy(attack_name[i], first[i]);
    for (int i = 0; i < 24; i++) snprintf(attack_name[15 + i], 16, "SPECIAL%d %c", i / 3 + 1, "LMH"[i % 3]);
    for (int i = 0; i < 15; i++) snprintf(attack_name[39 + i], 16, "S.COMBO%d LV%d", i / 3 + 1, i % 3 + 1);
    strcpy(attack_name[54], "VARIABLE 1");
}
/* a value: one of NAMES (any case, spaces ignored), or its number */
static int squash_eq(const char *a, const char *b) {
    for (;;) {
        while (*a == ' ') a++;
        while (*b == ' ') b++;
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
        if (!*a) return 1;
        a++; b++;
    }
}
static int lookup(const char *v, const char *const *names, int n) {
    for (int i = 0; i < n && names[i]; i++) if (squash_eq(v, names[i])) return i;
    char *end; long x = strtol(v, &end, 0);
    while (*end == ' ') end++;
    return (end != v && !*end && x >= 0 && x < n) ? (int)x : -1;
}
static int count(const char *const *names) { int n = 0; while (names[n]) n++; return n; }

/* ---------------------------------------------------------------- the settings in the options block */
enum { K_ENUM, K_STARS, K_NUM, K_SPEED, K_HEX16, K_HEX32, K_SIZE };
extern int rt_widescreen;                    /* widescreen.c: WIDE FORCED */
extern const int rt_widescreen_available;
typedef struct { const char *section, *key; int off, mirror, kind; const char *const *names; int max; const char *note; } Field;
static const Field fields[] = {
    { "game option", "difficulty", 0x3C, -1, K_STARS, NULL, 8, "stars, 1 - 8" },
    { "game option", "time", 0x3D, -1, K_ENUM, v_time, 0, "30, 60, 99 or INFINITE" },
    { "game option", "rounds", 0x3E, -1, K_ENUM, v_rounds, 0, "1, 3 or 5" },
    { "game option", "damage", 0x40, 0x42, K_STARS, NULL, 4, "stars, 1 - 4" },
    { "game option", "speed", 0x43, 0x44, K_SPEED, v_speed, 0, "NORMAL, FREE1, FREE2, TURBO1 - TURBO7" },
    { "game option", "sc_gauge", 0x45, -1, K_ENUM, v_gauge, 0, "0 or MAX" },
    { "game option", "mode_select", 0x204, -1, K_ENUM, off_on, 0, "OFF or ON" },
    { "game option", "player_side", 0x1FF, -1, K_ENUM, v_side, 0, "1P or 2P" },
    { "game option", "bgm_volume", 0x1FC, -1, K_NUM, NULL, 15, "0 - 15" },
    { "game option", "se_volume", 0x1FD, -1, K_NUM, NULL, 15, "0 - 15" },
    { "screen setting", "screen_size", 0x58, 0x62, K_SIZE, v_size, 0, "NORMAL, WIDE or WIDE FORCED (16:9 patch)" },
    { "screen setting", "wallpaper", 0x1FE, -1, K_ENUM, v_wall, 0, "NONE or 01 - 10" },
    { "progress", "unlocks", 0x52, -1, K_HEX16, NULL, 0, "modes cleared (hex); all held on" },
    { "progress", "ism_pluses", 0x200, -1, K_HEX32, NULL, 0, "ISM-Pluses earned, a bit each (hex); all held on" },
};
#define N_FIELDS (sizeof fields / sizeof *fields)
static const char *const button_key[9] = { "square", "triangle", "l", "cross", "circle", "r", "select", "l2", "r2" };

static int field_get(const uint8_t *o, const Field *f) {
    switch (f->kind) {
    case K_SPEED: return o[f->off] ? 2 + o[f->off] : o[f->mirror];
    case K_HEX16: return o[f->off] | o[f->off + 1] << 8;
    case K_HEX32: return (int)((uint32_t)o[f->off] | (uint32_t)o[f->off + 1] << 8 | (uint32_t)o[f->off + 2] << 16 | (uint32_t)o[f->off + 3] << 24);
    case K_SIZE: return o[f->off] ? 1 : rt_widescreen ? 2 : 0;
    default: return o[f->off];
    }
}
static void field_set(uint8_t *o, const Field *f, int v) {
    switch (f->kind) {
    case K_SPEED: if (v > 2) { o[f->off] = (uint8_t)(v - 2); o[f->mirror] = 0; } else { o[f->off] = 0; o[f->mirror] = (uint8_t)v; } return;
    case K_HEX16: o[f->off] = (uint8_t)v; o[f->off + 1] = (uint8_t)(v >> 8); return;
    case K_HEX32: for (int i = 0; i < 4; i++) o[f->off + i] = (uint8_t)((uint32_t)v >> (8 * i)); return;
    case K_SIZE: if (v == 2 && !rt_widescreen_available) v = 0;
                 rt_widescreen = v == 2; o[f->off] = o[f->mirror] = (uint8_t)(v == 1); return;
    default: o[f->off] = (uint8_t)v; if (f->mirror >= 0) o[f->mirror] = (uint8_t)v; return;
    }
}
static void field_text(const Field *f, int v, char *out, size_t n) {
    switch (f->kind) {
    case K_STARS: snprintf(out, n, "%d", v + 1); break;
    case K_NUM: snprintf(out, n, "%d", v); break;
    case K_HEX16: snprintf(out, n, "%04X", v & 0xFFFF); break;
    case K_HEX32: snprintf(out, n, "%08X", (uint32_t)v); break;
    default: { int c = count(f->names); snprintf(out, n, "%s", v >= 0 && v < c ? f->names[v] : "?"); }
    }
}
static int field_parse(const Field *f, const char *s) {
    char *end;
    switch (f->kind) {
    case K_STARS: { long x = strtol(s, &end, 10); return end != s && x >= 1 && x <= f->max ? (int)x - 1 : -1; }
    case K_NUM: { long x = strtol(s, &end, 10); return end != s && x >= 0 && x <= f->max ? (int)x : -1; }
    case K_HEX16: case K_HEX32: { unsigned long x = strtoul(s, &end, 16); return end != s ? (int)x : -1; }
    default: return lookup(s, f->names, count(f->names));
    }
}

/* settings read before there is an options block (no save yet): applied when there is one */
static struct { int field_v[N_FIELDS]; int btn[3][9]; int cmd[3][3]; } pend;
static int pending;

/* ---------------------------------------------------------------- reading */
static void trim(char *s) {
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    char *b = s; while (*b == ' ' || *b == '\t') b++;
    if (b != s) memmove(s, b, strlen(b) + 1);
}
static int is_on(const char *v) { int i = lookup(v, off_on, 2); return i < 0 ? atoi(v) != 0 : i; }

static void set_button(int p, int b, int v) {             /* player p (0-3), button b (0-8), attack v */
    if (v < 0) return;
    if (b == 7) { rt_l2[p] = v; return; }
    if (b == 8) { rt_r2[p] = v; return; }
    if (p == 3) { rt_p4_config[2 * b] = (uint8_t)v; rt_p4_config[2 * b + 1] = 0; return; }
    uint8_t *o = store_options();
    if (o) { o[20 * p + 2 * b] = (uint8_t)v; o[20 * p + 2 * b + 1] = 0; } else { pend.btn[p][b] = v; pending = 1; }
}
static void set_cmd(int p, int k, int v) {                /* COMMAND / TYPE / RAPID */
    if (v < 0) return;
    if (p == 3) { rt_p4_config[0x10 + k] = (uint8_t)v; return; }
    uint8_t *o = store_options();
    if (o) o[20 * p + 0x10 + k] = (uint8_t)v; else { pend.cmd[p][k] = v; pending = 1; }
}

void cfg_apply_pending(void) {
    uint8_t *o = store_options();
    if (!o || !pending) return;
    for (size_t i = 0; i < N_FIELDS; i++) if (pend.field_v[i] >= 0) field_set(o, &fields[i], pend.field_v[i]);
    for (int p = 0; p < 3; p++) {
        for (int b = 0; b < 7; b++) if (pend.btn[p][b] >= 0) { o[20 * p + 2 * b] = (uint8_t)pend.btn[p][b]; o[20 * p + 2 * b + 1] = 0; }
        for (int k = 0; k < 3; k++) if (pend.cmd[p][k] >= 0) o[20 * p + 0x10 + k] = (uint8_t)pend.cmd[p][k];
    }
    pending = 0;
}

void settings_load(void) {
    make_names();
    memset(&pend, 0xFF, sizeof pend);
    store_load_files();
    char path[1100]; store_path(path, sizeof path, "sfa3mr.cfg");
    FILE *f = fopen(path, "r");
    if (f) {
        char line[256], section[64] = "ex setting";
        while (fgets(line, sizeof line, f)) {
            char *hash = strchr(line, '#'); if (hash) *hash = 0;
            trim(line);
            if (!line[0]) continue;
            if (line[0] == '[') {
                char *e = strchr(line, ']'); if (e) *e = 0;
                snprintf(section, sizeof section, "%.63s", line + 1); trim(section);
                for (char *s = section; *s; s++) *s = (char)tolower((unsigned char)*s);
                continue;
            }
            char *eq = strchr(line, '='); if (!eq) continue;
            *eq = 0; char *key = line, *val = eq + 1; trim(key); trim(val);
            for (char *s = key; *s; s++) *s = (char)tolower((unsigned char)*s);
            /* SFA3MR's options (and the flat names of the first versions) */
            if (key[0] == 'p' && key[1] >= '1' && key[1] <= '3' && (!strcmp(key + 2, "_join_in") || !strcmp(key + 2, "_override"))) {   /* (_override: before 0.5.x) */
                int v = lookup(val, v_human, 2);              /* HUMAN / CPU (or the first versions' ON / OFF) */
                rt_join_in[key[1] - '1'] = v >= 0 ? v : is_on(val);
                continue;
            }
            if (!strcmp(key, "couch_coop")) { rt_couch_coop = is_on(val); continue; }
            if (!strcmp(key, "vsync")) { rt_vsync = is_on(val); continue; }
            if ((!strncmp(key, "l2_p", 4) || !strncmp(key, "r2_p", 4)) && key[4] >= '1' && key[4] <= '4' && !key[5]) {
                int v = atoi(val); if (v >= 0 && v < N_ATTACKS) set_button(key[4] - '1', key[0] == 'l' ? 7 : 8, v);
                continue;
            }
            if (!strcmp(key, "p4_config")) {
                for (int i = 0; i < 20 && val[2 * i] && val[2 * i + 1]; i++) { char h[3] = { val[2 * i], val[2 * i + 1], 0 }; rt_p4_config[i] = (uint8_t)strtol(h, NULL, 16); }
                continue;
            }
            /* the game's settings */
            int done = 0;
            for (size_t i = 0; i < N_FIELDS && !done; i++) if (!strcmp(section, fields[i].section) && !strcmp(key, fields[i].key)) {
                int v = field_parse(&fields[i], val); done = 1;
                if (v < 0 && fields[i].kind != K_HEX32) { rt_log("settings: %s = %s not understood", key, val); break; }
                uint8_t *o = store_options();
                if (o) field_set(o, &fields[i], v); else { pend.field_v[i] = v; pending = 1; }
            }
            if (done) continue;
            if (strlen(section) == 9 && section[0] == 'p' && section[1] >= '1' && section[1] <= '4' && !strcmp(section + 2, " config")) {
                int p = section[1] - '1';
                for (int b = 0; b < 9 && !done; b++) if (!strcmp(key, button_key[b])) {
                    const char *names[N_ATTACKS + 1]; for (int i = 0; i < N_ATTACKS; i++) names[i] = attack_name[i]; names[N_ATTACKS] = NULL;
                    int v = lookup(val, names, N_ATTACKS);
                    if (v < 0 && squash_eq(val, "CHOUHATU")) v = 14;
                    if (v < 0) rt_log("settings: [%s] %s = %s not understood", section, key, val);
                    set_button(p, b, v); done = 1;
                }
                if (!strcmp(key, "command")) { set_cmd(p, 0, lookup(val, v_command, 3)); done = 1; }
                if (!strcmp(key, "type")) { set_cmd(p, 1, lookup(val, v_type, 2)); done = 1; }
                if (!strcmp(key, "rapid")) { set_cmd(p, 2, lookup(val, off_on, 2)); done = 1; }
            }
            if (!done) rt_log("settings: [%s] %s not known", section, key);
        }
        fclose(f);
    }
    rt_log("settings: 1P %s, 2P %s, 3P %s, couch co-op %d", v_human[!!rt_join_in[0]], v_human[!!rt_join_in[1]], v_human[!!rt_join_in[2]], rt_couch_coop);
}

/* ---------------------------------------------------------------- writing */
static void write_fields(FILE *f, const uint8_t *o, const char *only, int not) {
    const char *sect = "";
    for (size_t i = 0; i < N_FIELDS; i++) {
        if ((strcmp(fields[i].section, only) == 0) == not) continue;
        int v = o ? field_get(o, &fields[i]) : pend.field_v[i];
        if (!o && v < 0) continue;
        if (strcmp(sect, fields[i].section)) { fprintf(f, "\n[%s]\n", fields[i].section); sect = fields[i].section; }
        char t[32]; field_text(&fields[i], v, t, sizeof t);
        char line[64]; snprintf(line, sizeof line, "%s=%s", fields[i].key, t);
        fprintf(f, "%-24s # %s\n", line, fields[i].note);
    }
}
static void cfg_body(FILE *f) {
    const uint8_t *o = store_options();
    fprintf(f, "# SFA3MR settings. Written by the program; it can be edited while the program is closed.\n"
               "# The game's own data (records, World Tour, ...) is in sfa3mr.dat; these settings win over it.\n"
               "# Button settings: see docs/PLAYER_CONFIG.md.\n");
    fprintf(f, "\n[ex setting]\n");
    for (int k = 0; k < 3; k++) { char l[48]; snprintf(l, sizeof l, "p%d_join_in=%s", k + 1, v_human[!!rt_join_in[k]]); fprintf(f, "%-24s # HUMAN: pad %d plays a fighter nobody started with; CPU: the computer does\n", l, k + 1); }
    fprintf(f, "couch_coop=%s\n", off_on[!!rt_couch_coop]);
    write_fields(f, o, "progress", 1);         /* ends with [screen setting] */
    { char l[48]; snprintf(l, sizeof l, "vsync=%s", off_on[!!rt_vsync]); fprintf(f, "%-24s # ON: wait for the monitor, no tearing; OFF: never wait\n", l); }
    for (int p = 0; p < 4; p++) {
        const uint8_t *c = p == 3 ? rt_p4_config : o ? o + 20 * p : NULL;
        fprintf(f, "\n[p%d config]\n", p + 1);
        for (int b = 0; b < 9; b++) {
            int a = b == 7 ? rt_l2[p] : b == 8 ? rt_r2[p] : c ? c[2 * b] : pend.btn[p][b];
            if (a >= 0 && a < N_ATTACKS) fprintf(f, "%s=%s\n", button_key[b], attack_name[a]);
        }
        int cm = c ? c[0x10] : pend.cmd[p][0], ty = c ? c[0x11] : pend.cmd[p][1], ra = c ? c[0x12] : pend.cmd[p][2];
        if (cm >= 0 && cm < 3) fprintf(f, "command=%s\n", v_command[cm]);
        if (ty >= 0 && ty < 2) fprintf(f, "type=%s\n", v_type[ty]);
        if (ra >= 0 && ra < 2) fprintf(f, "rapid=%s\n", off_on[ra]);
    }
    write_fields(f, o, "progress", 0);
}
static int write_cfg(const char *path) {
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { rt_log("settings: cannot write %s", tmp); return 0; }
    cfg_body(f);
    int ok = !ferror(f); fclose(f);
    if (!ok) { remove(tmp); return 0; }
    remove(path);
    return rename(tmp, path) == 0;
}
void settings_write(void) { char p[1100]; store_path(p, sizeof p, "sfa3mr.cfg"); write_cfg(p); }

/* our screens changed something: the game's current settings and ours */
void settings_save(void) { store_sync_live(); settings_write(); }
