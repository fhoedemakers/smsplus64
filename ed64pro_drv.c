/*
 * ed64pro_drv.c - EverDrive-64 PRO cartridge command protocol.
 *
 * Ported from the EverDrive-64 PRO developer sources (edio/everdrive.c),
 * Copyright (c) 2026 krikzz, MIT licensed:
 *     https://github.com/krikzz/ed64-pro-pub
 *
 * What changed on the way in, and why:
 *
 *  - Every wait is bounded. Upstream's fifo reader loops on "how many bytes
 *    are available" forever, and its wait-for-MCU spins on a status bit with
 *    no way out. On a card that has been pulled, or a cartridge that is not
 *    actually a PRO, that is a frozen console with no route back to the menu.
 *    Each wait here has a deadline that is reset whenever bytes actually
 *    arrive, and running out latches a fault that every later command
 *    refuses, so the failure surfaces as an error instead of a hang.
 *
 *  - Received strings are capped. Upstream reads a 16-bit length from the
 *    cartridge and writes a terminator at that offset before reading the
 *    body, with no bound; a long name would run off the end of the caller's
 *    buffer. Here the length is truncated to the buffer and the remainder is
 *    drained so the fifo stays in step.
 *
 *  - The EPO read/write helpers are not ported at all. Both advance their
 *    cartridge address by the whole remaining length instead of by the block
 *    just transferred, so they walk the wrong addresses for anything over one
 *    block. Nothing here needs them: file reads use the filesystem commands
 *    directly. Do not copy them back in from upstream without fixing that.
 *
 *  - Filling cartridge memory now waits for the MCU to finish. Upstream fires
 *    the transfer and returns, leaving the caller free to read back memory
 *    that has not been written yet.
 *
 * Byte order is deliberately left alone. Multi-byte arguments go onto the
 * wire in memory order, which is big-endian on the VR4300, and the MCU
 * expects them that way.
 */

#include <string.h>
#include <libdragon.h>

#include "ed64pro_drv.h"

/* ------------------------------------------------------------- protocol */

#define EDP_STATUS_KEY          0x5Au

#define EDP_CMD_STATUS          0x10u

#define EDP_CMD_FS              0x80u
#define EDP_FS_SCMD_INIT        0x10u
#define EDP_FS_SCMD_DIR_LD      0x13u
#define EDP_FS_SCMD_DIR_SIZE    0x14u
#define EDP_FS_SCMD_DIR_GET     0x16u
#define EDP_FS_SCMD_FOPN        0x17u
#define EDP_FS_SCMD_FCLOSE      0x18u
#define EDP_FS_SCMD_FPTR        0x19u
#define EDP_FS_SCMD_FINFO       0x1Au
#define EDP_FS_SCMD_DIR_MK      0x1Cu
#define EDP_FS_SCMD_DEL         0x1Du
#define EDP_FS_SCMD_AVB         0x1Fu
#define EDP_FS_SCMD_DTEST       0x22u
#define EDP_FS_SCMD_FTEST       0x23u

#define EDP_CMD_EPO             0x81u
#define EDP_EPO_SCMD_XFER       0x10u
#define EDP_EPO_TYPE_LINK_ACK   0x11u   /* console, in acknowledged blocks */
#define EDP_EPO_TYPE_FS         0x12u   /* the open file                   */
#define EDP_EPO_TYPE_FCI        0x13u   /* cartridge memory                */

#define EDP_CMD_FCI             0x82u
#define EDP_FCI_SCMD_MEM_SET    0x10u

#define EDP_CMD_DEV             0x85u
#define EDP_DEV_SCMD_SET_PATH   0x21u
#define EDP_DEV_SCMD_GET_PATH   0x22u

#define EDP_ACK_BLOCK           1024u   /* bytes between acknowledgements  */
#define EDP_FIFO_SIZE           2048u   /* hardware fifo depth             */

/* Deadlines. A command reply is a millisecond or two in practice; a bulk
 * card-to-cartridge transfer of a quarter megabyte is the long one. */
#define EDP_TIMEOUT_CMD_MS      5000u
#define EDP_TIMEOUT_BULK_MS     15000u

typedef struct
{
    uint32_t src_addr;
    uint32_t dst_addr;
    uint32_t len;
    uint8_t  src_type;
    uint8_t  dst_type;
    uint16_t reserved;
} EdpXfer;

