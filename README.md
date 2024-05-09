# smsPlus64

A Sega Master System and Game Gear Emulator running on the Nintendo 64. Use it on real hardware with an EverDrive-64. Tested with the [EverDrive-64 X7](https://krikzz.com/our-products/cartridges/ed64x7.html)

> Note This project is more of a fun thing for me to try if it works. It runs way too slow to be really usable and playable.  I don't know if the emulator will ever run at full speed. Help is always welcome. 
Game Gear games run faster than Master System games and are much more playable.


Built with [Libdragon](https://github.com/DragonMinded/libdragon)

The emulator code is a port of smsPlus.

## How to use on real hardware using an Everdrive 64 X7.

To try the emulator, download smsPlus64.z64 from the [releases](https://github.com/fhoedemakers/smsplus64/releases/latest) page en copy it to your flash card.

Games can be played from the flashcard's SD card. Create a folder named `smsPlus64` in the root of your flashcard's SD card and copy your .sms or .gg ROMs there. You can organize your games in subfolders.

## Building from source

1. Install the Libdragon SDK. For more info and instructions, see https://github.com/DragonMinded/libdragon
2. Make sure the environment var N64_INST points to the installed SDK
3. Get the sources and build

````bash
git clone https://github.com/fhoedemakers/smsplus64.git
cd smsplys64
chmod +x build*.sh
./build.sh
````

Then copy `smsPlus64.z64.z64` to your flash drive.

## Using an Emulator

You can also use an Emulator. Libdragon suggests [Ares](https://ares-emu.net/download). This requires building from source. Since an Everdrive is not used, copy your .gg or .sms files to the `filesystem` folder of this repoistory, then run `build.sh`.

The files `run64.sh`, `cp64.sh` are used to  run or copy `smsPlus64.z64` to the Everdrive, using an USB cable. (using `usb64.exe`). Since usb64.exe runs on Windows, you need to build the project using WSL (Windows Subsystem for Linux) to use thes scripts. If you are using Linux, you have to copy the file manually to the Everdrive.

