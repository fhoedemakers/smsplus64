
#include "shared.h"

/* Background drawing function */
void (*render_bg)(int line);

/* Pointer to output buffer */
uint8 *linebuf;

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
    int i;
    for (i = 0; i < bytes; i += DCACHE_LINE_SIZE)
    {
        __asm__ __volatile__("cache 0xD, 0(%0)" : : "r"(start + i) : "memory");
    }
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

            /* Draw double size sprite */
            if (vdp.reg[1] & 0x01)
            {
                int x;
                ctp = getCache((n & 0x1ff) + ((line - yp) >> 3), (n >> 9) & 3);
                uint8 *cache_ptr = (uint8 *)&ctp[(((line - yp) >> 1) << 3)];

                /* Draw sprite line */
                for (x = start; x < end; x += 1)
                {
                    /* Source pixel from cache */
                    uint8 sp = cache_ptr[(x >> 1)];

                    /* Only draw opaque sprite pixels */
                    if (sp)
                    {
                        /* Background pixel from line buffer */
                        uint8 bg = linebuf_ptr[x];

                        /* Resolve sprite against background */
                        linebuf_ptr[x] = sprite_mix(bg, sp);

                        /* Set sprite collision flag */
                        if (bg & 0x40)
                            vdp.status |= 0x20;
                    }
                }
            }
            else /* Regular size sprite (8x8 / 8x16) */
            {
                int x;
                ctp = getCache((n & 0x1ff) + ((line - yp) >> 3), (n >> 9) & 3);
                uint8 *cache_ptr = (uint8 *)&ctp[((line - yp) << 3) & 0x38];

                /* Draw sprite line */
                for (x = start; x < end; x += 1)
                {
                    /* Source pixel from cache */
                    uint8 sp = cache_ptr[x];

                    /* Only draw opaque sprite pixels */
                    if (sp)
                    {
                        /* Background pixel from line buffer */
                        uint8 bg = linebuf_ptr[x];

                        /* Resolve sprite against background */
                        linebuf_ptr[x] = sprite_mix(bg, sp);

                        /* Set sprite collision flag */
                        if (bg & 0x40)
                            vdp.status |= 0x20;
                    }
                }
            }
        }
    }
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
