# Sprite collision checker

Runs the emulator core headless on the host and checks, for every scanline of
every frame, that `render_line_collision()` raises the sprite collision flag on
exactly the lines `render_line()` does.

    tools/collisioncheck/run.sh          # 20000 frames per rom
    FRAMES=3000 tools/collisioncheck/run.sh

The drawing path is the oracle: both passes are run on the same line from the
same state with the sticky flag cleared, their answers compared, and the
drawing path's answer kept so the emulation carries on as it normally would.

This exists because the two passes no longer share a loop. The collision pass
walks a per-line sprite list built once per frame, while the drawing path
rediscovers the sprites on each scanline, so nothing structural keeps them in
step - only this check does. Run it after touching either one.

To confirm the check still bites, break something on purpose: disabling the
`spr_list_dirty` assignment in `vramMarkTileDirty()` should make every
collision go unreported.

The core is endian-parameterised, so `-DLSB_FIRST` is all the host build needs.
`filesystem/` roms are not in the repository; drop your own in.
