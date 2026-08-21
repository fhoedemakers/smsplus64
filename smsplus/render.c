
#include "shared.h"

/* Background drawing function */
void (*render_bg)(int line);

/* Pointer to output buffer */
uint8 *linebuf;

/* Sprite occupancy for the line being scanned, one bit per x.

   Collision is sprite against sprite: the background contributes nothing, which
   is what lets a skipped frame work it out without drawing anything. This is
   deliberately not the 0x40 marker the line buffer carries, which a priority
   background suppresses (sprite_mix returns the background pixel untagged, so a
   second sprite over that pixel went unreported) and which nothing ever clears
   in the Game Gear margins, where it accumulated into permanent false hits.

   32 bytes is two cache lines, so clearing it per line is a handful of stores. */
static uint32 spr_cov[8];

/* Fold one sprite's pixel coverage into the line and raise the collision flag if
   it lands on anything already covered. `cov` has bit k set for the pixel at
   x0 + k, for k < n.

   Merging a whole sprite at once rather than testing each pixel keeps the inner
   loops down to a shift and an or: the address arithmetic for a per-pixel test
   gets hoisted above the transparency check, so it would be paid on every pixel
   a sprite covers, opaque or not. Sprites within a line cannot overlap
   themselves, so deferring the merge to the end of one changes nothing. */
static __attribute__((noinline)) void (spr_cov_merge)(int x0, int n, uint32 cov)
{
    uint32 *word;
    uint32 low;

    if (!cov)
        return;

    word = &spr_cov[x0 >> 5];
    x0 &= 31;
    low = cov << x0;

    if (*word & low)
        vdp.status |= 0x20;
    *word |= low;

    /* A sprite is at most 16 pixels wide, so it straddles at most one word
       boundary, and only when x0 is past 16 - which keeps the shift below
       inside 1..15. */
    if ((x0 + n) > 32)
    {
        uint32 high = cov >> (32 - x0);

        word += 1;
        if (*word & high)
            vdp.status |= 0x20;
        *word |= high;
    }
}

/* Precalculated pixel table */
//uint16 pixel[PALETTE_SIZE];

// Each tile takes up 8*8=64 bytes. We have 512 tiles * 4 attribs, so 2K tiles max.
#define CACHEDTILES 512

int16 cachePtr[512 * 4];            //(tile+attr<<9) -> cache tile store index (i<<6); -1 if not cached
uint8 cacheStore[CACHEDTILES * 64]; // Tile store
uint8 cacheStoreUsed[CACHEDTILES];  // Marks if a tile is used

// Share memory for using in main.cpp and menu.cpp when emulator is not running
uint8_t *getcachestorefromemulator(size_t *size) {
    *size = CACHEDTILES * 64;
    printf("Acquired cacheStore from emulator: %d bytes\n", *size);
    return cacheStore;
}
uint8 is_vram_dirty;

int cacheKillPtr = 0;
int freePtr = 0;

/* Sprite/background priority resolution.
   This replaces the old 65536-byte `lut` table (lut.h): a scattered read into
   64K against the VR4300's 8K data cache missed on nearly every sprite pixel.
   The table was pure combinational logic, reproduced here:

     bg & 0x40          -> a sprite was already drawn here, it stays on top
     bg & 0x20 and      -> background pixel has priority and is not transparent
       bg & 0x0F
     otherwise          -> the sprite pixel wins, tagged 0x10 (sprite palette)
                           and 0x40 (marks the pixel as sprite-occupied)

   Only ever called with sp != 0, which is why the transparent-sprite case of
   the original table does not appear. */
static __inline__ uint8 (sprite_mix)(uint8 bg, uint8 sp)
{
    if (bg & 0x40) return (bg & 0x7F);
    if ((bg & 0x20) && (bg & 0x0F)) return (bg & 0x7F);
    return (sp | 0x50);
}