_Static_assert(sizeof(EdpXfer) == 16, "EdpXfer must match the wire layout");
_Static_assert(offsetof(EdpFileInfo, is_dir) == 8,
               "the first nine bytes of EdpFileInfo cross the wire as-is");

static bool edp_fault = false;

bool edp_faulted(void) { return edp_fault; }
void edp_clear_fault(void) { edp_fault = false; }

static int edp_timeout(void)
{
    edp_fault = true;
    return EDP_ERR_TIMEOUT;
}

static uint32_t edp_min(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* ------------------------------------------------------------ PI access */

uint32_t edp_reg_rd(uint32_t pi_addr)
{
    static uint32_t val __attribute__((aligned(8)));

    data_cache_hit_writeback_invalidate(&val, sizeof(val));
    dma_read_raw_async(&val, pi_addr & 0x1FFFFFFFu, sizeof(val));
    dma_wait();
    return val;
}

void edp_reg_wr(uint32_t pi_addr, uint32_t value)
{
    static uint32_t val __attribute__((aligned(8)));

    val = value;
    data_cache_hit_writeback(&val, sizeof(val));
    dma_write_raw_async(&val, pi_addr & 0x1FFFFFFFu, sizeof(val));
    dma_wait();
}

void edp_pi_rd(void *dst, uint32_t pi_addr, uint32_t len)
{
    /* dma_read_raw_async is only well defined for an 8-byte aligned
     * destination and a length it can move whole; anything else goes through
     * a bounce buffer, as the reference driver does. */
    if (((uintptr_t)dst & 7u) == 0 && (len & 7u) == 0)
    {
        data_cache_hit_writeback_invalidate(dst, len);
        dma_read_raw_async(dst, pi_addr & 0x1FFFFFFFu, len);
        dma_wait();
        return;
    }

    static uint8_t bounce[4096] __attribute__((aligned(16)));
    uint8_t *out = (uint8_t *)dst;

    while (len)
    {
        uint32_t block = edp_min(len, sizeof(bounce));
        uint32_t align = pi_addr & 7u;
        uint32_t grab  = (block + align + 7u) & ~7u;

        if (grab > sizeof(bounce))
        {
            grab  = sizeof(bounce);
            block = grab - align;
        }
        data_cache_hit_writeback_invalidate(bounce, grab);
        dma_read_raw_async(bounce, (pi_addr - align) & 0x1FFFFFFFu, grab);
        dma_wait();
        memcpy(out, bounce + align, block);

        out     += block;
        pi_addr += block;
        len     -= block;
    }
}

/* ----------------------------------------------------------------- fifo
 *
 * One payload byte travels per 32-bit word, and the FPGA only streams the
 * fifo for a burst that starts exactly at FIFODATA - a single word read at
 * FIFODATA+4 still returns FIFOSTAT. So the burst address is never anything
 * but FIFODATA, and this must not be rewritten as volatile loads. The block
 * size and the 512-byte alignment come from the reference driver; both are
 * hardware-tested, so treat a change to either as needing hardware.
 */
static uint32_t edp_fifo_avb(void)
{
    return edp_reg_rd(EDP_REG_FIFOSTAT) & 0xFFFFu;
}

static void edp_stream_rd(void *dst, uint32_t len)
{
    static uint32_t buff[128] __attribute__((aligned(512)));
    uint8_t *out = (uint8_t *)dst;

    while (len)
    {
        uint32_t block = edp_min(len, sizeof(buff) / 4);

        data_cache_hit_writeback_invalidate(buff, block * 4);
        dma_read_raw_async(buff, EDP_REG_FIFODATA, block * 4);
        dma_wait();

        for (uint32_t i = 0; i < block; i++) *out++ = (uint8_t)buff[i];
        len -= block;
    }
}

static void edp_stream_wr(const void *src, uint32_t len)
{
    static uint32_t buff[128] __attribute__((aligned(512)));
    const uint8_t *in = (const uint8_t *)src;

    while (len)
    {
        uint32_t block = edp_min(len, sizeof(buff) / 4);

        for (uint32_t i = 0; i < block; i++) buff[i] = *in++;

        data_cache_hit_writeback(buff, block * 4);
        dma_write_raw_async(buff, EDP_REG_FIFODATA, block * 4);
        dma_wait();
        len -= block;
    }
}

static void edp_fifo_wr(const void *src, uint32_t len)
{
    edp_stream_wr(src, len);
}

/* Read len bytes, giving up only after ms have passed with nothing arriving. */
static int edp_fifo_rd_to(void *dst, uint32_t len, uint32_t ms)
{
    uint8_t *out = (uint8_t *)dst;
    uint32_t budget = TICKS_FROM_MS(ms);
    uint32_t mark = TICKS_READ();

    while (len)
    {
        uint32_t avb = edp_fifo_avb();

        if (avb == 0)
        {
            if (TICKS_SINCE(mark) > (int32_t)budget) return edp_timeout();
            continue;
        }
        uint32_t block = edp_min(len, avb);
        edp_stream_rd(out, block);
        out += block;
        len -= block;
        mark = TICKS_READ();
    }
    return EDP_OK;
}

static int edp_fifo_rd(void *dst, uint32_t len)
{
    return edp_fifo_rd_to(dst, len, EDP_TIMEOUT_CMD_MS);
}

static int edp_fifo_skip(uint32_t len, uint32_t ms)
{
    uint8_t scratch[256];

    while (len)
    {
        uint32_t block = edp_min(len, sizeof(scratch));
        int res = edp_fifo_rd_to(scratch, block, ms);
        if (res != EDP_OK) return res;
        len -= block;
    }
    return EDP_OK;
}

static int edp_wait_mcu(uint32_t ms)
{
    uint32_t budget = TICKS_FROM_MS(ms);
    uint32_t mark = TICKS_READ();

    while (edp_reg_rd(EDP_REG_SYSSTAT) & EDP_SYSSTAT_BUSY)
    {
        if (TICKS_SINCE(mark) > (int32_t)budget) return edp_timeout();
    }
    return EDP_OK;
}

/* ------------------------------------------------------------- framing */

static void edp_cmd_tx(uint8_t cmd)
{
    uint8_t buf[4] = { '+', (uint8_t)('+' ^ 0xFF), cmd, (uint8_t)(cmd ^ 0xFF) };
    edp_fifo_wr(buf, sizeof(buf));
}

static void edp_scmd_tx(uint8_t cmd, uint8_t scmd)
{
    uint8_t buf[5] = { '+', (uint8_t)('+' ^ 0xFF), cmd, (uint8_t)(cmd ^ 0xFF), scmd };
    edp_fifo_wr(buf, sizeof(buf));
}

static void edp_tx_string(const char *s)
{
    uint16_t len = (uint16_t)strlen(s);

    edp_fifo_wr(&len, sizeof(len));
    edp_fifo_wr(s, len);
}

/* Read a length-prefixed string into at most cap bytes including the
 * terminator, draining anything that did not fit so the fifo stays in step. */
static int edp_rx_string(char *dst, size_t cap)
{
    uint16_t len = 0;
    int res = edp_fifo_rd(&len, sizeof(len));
    if (res != EDP_OK) return res;

    uint32_t keep = len;
    if (cap == 0) keep = 0;
    else if (keep > cap - 1) keep = (uint32_t)(cap - 1);

    if (keep)
    {
        res = edp_fifo_rd(dst, keep);
        if (res != EDP_OK) return res;
    }
    if (cap) dst[keep] = '\0';

    return edp_fifo_skip(len - keep, EDP_TIMEOUT_CMD_MS);
}

static int edp_check_status(void)
{
    uint8_t resp[4];

    edp_cmd_tx(EDP_CMD_STATUS);

    int res = edp_fifo_rd(resp, sizeof(resp));
    if (res != EDP_OK) return res;
    if (resp[0] != EDP_STATUS_KEY) return EDP_ERR_UNXP_STAT;

    return resp[3];
}

static void edp_run_xfer(void)
{
    uint8_t ack = 0;
    edp_fifo_wr(&ack, 1);
}

static void edp_xfer_tx(const EdpXfer *xfer)
{
    edp_scmd_tx(EDP_CMD_EPO, EDP_EPO_SCMD_XFER);
    edp_fifo_wr(xfer, sizeof(*xfer));
    edp_run_xfer();
}

/* The console end of a transfer acknowledges one block at a time. */
static int edp_rd_acked(void *dst, uint32_t len)
{
    uint8_t *out = (uint8_t *)dst;
    uint8_t ack = 0;

    while (len)
    {
        uint32_t block = edp_min(EDP_ACK_BLOCK, len);

        edp_fifo_wr(&ack, 1);
        int res = edp_fifo_rd_to(out, block, EDP_TIMEOUT_BULK_MS);
        if (res != EDP_OK) return res;

        out += block;
        len -= block;
    }
    return EDP_OK;
}

static int edp_wr_acked(const void *src, uint32_t len)
{
    const uint8_t *in = (const uint8_t *)src;
    uint8_t ack;

    while (len)
    {
        uint32_t block = edp_min(EDP_ACK_BLOCK, len);

        int res = edp_fifo_rd_to(&ack, 1, EDP_TIMEOUT_BULK_MS);
        if (res != EDP_OK) return res;

        edp_fifo_wr(in, block);
        in  += block;
        len -= block;
    }
    return EDP_OK;
}

/* --------------------------------------------------------------- link up */

int edp_handshake(void)
{
    uint8_t resp[4];

    edp_fault = false;

    /* The Everdrive menu may have left bytes behind. Drain what is there,
     * but never chase a length a broken link could invent. */
    uint32_t stale = edp_fifo_avb();
    if (stale > EDP_FIFO_SIZE) stale = EDP_FIFO_SIZE;
    if (stale)
    {
        int res = edp_fifo_skip(stale, 200);
        if (res != EDP_OK) return res;
    }

    /* Deliberately impatient. This is also the last gate of the probe that
     * decides whether this cartridge is a PRO at all, so on anything else it
     * has to give up quickly rather than stall the console on the way to the
     * game browser. A real controller answers in a millisecond or two. */
    edp_cmd_tx(EDP_CMD_STATUS);

    int res = edp_fifo_rd_to(resp, sizeof(resp), 500);
    if (res != EDP_OK) return res;
    if (resp[0] != EDP_STATUS_KEY) return EDP_ERR_UNXP_STAT;

    return resp[3];
}

/* ------------------------------------------------------------ filesystem */

#define EDP_GUARD() do { if (edp_fault) return EDP_ERR_FAULTED; } while (0)

static int edp_rx_file_info(EdpFileInfo *inf, size_t name_cap)
{
    /* Nine bytes of fixed fields, then the name. */
    int res = edp_fifo_rd(inf, 9);
    if (res != EDP_OK) return res;

    res = edp_rx_string((char *)inf->file_name, name_cap);
    if (res != EDP_OK) return res;

    inf->is_dir &= EDP_AT_DIR;
    return EDP_OK;
}

int edp_fs_init(void)
{
    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_INIT);
    return edp_check_status();
}

