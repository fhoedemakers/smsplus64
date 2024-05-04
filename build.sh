:
# Build release version of the project
make clean
echo "Building release version of the project"
echo "make RELEASE=1 disables access to the Everdrive64's SD card. (because of -NDEBUG flag)"
echo "Running debug version of the project is recommended."
make RELEASE=1
