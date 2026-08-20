# smsPlus64

A Sega Master System and Game Gear Emulator running on the Nintendo 64. Use it on real hardware with a flashcart. Tested with the [EverDrive-64 X7](https://krikzz.com/our-products/cartridges/ed64x7.html) and [SummerCart64](https://summercart64.dev/) on both an NTSC and PAL Nintendo 64.

> [!NOTE]
> This project is more of a fun thing for me to try if it works. Help for improvement is always welcome.


Built with [Libdragon](https://github.com/DragonMinded/libdragon)

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

More details can be found in the N64FlashcartMenu [Getting Started Guide](https://github.com/Polprzewodnikowy/N64FlashcartMenu/blob/main/docs/00_getting_started_sd.md).

---

## Run the emulator standalone

To run the emulator as a standalone ROM:

1. Download [smsPlus64.z64](https://github.com/fhoedemakers/smsplus64/releases/latest/download/smsPlus64.z64) from the [releases page](https://github.com/fhoedemakers/smsplus64/releases/latest).
2. Copy it to your flashcart.
3. Launch **smsPlus64.z64** from the Everdrive or SummerCart menu.  
   The emulator will display its built-in game browser.
4. On the root of your SD card, create a folder named `smsPlus64`.
5. Place your `.sms` and `.gg` ROMs in this folder.  
   Subfolders are supported; the menu will scan them automatically.

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
- Z + Start: return to the game browser
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
| Frameskip | Skips drawing some frames so games keep running at the right speed. The default `Auto` only skips when a game cannot keep up, so games that already run at full speed still draw every frame. `Off` never skips, but games may run slow. `1`, `2` and `3` always skip that many. |
| Sound | Turns the sound on or off. Turning it off makes games run slightly faster. |
| Frame rate | Shows a small frame rate display in the corner of the screen. |
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

With Upscale on it is drawn over the picture, hiding the first few rows.

## Known issues

- Starting the emulator on its own can start the last game you played from the
  Everdrive menu instead of showing the game browser. Hold Z while the emulator
  starts to always get the browser.
- Sound can be missing when you start a second game without switching the console
  off in between. Switching the console off and on again restores it.
- A few games rely on sprite collision and can behave differently while frameskip
  is active. Set Frameskip to Off in the settings if you run into this.
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

You can also use an Emulator. Libdragon suggests [Ares](https://ares-emu.net/download). This however requires building from source. Since an Everdrive is not used, copy your .gg or .sms files to the `filesystem` folder of this repoistory, then run `build_dfs.sh`. The roms will be baked into `smsPlus64.z64` 

The files `run64.sh`, `cp64.sh` are used to  run or copy `smsPlus64.z64` to the Everdrive, using an USB cable. (using `usb64.exe`). Since usb64.exe runs on Windows, you need to build the project using WSL (Windows Subsystem for Linux) in order to use these scripts. If you are using Linux, you have to copy the file manually to the Everdrive.

