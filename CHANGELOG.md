# CHANGELOG

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