int edp_fs_file_open(const char *path, uint8_t mode)
{
    EDP_GUARD();
    if (path == NULL || path[0] == '\0') return EDP_ERR_NULL_PATH;

    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_FOPN);
    edp_fifo_wr(&mode, 1);
    edp_tx_string(path);
    return edp_check_status();
}

int edp_fs_file_close(void)
{
    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_FCLOSE);
    return edp_check_status();
}

int edp_fs_file_available(uint32_t *out)
{
    uint32_t len[2];

    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_AVB);

    int res = edp_fifo_rd(len, sizeof(len));
    if (res != EDP_OK) return res;

    *out = len[0];
    return EDP_OK;
}

int edp_fs_file_read(void *dst, uint32_t len)
{
    EDP_GUARD();
    if (len == 0) return EDP_OK;

    EdpXfer xfer = {
        .src_addr = 0, .src_type = EDP_EPO_TYPE_FS,
        .dst_addr = 0, .dst_type = EDP_EPO_TYPE_LINK_ACK,
        .len = len, .reserved = 0
    };
    edp_xfer_tx(&xfer);

    int res = edp_rd_acked(dst, len);
    if (res != EDP_OK) return res;

    return edp_check_status();
}

int edp_fs_file_read_fci(uint32_t fci_addr, uint32_t len)
{
    EDP_GUARD();
    if (len == 0) return EDP_OK;

    EdpXfer xfer = {
        .src_addr = 0,        .src_type = EDP_EPO_TYPE_FS,
        .dst_addr = fci_addr, .dst_type = EDP_EPO_TYPE_FCI,
        .len = len, .reserved = 0
    };
    edp_xfer_tx(&xfer);

    /* The status handshake is a fifo round trip, so the MCU has finished
     * writing cartridge memory by the time this returns. */
    uint8_t resp[4];
    edp_cmd_tx(EDP_CMD_STATUS);
    int res = edp_fifo_rd_to(resp, sizeof(resp), EDP_TIMEOUT_BULK_MS);
    if (res != EDP_OK) return res;
    if (resp[0] != EDP_STATUS_KEY) return EDP_ERR_UNXP_STAT;

    return resp[3];
}

