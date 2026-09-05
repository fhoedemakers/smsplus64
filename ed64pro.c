/*
 * ed64pro.c - EverDrive-64 PRO glue: detection, the "sd:/" filesystem, and
 * the file the Everdrive menu launched us with.
 *
 * See ed64pro.h for why this exists at all rather than going through libcart.
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#include <libdragon.h>
#include <system.h>

#include "ed64pro.h"
#include "ed64pro_drv.h"

#define EDP_PATH_MAX        512
#define EDP_MAX_FILES       4
#define EDP_NAME_MAX        256

/* Large reads are staged through cartridge memory. Below this a file is not
 * worth the round trip and goes straight down the command fifo. */
#define EDP_STAGE_MIN       (32u * 1024u)
#define EDP_STAGE_SIZE      (64u * 1024u)

/* Where in cartridge memory to stage. Not 1 MB as krikzz's sample uses, and
 * not the 2 MB the injected-rom convention uses: build_dfs.sh packs the
 * contents of filesystem/ into the z64, so this rom can itself run past 2 MB
 * and staging there would overwrite the code doing the staging. 16 MB is well
 * clear of that and well below the cartridge's 256 MB, and it is checked at
 * mount time anyway. */
#define EDP_STAGE_FCI       0x01000000u
#define EDP_STAGE_FCI_ALT   0x00400000u

static bool     edp_is_pro     = false;
static bool     edp_probed     = false;
static bool     edp_fast       = false;
static uint32_t edp_edid_value = 0;
static uint32_t edp_stage_base = EDP_STAGE_FCI;

/* --------------------------------------------------------------- errors */

static int edp_to_errno(int res)
{
    switch (res)
    {
    case EDP_OK:                    return 0;
    case EDP_ERR_NO_FILE:
    case EDP_ERR_NO_PATH:
    case EDP_ERR_NULL_PATH:         return ENOENT;
    case EDP_ERR_INVALID_NAME:      return EINVAL;
    case EDP_ERR_DENIED:
    case EDP_ERR_WRITE_PROTECTED:   return EACCES;
    case EDP_ERR_EXIST:             return EEXIST;
    case EDP_ERR_NOT_READY:         return EBUSY;
    case EDP_ERR_LOCKED:            return EPERM;
    case EDP_ERR_NO_FILESYSTEM:     return ENODEV;
    case EDP_ERR_TIMEOUT:
    case EDP_ERR_FAULTED:           return ETIMEDOUT;
    default:                        return EIO;
    }
}

static int edp_fail(int res)
{
    errno = edp_to_errno(res);
    return -1;
}

/* Directory walks are the one place where -1 is not "failed": libdragon reads
 * it as "the directory exists and is empty". Real errors go back as -2. */
static int edp_fail_dir(int res)
{
    errno = edp_to_errno(res);
    return -2;
}

/* ----------------------------------------------------------------- paths
 *
 * The cartridge wants a path relative to the card root: no drive prefix, no
 * leading slash, "" for the root itself. libdragon is not consistent about
 * what it hands a filesystem - open() and unlink() strip the whole prefix
 * while stat(), findfirst(), findnext2() and mkdir() leave a leading slash on
 * - and the browser builds paths as "%s/%s", which yields "sd://name" when it
 * is rooted at the card. Stripping every leading slash and collapsing the
 * rest covers all of it.
 */
static bool edp_norm(const char *in, char *out, size_t cap)
{
    size_t n = 0;

    if (in == NULL) { errno = EINVAL; return false; }

    while (*in)
    {
        if (*in == '/')
        {
            while (*in == '/') in++;
            if (*in == '\0') break;
            if (n && n + 1 < cap) out[n++] = '/';
            continue;
        }
        /* Refuse to walk out of the card root. */
        if (in[0] == '.' && in[1] == '.' && (in[2] == '/' || in[2] == '\0'))
        {
            errno = EINVAL;
            return false;
        }
        while (*in && *in != '/')
        {
            if (n + 1 >= cap) { errno = ENAMETOOLONG; return false; }
            out[n++] = *in++;
        }
    }
    out[n] = '\0';
    return true;
}

