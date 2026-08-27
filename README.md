# smsPlus64

A Sega Master System and Game Gear Emulator running on the Nintendo 64. Use it on real hardware with a flashcart. See [Compatibility](#compatibility) for the hardware it has been tested on.

> [!NOTE]
> This project is more of a fun thing for me to try if it works. Help for improvement is always welcome.


Built with [Libdragon](https://github.com/DragonMinded/libdragon)

## Compatibility

✅ works &nbsp;&nbsp; ⚠️ works, with a catch &nbsp;&nbsp; ❌ does not work &nbsp;&nbsp; ❔ untested

### Consoles

| Console | Status | Notes |
| --- | --- | --- |
| Nintendo 64, NTSC | ✅ | Tested with a SummerCart64 and an EverDrive-64 X7. |
| Nintendo 64, PAL | ❔ | Untested, but will probably work |
| ModRetro M64 (FPGA) | ⚠️ | With an EverDrive-64 X7, games start from the Everdrive menu, but the built-in game browser does not see the SD card. [#12](https://github.com/fhoedemakers/smsplus64/issues/12) Other flashcarts are untested. |
| Analogue 3D (FPGA) | ✅ | Tested with a SummerCart64 and an EverDrive-64 X7. |

### Flashcarts

| Flashcart | Status | Notes |
| --- | --- | --- |
| [SummerCart64](https://summercart64.dev/) | ✅ | Games can also be started straight from [N64FlashcartMenu](https://github.com/Polprzewodnikowy/N64FlashcartMenu), see [below](#how-to-use-on-real-hardware-with-a-summercart64-and-n64flashcartmenu). |
| [EverDrive-64 X7](https://krikzz.com/our-products/cartridges/ed64x7.html) | ✅ | Games can also be started straight from the Everdrive menu, see [below](#how-to-use-on-real-hardware-with-an-everdrive-64-x7). Needs Everdrive OS v3.09 or higher. |
| EverDrive-64 V3 | ⚠️ | Shows an invalid checksum warning on boot. The emulator itself runs fine. [#11](https://github.com/fhoedemakers/smsplus64/issues/11) |
| [EverDrive-64 PRO](https://krikzz.com/our-products/cartridges/everdrive-64-pro.html) | ❌ | The emulator does not start yet. [#13](https://github.com/fhoedemakers/smsplus64/issues/13) |
| 64drive | ❔ | Untested. |

### Emulators

| Emulator | Status | Notes |
| --- | --- | --- |
| [Ares](https://ares-emu.net/download) | ✅ | There is no SD card, so build with `build_dfs.sh` to bake your roms into the rom, see [Using an Emulator](#using-an-emulator). |

Reports for anything untested here are welcome.


<img src="/assets/libdragon.png" width="200" />

The emulator code is a port of smsPlus.

Download smsPlus64.z64 from the [releases](https://github.com/fhoedemakers/smsplus64/releases/latest) page and copy it to your flashcart.

## How to use on real hardware with an Everdrive 64 X7

You can launch ROMs directly from the Everdrive menu. Download
[sms.v64](https://github.com/fhoedemakers/smsplus64/releases/latest/download/sms.v64) and
[gg.v64](https://github.com/fhoedemakers/smsplus64/releases/latest/download/gg.v64) from the
[releases](https://github.com/fhoedemakers/smsplus64/releases/latest) page and copy them to
the `ED64/emu` folder on your SD card. No renaming needed: both are the emulator itself,
just under the names the Everdrive menu looks for.

Selecting a `.sms` or `.gg` rom in the Everdrive menu then starts it in the emulator.

> [!NOTE]  
> Everdrive OS **v3.09 or higher** is required.  
> Downloads: https://krikzz.com/pub/support/everdrive-64/x-series/OS/

---

## How to use on real hardware with a SummerCart64 and N64FlashcartMenu

Games can be started directly from the [N64FlashCartMenu](https://github.com/Polprzewodnikowy/N64FlashcartMenu) when using a [SummerCart64](https://github.com/Polprzewodnikowy/SummerCart64).

To set this up:

1. Copy [smsPlus64.z64](https://github.com/fhoedemakers/smsplus64/releases/latest/download/smsPlus64.z64)  to the `menu/emulators` folder on your SD card.
2. Launch the menu; the emulator will appear automatically.

More details can be found in the N64FlashcartMenu [Getting Started Guide](https://github.com/Polprzewodnikowy/N64FlashcartMenu/blob/main/docs/10_getting_started_sd.md)
and its [Emulators](https://github.com/Polprzewodnikowy/N64FlashcartMenu/blob/main/docs/18_emulators.md) page.

---

## Run the emulator standalone

To run the emulator as a standalone ROM:

1. On the root of your SD card, create a folder named `smsPlus64`.
2. Place your `.sms` and `.gg` ROMs in this folder.  
   Subfolders are supported; the menu will scan them automatically.
3. Download [smsPlus64.z64](https://github.com/fhoedemakers/smsplus64/releases/latest/download/smsPlus64.z64) from the [releases page](https://github.com/fhoedemakers/smsplus64/releases/latest).
4. Copy it to your flashcart.
5. Launch **smsPlus64.z64** from the Everdrive or SummerCart menu.  
   The emulator will display its built-in game browser.


## Controls

### In the game browser

- D-pad Up / Down: previous or next game
- D-pad Left / Right: previous or next page
- A: open a folder, or start the selected game
- B: go back to the previous folder
- Start: open the settings

### In game

- D-Pad: movement
- Start: Pause / Start
- B: Button 1
- A: Button 2
- Z + Start: return to the game browser. Does nothing for a game started straight
  from the Everdrive or N64FlashcartMenu; reset the console to get back to that menu.
- Z + C-Right: open the settings

The settings can also be changed with these shortcuts while playing:

- Z + A: show or hide the frame rate
- Z + B: turn sound on or off
- Z + C-Left: change frameskip
- Z + C-Up: show or hide the performance breakdown

> [!NOTE]
> Holding Z while the emulator starts always takes you to the game browser.

## Settings

Press Start in the game browser, or Z + C-Right while playing. Use Up and Down to pick
a setting, Left and Right to change it, and B to go back.

| Setting | What it does |
| --- | --- |
| Frameskip | Skips drawing some frames so games keep running at the right speed. `Auto` only skips when a game cannot keep up, so games that already run at full speed still draw every frame. `Off` never skips, but games may run slow. `1` (default), `2` and `3` always skip that many. |
| Blink fix | Some games blink a character on and off after it is hit. Frameskip can drop exactly the frames the character is drawn on, so it seems to disappear for a moment instead of blinking. This varies which frames are drawn so the blink stays visible. It makes the picture a little less smooth, so it is off unless you turn it on. Only frameskip `1` and `3` are affected; `2` and `Off` never had the problem. |
| Sound | Turns the sound on or off. Turning it off makes games run slightly faster. |
| Frame rate | Shows among other stats a small frame rate display in the corner of the screen. (see below) |
| Profiler | Shows a breakdown of where the emulator spends its time. Mainly useful for troubleshooting. |
| Upscale | Fills the screen with the picture. Turn it off if you would rather have a 1:1 picture with a black border around it. |
| Autostart | When on, a game picked in the Everdrive or flashcart menu starts straight away. Turn it off if you would rather always see the game browser. |

Your settings are saved as `settings.cfg` in the same folder as your games on the SD
card, and are restored the next time you start the emulator. Without an SD card the
settings still work, they just cannot be saved.

The frame rate display reads something like `SS 060/30 A1`:

- first letter: `S` for Master System, `G` for Game Gear
- second letter: `S` when sound is on, `M` when muted
- `060`: how fast the game is running, out of 60
- `30`: how many frames per second are actually drawn
- `A1`: the frameskip setting. `A` means Auto, followed by the level it settled on; a plain digit is a level you set yourself

The performance breakdown reads something like `Z731 R678 B147 A218 I000 S060`. Each
letter is one part of the work of running a game:

- `Z`: emulating the game's own processor
- `R`: drawing the picture
- `B`: handing the finished picture to the Nintendo 64's graphics chip
- `A`: sound
- `I`: spare time, spent waiting so the game runs at the right speed and no faster
- `S`: waiting for the Nintendo 64's graphics chip to catch up

Each number is how long that part took on one frame, in milliseconds with the dot left
out: `R678` means 6.78 milliseconds spent drawing. A frame lasts 16.67 milliseconds, so
when a game is keeping up the numbers add up to roughly `1667`.

`I` is the one to look at first, because it is the time left over. While there is spare
time the game runs at full speed. When `I` reads `000` there is none left and the game is
running slow — and then the biggest of the other numbers is what is costing you. `I` and
`S` stop counting at `999`.

With frameskip on, `R` and `B` are averages over all frames, drawn and skipped, so they
read lower than the drawing of a single frame actually costs. To see the real cost of
drawing a frame, set frameskip to `Off`.

With Upscale on both displays are drawn over the picture, hiding the first few rows.

## Known issues

- Settings are only saved when a `smsPlus64` folder exists on the SD card. The
  settings screen says so if it cannot save them.

## Building from source

1. Install the Libdragon SDK. For more info and instructions, see https://github.com/DragonMinded/libdragon
2. Make sure the environment var N64_INST points to the installed SDK
3. Get the sources and build

````bash
git clone https://github.com/fhoedemakers/smsplus64.git
cd smsplus64
chmod +x build*.sh
./build.sh
````

Then copy `smsPlus64.z64` to your flash drive.

`build.sh` leaves the `filesystem` folder out of the rom, so `smsPlus64.z64`
lists only what is on the SD card. Use `./build_dfs.sh` instead to bake that
folder into the rom, so the games in it can be played without a card. No roms
come with this repository: put your own `.sms` or `.gg` files in `filesystem`
first, or you will just get a bigger rom with nothing in it.

## Using an Emulator

You can also use an Emulator. Libdragon suggests [Ares](https://ares-emu.net/download). This however requires building from source. Since an Everdrive is not used, copy your .gg or .sms files to the `filesystem` folder of this repository, then run `build_dfs.sh`. The roms will be baked into `smsPlus64.z64` 

The files `run64.sh`, `cp64.sh` are used to  run or copy `smsPlus64.z64` to the Everdrive, using an USB cable. (using `usb64.exe`). Since usb64.exe runs on Windows, you need to build the project using WSL (Windows Subsystem for Linux) in order to use these scripts. If you are using Linux, you have to copy the file manually to the Everdrive.

## Credits

This emulator is other people's work brought together on a Nintendo 64.

**Emulation**

- [SMS Plus](https://segaretro.org/SMS_Plus) by **Charles MacDonald** — the Sega Master System and Game Gear emulator core in `smsplus/` that this project is built on.
- The Z80 CPU core is **Juergen Buchmueller**'s portable Z80 emulator.
- The core reached this project by way of [pico-smsplus](https://github.com/fhoedemakers/pico-smsplus), the Raspberry Pi Pico version of the same emulator.

**Nintendo 64 side**

- [Libdragon](https://github.com/DragonMinded/libdragon) by **DragonMinded** and its contributors — the open source SDK everything here is built with: display and RDP, audio, controllers, and the filesystem code. The dragon in the game browser is its logo.
- [libcart](https://github.com/devwizard64/libcart) by **devwizard** ([@devwizard64](https://github.com/devwizard64)), as bundled with Libdragon — flashcart detection and SD card access on the EverDrive-64 and the SummerCart64.
- [FatFs](http://elm-chan.org/fsw/ff/00index_e.html) by **ChaN**, also by way of Libdragon — reading the SD card.
- [SummerCart64](https://summercart64.dev/) and [N64FlashcartMenu](https://github.com/Polprzewodnikowy/N64FlashcartMenu) by **Mateusz Faderewski** ([@Polprzewodnikowy](https://github.com/Polprzewodnikowy)) — the flashcart and menu that games can be started from directly.
- Thanks to **Wayne Reilly** and **Dan Moore** for testing releases on hardware this project does not have and reporting back.

**This project**

- **smsPlus64** — the port to the Nintendo 64, the game browser, the settings screen and the renderer — is by **Frank Hoedemakers** ([@fhoedemakers](https://github.com/fhoedemakers)).
- The game browser and the `Frens::` helpers are the same ideas as in [pico-infonesPlus](https://github.com/fhoedemakers/pico-infonesPlus) and the other emulators in that family, rewritten here for Libdragon.

## Use of AI

Part of the code and the documentation was written with the assistance of
[Anthropic Claude Opus](https://www.anthropic.com/claude/opus), through
[Claude Code](https://claude.com/claude-code). It helped with:

- the speed work in v0.8: the per-scanline renderer, frameskip and its automatic cadence, building the sprite list once per frame, and choosing the Z80 optimization level by measurement
- the sound work: generating the PSG per scanline, so digitized speech no longer crackles
- keeping sprite collision correct on skipped frames, and the Blink fix setting
- the settings screen and saving settings to the SD card
- filling the screen with the picture (Upscale), taking the console type from the ROM instead of the file name, and loading the whole ROM whatever its header claims
- the frame rate and profiler overlay, and the `tools/collisioncheck` host harness
- this readme and the changelog