int edp_fs_file_write(const void *src, uint32_t len)
{
    EDP_GUARD();
    if (len == 0) return EDP_OK;

    EdpXfer xfer = {
        .src_addr = 0, .src_type = EDP_EPO_TYPE_LINK_ACK,
        .dst_addr = 0, .dst_type = EDP_EPO_TYPE_FS,
        .len = len, .reserved = 0
    };
    edp_xfer_tx(&xfer);

    int res = edp_wr_acked(src, len);
    if (res != EDP_OK) return res;

    return edp_check_status();
}

int edp_fs_file_set_ptr(uint32_t offset)
{
    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_FPTR);
    edp_fifo_wr(&offset, sizeof(offset));
    return edp_check_status();
}

int edp_fs_file_info(const char *path, EdpFileInfo *inf, size_t name_cap)
{
    uint8_t resp;

    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_FINFO);
    edp_tx_string(path);

    int res = edp_fifo_rd(&resp, 1);
    if (res != EDP_OK) return res;
    if (resp) return resp;

    return edp_rx_file_info(inf, name_cap);
}

int edp_fs_file_del(const char *path)
{
    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_DEL);
    edp_tx_string(path);
    return edp_check_status();
}

int edp_fs_file_test(const char *path)
{
    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_FTEST);
    edp_tx_string(path);
    return edp_check_status();
}