/* Opacity of one row of a sprite pattern: bit k is set when pixel k is not
   transparent. Left to right, so bit 0 is the leftmost pixel.

   The pattern cache gathers the four bit-plane bits of a pixel into a nibble,
   so a pixel is opaque exactly when any plane has its bit set - which the four
   VRAM bytes answer directly, with no tile to expand. That keeps the collision
   pass off the pattern cache: no getCacheSlow() expansions, no evictions and no
   pollution of it on a frame nobody is going to look at. It also replaces the
   per-pixel loop with straight-line code, which matters more than the
   instruction count: the drawing loop unrolls to some 2.7K of code, and running
   that on every scanline evicts the Z80 interpreter from the VR4300's 16K
   direct-mapped instruction cache.

   Sprites carry no flip bits - only background tiles do - so there is no
   attribute to fold in here. */
static __inline__ uint32 (spr_row_opacity)(int tile, int row)
{
    const uint8 *p = &vdp.vram[(tile << 5) | ((row & 7) << 2)];

    /* Bit 7 is the leftmost pixel, as the pattern cache reads it */
    uint32 op = p[0] | p[1] | p[2] | p[3];

    /* Turn it around so bit k is pixel k */
    op = ((op & 0xF0) >> 4) | ((op & 0x0F) << 4);
    op = ((op & 0xCC) >> 2) | ((op & 0x33) << 2);
    op = ((op & 0xAA) >> 1) | ((op & 0x55) << 1);

    return op;
}

/* Which sprites land on each scanline, built once per frame instead of being
   rediscovered per line.

   The drawing pass walks all 64 attribute table entries on every scanline to
   find the few that cross it. Measured on Sonic and Aladdin that is 11750
   entries examined per frame against 186 sprite rows actually evaluated: over
   95% of the work is the y-range test, and neither game ever writes the 208
   end-of-list marker that would cut the scan short. Turning it inside out -
   once per sprite, over the 8 or 16 lines it covers - replaces some 12000
   y-tests with about 512 appends.

   Capped at the 8 sprites the hardware draws per line, so the list holds
   exactly the set the drawing pass would have processed before its ninth-sprite
   bail-out. That assumes vdp.limit is set, which it is throughout this port
   (vdp_reset raises it and nothing clears it); were it ever cleared, collisions
   involving the ninth sprite onwards would go unreported. */

#define SPR_PER_LINE 8
#define SPR_MAX_LINES 192
static uint8 spr_line_list[SPR_MAX_LINES][SPR_PER_LINE];
static uint8 spr_line_count[SPR_MAX_LINES];

/* Rebuild triggers: a write into the attribute table, a move of the table, or a
   change of sprite size. The x and pattern bytes do not affect which lines a
   sprite covers, but they share the table and are not worth telling apart. */
static int spr_list_dirty = 1;
static int spr_list_satb = -1;
static int spr_list_size = -1;

static void (spr_list_build)(void)
{
    uint8 *st = (uint8 *)&vdp.vram[vdp.satb];
    int height = (vdp.reg[1] & 0x02) ? 16 : 8;
    int i;

    if (vdp.reg[1] & 0x01)
        height *= 2;

    __builtin_memset(spr_line_count, 0, sizeof(spr_line_count));

    for (i = 0; i < 64; i += 1)
    {
        /* Y position, as the drawing pass reads it */
        int yp = st[i];
        int y0, y1, y;

        /* End of sprite list marker? */
        if (yp == 208)
            break;

        /* Actual Y position is +1, and wraps for sprites > 240 */
        yp += 1;
        if (yp > 240)
            yp -= 256;

        y0 = yp;
        y1 = yp + height;
        if (y0 < 0)
            y0 = 0;
        if (y1 > SPR_MAX_LINES)
            y1 = SPR_MAX_LINES;

        for (y = y0; y < y1; y += 1)
        {
            if (spr_line_count[y] < SPR_PER_LINE)
                spr_line_list[y][spr_line_count[y]++] = (uint8)i;
        }
    }

    spr_list_dirty = 0;
    spr_list_satb = vdp.satb;
    spr_list_size = (vdp.reg[1] & 0x03);
}

/* Attribute expansion table */
uint32 atex[4] =
    {
        0x00000000,
        0x10101010,
        0x20202020,
        0x30303030,
};

