# CHANGELOG

## v0.8

### Speed

Emulated frames per second on real hardware, at the default settings. 60 is full
speed.

| | v0.7 | v0.8 |
| --- | --- | --- |
| Master System | 30-40 | 59 |
| Game Gear | not measured | 58 |

Master System draws every second frame to hold that speed, Game Gear draws them all.

### Features

- The picture now fills the screen. Turn the new Upscale setting off for the old
  1:1 picture with a border around it.
- Settings screen. Press Start in the game browser, or Z + C-Right while playing,
  to change frameskip, sound and the frame rate display without having to remember
  button combinations. Your settings are saved on the SD card and restored the next
  time you start the emulator.
- Blink fix setting, off by default. Turn it on if a character disappears for a
  moment when it is hit instead of blinking.
- Frameskip, set to automatic by default. Games keep running at the right speed
  even when the Nintendo 64 cannot draw every frame, and games that already run
  at full speed are left alone. Change it in the settings if you prefer.
- The frame rate display now also shows which console is being emulated and how
  many frames are actually being drawn.
- `build.sh` now builds a rom with no filesystem attached, which loads only from
  the SD card. Use the new `build_dfs.sh` to bake your own roms from the
  `filesystem` folder into the rom as before.

### Fixes

- Master System / Game Gear games run considerably faster.
- Sound is smoother.
- The "Segaaa" shout and other digitized speech no longer crackles.
- Game Gear games no longer start with completely wrong colours. The console type
  is now taken from the ROM itself instead of from the file name.
- ROMs with upper case file extensions (.GG, .SMS) are now listed.
- The built-in game browser now shows which folder it is listing, and tells you when it
  cannot find any games there.
- Homebrew games no longer come up as a black screen when started from the
  Everdrive or N64FlashcartMenu. Their ROM header often understates how big
  they are, and only that much of the game was being loaded. [#8](https://github.com/fhoedemakers/smsplus64/issues/8)
- Starting a second game from the built-in game browser no longer comes up silent, and
  no longer crashes on some flashcarts. [#10](https://github.com/fhoedemakers/smsplus64/issues/10)
  [#14](https://github.com/fhoedemakers/smsplus64/issues/14)
- Replaced deprecated controller functions.
- Starting the emulator on its own no longer starts the last game you played from the
  Everdrive menu instead of showing the game browser. 

### Known issues


- The emulator does not start on the EverDrive 64 PRO. [#13](https://github.com/fhoedemakers/smsplus64/issues/13)
- On an EverDrive 64 V3 the emulator shows an invalid checksum warning on boot. It runs
  fine. [#11](https://github.com/fhoedemakers/smsplus64/issues/11)
- On a ModRetro M64 the built-in game browser does not see the SD card. Games can still
  be started from the Everdrive menu. [#12](https://github.com/fhoedemakers/smsplus64/issues/12)

### Thanks

Thanks to Wayne Reilly and Dan Moore for testing this release.

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

Using a [SummerCart64](https://github.com/Polprzewodnikowy/SummerCart64), Master system and GameGear roms can be started directly from [N64FlashCartmenu](https://github.com/Polprzewodnikowy/N64FlashcartMenu). [#4](https://github.com/fhoedemakers/smsplus64/issues/4)

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
- Enabled audio [#1](https://github.com/fhoedemakers/smsplus64/issues/1)

### Fixes
- None


## v0.1

### Features
- Initial release, based on infonesPlus.

### Fixes

