/* Host-side harness: runs the SMS core headless and checks, for every scanline
   of every frame, that the collision-only pass raises the sprite collision flag
   on exactly the lines the drawing path does. The drawing path is the oracle. */
#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int vp_vstart, vp_vend;
#ifdef COLLISION_STATS
extern long cstat_appends, cstat_sprites, cstat_calls, cstat_earlyout,
            cstat_lines, cstat_builds;
#endif

/* --- things the N64 frontend normally provides ------------------------- */
uint32_t prof_acc[8];
int soundEnabled = 0;
static uint8_t frame_buffer[256 * 192];
uint8_t *sms_line_target = frame_buffer;
void sms_palette_sync(int index) { (void)index; }
void sms_palette_syncGG(int index) { (void)index; }
char unalChar(const char *adr) { return *adr; }
void system_load_sram(void) { }

static long lines_checked, lines_draw_hit, lines_coll_hit, mismatches;
static int  mm_frame[16], mm_line[16], mm_kind[16], nmm;

int main(int argc, char **argv)
{
    const char *path = argv[1];
    int frames = (argc > 2) ? atoi(argv[2]) : 600;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc(size);
    if (fread(rom, 1, size, f) != (size_t)size) { fprintf(stderr, "short read\n"); return 1; }
    fclose(f);

    int is_gg = (strstr(path, ".gg") || strstr(path, ".GG")) != NULL;
    if (!load_rom(rom, size, is_gg)) { fprintf(stderr, "load_rom failed\n"); return 1; }

    snd.enabled = 0;
    system_init(0);
    system_reset();

    for (int frame = 0; frame < frames; frame++)
    {
        for (vdp.line = 0; vdp.line < 262; vdp.line++)
        {
            vdp_run();

            /* Both passes get the same starting state, with the sticky flag
               cleared so every line is judged on its own. */
            uint8 saved = vdp.status;

            vdp.status = saved & ~0x20;
            render_line(vdp.line);
            int draw_hit = (vdp.status & 0x20) != 0;

            vdp.status = saved & ~0x20;
            render_line_collision(vdp.line);
            int coll_hit = (vdp.status & 0x20) != 0;

            /* Carry on from the drawing path, which is the reference */
            vdp.status = (saved & ~0x20) | (draw_hit ? 0x20 : 0);

            if (vdp.line >= vp_vstart && vdp.line < vp_vend) lines_checked++;
            lines_draw_hit += draw_hit;
            lines_coll_hit += coll_hit;
            if (draw_hit != coll_hit)
            {
                mismatches++;
                if (nmm < 16)
                {
                    mm_frame[nmm] = frame; mm_line[nmm] = vdp.line;
                    mm_kind[nmm] = draw_hit ? 1 : 2; nmm++;
                }
            }

            z80_execute(227);
        }
    }

    printf("%s  (%d frames)\n", path, frames);
    printf("  visible lines checked : %ld\n", lines_checked);
    printf("  collision lines       : draw=%ld  collision-pass=%ld\n",
           lines_draw_hit, lines_coll_hit);
    printf("  MISMATCHES            : %ld\n", mismatches);
    for (int i = 0; i < nmm; i++)
        printf("    frame %5d line %3d : %s\n", mm_frame[i], mm_line[i],
               mm_kind[i] == 1 ? "draw saw a hit, collision pass did not"
                               : "collision pass saw a hit, draw did not");
#ifdef COLLISION_STATS
    printf("  --- collision pass cost, per skipped frame ---\n");
    printf("  list rebuilds         : %8.2f\n", (double)cstat_builds / frames);
    printf("  list appends          : %8.1f   <-- replaces the per-line y-scan\n",
           (double)cstat_appends / frames);
    printf("  lines doing work      : %8.1f\n", (double)cstat_lines / frames);
    printf("  sprite rows evaluated : %8.1f   <-- opacity + merge\n",
           (double)cstat_sprites / frames);
#endif
    return mismatches ? 2 : 0;
}
