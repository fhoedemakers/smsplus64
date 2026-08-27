:
# Build release version of the project, without the dfs filesystem.
# Nothing in filesystem/ is packed into the rom, so smsPlus64.z64 carries no
# roms of its own and loads only from the SD card. Use build_dfs.sh instead to
# bake your own roms into it (for an emulator, for instance).
make clean
echo "Building release version of the project without dfs (no built-in roms)"
make RELEASE=1 NODFS=1