/* Display sizes */
int vp_vstart;
int vp_vend;
int vp_hstart;
int vp_hend;

void render_bg_sms(int line);

void render_bg_gg(int line);

void render_obj(int line);

void palette_sync(int index);

void render_reset(void);

void render_init(void);

void (vramMarkTileDirty)(int index)
{
    int i = index;

    /* The attribute table lives in VRAM, so the tile invalidation this already
       gets on every changed byte doubles as the signal that the per-line sprite
       lists are stale. The table is 256 bytes, which is 8 tiles' worth. */
    if ((unsigned)(index - (vdp.satb >> 5)) < 8)
        spr_list_dirty = 1;

    while (i < 0x800)
    {
        if (cachePtr[i] != -1)
        {
            freePtr = cachePtr[i] >> 6;
            //			printf("Freeing cache loc %d for tile %d\n", freePtr, index);
            cacheStoreUsed[freePtr] = 0;
            cachePtr[i] = -1;
        }
        i += 0x200;
    }
}


// Slow path: the tile is not expanded yet. Kept out of line so the hit path
// below stays a handful of instructions - this is called ~6000 times per frame
// and the old single function saved and restored nine registers on every hit.
static uint8 *(getCacheSlow)(int tile, int attr)
{
    int n, i, x, y, c;
    int b0, b1, b2, b3;
    int i0, i1, i2, i3;
    static bool isoverFlowed = false;

    // Generate cache tile.
    // Find free cache idx first.
    do
    {
        i = freePtr;
        n = 0;
        while (cacheStoreUsed[i] && n < CACHEDTILES)
        {
            i++;
            n++;
            if (i == CACHEDTILES)
                i = 0;
        }

        if (n == CACHEDTILES)
        {
            // Print only once
            if ( ! isoverFlowed ) {
                printf("Eek, tile cache overflow\n");
                isoverFlowed = true;
            }
            // Crap, out of cache. Kill a tile.
            vramMarkTileDirty(cacheKillPtr++);
            if (cacheKillPtr >= 512)
                cacheKillPtr = 0;
            i = freePtr;
        }
    } while (cacheStoreUsed[i]);
    // Okay, somehow we have a free cache tile in i now.
    cacheStoreUsed[i] = 1;
    cachePtr[tile + (attr << 9)] = i << 6;

    //	printf("Generating cache loc %d for tile %d attr %d\n", i, tile, attr);
    // Calculate tile
    for (y = 0; y < 8; y += 1)
    {
        b0 = vdp.vram[(tile << 5) | (y << 2) | (0)];
        b1 = vdp.vram[(tile << 5) | (y << 2) | (1)];
        b2 = vdp.vram[(tile << 5) | (y << 2) | (2)];
        b3 = vdp.vram[(tile << 5) | (y << 2) | (3)];
        for (x = 0; x < 8; x += 1)
        {
            i0 = (b0 >> (x ^ 7)) & 1;
            i1 = (b1 >> (x ^ 7)) & 1;
            i2 = (b2 >> (x ^ 7)) & 1;
            i3 = (b3 >> (x ^ 7)) & 1;

            c = (i3 << 3 | i2 << 2 | i1 << 1 | i0);
            if (attr == 0)
                cacheStore[(i << 6) | (y << 3) | (x)] = c;
            if (attr == 1)
                cacheStore[(i << 6) | (y << 3) | (x ^ 7)] = c;
            if (attr == 2)
                cacheStore[(i << 6) | ((y ^ 7) << 3) | (x)] = c;
            if (attr == 3)
                cacheStore[(i << 6) | ((y ^ 7) << 3) | (x ^ 7)] = c;
        }
    }
    return &cacheStore[i << 6];
}

// Fast path: the tile is already expanded in the pattern cache, which is the
// overwhelmingly common case. Inlines to a load, a test and an add.
static __inline__ uint8 *(getCache)(int tile, int attr)
{
    int idx = cachePtr[tile + (attr << 9)];
    if (__builtin_expect(idx >= 0, 1))
        return &cacheStore[idx];
    return getCacheSlow(tile, attr);
}

