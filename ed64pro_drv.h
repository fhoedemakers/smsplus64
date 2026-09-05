/*
 * ed64pro_drv.h - EverDrive-64 PRO cartridge command protocol.
 *
 * Ported from the EverDrive-64 PRO developer sources (edio/everdrive.{c,h}),
 * Copyright (c) 2026 krikzz, MIT licensed:
 *     https://github.com/krikzz/ed64-pro-pub
 *
 * Trimmed to what smsPlus64 needs - the command FIFO, the filesystem commands
 * and the launched-file path - and retyped on <stdint.h>. Upstream's types.h
 * defines u8/u32 as macros rather than typedefs and its errors.h defines an
 * FR_* enum that collides with FatFs, so neither header is used here.
 *
 * The PRO is not a bigger X-series. It has no SD registers and no unlock key:
 * the card and its FAT filesystem belong to an on-cartridge MCU, and the N64
 * talks to it by pushing command bytes through a single FIFO register.
 */
#ifndef ED64PRO_DRV_H
#define ED64PRO_DRV_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- registers
 *
 * Seven words in the PI ROM domain. Addresses are physical; every access here
 * goes through PI DMA rather than a volatile pointer.
 */
#define EDP_REG_BASE        0x1F800000u
#define EDP_REG_FIFODATA    (EDP_REG_BASE + 0x00u) /* R/W MCU fifo io port    */
#define EDP_REG_FIFOSTAT    (EDP_REG_BASE + 0x04u) /* R   bytes available     */
#define EDP_REG_SYSSTAT     (EDP_REG_BASE + 0x08u) /* R   MCU/SD/FPGA status  */
#define EDP_REG_TIMER       (EDP_REG_BASE + 0x0Cu) /* R   1 ms counter        */
#define EDP_REG_MBX         (EDP_REG_BASE + 0x10u) /* R/W mailbox             */
#define EDP_REG_EDID        (EDP_REG_BASE + 0x14u) /* R   0xED64xxxx          */
#define EDP_REG_MCTR        (EDP_REG_BASE + 0x18u) /* R   50 MHz counter      */

#define EDP_SYSSTAT_BUSY    (1u << 0)   /* MCU busy                           */
#define EDP_SYSSTAT_STROBE  (1u << 3)   /* inverts after every read           */
#define EDP_SYSSTAT_CMSK    0xF0u       /* constant field mask                */
#define EDP_SYSSTAT_CVAL    0xA0u       /* ... and its only valid value       */

/* Cartridge SDRAM is 256 MB and FCI address n is PI address 0xB0000000 + n. */
#define EDP_FCI_RAM         0x00000000u
#define EDP_PI_ROM          0x10000000u

/* --------------------------------------------------------------- file modes */
#define EDP_FA_READ             0x01u
#define EDP_FA_WRITE            0x02u
#define EDP_FA_OPEN_EXISTING    0x00u
#define EDP_FA_CREATE_NEW       0x04u
#define EDP_FA_CREATE_ALWAYS    0x08u
#define EDP_FA_OPEN_ALWAYS      0x10u
#define EDP_FA_OPEN_APPEND      0x30u

#define EDP_AT_DIR              0x10u   /* FileInfo.is_dir bit that counts    */

#define EDP_DIR_OPT_SORTED      0x01u

/* ------------------------------------------------------------- result codes
 *
 * 0..19 are the MCU's own FatFs FRESULT values forwarded verbatim; the rest
 * are the protocol's and this port's.
 */