int edp_fs_dir_test(const char *path)
{
    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_DTEST);
    edp_tx_string(path);
    return edp_check_status();
}

int edp_fs_dir_make(const char *path)
{
    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_DIR_MK);
    edp_tx_string(path);
    return edp_check_status();
}

int edp_fs_dir_load(const char *path, uint8_t opt)
{
    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_DIR_LD);
    edp_fifo_wr(&opt, 1);
    edp_tx_string(path);
    return edp_check_status();
}

int edp_fs_dir_get_size(uint16_t *out)
{
    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_DIR_SIZE);
    return edp_fifo_rd(out, sizeof(*out));
}

int edp_fs_dir_read_rec(uint16_t index, EdpFileInfo *inf, size_t name_cap)
{
    uint8_t resp;

    EDP_GUARD();

    /* One record per request. Asking for several is faster but the unread
     * total must stay inside the 2 KB fifo, and a record with a long name is
     * most of a quarter of that. */
    uint16_t amount = 1;
    uint16_t max_name = (uint16_t)(name_cap ? name_cap - 1 : 0);
    if (max_name > 255) max_name = 255;

    edp_scmd_tx(EDP_CMD_FS, EDP_FS_SCMD_DIR_GET);
    edp_fifo_wr(&index, sizeof(index));
    edp_fifo_wr(&amount, sizeof(amount));
    edp_fifo_wr(&max_name, sizeof(max_name));

    int res = edp_fifo_rd(&resp, 1);
    if (res != EDP_OK) return res;
    if (resp) return resp;

    return edp_rx_file_info(inf, name_cap);
}

/* ---------------------------------------------------------------- device */

int edp_dev_get_path(char *out, size_t cap, EdpPathType type)
{
    uint8_t sel = (uint8_t)type;

    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_DEV, EDP_DEV_SCMD_GET_PATH);
    edp_fifo_wr(&sel, 1);

    int res = edp_rx_string(out, cap);
    if (res != EDP_OK) return res;

    return edp_check_status();
}

int edp_dev_set_path(const char *path, EdpPathType type)
{
    uint8_t sel = (uint8_t)type;

    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_DEV, EDP_DEV_SCMD_SET_PATH);
    edp_fifo_wr(&sel, 1);
    edp_tx_string(path);
    return edp_check_status();
}

/* ----------------------------------------------------------- cart memory */

int edp_fci_set(uint8_t val, uint32_t fci_addr, uint32_t len)
{
    EDP_GUARD();
    edp_scmd_tx(EDP_CMD_FCI, EDP_FCI_SCMD_MEM_SET);
    edp_fifo_wr(&fci_addr, sizeof(fci_addr));
    edp_fifo_wr(&len, sizeof(len));
    edp_fifo_wr(&val, 1);
    edp_run_xfer();

    /* This command sends no reply, so the only way to know the memory has
     * actually been written is to wait for the MCU to go idle. */
    return edp_wait_mcu(EDP_TIMEOUT_CMD_MS);
}