/* Access memory 32-bits at a time (from MAME's drawgfx.c).
   The line buffer is written at a horizontal-scroll offset, so these are
   potentially unaligned. Going through a byte-aligned type makes the compiler
   emit MIPS lwl/lwr and swl/swr, which handle any alignment in two
   instructions - the previous version tested alignment at runtime and
   branched into a byte-wise fallback twice per background column.

   Note the callers hand these a uint32*, so the alignment has to be spelled
   out here: without it the compiler infers 4-byte alignment from the pointer
   type and emits a plain sw, which faults on a horizontal scroll that is not
   a multiple of four. */

typedef struct { uint32 v; } __attribute__((packed, aligned(1))) unaligned_dword;

static __inline__ uint32 (read_dword)(const void *address)
{
    return ((const unaligned_dword *)address)->v;
}

static __inline__ void (write_dword)(void *address, uint32 data)
{
    ((unaligned_dword *)address)->v = data;
}

/****************************************************************************/

/* Initialize the rendering data */
void render_init(void)
{
    /* The 64K priority look-up table this used to build now lives as
       arithmetic in sprite_mix(). */
    render_reset();
}

/* Reset the rendering data */
void (render_reset)(void)
{
    int i;

    /* Clear the CI8 frame the renderer draws into, so a reset does not leave
       the previous game's image on screen. */
    __builtin_memset(sms_line_target, 0, SMS_WIDTH * SMS_HEIGHT);

    /* Clear palette */
    for (i = 0; i < PALETTE_SIZE; i += 1)
    {
        palette_sync(i);
    }

    /* Invalidate pattern cache.

       cacheStoreUsed has to be cleared here explicitly. This used to rely on
       vramMarkTileDirty(), but that only releases a slot while cachePtr still
       points at it, and cachePtr is wiped just above - so it released nothing
       and every slot stayed marked as occupied for the next game. Starting a
       second game then had to scan ever further for a free slot, which showed
       up as the renderer slowing down, and once all the slots were marked used
       the search loop in getCacheSlow() could not terminate at all. */
    for (i = 0; i < 512 * 4; i++)
        cachePtr[i] = -1;
    __builtin_memset(cacheStoreUsed, 0, sizeof(cacheStoreUsed));
    freePtr = 0;
    cacheKillPtr = 0;
    spr_list_dirty = 1;

    /* Set up viewport size */
    if (IS_GG)
    {
        vp_vstart = 24;
        vp_vend = 168;
        vp_hstart = 6;
        vp_hend = 26;
    }
    else
    {
        vp_vstart = 0;
        vp_vend = 192;
        vp_hstart = 0;
        vp_hend = 32;
    }

    /* Pick render routine */
    render_bg = IS_GG ? render_bg_gg : render_bg_sms;
}

/* Claim a range of the CI8 frame in the data cache without fetching it from
   RDRAM first.

   A normal write miss on the VR4300 reads the 16-byte line in before the store
   can happen. Every scanline is completely overwritten here and is then only
   ever read by the RDP, so that read is pure waste: 3072 line fills per frame
   for data nobody looks at. "Create Dirty Exclusive" (cache op 0xD) marks the
   line valid and dirty and skips the fetch.

   The caller must overwrite every byte of the range afterwards. Whatever is
   left untouched is stale cache contents and gets written back as if it were
   pixel data. */
#define DCACHE_LINE_SIZE 16
static __inline__ void (claim_dcache_range)(uint8 *start, int bytes)
{
#ifdef N64
    int i;
    for (i = 0; i < bytes; i += DCACHE_LINE_SIZE)
    {
        __asm__ __volatile__("cache 0xD, 0(%0)" : : "r"(start + i) : "memory");
    }
#else
    /* Host builds (the collision test harness) have no such instruction, and
       nothing to gain from it - the caller overwrites the range regardless. */
    (void)start; (void)bytes;
#endif
}

