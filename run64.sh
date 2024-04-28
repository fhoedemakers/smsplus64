:
# Run on Real Hardware (Nintendo 64)
# Needs usb64.exe ( https://krikzz.com/pub/support/everdrive-64/x-series/dev/usb64-v1.0.0.3.zip )
# This script is for Windows WSL (Windows Subsystem for Linux), since USB64 is a Windows tool.
z64file=smsPlus64.z64
[ -f $z64file ] || { echo "$z64file not found"; exit 1; }
echo "Running $z64file on real hardware"
# Note: usb64.exe must be in the same directory as this script and smsPlus64.z64, otherwise the rom will not start.
./usb64.exe -rom=$z64file -start
# End of run64.sh
