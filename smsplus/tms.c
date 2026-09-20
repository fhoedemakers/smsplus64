/*
    TMS9918A video modes for SG-1000 cartridges.
    Ported from SMS Plus GX tms.c (C) 1998-2007 Charles MacDonald, GPL v2,
    by way of pico-smsplus.

    Differences from SMS Plus GX:
    - No lookup tables (GX uses ~46 KB); colours and pixels are expanded
      inline, which also keeps them out of the VR4300's 8K data cache.
    - Graphics II uses the TMS9918A colour/pattern table masks from R3/R4
      instead of the SMS VDP's fixed tables.
    - Sprites are parsed and drawn for the current line in one pass; any set
      pattern bit counts for collision, whatever the sprite colour.
    - A skipped frame works out the sprite status without drawing, the same
      way render_line_collision() does for the Master System.

    Pixel format written to linebuf: 0x10 | colour, with 0x40 marking pixels
    covered by a sprite (used for collision detection). The TLUT repeats the
    32 palette entries, so both land on entries 16-31.
*/

#include "shared.h"

/* TMS9918A palette from Sean Young (http://www.smspower.org/dev/docs/tms9918a.txt) */
const uint8 tms_palette_rgb[16][3] =
{
    {  0,   0,   0}, /* 0 transparent */
    {  0,   0,   0}, /* 1 black */
    { 33, 200,  66}, /* 2 medium green */
    { 94, 220, 120}, /* 3 light green */
    { 84,  85, 237}, /* 4 dark blue */
    {125, 118, 252}, /* 5 light blue */
    {212,  82,  77}, /* 6 dark red */
    { 66, 235, 245}, /* 7 cyan */
    {252,  85,  84}, /* 8 medium red */
    {255, 121, 120}, /* 9 light red */
    {212, 193,  84}, /* A dark yellow */
    {230, 206, 128}, /* B light yellow */
    { 33, 176,  59}, /* C dark green */
    {201,  91, 186}, /* D magenta */
    {204, 204, 204}, /* E gray */
    {255, 255, 255}, /* F white */
};

/* Pointer to output buffer (render.c) */
extern uint8 *linebuf;

#define PN_BASE     ((vdp.reg[2] & 0x0F) << 10)
#define PG_BASE     ((vdp.reg[4] & 0x07) << 11)
#define BACKDROP    (vdp.reg[7] & 0x0F)

/* Expand one pattern byte into 8 pixels of foreground / background colour */
#define PUT8(lb, pat, fg, bg)                       \
    do {                                            \
        int _p = (pat), _x;                         \
        for (_x = 0; _x < 8; _x++) {                \
            *(lb)++ = (_p & 0x80) ? (fg) : (bg);    \
            _p <<= 1;                               \
        }                                           \
    } while (0)

/* Split a colour table byte into 0x10-tagged fg/bg, transparent -> backdrop */
#define SPLIT_COLOUR(c, fg, bg, bd)                 \
    do {                                            \
        (fg) = ((c) >> 4) & 0x0F;                   \
        (bg) = (c) & 0x0F;                          \
        if (!(fg)) (fg) = (bd);                     \
        if (!(bg)) (bg) = (bd);                     \
        (fg) |= 0x10;                               \
        (bg) |= 0x10;                               \
    } while (0)

/* Graphics I/II are what nearly every cartridge uses. Text, Multicolor and the
   invalid modes are rare, so they are kept out of line: whatever sits on the
   per-scanline path competes with the Z80 interpreter for the VR4300's
   direct-mapped instruction cache. */
#define TMS_COLD __attribute__((noinline))

static void TMS_COLD fill(uint8 *lb, int count, uint8 colour)
{
    while (count--) *lb++ = colour;
}

/* Graphics I: 32x24 names, one colour byte per 8 patterns */
static void render_bg_m0(int line)
{
    int column;
    uint8 *lb = linebuf;
    uint8 bd = BACKDROP;
    const uint8 *pn = &vdp.vram[PN_BASE + ((line >> 3) << 5)];
    const uint8 *ct = &vdp.vram[vdp.reg[3] << 6];
    const uint8 *pg = &vdp.vram[PG_BASE | (line & 7)];

    for (column = 0; column < 32; column++) {
        int name = pn[column];
        uint8 fg, bg;
        SPLIT_COLOUR(ct[name >> 3], fg, bg, bd);
        PUT8(lb, pg[name << 3], fg, bg);
    }
}

/* Graphics II: a pattern and colour byte per line of each of 768 names.
   R3/R4 select the table base (bit 7 / bit 2) and mask the name, as on the
   TMS9918A; the colour mask also limits the pattern mask. */
