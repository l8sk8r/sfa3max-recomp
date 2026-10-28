/* IoFileMgrForUser, StdioForUser and sceUmdUser: the disc is the extracted ISO folder, ms0: is saves/. */
#include "rt.h"
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define fseeko _fseeki64
#define ftello _ftelli64
#endif

char rt_disc_root[1024];
char rt_save_root[1024];

typedef struct { FILE *f; char path[512]; int64_t async_result; int async_pending; } Fd;
#define MAX_FD 64
static Fd fds[MAX_FD];

/* PSP path (disc0:/PSP_GAME/...) to host path. Returns 0 if the device is unknown. */
static int host_path(const char *psp, char *out, size_t n) {
    const char *colon = strchr(psp, ':');
    if (!colon) return 0;
    size_t dl = (size_t)(colon - psp);
    char dev[16] = {0};
    if (dl >= sizeof dev) return 0;
    for (size_t i = 0; i < dl; i++) dev[i] = (char)tolower((unsigned char)psp[i]);
    const char *root;
    if (!strncmp(dev, "disc", 4) || !strncmp(dev, "umd", 3)) root = rt_disc_root;
    else if (!strncmp(dev, "ms", 2) || !strncmp(dev, "fatms", 5)) root = rt_save_root;
    else return 0;
    const char *rest = colon + 1;
    while (*rest == '/' || *rest == '\\') rest++;
    snprintf(out, n, "%s/%s", root, rest);
    return 1;
}

