:
# Build release version of the project with the dfs filesystem.
# Whatever you put in filesystem/ is packed into smsPlus64.z64, so those roms
# can be played without an SD card. Handy for emulators. No roms come with this
# repository, so an untouched filesystem/ only makes the rom bigger.
make clean
echo "Building release version of the project with dfs (roms from filesystem/)"
make RELEASE=1
