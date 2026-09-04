:
# Run on Real Hardware (Nintendo 64 with Everdrive 64 PRO)
# Everdrive 64 PRO must be inserted into the Nintendo 64 and the console must be switched on and in the menu.
# Needs edlink.exe ( https://github.com/krikzz/ed64-pro-pub/blob/main/edlink.exe )
# This script is for Windows WSL (Windows Subsystem for Linux), since USB64 is a Windows tool.
z64file=smsPlus64.z64
targetfile=smsplus64.v64
[ -f $z64file ] || { echo "$z64file not found, build it first with ./build.sh"; exit 1; }
# [ -f ./usb64.exe ] || { echo "usb64.exe not found, see https://krikzz.com/pub/support/everdrive-64/x-series/dev/usb64-v1.0.0.3.zip"; exit 1; }
[ -f ./edlink.exe ] || { echo "edlink.exe not found, see https://github.com/krikzz/ed64-pro-pub/blob/main/edlink.exe"; exit 1; }
# Note: usb64.exe must be in the same directory as this script and smsPlus64.z64, otherwise the rom will not start.
# Also, the usb cable must be connected to the N64 and the PC and the console must be in the everdrive menu.
# check optional commandline parameter -c for copy only.
if [ "$1" == "-c" ]; then
    echo "Copying $z64file to Nintendo 64"
    ./edlink.exe cp --src $z64file --dst sd:/ED64/edapp/sms/$targetfile
    ./edlink.exe cp --src $z64file --dst sd:/ED64/edapp/gg/$targetfile
    ./edlink.exe cp --src $z64file --dst sd:/$z64file
else
  
    echo "Running $z64file on real hardware"
    ./edlink.exe run --file $z64file
fi
# End of run64.sh