/* ----------------------------------------------------------------- files
 *
 * The cartridge has one open file and one loaded directory at a time, for the
 * whole machine. The emulator never needs two at once - the browser opens a
 * rom, reads it and closes it, and the settings file is read and written on
 * its own - but rather than depend on that, read handles are bound to the
 * cartridge lazily and re-bound when they are used again. A second handle is
 * only refused when a write is involved, because re-opening a file that was
 * created with "truncate" would throw away what had just been written.
 */
typedef struct
{
    bool     used;
    bool     writable;
    char     path[EDP_PATH_MAX];
    uint32_t size;
    uint32_t pos;
    uint32_t mcu_pos;
    uint32_t win_off;   /* what of this file is staged in cartridge memory */
    uint32_t win_len;
} EdpFile;

static EdpFile edp_files[EDP_MAX_FILES];
static int     edp_bound = -1;

/* Bumped by anything that could disturb the cartridge's loaded directory, so
 * a directory walk interleaved with file access reloads instead of reading
 * records out of the wrong directory. */
static uint32_t edp_fs_gen = 0;

static int edp_bind(EdpFile *f)
{
    int idx = (int)(f - edp_files);

    if (edp_bound == idx) return EDP_OK;

    if (edp_bound >= 0)
    {
        EdpFile *cur = &edp_files[edp_bound];
        if (cur->writable) return EDP_ERR_LOCKED;

        edp_fs_file_close();
        cur->win_len = 0;
        edp_bound = -1;
    }

    int res = edp_fs_file_open(f->path, EDP_FA_READ);
    if (res != EDP_OK) return res;

    edp_fs_gen++;
    f->mcu_pos = 0;
    f->win_len = 0;

    if (f->pos)
    {
        res = edp_fs_file_set_ptr(f->pos);
        if (res != EDP_OK) { edp_fs_file_close(); return res; }
        f->mcu_pos = f->pos;
    }
    edp_bound = idx;
    return EDP_OK;
}

static int edp_seek_mcu(EdpFile *f)
{
    if (f->mcu_pos == f->pos) return EDP_OK;

    int res = edp_fs_file_set_ptr(f->pos);
    if (res != EDP_OK) return res;

    f->mcu_pos = f->pos;
    return EDP_OK;
}

static void *edp_open(char *name, int flags)
{
    char norm[EDP_PATH_MAX];
    if (!edp_norm(name, norm, sizeof(norm))) return NULL;
    if (norm[0] == '\0') { errno = ENOENT; return NULL; }

    bool writable = (flags & (O_WRONLY | O_RDWR)) != 0;
    uint8_t mode;

    if (!writable)
    {
        mode = EDP_FA_READ;
    }
    else if (flags & O_APPEND)
    {
        mode = EDP_FA_READ | EDP_FA_WRITE | EDP_FA_OPEN_APPEND;
    }
    else if (flags & O_TRUNC)
    {
        mode = EDP_FA_READ | EDP_FA_WRITE | EDP_FA_CREATE_ALWAYS;
    }
    else if (flags & O_CREAT)
    {
        mode = EDP_FA_READ | EDP_FA_WRITE | EDP_FA_OPEN_ALWAYS;
    }
    else
    {
        mode = EDP_FA_READ | EDP_FA_WRITE;
    }

    /* A write cannot share the cartridge with anything else. */
    if (edp_bound >= 0 && (writable || edp_files[edp_bound].writable))
    {
        errno = EMFILE;
        return NULL;
    }

    int slot = -1;
    for (int i = 0; i < EDP_MAX_FILES; i++)
    {
        if (!edp_files[i].used) { slot = i; break; }
    }
    if (slot < 0) { errno = EMFILE; return NULL; }

    EdpFile *f = &edp_files[slot];
    memset(f, 0, sizeof(*f));
    strcpy(f->path, norm);
    f->writable = writable;

    if (edp_bound >= 0)
    {
        edp_fs_file_close();
        edp_files[edp_bound].win_len = 0;
        edp_bound = -1;
    }

    int res = edp_fs_file_open(norm, mode);
    if (res != EDP_OK) { errno = edp_to_errno(res); return NULL; }

    edp_fs_gen++;

    /* Ask for the size while the pointer is still at the start. Using the
     * by-path info command instead would risk disturbing the file just
     * opened, and every fopen() needs this: newlib calls fstat while it is
     * setting up its buffer, and the browser measures the rom with
     * fseek/ftell before reading it. */
    uint32_t avail = 0;
    res = edp_fs_file_available(&avail);
    if (res != EDP_OK) { edp_fs_file_close(); errno = edp_to_errno(res); return NULL; }

    f->size    = avail;
    f->used    = true;
    f->mcu_pos = 0;
    edp_bound  = slot;

    return f;
}

