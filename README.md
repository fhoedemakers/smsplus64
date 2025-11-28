# smsPlus64

A Sega Master System and Game Gear Emulator running on the Nintendo 64. Use it on real hardware with a flashcart. Tested with the [EverDrive-64 X7](https://krikzz.com/our-products/cartridges/ed64x7.html) and [SummerCart64](https://summercart64.dev/) on both an NTSC and PAL Nintendo 64.

> [!NOTE]
> This project is more of a fun thing for me to try if it works. Help for improvement is always welcome. 
Master System games don't reach 60fps, Game Gear games mostly do. Try it yourself to find out.


Built with [Libdragon](https://github.com/DragonMinded/libdragon)

<img src="/assets/libdragon.png" width="200" />

The emulator code is a port of smsPlus.

Download smsPlus64.z64 from the [releases](https://github.com/fhoedemakers/smsplus64/releases/latest) page and copy it to your flashcart.

## How to use on real hardware with an Everdrive 64 X7

You can launch ROMs directly from the Everdrive menu by saving [smsPlus64.z64](https://github.com/fhoedemakers/smsplus64/releases/latest/download/smsPlus64.z64) as:

- `ED64/emu/gg.v64`  
- `ED64/emu/sms.v64`

> [!NOTE]  
> Everdrive OS **v3.09 or higher** is required.  
> Downloads: https://krikzz.com/pub/support/everdrive-64/x-series/OS/

---

## How to use on real hardware with a SummerCart64 and N64FlashcartMenu

Games can be started directly from the [N64FlashCartMenu](https://github.com/Polprzewodnikowy/N64FlashcartMenu) when using a [SummerCart64](https://github.com/Polprzewodnikowy/SummerCart64).

To set this up:

1. Copy [smsPlus64.z64](https://github.com/fhoedemakers/smsplus64/releases/latest/download/smsPlus64.z64)  to the `menu/emulators` folder on your SD card.
2. Launch the menu; the emulator will appear automatically.

More details can be found in the N64FlashcartMenu  
[Getting Started Guide](https://github.com/Polprzewodnikowy/N64FlashcartMenu/blob/main/docs/00_getting_started_sd.md).

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

### In menu

- D-pad Up or Down: Next previous game
- D-pad left or right: previous or next page.
- B: Go to previous folder
- A: Open folder or start selected game

### In Game

- D-Pad: movement
- Start: Pause game
- B: Button 1
- A: Button 2
- Z + Start: Go back to menu
- Z + A: Toggle framerate
- Z + B: Toggle sound (improves framerate somewhat)

>[!NOTE]
> Holding Z after starting the emulator will force to load the built-in menu. 

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

## Using an Emulator

You can also use an Emulator. Libdragon suggests [Ares](https://ares-emu.net/download). This however requires building from source. Since an Everdrive is not used, copy your .gg or .sms files to the `filesystem` folder of this repoistory, then run `build.sh`. The roms will be baked into `smsPlus64.z64` 

The files `run64.sh`, `cp64.sh` are used to  run or copy `smsPlus64.z64` to the Everdrive, using an USB cable. (using `usb64.exe`). Since usb64.exe runs on Windows, you need to build the project using WSL (Windows Subsystem for Linux) in order to use these scripts. If you are using Linux, you have to copy the file manually to the Everdrive.