/* Draw a line of the display */
void (render_line)(int line)
{
    /* Ensure we're within the viewport range. Lines outside it fall outside
       the rectangle the RDP blits to the framebuffer, so there is nothing to
       draw for them. */
    if ((line < vp_vstart) || (line >= vp_vend))
        return;

    /* Point straight at this line inside the CI8 frame the RDP will read.
       Rendering here directly removes the 256-byte copy per scanline that
       the old scratch line buffer needed. */
    linebuf = sms_line_target + (line * SMS_WIDTH);

    /* Claim exactly the span both branches below rewrite in full: the whole
       256-byte row for the Master System, and only the visible window for the
       Game Gear. The Game Gear margins are deliberately left out - nothing
       rewrites them, and render_obj reads the line back to resolve sprite
       priority, so stale bytes there must stay as they are. */
    claim_dcache_range(linebuf + (vp_hstart << 3), BMP_WIDTH);

    /* Blank line */
    if ((!(vdp.reg[1] & 0x40)) || (((vdp.reg[2] & 1) == 0) && (IS_SMS)))
    {
        __builtin_memset(linebuf + (vp_hstart << 3), BACKDROP_COLOR, BMP_WIDTH);
    }
    else
    {
        /* Draw background */
        render_bg(line);

        /* Draw sprites */
        render_obj(line);

        /* Blank leftmost column of display */
        if (vdp.reg[0] & 0x20)
        {
            __builtin_memset(linebuf, BACKDROP_COLOR, 8);
        }
    }
}

/* Draw the Master System background */
void (render_bg_sms)(int line)
{
    int locked = 0;
    int v_line = (line + vdp.reg[9]) % 224;
    int v_row = (v_line & 7) << 3;
    int hscroll = ((vdp.reg[0] & 0x40) && (line < 0x10)) ? 0 : (0x100 - vdp.reg[8]);
    int column = vp_hstart;
    uint16 attr;
    uint16 *nt = (uint16 *)&vdp.vram[vdp.ntab + ((v_line >> 3) << 6)];
    int nt_scroll = (hscroll >> 3);
    int shift = (hscroll & 7);
    uint32 atex_mask;
    uint32 *cache_ptr;
    uint32 *linebuf_ptr = (uint32 *)&linebuf[0 - shift];
    uint8 *ctp;

    /* Draw first column (clipped) */
    if (shift)
    {
        int x, c, a;

        attr = nt[(column + nt_scroll) & 0x1F];

#ifndef LSB_FIRST
        attr = (((attr & 0xFF) << 8) | ((attr & 0xFF00) >> 8));
#endif
        a = (attr >> 7) & 0x30;

        for (x = shift; x < 8; x += 1)
        {
            ctp = getCache((attr & 0x1ff), (attr >> 9) & 3);
            c = ctp[(v_row) | (x)];
            linebuf[(0 - shift) + (x)] = ((c) | (a));
        }

        column += 1;
    }

    /* Draw a line of the background */
    for (; column < vp_hend; column += 1)
    {
        /* Stop vertical scrolling for leftmost eight columns */
        if ((vdp.reg[0] & 0x80) && (!locked) && (column >= 24))
        {
            locked = 1;
            v_row = (line & 7) << 3;
            nt = (uint16 *)&vdp.vram[((vdp.reg[2] << 10) & 0x3800) + ((line >> 3) << 6)];
        }

        /* Get name table attribute word */
        attr = nt[(column + nt_scroll) & 0x1F];

#ifndef LSB_FIRST
        attr = (((attr & 0xFF) << 8) | ((attr & 0xFF00) >> 8));
#endif
        /* Expand priority and palette bits */
        atex_mask = atex[(attr >> 11) & 3];

        /* Point to a line of pattern data in cache */
        ctp = getCache((attr & 0x1ff), (attr >> 9) & 3);
        cache_ptr = (uint32 *)&ctp[(v_row)];

        /* Copy the left half, adding the attribute bits in */
        write_dword(&linebuf_ptr[(column << 1)], read_dword(&cache_ptr[0]) | (atex_mask));

        /* Copy the right half, adding the attribute bits in */
        write_dword(&linebuf_ptr[(column << 1) | (1)], read_dword(&cache_ptr[1]) | (atex_mask));
    }

    /* Draw last column (clipped) */
    if (shift)
    {
        int x, c, a;

        char *p = &linebuf[(0 - shift) + (column << 3)];

        attr = nt[(column + nt_scroll) & 0x1F];

#ifndef LSB_FIRST
        attr = (((attr & 0xFF) << 8) | ((attr & 0xFF00) >> 8));
#endif
        a = (attr >> 7) & 0x30;

        for (x = 0; x < shift; x += 1)
        {
            ctp = getCache((attr & 0x1ff), (attr >> 9) & 3);
            c = ctp[(v_row) | (x)];
            p[x] = ((c) | (a));
        }
    }
}

