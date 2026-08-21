# Sprite collision checker

Runs the emulator core headless on the host and checks two things on every
scanline of every frame:

1. that `render_line_collision()` raises the sprite collision flag on exactly
   the lines `render_line()` does, and
2. that the per-line sprite list both passes walk holds exactly what the plain
   64-entry attribute table scan would have found.

    tools/collisioncheck/run.sh          # 20000 frames per rom
    FRAMES=3000 tools/collisioncheck/run.sh

The core is endian-parameterised, so `-DLSB_FIRST` is all the host build needs.
`filesystem/` roms are not in the repository; drop your own in.

## Why there are two checks

For check 1 the drawing path is the oracle: both passes are run on the same line
from the same state with the sticky flag cleared, their answers compared, and the
drawing path's answer kept so the emulation carries on as it normally would.

That worked as long as the drawing path rediscovered the sprites on each
scanline, which made it independent of the list. It no longer does - both passes
read the same per-line list now - so a bug in `spr_list_build()` moves both
answers together and check 1 goes quiet. Measured: breaking the list build so
sprites lose their bottom row used to produce 716 / 151 / 36 mismatches across
the three roms, and after `render_obj()` moved to the list it produces **zero**,
while check 2 reports the same 21198 / 7328 / 8740 list differences either way.

So check 2 is the one holding the list up. `ref_scan()` in `main.c` is the old
per-line scan, kept in the harness rather than in `render.c` so it cannot drift
along with the code it checks. It is compared against `spr_list_for_line()`,
which `render.c` exports only under `-DSPR_LIST_CHECK`.

A list bug now shows in the picture as well as in the collision flag, which is
what makes check 2 worth its keep.

## Negative controls

Neither check means anything until it has been seen to fail.

- Check 1: disable the `spr_list_dirty` assignment in `vramMarkTileDirty()`.
  All of Sonic's collisions go unreported.
- Check 2: change `y1 = yp + height` to `yp + height - 1` in
  `spr_list_build()`. Tens of thousands of list differences, on every rom.

## Operation counters

`-DCOLLISION_STATS` adds semantic counters, reported per frame:

    entries the old scan examined :  12280.1
    list entries walked instead   :    187.8
    list appends to build them    :    120.2
    list rebuilds                 :     0.36

Counting what a loop actually does has twice found the real cost here when
reasoning about instruction counts and code size had pointed the wrong way, so
they are kept rather than re-derived each time.

## Turning that into milliseconds

The counters say how much work went away; only the console says what it was
worth. Three things make a hardware A/B mean anything here, all of them learned
by getting it wrong first:

- **Frameskip off, and check the profiler's `I` reads 000.** With no idle the
  emulator is CPU bound, so the emulated frame rate *is* the frame cost - 50 fps
  is 20.0 ms - and it owes nothing to the profiler. At any other frameskip level
  the slots average drawn and skipped frames together; the giveaway is the blit
  slot, which halves.
- **A static screen, and treat `Z` as a validity gate.** The Z80 slot is
  identical code in every build, so if it disagrees the readings are of different
  scenes and nothing can be attributed to the change. A title screen is the right
  place even for sprite work, because a per-line attribute table scan costs the
  same whether or not there are sprites on screen. Taken mid-level instead, `Z`
  varied 16% and the build doing the most work reported the *least* total time.
- **Build a third rom with the removed work added back**, running for its cost
  alone into a `volatile` sink. A change on a per-scanline path can gain or lose
  as much from where the instruction cache lands as from the work removed, and
  only this tells the two apart. For the sprite scan: removing it read as 1.42 ms
  per drawn frame and adding it back as 2.42 ms, which brackets the real figure -
  the second overshoots because the re-added loop cannot share the drawing loop's
  overhead the way the original did. An A/B pair alone cannot produce that
  bracket, and its single number could have been either.

## The pixel oracle

Check 2 covers the list. It does not cover the body of `render_obj()` - whether
the row, the clipping and the cache index survived being moved. That was proved
once, with a harness that renders every line twice and compares the pixels: live
`render.c` against a frozen copy of it from before the change. 0 differing pixels
and 0 collision-flag differences over 1.58M lines on the three roms.

It is not committed, because it needs a frozen copy of the whole file, which goes
stale the moment anything else in `render.c` legitimately changes. Rebuild it if
`render_obj()` is ever restructured again:

    OUT=$(mktemp -d); REF=c541f05        # a commit whose render.c is known good
    CFLAGS="-O2 -DLSB_FIRST -Itools/collisioncheck -I. -Ismsplus"

    # the reference, with every symbol it defines prefixed ref_
    git show "$REF:smsplus/render.c" > $OUT/ref_render.c
    gcc -c $CFLAGS -o $OUT/ref_render.o $OUT/ref_render.c
    nm --defined-only $OUT/ref_render.o | awk '{print $3, "ref_" $3}' > $OUT/renames.txt
    objcopy --redefine-syms=$OUT/renames.txt $OUT/ref_render.o $OUT/ref_render_pfx.o

    # the live one, with only what vdp.c calls directly renamed
    gcc -c $CFLAGS -o $OUT/cur_render.o smsplus/render.c
    printf 'vramMarkTileDirty cur_vramMarkTileDirty\npalette_sync cur_palette_sync\nrender_reset cur_render_reset\nrender_init cur_render_init\n' > $OUT/cur_renames.txt
    objcopy --redefine-syms=$OUT/cur_renames.txt $OUT/cur_render.o $OUT/cur_render_pfx.o

Then link both against a harness modelled on `main.c` that defines
`vramMarkTileDirty`, `palette_sync`, `render_reset` and `render_init` as shims
calling **both** `cur_*` and `ref_*`. `vdp.c` calls those four directly, and
without the shims the reference's pattern cache and palette go stale and report
differences that have nothing to do with the change. Per line: point
`sms_line_target` at buffer A and call `render_line()`, at buffer B and call
`ref_render_line()`, then compare.

Compare **`(a[x] ^ b[x]) & 0x3F`**, not the raw byte. Bit 7 is never set, bit 6
is the sprite-occupancy marker, and the TLUT is replicated 8 times, so only bits
0..5 reach the screen; comparing raw bytes reports differences the display cannot
show.

Its own negative control: flip a bit in what `render_obj()` stores
(`sprite_mix(...) ^ 1`). That produced 11432 differing pixels on 1368 lines in
200 frames of Aladdin.
