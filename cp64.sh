:
# copy the file to Everdrive X7 SD card on  Nintendo 64
# Everdrive X7 must be inserted into the Nintendo 64 and the console must be switched on and in the menu.
# 
z64file=smsPlus64.z64
echo "Copy $z64file to the EverDrive X7 SD card"
echo
echo "Before continuing, make sure that:"
echo "  * usb64.exe and $z64file are in this directory"
echo "  * the EverDrive X7 is inserted into the Nintendo 64"
echo "  * the USB cable connects the EverDrive X7 to the PC"
echo "  * the console is switched ON and sitting in the EverDrive menu"
echo
./run64.sh -c