/* Draw the Game Gear background */
void render_bg_gg(int line)
{
    int v_line = (line + vdp.reg[9]) % 224;
    int v_row = (v_line & 7) << 3;
    int hscroll = (0x100 - vdp.reg[8]);
    int column;
    uint16 attr;
    uint16 *nt = (uint16 *)&vdp.vram[vdp.ntab + ((v_line >> 3) << 6)];
    int nt_scroll = (hscroll >> 3);
    uint32 atex_mask;
    uint32 *cache_ptr;
    uint32 *linebuf_ptr = (uint32 *)&linebuf[0 - (hscroll & 7)];
    uint8_t *ctp;

    /* Draw a line of the background */
    for (column = vp_hstart; column <= vp_hend; column += 1)
    {
        /* Get name table attribute word */
        attr = nt[(column + nt_scroll) & 0x1F];

#ifndef LSB_FIRST
        attr = (((attr & 0xFF) << 8) | ((attr & 0xFF00) >> 8));
#endif
        /* Expand priority and palette bits */
        atex_mask = atex[(attr >> 11) & 3];

        /* Point to a line of pattern data in cache */
        ctp = getCache((attr & 0x1ff), (attr >> 9) & 3);
        cache_ptr = (uint32 *)&ctp[(v_row)];

        /* Copy the left half, adding the attribute bits in */
        write_dword(&linebuf_ptr[(column << 1)], read_dword(&cache_ptr[0]) | (atex_mask));

        /* Copy the right half, adding the attribute bits in */
        write_dword(&linebuf_ptr[(column << 1) | (1)], read_dword(&cache_ptr[1]) | (atex_mask));
    }
}

