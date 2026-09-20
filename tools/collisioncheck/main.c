/* Host-side harness: runs the SMS core headless and checks two things on every
   scanline of every frame.

   1. That the collision-only pass raises the sprite collision flag on exactly
      the lines the drawing path does.
   2. That the per-line sprite list both passes now walk holds exactly what the
      plain 64-entry attribute table scan would have found.

   The second check exists because the first one lost its independence: the
   drawing path used to rediscover the sprites on each scanline, which made it an
   oracle for the list. Now both passes read the list, so a list bug moves both
   answers together and check 1 cannot see it - while a list bug is now visible
   in the picture as well as in the collision flag. ref_scan() below is the
   scan the drawing path used to do, kept here rather than in render.c so it
   cannot drift along with the code it is checking.

   SG-1000 roms (.sg) get check 1 only, on the TMS9918A passes in tms.c, and
   it compares the fifth sprite flag and number as well as the collision flag:
   the skipped-frame pass has to latch all three. */
#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern int vp_vstart, vp_vend;
#ifdef COLLISION_STATS
extern long cstat_appends, cstat_builds;
/* Counted here rather than in render.c: ref_scan() below already is the scan
   render_obj() used to do, so it can report what that scan cost as it goes. */
static long cstat_scanned, cstat_walked;
#endif
#ifdef SPR_LIST_CHECK
extern const uint8 *spr_list_for_line(int line, int *count);
#endif

/* --- things the N64 frontend normally provides ------------------------- */
uint32_t prof_acc[8];
int soundEnabled = 0;
static uint8_t frame_buffer[256 * 192];
uint8_t *sms_line_target = frame_buffer;
void sms_palette_sync(int index) { (void)index; }
void sms_palette_syncGG(int index) { (void)index; }
void sms_palette_syncSG(int index) { (void)index; }
char unalChar(const char *adr) { return *adr; }
void system_load_sram(void) { }

static long lines_checked, lines_draw_hit, lines_coll_hit, mismatches;
static int  mm_frame[16], mm_line[16], mm_kind[16], nmm;

#ifdef SPR_LIST_CHECK
static long list_lines_checked, list_diffs;
static int  ld_frame[16], ld_line[16], nld;

/* Which sprites land on this line, worked out the way render_obj() used to:
   walk all 64 attribute table entries in order, stop at the end-of-list marker,
   and stop again once a ninth sprite is found on the line - the ninth is not
   drawn and neither is anything after it. Deliberately a transcription of the
   old loop rather than anything cleverer. */
static int ref_scan(int line, uint8 *out)
{
    uint8 *st = (uint8 *)&vdp.vram[vdp.satb];
    int height = (vdp.reg[1] & 0x02) ? 16 : 8;
    int count = 0;
    int n = 0;
    int i;

    if (vdp.reg[1] & 0x01)
        height *= 2;

    for (i = 0; i < 64; i += 1)
    {
        int yp = st[i];

#ifdef COLLISION_STATS
        cstat_scanned++;
#endif
        if (yp == 208)
            break;

        yp += 1;
        if (yp > 240)
            yp -= 256;

        if ((line >= yp) && (line < (yp + height)))
        {
            count += 1;
            if (vdp.limit && count == 9)
                break;
            out[n++] = (uint8)i;
        }
    }

    return n;
}

/* Compare the built list for one line against that scan, at the point in the
   frame where the renderer is about to use it. Checking the line being drawn
   rather than sweeping all 192 once a frame is both far cheaper and stricter:
   it catches a list left stale by a mid-frame write to the attribute table,
   which a sweep at the frame boundary would never see. */
static void check_list(int frame, int line)
{
    uint8 ref[64];
    int n_ref = ref_scan(line, ref);
    int n_got = 0;
    const uint8 *got = spr_list_for_line(line, &n_got);
    int bad = (n_ref != n_got);
    int k;

#ifdef COLLISION_STATS
    cstat_walked += n_got;
#endif

    for (k = 0; !bad && k < n_ref; k += 1)
        bad = (ref[k] != got[k]);

    list_lines_checked++;
    if (bad)
    {
        list_diffs++;
        if (nld < 16)
        {
            ld_frame[nld] = frame;
            ld_line[nld] = line;
            nld++;
        }
    }
}
#endif

static int ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls > lx && strcasecmp(s + ls - lx, suffix) == 0;
}