static void render_bg_m2(int line)
{
    int column;
    uint8 *lb = linebuf;
    uint8 bd = BACKDROP;
    int v_row = line & 7;
    int third = (line >> 6) << 8;
    int ct_mask = ((vdp.reg[3] & 0x7F) << 3) | 7;
    int pg_mask = ((vdp.reg[4] & 0x03) << 8) | (ct_mask & 0xFF);
    const uint8 *pn = &vdp.vram[PN_BASE + ((line >> 3) << 5)];
    const uint8 *ct = &vdp.vram[((vdp.reg[3] & 0x80) << 6) | v_row];
    const uint8 *pg = &vdp.vram[((vdp.reg[4] & 0x04) << 11) | v_row];

    for (column = 0; column < 32; column++) {
        int name = pn[column] | third;
        uint8 fg, bg;
        SPLIT_COLOUR(ct[(name & ct_mask) << 3], fg, bg, bd);
        PUT8(lb, pg[(name & pg_mask) << 3], fg, bg);
    }
}

/* Text: 40x24 names of 6 pixels, colours from R7, 8 pixel borders */
static void TMS_COLD render_bg_m1(int line)
{
    int column, x;
    int row = line >> 3;
    uint8 *lb = linebuf;
    uint8 bg = BACKDROP;
    uint8 fg = (vdp.reg[7] >> 4) & 0x0F;
    const uint8 *pn = &vdp.vram[PN_BASE + (row << 5) + (row << 3)];
    const uint8 *pg = &vdp.vram[PG_BASE | (line & 7)];

    if (!fg) fg = bg;
    fg |= 0x10;
    bg |= 0x10;

    fill(lb, 8, bg);
    lb += 8;
    for (column = 0; column < 40; column++) {
        int pat = pg[pn[column] << 3];
        for (x = 0; x < 6; x++) {
            *lb++ = (pat & 0x80) ? fg : bg;
            pat <<= 1;
        }
    }
    fill(lb, 8, bg);
}

/* Invalid mode combinations: 40 columns of 4 foreground + 2 background pixels */
static void TMS_COLD render_bg_inv(int line)
{
    int column;
    uint8 *lb = linebuf;
    uint8 bg = BACKDROP;
    uint8 fg = (vdp.reg[7] >> 4) & 0x0F;

    (void)line;
    if (!fg) fg = bg;
    fg |= 0x10;
    bg |= 0x10;

    fill(lb, 8, bg);
    lb += 8;
    for (column = 0; column < 40; column++) {
        lb[0] = lb[1] = lb[2] = lb[3] = fg;
        lb[4] = lb[5] = bg;
        lb += 6;
    }
    fill(lb, 8, bg);
}

/* Multicolor: each name is 2x2 blocks of 4x4 pixels; the pattern byte used
   depends on the name row, upper nibble left block, lower nibble right */
static void TMS_COLD render_bg_m3(int line)
{
    int column;
    uint8 *lb = linebuf;
    uint8 bd = BACKDROP;
    const uint8 *pn = &vdp.vram[PN_BASE + ((line >> 3) << 5)];
    const uint8 *pg = &vdp.vram[PG_BASE + ((line >> 2) & 7)];

    for (column = 0; column < 32; column++) {
        int c = pg[pn[column] << 3];
        uint8 l = (c >> 4) & 0x0F;
        uint8 r = c & 0x0F;
        if (!l) l = bd;
        if (!r) r = bd;
        l |= 0x10;
        r |= 0x10;
        lb[0] = lb[1] = lb[2] = lb[3] = l;
        lb[4] = lb[5] = lb[6] = lb[7] = r;
        lb += 8;
    }
}

/* Every mode writes all 256 bytes of the line: render_line() claims the whole
   row in the data cache without reading it first. */
static void tms_render_bg(int line)
{
    /* R1 bit 4 = Text, R0 bit 1 = Graphics II, R1 bit 3 = Multicolor */
    int mode = ((vdp.reg[1] >> 1) & 4) | (vdp.reg[0] & 2) | ((vdp.reg[1] >> 4) & 1);

    switch (mode) {
        case 0: render_bg_m0(line); break;  /* Graphics I */
        case 1:                             /* Text */
        case 3: render_bg_m1(line); break;  /* Text + Graphics II bit */
        case 2: render_bg_m2(line); break;  /* Graphics II */
        case 4:                             /* Multicolor */
        case 6: render_bg_m3(line); break;  /* Multicolor + Graphics II bit */
        default: render_bg_inv(line); break;
    }
}

/* A sprite latched for the current line */
typedef struct {
    int xpos;
    uint8 colour;
    uint8 pat[2];
} tms_sprite;

/* Find the sprites on this line, at most four, in priority order, and latch the
   5S flag and fifth sprite number. The hardware does that whether or not the
   picture is looked at, so the drawing pass and the skipped-frame pass both come
   through here. Returns the number of sprites found. */