static int edp_close(void *file)
{
    EdpFile *f = (EdpFile *)file;
    int res = EDP_OK;

    if (f == NULL) { errno = EINVAL; return -1; }

    if (edp_bound == (int)(f - edp_files))
    {
        res = edp_fs_file_close();
        edp_bound = -1;
        edp_fs_gen++;
    }
    f->used = false;

    return res == EDP_OK ? 0 : edp_fail(res);
}

/* Refill the cartridge-memory window so that it covers f->pos. */
static int edp_stage(EdpFile *f)
{
    int res = edp_seek_mcu(f);
    if (res != EDP_OK) return res;

    uint32_t want = f->size - f->pos;
    if (want > EDP_STAGE_SIZE) want = EDP_STAGE_SIZE;

    res = edp_fs_file_read_fci(edp_stage_base, want);
    if (res != EDP_OK) return res;

    f->mcu_pos += want;
    f->win_off  = f->pos;
    f->win_len  = want;
    return EDP_OK;
}

static int edp_read(void *file, uint8_t *ptr, int len)
{
    EdpFile *f = (EdpFile *)file;

    if (f == NULL || !f->used) { errno = EBADF; return -1; }
    if (len <= 0) return 0;

    int res = edp_bind(f);
    if (res != EDP_OK) return edp_fail(res);

    uint32_t left = (uint32_t)len;
    if (f->pos >= f->size) return 0;
    if (left > f->size - f->pos) left = f->size - f->pos;

    /* Reads arrive here in 1 KB pieces however big the caller's request was -
     * newlib's fread always goes through its own 1024 byte buffer - so a
     * whole rom would otherwise cost hundreds of cartridge round trips over a
     * fifo that moves one byte per four bytes of bus traffic. Staging a large
     * window in cartridge memory turns that into a couple of transfers plus
     * plain DMA, without menu.cpp having to know. */
    bool staged = edp_fast && f->size >= EDP_STAGE_MIN;
    int done = 0;

    while (left)
    {
        uint32_t block;

        if (staged)
        {
            if (f->win_len == 0 ||
                f->pos < f->win_off || f->pos >= f->win_off + f->win_len)
            {
                res = edp_stage(f);
                if (res != EDP_OK)
                {
                    /* Staging is an optimisation, not a requirement. If the
                     * cartridge will not do it, drop to the fifo for the rest
                     * of this read and every one after it rather than failing
                     * the load. */
                    debugf("ED64 PRO: staging failed (%d), falling back to the fifo\n", res);
                    edp_fast   = false;
                    staged     = false;
                    f->win_len = 0;

                    /* A failed transfer leaves the cartridge's file pointer
                     * somewhere unknown, so put it back explicitly. */
                    res = edp_fs_file_set_ptr(f->pos);
                    if (res != EDP_OK) return done ? done : edp_fail(res);
                    f->mcu_pos = f->pos;
                    continue;
                }
            }
            block = f->win_off + f->win_len - f->pos;
            if (block > left) block = left;

            edp_pi_rd(ptr, EDP_PI_ROM + edp_stage_base + (f->pos - f->win_off), block);
        }
        else
        {
            res = edp_seek_mcu(f);
            if (res != EDP_OK) return done ? done : edp_fail(res);

            block = left;
            res = edp_fs_file_read(ptr, block);
            if (res != EDP_OK) return done ? done : edp_fail(res);

            f->mcu_pos += block;
        }
        ptr    += block;
        f->pos += block;
        left   -= block;
        done   += (int)block;
    }
    return done;
}

