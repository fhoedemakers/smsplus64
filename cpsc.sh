:
# copy the file to Summercart 64 SD card on  Nintendo 64 
# When SummerCart64 is inserted into N64, the console must be switched off!
z64file=smsPlus64.z64
deployer=./sc64deployer.exe
echo "Copy $z64file to the SummerCart64 SD card"
echo
echo "Before continuing, make sure that:"
echo "  * sc64deployer.exe and $z64file are in this directory"
echo "  * the USB cable connects the SummerCart64 to the PC"
echo "  * the console is switched OFF if the SummerCart64 is inserted into it"
echo
[ -f $z64file ] || { echo "$z64file not found, build it first with ./build.sh"; exit 1; }
[ -f $deployer ] || { echo "$deployer not found, see https://github.com/Polprzewodnikowy/SummerCart64"; exit 1; }
$deployer sd upload $z64file /menu/emulators/$z64file || exit 1
$deployer sd upload $z64file /$z64file || exit 1