int main(int argc, char **argv)
{
    const char *path = argv[1];
    int frames = (argc > 2) ? atoi(argv[2]) : 600;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);

    int cart_type = ends_with(path, ".gg") ? TYPE_GG
                  : ends_with(path, ".sg") ? TYPE_SG : TYPE_SMS;
    int is_sg = (cart_type == TYPE_SG);

    /* SG images are padded to a 16 KB boundary with $FF, as loadRomFile()
       does on the console, so a build with -fsanitize=address sees exactly the
       buffer the emulator gets there. */
    long alloc = is_sg ? ((size + 0x3FFF) & ~0x3FFF) : size;
    uint8_t *rom = malloc(alloc);
    if (fread(rom, 1, size, f) != (size_t)size) { fprintf(stderr, "short read\n"); return 1; }
    fclose(f);
    memset(rom + size, 0xFF, alloc - size);

    if (!load_rom(rom, size, cart_type, false)) { fprintf(stderr, "load_rom failed\n"); return 1; }

    /* Status bits both passes must agree on. On the TMS9918A that is the
       fifth sprite flag and number too. */
    uint8 status_mask = is_sg ? (0x20 | 0x40 | 0x1F) : 0x20;
    long sg_fifth_lines = 0;

    snd.enabled = 0;
    system_init(0);
    system_reset();

    for (int frame = 0; frame < frames; frame++)
    {
        /* SG-1000 games wait on their title screen for a button, so press
           button 1 for a few frames now and then, as pico-smsplus's hosttest
           run does. Master System roms run without input, as they always
           have here. */
        input.pad[0] = 0;
        if (is_sg)
        {
            static const int presses[] = {300, 420, 540, 700};
            for (int p = 0; p < 4; p++)
                if (frame >= presses[p] && frame < presses[p] + 6)
                    input.pad[0] = INPUT_BUTTON1;
        }

        for (vdp.line = 0; vdp.line < 262; vdp.line++)
        {
            vdp_run();

            /* Both passes get the same starting state, with the sticky flag
               cleared so every line is judged on its own. */
            uint8 saved = vdp.status;

            vdp.status = saved & ~0x20;
            render_line(vdp.line);
            uint8 draw_status = vdp.status;
            int draw_hit = (draw_status & 0x20) != 0;

            vdp.status = saved & ~0x20;
            render_line_collision(vdp.line);
            uint8 coll_status = vdp.status;
            int coll_hit = (coll_status & 0x20) != 0;

            /* Carry on from the drawing path, which is the reference */
            if (is_sg)
            {
                /* Everything the pass latched, with the collision flag kept
                   sticky as it is on the console */
                vdp.status = draw_status | (saved & 0x20);
                if ((draw_status & 0x40) && !(saved & 0x40)) sg_fifth_lines++;
            }
            else
            {
                vdp.status = (saved & ~0x20) | (draw_hit ? 0x20 : 0);
            }

#ifdef SPR_LIST_CHECK
            /* After the two passes, so the list being compared is the one they
               just read: both sync it themselves when they reach the sprites,
               which leaves spr_list_for_line()'s own sync a no-op on exactly
               the lines that matter. A pass that forgot to sync at all is check
               1's business - the two call it independently, so one using a stale
               list disagrees with the other. The lists only cover the 192
               visible lines, and only the Master System renderer has them. */
            if (vdp.line < 192 && !is_sg)
                check_list(frame, vdp.line);
#endif

            if (vdp.line >= vp_vstart && vdp.line < vp_vend) lines_checked++;
            lines_draw_hit += draw_hit;
            lines_coll_hit += coll_hit;
            int kind = (draw_hit != coll_hit) ? (draw_hit ? 1 : 2)
                     : ((draw_status ^ coll_status) & status_mask) ? 3 : 0;
            if (kind)
            {
                mismatches++;
                if (nmm < 16)
                {
                    mm_frame[nmm] = frame; mm_line[nmm] = vdp.line;
                    mm_kind[nmm] = kind; nmm++;
                }
            }

            z80_execute(227);
        }
    }

    printf("%s  (%d frames)\n", path, frames);
    printf("  visible lines checked : %ld\n", lines_checked);
    printf("  collision lines       : draw=%ld  collision-pass=%ld\n",
           lines_draw_hit, lines_coll_hit);
    if (is_sg)
        printf("  fifth sprite latched  : %ld lines\n", sg_fifth_lines);
    printf("  MISMATCHES            : %ld\n", mismatches);
    for (int i = 0; i < nmm; i++)
        printf("    frame %5d line %3d : %s\n", mm_frame[i], mm_line[i],
               mm_kind[i] == 1 ? "draw saw a hit, collision pass did not"
               : mm_kind[i] == 2 ? "collision pass saw a hit, draw did not"
                                 : "fifth sprite flag or number differs");
#ifdef SPR_LIST_CHECK
    printf("  sprite lists checked  : %ld\n", list_lines_checked);
    printf("  LIST DIFFERENCES      : %ld\n", list_diffs);
    for (int i = 0; i < nld; i++)
        printf("    frame %5d line %3d : list differs from the 64-entry scan\n",
               ld_frame[i], ld_line[i]);
#endif
#ifdef COLLISION_STATS
    printf("  --- per frame, over the 192 lines the lists cover ---\n");
    printf("  entries the old scan examined : %8.1f\n", (double)cstat_scanned / frames);
    printf("  list entries walked instead   : %8.1f\n", (double)cstat_walked / frames);
    printf("  list appends to build them    : %8.1f\n", (double)cstat_appends / frames);
    printf("  list rebuilds                 : %8.2f\n", (double)cstat_builds / frames);
    printf("  ratio                         : %8.1fx\n",
           (double)cstat_scanned / (double)(cstat_walked + cstat_appends + 1));
#endif
#ifdef SPR_LIST_CHECK
    return (mismatches || list_diffs) ? 2 : 0;
#else
    return mismatches ? 2 : 0;
#endif
}