static int scan_sprites(int line, tms_sprite *found)
{
    int count = 0;
    int i;

    /* No sprites in Text mode */
    if (vdp.reg[1] & 0x10) return 0;

    int size = (vdp.reg[1] & 0x02) ? 16 : 8;
    int zoom = vdp.reg[1] & 0x01;
    int height = size << zoom;
    int sa = (vdp.reg[5] & 0x7F) << 7;
    int sg = (vdp.reg[6] & 0x07) << 11;
    int overflow = 0;

    for (i = 0; i < 32; i++, sa += 4) {
        int yp = vdp.vram[sa];

        /* End of sprite list marker */
        if (yp == 0xD0) break;

        /* Wrap Y coordinate for sprites entering at the top; a sprite is
           displayed one line below its Y value */
        if (yp > 0xE0) yp -= 256;
        int dy = line - (yp + 1);
        if (dy < 0 || dy >= height) continue;

        /* More than four sprites on this line: flag and stop */
        if (count == 4) {
            overflow = 1;
            break;
        }

        int name = vdp.vram[sa + 2] << 3;
        int attr = vdp.vram[sa + 3];
        dy >>= zoom;

        found[count].xpos = vdp.vram[sa + 1] - ((attr & 0x80) ? 32 : 0);
        found[count].colour = attr & 0x0F;
        /* 16x16 patterns are 32 bytes: left column rows 0-15, then right */
        found[count].pat[0] = vdp.vram[(sg + name + dy) & 0x3FFF];
        found[count].pat[1] = (size == 16) ? vdp.vram[(sg + name + 16 + dy) & 0x3FFF] : 0;
        count++;
    }

    /* Status: the fifth sprite number stays latched while 5S is set */
    if (!(vdp.status & 0x40)) {
        vdp.status = (vdp.status & 0xE0) | ((i > 31) ? 31 : i);
        if (overflow) vdp.status |= 0x40;
    }

    return count;
}

static void tms_render_obj(int line)
{
    tms_sprite found[4];
    int count = scan_sprites(line, found);
    int size = (vdp.reg[1] & 0x02) ? 16 : 8;
    int zoom = vdp.reg[1] & 0x01;
    int width = size << zoom;
    int n;

    /* Draw in priority order: an earlier sprite keeps its pixels */
    for (n = 0; n < count; n++) {
        int xpos = found[n].xpos;
        int start = (xpos < 0) ? -xpos : 0;
        int end = (xpos + width > 256) ? 256 - xpos : width;
        uint8 colour = found[n].colour;
        uint8 *lb = linebuf + xpos;
        int x;

        for (x = start; x < end; x++) {
            int px = x >> zoom;
            int bit = (px < 8) ? (found[n].pat[0] << px) : (found[n].pat[1] << (px - 8));

            if (!(bit & 0x80)) continue;

            if (lb[x] & 0x40) {
                /* Two sprite pattern bits overlap */
                vdp.status |= 0x20;
                continue;
            }
            /* Transparent sprites still mark pixels for collision */
            lb[x] = colour ? (0x50 | colour) : (lb[x] | 0x40);
        }
    }
}

void tms_render_line(int line)
{
    /* R1 bit 6 enables the display */
    if (vdp.reg[1] & 0x40) {
        tms_render_bg(line);
        tms_render_obj(line);
    } else {
        __builtin_memset(linebuf, BACKDROP_COLOR, SMS_WIDTH);
    }
}

/* The sprite status of a line whose pixels are being skipped.

   Collision is sprite against sprite only: the background never carries the
   0x40 marker tms_render_obj() tests, so a coverage bitmap of this line's
   sprites answers the same question without a line buffer. Zoomed 16x16 sprites
   are 32 pixels wide, which is why this is not render.c's spr_cov_merge(): that
   one assumes a sprite fits in 16 bits. */
void tms_line_collision(int line)
{
    tms_sprite found[4];
    uint32 cov[8];
    int count, size, zoom, width, n;

    /* Only where render_line() would have drawn sprites: the active display,
       with the display enabled */
    if (line >= SMS_HEIGHT || !(vdp.reg[1] & 0x40)) return;

    count = scan_sprites(line, found);
    size = (vdp.reg[1] & 0x02) ? 16 : 8;
    zoom = vdp.reg[1] & 0x01;
    width = size << zoom;

    /* One sprite cannot collide, and the flag is sticky until the status port
       is read, which cannot happen before the next line. The 5S bits above
       have to be latched either way. */
    if (count < 2 || (vdp.status & 0x20)) return;

    __builtin_memset(cov, 0, sizeof(cov));

    for (n = 0; n < count; n++) {
        int xpos = found[n].xpos;
        int start = (xpos < 0) ? -xpos : 0;
        int end = (xpos + width > 256) ? 256 - xpos : width;
        int x;

        for (x = start; x < end; x++) {
            int px = x >> zoom;
            int bit = (px < 8) ? (found[n].pat[0] << px) : (found[n].pat[1] << (px - 8));
            int sx = xpos + x;
            uint32 mask = 1u << (sx & 31);

            if (!(bit & 0x80)) continue;

            if (cov[sx >> 5] & mask) {
                vdp.status |= 0x20;
                return;
            }
            cov[sx >> 5] |= mask;
        }
    }
}