/* Draw sprites */
void (render_obj)(int line)
{
    int i;
    uint8_t *ctp;

    /* Sprite count for current line (8 max.) */
    int count = 0;

    /* Nothing is covered yet on this line */
    __builtin_memset(spr_cov, 0, sizeof(spr_cov));

    /* Sprite dimensions */
    int width = 8;
    int height = (vdp.reg[1] & 0x02) ? 16 : 8;

    /* Pointer to sprite attribute table */
    uint8 *st = (uint8 *)&vdp.vram[vdp.satb];

    /* Adjust dimensions for double size sprites */
    if (vdp.reg[1] & 0x01)
    {
        width *= 2;
        height *= 2;
    }

    /* Draw sprites in front-to-back order */
    for (i = 0; i < 64; i += 1)
    {
        /* Sprite Y position */
        int yp = st[i];

        /* End of sprite list marker? */
        if (yp == 208)
            return;

        /* Actual Y position is +1 */
        yp += 1;

        /* Wrap Y coordinate for sprites > 240 */
        if (yp > 240)
            yp -= 256;

        /* Check if sprite falls on current line */
        if ((line >= yp) && (line < (yp + height)))
        {
            uint8 *linebuf_ptr;

            /* Width of sprite */
            int start = 0;
            int end = width;

            /* Sprite X position */
            int xp = st[0x80 + (i << 1)];

            /* Pattern name */
            int n = st[0x81 + (i << 1)];

            /* Bump sprite count */
            count += 1;

            /* Too many sprites on this line ? */
            if ((vdp.limit) && (count == 9))
                return;

            /* X position shift */
            if (vdp.reg[0] & 0x08)
                xp -= 8;

            /* Add MSB of pattern name */
            if (vdp.reg[6] & 0x04)
                n |= 0x0100;

            /* Mask LSB for 8x16 sprites */
            if (vdp.reg[1] & 0x02)
                n &= 0x01FE;

            /* Point to offset in line buffer */
            linebuf_ptr = (uint8 *)&linebuf[xp];

            /* Clip sprites on left edge */
            if (xp < 0)
            {
                start = (0 - xp);
            }

            /* Clip sprites on right edge */
            if ((xp + width) > 256)
            {
                end = (256 - xp);
            }

            /* Where the sprite lands, bit k for the pixel at xp + start + k.
               The clipping above keeps xp + start inside 0..255. */
            uint32 cov = 0;

            /* Draw double size sprite */
            if (vdp.reg[1] & 0x01)
            {
                int x;
                uint32 bit = 1;
                ctp = getCache((n & 0x1ff) + ((line - yp) >> 3), (n >> 9) & 3);
                /* The mask keeps this inside the 64-byte tile. Without it a
                   zoomed 8x16 sprite runs off the end of its cache entry and
                   reads whichever tile happens to sit in the next slot. */
                uint8 *cache_ptr = (uint8 *)&ctp[(((line - yp) >> 1) << 3) & 0x38];

                /* Draw sprite line */
                for (x = start; x < end; x += 1, bit <<= 1)
                {
                    /* Source pixel from cache */
                    uint8 sp = cache_ptr[(x >> 1)];

                    /* Only draw opaque sprite pixels */
                    if (sp)
                    {
                        /* Note where the sprite is for collision detection */
                        cov |= bit;

                        /* Resolve sprite against background */
                        linebuf_ptr[x] = sprite_mix(linebuf_ptr[x], sp);
                    }
                }
            }
            else /* Regular size sprite (8x8 / 8x16) */
            {
                int x;
                uint32 bit = 1;
                ctp = getCache((n & 0x1ff) + ((line - yp) >> 3), (n >> 9) & 3);
                uint8 *cache_ptr = (uint8 *)&ctp[((line - yp) << 3) & 0x38];

                /* Draw sprite line */
                for (x = start; x < end; x += 1, bit <<= 1)
                {
                    /* Source pixel from cache */
                    uint8 sp = cache_ptr[x];

                    /* Only draw opaque sprite pixels */
                    if (sp)
                    {
                        /* Note where the sprite is for collision detection */
                        cov |= bit;

                        /* Resolve sprite against background */
                        linebuf_ptr[x] = sprite_mix(linebuf_ptr[x], sp);
                    }
                }
            }

            spr_cov_merge(xp + start, end - start, cov);
        }
    }
}

/* Work out sprite collisions without drawing anything.

   Walks the precomputed list for this line rather than the whole attribute
   table. The drawing pass above re-reads all 64 entries per scanline to find
   the handful that land on it, which measured at 11750 entries examined per
   frame against 186 sprite rows actually evaluated - a 63x overhead that
   dwarfed everything else and is what made this too slow to ship at first. */
