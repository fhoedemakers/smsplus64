:
# copy the file to Everdrive 64 PRO SD card on  Nintendo 64
# Everdrive 64 PRO must be inserted into the Nintendo 64 and the console must be switched on and in the menu.
# 
z64file=smsPlus64.z64
echo "Copy $z64file to the EverDrive 64 PRO SD card"
echo
echo "Before continuing, make sure that:"
echo "  * edlink.exe and $z64file are in this directory"
echo "  * the EverDrive 64 PRO is inserted into the Nintendo 64"
echo "  * the USB cable connects the EverDrive 64 PRO to the PC"
echo "  * the console is switched ON and sitting in the EverDrive menu"
echo
./run64pro.sh -c