static int edp_write(void *file, uint8_t *ptr, int len)
{
    EdpFile *f = (EdpFile *)file;

    if (f == NULL || !f->used) { errno = EBADF; return -1; }
    if (!f->writable) { errno = EACCES; return -1; }
    if (len <= 0) return 0;

    int res = edp_bind(f);
    if (res != EDP_OK) return edp_fail(res);

    res = edp_seek_mcu(f);
    if (res != EDP_OK) return edp_fail(res);

    res = edp_fs_file_write(ptr, (uint32_t)len);
    if (res != EDP_OK) return edp_fail(res);

    f->pos     += (uint32_t)len;
    f->mcu_pos  = f->pos;
    f->win_len  = 0;
    if (f->pos > f->size) f->size = f->pos;
    edp_fs_gen++;

    return len;
}

static int edp_lseek(void *file, int ptr, int dir)
{
    EdpFile *f = (EdpFile *)file;
    int64_t base;

    if (f == NULL || !f->used) { errno = EBADF; return -1; }

    switch (dir)
    {
    case SEEK_SET: base = 0;              break;
    case SEEK_CUR: base = f->pos;         break;
    case SEEK_END: base = f->size;        break;
    default: errno = EINVAL; return -1;
    }

    int64_t want = base + ptr;
    if (want < 0) { errno = EINVAL; return -1; }

    /* The cartridge is told about this only when the position is next used. */
    f->pos = (uint32_t)want;
    return (int)f->pos;
}

static int edp_fstat(void *file, struct stat *st)
{
    EdpFile *f = (EdpFile *)file;

    if (f == NULL || !f->used) { errno = EBADF; return -1; }

    memset(st, 0, sizeof(*st));
    st->st_mode  = S_IFREG | 0666;
    st->st_size  = (off_t)f->size;
    st->st_nlink = 1;
    return 0;
}

static int edp_stat(char *name, struct stat *st)
{
    char norm[EDP_PATH_MAX];
    uint8_t namebuf[EDP_NAME_MAX];
    EdpFileInfo inf = { .file_name = namebuf };

    if (!edp_norm(name, norm, sizeof(norm))) return -1;

    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;

    if (norm[0] == '\0')
    {
        st->st_mode = S_IFDIR | 0777;
        return 0;
    }

    int res = edp_fs_file_info(norm, &inf, sizeof(namebuf));
    if (res != EDP_OK) return edp_fail(res);

    if (inf.is_dir & EDP_AT_DIR)
    {
        st->st_mode = S_IFDIR | 0777;
    }
    else
    {
        st->st_mode = S_IFREG | 0666;
        st->st_size = (off_t)inf.size;
    }
    return 0;
}

static int edp_unlink(char *name)
{
    char norm[EDP_PATH_MAX];

    if (!edp_norm(name, norm, sizeof(norm))) return -1;
    if (norm[0] == '\0') { errno = EISDIR; return -1; }

    int res = edp_fs_file_del(norm);
    edp_fs_gen++;
    return res == EDP_OK ? 0 : edp_fail(res);
}

static int edp_mkdir(char *path, mode_t mode)
{
    char norm[EDP_PATH_MAX];
    (void)mode;

    if (!edp_norm(path, norm, sizeof(norm))) return -1;
    if (norm[0] == '\0') { errno = EEXIST; return -1; }

    int res = edp_fs_dir_make(norm);
    edp_fs_gen++;
    return res == EDP_OK ? 0 : edp_fail(res);
}

/* ----------------------------------------------------------- directories
 *
 * The cartridge holds one loaded directory and serves it by index, which maps
 * cleanly onto the opaque cookie libdragon carries in dir_t. findnext2 gets
 * the path again, so a walk can recover if something displaced the listing in
 * between; in practice the browser reads a whole directory before it opens
 * anything, so it never does.
 */
static char     edp_dir_path[EDP_PATH_MAX];
static bool     edp_dir_valid = false;
static uint16_t edp_dir_count = 0;
static uint32_t edp_dir_gen   = 0;