static void (render_obj_collision)(int line)
{
    const uint8 *list = spr_line_list[line];
    int n_line = spr_line_count[line];
    uint8 *st = (uint8 *)&vdp.vram[vdp.satb];
    int height = (vdp.reg[1] & 0x02) ? 16 : 8;
    int width = 8;
    int k;

    if (vdp.reg[1] & 0x01)
    {
        width *= 2;
        height *= 2;
    }

    /* Nothing is covered yet on this line */
    __builtin_memset(spr_cov, 0, sizeof(spr_cov));

    for (k = 0; k < n_line; k += 1)
    {
        int i = list[k];

        /* Y as the list was built from it */
        int yp = st[i] + 1;
        int row;

        int start = 0;
        int end = width;

        int xp = st[0x80 + (i << 1)];
        int n = st[0x81 + (i << 1)];
        uint32 cov;

        if (yp > 240)
            yp -= 256;
        row = line - yp;

        /* X position shift */
        if (vdp.reg[0] & 0x08)
            xp -= 8;

        /* Add MSB of pattern name */
        if (vdp.reg[6] & 0x04)
            n |= 0x0100;

        /* Mask LSB for 8x16 sprites */
        if (vdp.reg[1] & 0x02)
            n &= 0x01FE;

        /* Clip sprites on left and right edge */
        if (xp < 0)
            start = (0 - xp);
        if ((xp + width) > 256)
            end = (256 - xp);

        /* The row's opacity comes straight out of VRAM, so there is no pattern
           cache to touch and no pixel loop. */
        if (vdp.reg[1] & 0x01)
        {
            /* Double size: every source pixel covers two columns */
            uint32 src = spr_row_opacity((n & 0x1ff) + (row >> 3), row >> 1);
            int b;

            cov = 0;
            for (b = 0; b < 8; b += 1)
            {
                if (src & (1u << b))
                    cov |= 3u << (b << 1);
            }
        }
        else
        {
            cov = spr_row_opacity((n & 0x1ff) + (row >> 3), row);
        }

        /* Drop the pixels the clipping took off either edge */
        cov = (cov >> start) & ((1u << (end - start)) - 1);

        spr_cov_merge(xp + start, end - start, cov);
    }
}

/* Sprite collision for a frame whose pixels are being skipped.

   Games poll the collision flag for hit detection, and it is the only thing
   render_line() produces that the emulation itself can observe - so it has to
   be worked out on skipped frames too, or frameskip changes how a game plays.

   Mirrors render_line()'s gates so the flag goes up on exactly the scanlines it
   would have on a drawn frame. Touches neither linebuf nor sms_line_target: the
   buffer still holds the last displayed image while frames are being skipped. */
void (render_line_collision)(int line)
{
    /* The flag is sticky until the game reads the status port, and the Z80 only
       runs between scanlines, so once it is up nothing later in this frame can
       change the outcome. */
    if (vdp.status & 0x20)
        return;

    if ((line < vp_vstart) || (line >= vp_vend))
        return;

    /* Blank line - render_line() does not reach the sprites either */
    if ((!(vdp.reg[1] & 0x40)) || (((vdp.reg[2] & 1) == 0) && (IS_SMS)))
        return;

    if (spr_list_dirty || spr_list_satb != vdp.satb ||
        spr_list_size != (vdp.reg[1] & 0x03))
    {
        spr_list_build();
    }

    /* One sprite cannot collide with anything, and the coverage map starts
       empty on every line, so fewer than two is nothing to work out. This has
       to come before render_obj_collision() rather than inside it: measured on
       Aladdin and Sonic, 70% of lines carry no sprite at all and only 21% to
       31% carry two or more, so on most lines the setup - the call, the 32-byte
       coverage clear, the register and attribute table reads - was the entire
       cost. */
    if (spr_line_count[line] < 2)
        return;

    render_obj_collision(line);
}

/* Update pattern cache with modified tiles */

extern void sms_palette_sync(int index);
extern void sms_palette_syncGG(int index);

/* Update a palette entry */
void (palette_sync)(int index)
{
    // FH: Changed begin
#if 0
    int r, g, b;

    if (IS_GG)
    {
        r = ((vdp.cram[(index << 1) | 0] >> 1) & 7) << 5;
        g = ((vdp.cram[(index << 1) | 0] >> 5) & 7) << 5;
        b = ((vdp.cram[(index << 1) | 1] >> 1) & 7) << 5;
    }
    else
    {
        r = ((vdp.cram[index] >> 0) & 3) << 6;
        g = ((vdp.cram[index] >> 2) & 3) << 6;
        b = ((vdp.cram[index] >> 4) & 3) << 6;
    }
   
    bitmap.pal.color[index][0] = r;
    bitmap.pal.color[index][1] = g;
    bitmap.pal.color[index][2] = b;
  
    pixel[index] = MAKE_PIXEL(r, g, b);
#endif
    // FH: Changed end

    bitmap.pal.dirty[index] = bitmap.pal.update = 1;
    if (IS_GG)
    {
        sms_palette_syncGG(index);
    }
    else
    {
        sms_palette_sync(index);
    }
}
