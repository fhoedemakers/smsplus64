#ifndef MENU_H
#define MENU_H
#define ROMSELECT
#define ROMINFOFILE "/currentloadedrom.txt"
#define SWVERSION "VX.X"
void processinput(DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem, bool ignorepushed );
typedef struct RomInfo
{
    uint8_t *rom;
    int size;
    int isGameGear;
    char title[256];
} RomInfo;
RomInfo menu(char *mountPoint, uintptr_t NES_FILE_ADDR, char *errorMessage, bool isFatalError, bool reset);

/* Outcome of loadRomFile(). ROMLOAD_EMPTY is a file with nothing in it, which
 * is not worth an error message; ROMLOAD_CANNOT_OPEN is the only one the
 * browser treats as fatal to itself. */
typedef enum
{
    ROMLOAD_OK = 0,
    ROMLOAD_CANNOT_OPEN,
    ROMLOAD_EMPTY,
    ROMLOAD_FAILED
} RomLoadResult;

/* Read a rom into a freshly allocated buffer. displayName is the bare file
 * name and decides Game Gear versus Master System. */
RomLoadResult loadRomFile(const char *fullPath, const char *displayName, RomInfo *info, char *errorMessage, size_t errCap);
char getcharslicefrom8x8font(char c, int rowInChar);
int ProcessAfterFrameIsRendered(surface_t *display, bool fromMenu);
extern char sdStatus[48];

#endif