#define EDP_OK                  0
#define EDP_ERR_DISK            1
#define EDP_ERR_NOT_READY       3
#define EDP_ERR_NO_FILE         4
#define EDP_ERR_NO_PATH         5
#define EDP_ERR_INVALID_NAME    6
#define EDP_ERR_DENIED          7
#define EDP_ERR_EXIST           8
#define EDP_ERR_WRITE_PROTECTED 10
#define EDP_ERR_NO_FILESYSTEM   13
#define EDP_ERR_LOCKED          16
#define EDP_ERR_UNXP_STAT       0x40    /* status reply did not start with 5A */
#define EDP_ERR_NULL_PATH       0x41
#define EDP_ERR_TIMEOUT         0x50    /* ours: a bounded wait ran out       */
#define EDP_ERR_FAULTED         0x51    /* ours: link given up after a timeout*/

/* ---------------------------------------------------------------- file info
 *
 * Nine bytes cross the wire (size, date, time, is_dir) followed by the name as
 * a length-prefixed string, so the first four fields must keep this order and
 * these offsets. file_name points at a caller-owned buffer.
 */
typedef struct
{
    uint32_t size;
    uint16_t date;      /* MS-DOS format */
    uint16_t time;      /* MS-DOS format */
    uint8_t  is_dir;
    uint8_t *file_name;
} EdpFileInfo;

/* Which of the cartridge's remembered paths to ask for. */
typedef enum
{
    EDP_PATH_GPAK  = 0, /* the rom the cartridge booted                       */
    EDP_PATH_GDATA = 1, /* per-game data folder                               */
    EDP_PATH_BRM   = 2, /* backup ram file                                    */
    EDP_PATH_APPF  = 3, /* the file this app was launched with, if any        */
    EDP_PATH_DISK  = 4  /* 64DD disk image, if mounted                        */
} EdpPathType;

/* ------------------------------------------------------------------- link */

/* Raw register access. Both are safe to call on any cartridge. */
uint32_t edp_reg_rd(uint32_t pi_addr);
void     edp_reg_wr(uint32_t pi_addr, uint32_t val);

/* PI DMA to and from cartridge memory, with the alignment rules handled. */
void     edp_pi_rd(void *dst, uint32_t pi_addr, uint32_t len);

/* True once a bounded wait has run out. Every command refuses to run after
 * that, so a card pulled mid-game turns into errors rather than a hang. */
bool     edp_faulted(void);
void     edp_clear_fault(void);

/* Drop whatever the Everdrive menu left in the fifo, then handshake. */
int      edp_handshake(void);

/* -------------------------------------------------------------- filesystem
 *
 * Paths are relative to the card root: forward slashes, no leading slash and
 * no drive prefix. "" is the root itself.
 *
 * There is one open file and one loaded directory at a time, cartridge-wide.
 */
int      edp_fs_init(void);
int      edp_fs_file_open(const char *path, uint8_t mode);
int      edp_fs_file_close(void);
int      edp_fs_file_available(uint32_t *out);
int      edp_fs_file_read(void *dst, uint32_t len);
int      edp_fs_file_read_fci(uint32_t fci_addr, uint32_t len);
int      edp_fs_file_write(const void *src, uint32_t len);
int      edp_fs_file_set_ptr(uint32_t offset);
int      edp_fs_file_info(const char *path, EdpFileInfo *inf, size_t name_cap);
int      edp_fs_file_del(const char *path);
int      edp_fs_file_test(const char *path);
int      edp_fs_dir_test(const char *path);
int      edp_fs_dir_make(const char *path);
int      edp_fs_dir_load(const char *path, uint8_t opt);
int      edp_fs_dir_get_size(uint16_t *out);
int      edp_fs_dir_read_rec(uint16_t index, EdpFileInfo *inf, size_t name_cap);

/* ------------------------------------------------------------------ device */
int      edp_dev_get_path(char *out, size_t cap, EdpPathType type);
int      edp_dev_set_path(const char *path, EdpPathType type);

/* --------------------------------------------------------------- cart memory
 *
 * Fill a range of cartridge memory with a byte. Used only by the staging
 * self-test, which is why nothing else from CMD_FCI is ported.
 */
int      edp_fci_set(uint8_t val, uint32_t fci_addr, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* ED64PRO_DRV_H */
