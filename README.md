# smsPlus64

A Sega Master System and Game Gear Emulator running on the Nintendo 64. Use it on real hardware with a flashcart. Tested with the [EverDrive-64 X7](https://krikzz.com/our-products/cartridges/ed64x7.html) and [SummerCart64](https://summercart64.dev/) on both an NTSC and PAL Nintendo 64.

> [!NOTE]
> This project is more of a fun thing for me to try if it works. Help for improvement is always welcome. 
Master System games don't reach 60fps, Game Gear games mostly do. Try it yourself to find out.


Built with [Libdragon](https://github.com/DragonMinded/libdragon)

<img src="/assets/libdragon.png" width="200" />

The emulator code is a port of smsPlus.

## How to use on real hardware using an Everdrive 64 X7.

To try the emulator, download smsPlus64.z64 from the [releases](https://github.com/fhoedemakers/smsplus64/releases/latest) page and copy it to your flashcart. When smsPLus.z64 is started from the Everdrive menu, the emulator shows a builtin menu with a list of games that can be started.
Create a folder named `smsPlus64` in the root of your flashcart's SD card and copy your .sms or .gg ROMs there. You can organize your games in subfolders. The menu reads the contents of this folder.

## How to use on real hardware using a SummerCart64 flashcart and N64FlashcartMenu.

Games can be started directly from the N64FlashcartMenu instead of the builtin menu.
For this to work with  [N64FlashCartMenu](https://github.com/Polprzewodnikowy/N64FlashcartMenu) on a [SummerCart64](https://github.com/Polprzewodnikowy/SummerCart64) flashcart, copy smsPlus64.z64 to the menu/emulators folder of your SD-card. More info in the N64FlashcartMenu [Getting Started Guide](https://github.com/Polprzewodnikowy/N64FlashcartMenu/blob/main/docs/00_getting_started_sd.md)


> [!NOTE]
> According to the Everdrive manual, roms could also be run using the Everdrive menu by saving smsPlus64.z64 as ED64/emu/gg.v64 and ED64/emu/sms.v64. This does not work however.
> As a workaround, you can rename your .sms .gg roms to have the .gen extension and then copy smsPlus64.z64 to ED64/emu/gen.v64

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