static int edp_dir_ensure(const char *norm)
{
    if (edp_dir_valid && edp_dir_gen == edp_fs_gen &&
        strcmp(edp_dir_path, norm) == 0)
    {
        return EDP_OK;
    }
    edp_dir_valid = false;

    int res = edp_fs_dir_load(norm, EDP_DIR_OPT_SORTED);
    if (res != EDP_OK) return res;

    res = edp_fs_dir_get_size(&edp_dir_count);
    if (res != EDP_OK) return res;

    strcpy(edp_dir_path, norm);
    edp_dir_gen   = edp_fs_gen;
    edp_dir_valid = true;
    return EDP_OK;
}

static int edp_dir_fetch(dir_t *dir)
{
    uint8_t namebuf[EDP_NAME_MAX];
    EdpFileInfo inf = { .file_name = namebuf };

    for (;;)
    {
        if (dir->d_cookie >= edp_dir_count) return -1;

        uint16_t index = (uint16_t)dir->d_cookie++;
        int res = edp_fs_dir_read_rec(index, &inf, sizeof(namebuf));
        if (res != EDP_OK) return edp_fail_dir(res);

        const char *name = (const char *)namebuf;

        /* FatFs does not hand these out, but nothing says the cartridge's
         * sorted listing will not, and the browser would offer "" as a
         * folder to walk into. */
        if (name[0] == '.' &&
            (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) continue;

        snprintf(dir->d_name, sizeof(dir->d_name), "%s", name);
        dir->d_type = (inf.is_dir & EDP_AT_DIR) ? DT_DIR : DT_REG;
        dir->d_size = (inf.is_dir & EDP_AT_DIR) ? -1 : (int64_t)inf.size;
        return 0;
    }
}

static int edp_findfirst(char *path, dir_t *dir)
{
    char norm[EDP_PATH_MAX];

    if (!edp_norm(path, norm, sizeof(norm))) return -2;

    int res = edp_dir_ensure(norm);
    if (res != EDP_OK) return edp_fail_dir(res);

    dir->d_cookie = 0;
    return edp_dir_fetch(dir);
}

static int edp_findnext(const char *path, dir_t *dir)
{
    char norm[EDP_PATH_MAX];

    if (!edp_norm(path, norm, sizeof(norm))) return -2;

    int res = edp_dir_ensure(norm);
    if (res != EDP_OK) return edp_fail_dir(res);

    return edp_dir_fetch(dir);
}

static filesystem_t edp_fs = {
    .open      = edp_open,
    .fstat     = edp_fstat,
    .stat      = edp_stat,
    .lseek     = edp_lseek,
    .read      = edp_read,
    .write     = edp_write,
    .close     = edp_close,
    .unlink    = edp_unlink,
    .findfirst = edp_findfirst,
    .findnext2 = edp_findnext,
    .mkdir     = edp_mkdir,
};

/* -------------------------------------------------------------- detection */

bool ed64pro_detect(void)
{
    if (edp_probed) return edp_is_pro;
    edp_probed = true;

    /* Reading PI addresses outside the rom bus-errors on an iQue, which is
     * why libcart and libdragon both refuse to probe there. */
    if (sys_bbplayer()) return false;

    edp_edid_value = edp_reg_rd(EDP_REG_EDID);
    if ((edp_edid_value >> 16) != 0xED64u) return false;

    /* The device id is not enough on its own: a Series X answers 0xED64 at
     * this same address, which is exactly how libcart comes to mistake a PRO
     * for one. The status register is the discriminator - it carries a
     * constant nibble and a strobe bit that inverts on every read, and there
     * is nothing mapped here on an X-series. */
    uint32_t prev = edp_reg_rd(EDP_REG_SYSSTAT);
    if ((prev & EDP_SYSSTAT_CMSK) != EDP_SYSSTAT_CVAL) return false;

    int toggles = 0;
    for (int i = 0; i < 64 && toggles < 3; i++)
    {
        uint32_t now = edp_reg_rd(EDP_REG_SYSSTAT);
        if ((now & EDP_SYSSTAT_CMSK) != EDP_SYSSTAT_CVAL) return false;
        if ((now ^ prev) & EDP_SYSSTAT_STROBE) toggles++;
        prev = now;
    }
    if (toggles < 3) return false;

    /* Only now write anything: talk to the MCU and require it to answer. */
    if (edp_handshake() != EDP_OK) return false;

    edp_is_pro = true;
    return true;
}

bool ed64pro_present(void) { return edp_is_pro; }

uint32_t ed64pro_edid(void) { return edp_edid_value; }

bool ed64pro_fastpath(void) { return edp_fast; }

/* ---------------------------------------------------------------- staging
 *
 * Confirm that the staging area is real memory and is not an alias of the
 * running rom. Two patterns rule out a stuck bus; checking that the start of
 * the rom did not change rules out an address that wraps back onto us, which
 * a read-back test on its own would happily pass while overwriting the code
 * running the test.
 */
static bool edp_stage_probe(uint32_t base)
{
    static uint8_t probe[64] __attribute__((aligned(16)));
    static uint8_t rom_before[64] __attribute__((aligned(16)));
    static uint8_t rom_after[64] __attribute__((aligned(16)));
    static const uint8_t patterns[2] = { 0x5A, 0xA5 };

    edp_pi_rd(rom_before, EDP_PI_ROM, sizeof(rom_before));

    for (int p = 0; p < 2; p++)
    {
        if (edp_fci_set(patterns[p], base, sizeof(probe)) != EDP_OK) return false;

        edp_pi_rd(probe, EDP_PI_ROM + base, sizeof(probe));
        for (size_t i = 0; i < sizeof(probe); i++)
        {
            if (probe[i] != patterns[p]) return false;
        }
    }

    edp_pi_rd(rom_after, EDP_PI_ROM, sizeof(rom_after));
    return memcmp(rom_before, rom_after, sizeof(rom_before)) == 0;
}

/* ------------------------------------------------------------------ mount */

bool ed64pro_attach_fs(const char *prefix)
{
    if (!edp_is_pro) return false;

    /* Listing the root is the cheapest proof that there is a working card. */
    int res = edp_fs_dir_load("", EDP_DIR_OPT_SORTED);
    if (res != EDP_OK)
    {
        edp_fs_init();
        res = edp_fs_dir_load("", EDP_DIR_OPT_SORTED);
    }
    if (res != EDP_OK)
    {
        debugf("ED64 PRO: cannot read the card (%d)\n", res);
        return false;
    }
    edp_dir_valid = false;

    edp_fast = edp_stage_probe(EDP_STAGE_FCI);
    if (edp_fast)
    {
        edp_stage_base = EDP_STAGE_FCI;
    }
    else if (edp_stage_probe(EDP_STAGE_FCI_ALT))
    {
        edp_stage_base = EDP_STAGE_FCI_ALT;
        edp_fast = true;
    }
    debugf("ED64 PRO: staging %s at %08lx\n",
           edp_fast ? "enabled" : "disabled (falling back to the fifo)",
           (unsigned long)edp_stage_base);

    if (attach_filesystem(prefix, &edp_fs) != 0)
    {
        debugf("ED64 PRO: attach_filesystem(%s) failed\n", prefix);
        return false;
    }
    return true;
}

bool ed64pro_dir_exists(const char *path)
{
    if (!edp_is_pro) return false;
    return edp_fs_dir_test(path) == EDP_OK;
}

/* ------------------------------------------------------- the launched file */

bool ed64pro_app_file(char *out, size_t cap)
{
    char raw[EDP_PATH_MAX];

    if (out == NULL || cap == 0) return false;
    out[0] = '\0';
    if (!edp_is_pro) return false;

    if (edp_dev_get_path(raw, sizeof(raw), EDP_PATH_APPF) != EDP_OK) return false;
    if (raw[0] == '\0') return false;
    if (!edp_norm(raw, out, cap)) { out[0] = '\0'; return false; }

    return out[0] != '\0';
}

void ed64pro_clear_app_file(void)
{
    if (!edp_is_pro) return;
    edp_dev_set_path("", EDP_PATH_APPF);
}
