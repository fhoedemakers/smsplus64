:
# Run on Real Hardware (Nintendo 64 with Everdrive X7)
# Everdrive X7 must be inserted into the Nintendo 64 and the console must be switched on and in the menu.
# Needs usb64.exe ( https://krikzz.com/pub/support/everdrive-64/x-series/dev/usb64-v1.0.0.3.zip )
# This script is for Windows WSL (Windows Subsystem for Linux), since USB64 is a Windows tool.
z64file=smsPlus64.z64
[ -f $z64file ] || { echo "$z64file not found, build it first with ./build.sh"; exit 1; }
[ -f ./usb64.exe ] || { echo "usb64.exe not found, see https://krikzz.com/pub/support/everdrive-64/x-series/dev/usb64-v1.0.0.3.zip"; exit 1; }
# Note: usb64.exe must be in the same directory as this script and smsPlus64.z64, otherwise the rom will not start.
# Also, the usb cable must be connected to the N64 and the PC and the console must be in the everdrive menu.
# check optional commandline parameter -c for copy only.
if [ "$1" == "-c" ]; then
    echo "Copying $z64file to Nintendo 64"
    ./usb64.exe -cp $z64file sd:/ED64/emu/sms.v64
    ./usb64.exe -cp $z64file sd:/ED64/emu/gg.v64
    ./usb64.exe -cp $z64file sd:/$z64file
else
  
    echo "Running $z64file on real hardware"
    ./usb64.exe -rom=$z64file -start
fi
# End of run64.sh