static FILE *open_ci(const char *path, const char *mode) {
    FILE *f = fopen(path, mode);
    if (f) return f;
    /* the disc is case-insensitive; try lower and upper case of the last component */
    char tmp[1024]; strncpy(tmp, path, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
    char *slash = strrchr(tmp, '/');
    char *p = slash ? slash + 1 : tmp;
    for (char *q = p; *q; q++) *q = (char)tolower((unsigned char)*q);
    if ((f = fopen(tmp, mode))) return f;
    for (char *q = p; *q; q++) *q = (char)toupper((unsigned char)*q);
    return fopen(tmp, mode);
}

void rt_io_init(void) {}

void hle_sceIoOpen(Cpu *c) {
    const char *p = rt_str(ARG(0)); uint32_t flags = ARG(1);
    char hp[1024];
    if (!p || !host_path(p, hp, sizeof hp)) { TRACE("sceIoOpen %s: unknown device", p ? p : "(null)"); RET(ERR_FILE_NOT_FOUND); }
    const char *mode = "rb";
    if (flags & 0x0002) mode = (flags & 0x0200) ? ((flags & 0x0400) ? "w+b" : "r+b") : "r+b";
    if ((flags & 0x0002) && (flags & 0x0400)) mode = "w+b";
    if ((flags & 0x0002) && (flags & 0x0100)) mode = "a+b";
    FILE *f = open_ci(hp, mode);
    if (!f && (flags & 0x0200)) f = open_ci(hp, "w+b");
    if (!f) { TRACE("sceIoOpen %s -> not found", p); RET(ERR_FILE_NOT_FOUND); }
    for (int i = 3; i < MAX_FD; i++) if (!fds[i].f) {
        fds[i].f = f; strncpy(fds[i].path, p, sizeof fds[i].path - 1); fds[i].async_pending = 0;
        TRACE("sceIoOpen %s -> fd %d", p, i);
        RET(i);
    }
    fclose(f); RET(0x80020320);
}
void hle_sceIoClose(Cpu *c) {
    uint32_t fd = ARG(0);
    if (fd >= MAX_FD || !fds[fd].f) RET(ERR_BAD_FD);
    fclose(fds[fd].f); fds[fd].f = NULL; RET(0);
}
static int64_t do_read(uint32_t fd, uint32_t buf, uint32_t size) {
    if (fd >= MAX_FD || !fds[fd].f) return (int32_t)ERR_BAD_FD;
    return (int64_t)fread(HOST(buf), 1, size, fds[fd].f);
}
void hle_sceIoRead(Cpu *c) { RET((uint32_t)do_read(ARG(0), ARG(1), ARG(2))); }
void hle_sceIoReadAsync(Cpu *c) {
    uint32_t fd = ARG(0);
    if (fd >= MAX_FD || !fds[fd].f) RET(ERR_BAD_FD);
    fds[fd].async_result = do_read(fd, ARG(1), ARG(2)); fds[fd].async_pending = 1;
    RET(0);
}
void hle_sceIoPollAsync(Cpu *c) {
    uint32_t fd = ARG(0), res = ARG(1);
    if (fd >= MAX_FD || !fds[fd].f) RET(ERR_BAD_FD);
    if (!fds[fd].async_pending) RET(1);          /* nothing pending */
    if (res) { WW(res, (uint32_t)fds[fd].async_result); WW(res + 4, (uint32_t)((uint64_t)fds[fd].async_result >> 32)); }
    fds[fd].async_pending = 0;
    RET(0);
}
void hle_sceIoWrite(Cpu *c) {
    uint32_t fd = ARG(0), buf = ARG(1), size = ARG(2);
    if (fd == 1 || fd == 2) { rt_log("[game] %.*s", (int)size, (char *)HOST(buf)); RET(size); }
    if (fd >= MAX_FD || !fds[fd].f) RET(ERR_BAD_FD);
    RET(fwrite(HOST(buf), 1, size, fds[fd].f));
}
void hle_sceIoLseek(Cpu *c) {
    uint32_t fd = ARG(0); int64_t off = (int64_t)ARG64(2); int whence = (int)ARG(4);
    if (fd >= MAX_FD || !fds[fd].f) RET64((int64_t)(int32_t)ERR_BAD_FD);
    fseeko(fds[fd].f, off, whence == 1 ? SEEK_CUR : whence == 2 ? SEEK_END : SEEK_SET);
    RET64(ftello(fds[fd].f));
}
void hle_sceIoGetstat(Cpu *c) {
    const char *p = rt_str(ARG(0)); uint32_t st = ARG(1);
    char hp[1024]; struct stat s;
    if (!p || !host_path(p, hp, sizeof hp) || stat(hp, &s) != 0) RET(ERR_FILE_NOT_FOUND);
    memset(HOST(st), 0, 0x58);
    int dir = (s.st_mode & S_IFDIR) != 0;
    WW(st + 0, dir ? 0x11FF : 0x21FF);           /* mode */
    WW(st + 4, dir ? 0x10 : 0x20);               /* attr */
    WW(st + 8, (uint32_t)s.st_size); WW(st + 12, (uint32_t)((uint64_t)s.st_size >> 32));
    RET(0);
}
void hle_sceIoRename(Cpu *c) {
    char a[1024], b[1024];
    if (!host_path(rt_str(ARG(0)), a, sizeof a) || !host_path(rt_str(ARG(1)), b, sizeof b)) RET(ERR_FILE_NOT_FOUND);
    RET(rename(a, b) == 0 ? 0 : ERR_FILE_NOT_FOUND);
}
/* Devices: only the memory stick (fatms0:/ms0:) is asked anything; it is always inserted, never full. */
void hle_sceIoDevctl(Cpu *c) {
    const char *dev = rt_str(ARG(0)); uint32_t cmd = ARG(1), argp = ARG(2), argl = ARG(3), outp = ARG(4);
    TRACE("sceIoDevctl %s cmd=%08X", dev ? dev : "", cmd);
    switch (cmd) {
    case 0x02415821:                               /* register insert/eject callback: fire "inserted" now */
        if (argp && argl >= 4) rt_notify_callback(RW(argp), 1);
        RET(0);
    case 0x02415822: RET(0);                       /* unregister it */
    case 0x02415823: RET(0);                       /* set FAT state */
    case 0x02425823: if (outp) WW(outp, 1); RET(0);   /* FAT enabled / stick inserted */
    case 0x02425824: if (outp) WW(outp, 0); RET(0);   /* not write-protected */
    case 0x02425818:                               /* capacity: pointer to {max, free clusters, max sectors, sector size, count} */
        if (argp && argl >= 4) {
            uint32_t d = RW(argp);
            uint32_t clusters = (uint32_t)((uint64_t)1 << 30) / 0x8000u;   /* 1 GB free in 32 KB clusters */
            WW(d, clusters); WW(d + 4, clusters); WW(d + 8, clusters); WW(d + 12, 0x200); WW(d + 16, 0x40);
        }
        RET(0);
    default: RET(0);
    }
}

void hle_sceKernelStdin(Cpu *c) { RET(0); }
void hle_sceKernelStdout(Cpu *c) { RET(1); }
void hle_sceKernelStderr(Cpu *c) { RET(2); }

void hle_sceUmdCheckMedium(Cpu *c) { RET(1); }
void hle_sceUmdActivate(Cpu *c) { RET(0); }
void hle_sceUmdWaitDriveStat(Cpu *c) { RET(0); }
void hle_sceUmdGetDriveStat(Cpu *c) { RET(0x32); }   /* present | ready | readable */
