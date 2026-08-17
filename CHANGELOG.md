# CHANGELOG

## Unreleased

### Features

- Frameskip. **Z + C-Left** cycles `AUTO -> off -> 1 -> 2 -> 3 -> AUTO`. AUTO (the
  default) drops rendering only while emulation has fallen a frame behind, so games
  run at the correct speed and pitch even when the N64 cannot draw every frame.
  The mode is shown as the last character of the frame rate overlay.
- The frame rate overlay now reads `S 060/30 A`: sound on/muted, *emulated* frames
  per second, *displayed* frames per second, and the frameskip mode.
- On-screen phase profiler, toggled with **Z + C-Up**. Shows the percentage of each
  second spent in the Z80 core, the scanline renderer, the RDP blit, sound
  generation, and waiting (`Z.. R.. B.. A.. I..`).

### Performance

- Replaced the 64KB sprite/background priority lookup table with arithmetic. The
  table was larger than the VR4300's 8KB data cache, so it missed on nearly every
  sprite pixel. Verified bit-identical to the old table for all reachable inputs.
- The scanline renderer now writes straight into the CI8 frame the RDP reads,
  removing a 256-byte copy per scanline.
- Inlined the pattern-cache hit path, which previously paid a full nine-register
  call frame roughly 6000 times per frame.
- Background line writes now use MIPS unaligned store instructions instead of
  testing alignment and branching twice per column.
- The CI8 frame is double buffered and the display swap is scheduled on RDP
  completion, so the CPU no longer parks on the RDP before every buffer swap.

### Fixes

- Writes by the emulated Z80 to ROM space were landing in a 256-byte buffer that
  `cpu_writemem16` could index 8KB into, overrunning it and corrupting the
  scanline being rendered. They now go to a dedicated 8KB dummy page.

### Known trade-off

- The VDP sprite-collision flag is only updated while sprites are being drawn, so
  it is not refreshed on skipped frames. The handful of games that poll it can be
  run with frameskip off (**Z + C-Left**).

## v0.7

### Features

- none

### Fixes

- Fixed corrupt framerate display when starting from N64FlashcartMenu.
- Disabled Start + Z when game is started from N64FlashcartMenu.
- When starting emulator, holding Z will force to load the built-in menu.
- Fixed libcart preventing internal menu to list roms.

## v0.6

### Features

Using a [SummerCart64](https://github.com/Polprzewodnikowy/SummerCart64), Master system and GameGear roms can be started directly from [N64FlashCartmenu](https://github.com/Polprzewodnikowy/N64FlashcartMenu).

For this to work with N64FlashCartmenu, copy smsPlus64.z64 to the menu/emulators folder of your SD-card. More info in the N64FlashcartMenu [Getting Started Guide](https://github.com/Polprzewodnikowy/N64FlashcartMenu/blob/main/docs/00_getting_started_sd.md)

> [!NOTE]
> According to the Everdrive manual, roms could also be run this way via the Everdrive menu by saving smsPlus64.z64 as ED64/emu/gg.v64 and ED64/emu/sms.v64. This does not work however.
> As a workaround, you can rename your .sms .gg roms to have the .gen extension and then copy smsPlus64.z64 to ED64/emu/gen.v64


### Fixes

- various bugfixes and improvements.

## v0.5

### Features

- none

### Fixes

- Fix bug in splash screen

## v0.4

### Features
- show program version in splash screen.

### Fixes
- Fixed sound toggle bug.
- Last column in menu was not visible.

## v0.3

### Features

- Changed framerate toggle from START + A to Z + A
- Added sound toggle Z + B
- Added libdragon logo to splash screen.

## v0.2

### Features
- Enabled audio

### Fixes
- None


## v0.1

### Features
- Initial release, based on infonesPlus.

### Fixes

