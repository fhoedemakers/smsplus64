#include <stdio.h>
#include <string.h>
#include "libdragon.h"
#include "common.h"
#include "FrensHelpers.h"
#include <memory>
#include "RomLister.h"
#include "menu.h"
#include "settings.h"
#include "shared.h"

#ifdef __cplusplus
extern "C" {
#endif
sprite_t  *loaddragonsprite();
#ifdef __cplusplus
}
#endif
#define SCREEN_ROWS 29
#define SCREEN_COLS 38
#define STARTROW 2
#define ENDROW (SCREEN_ROWS - 2)
#define PAGESIZE (ENDROW - STARTROW + 1)

#define VISIBLEPATHSIZE (SCREEN_COLS - 3)


#define DEFAULT_FGCOLOR CBLACK // 60
#define DEFAULT_BGCOLOR CWHITE

#define MAXDIRDEPTH 5

sprite_t *dragonsprite = nullptr;

char (*dirstack)[MAX_FILENAME_LEN]; // [MAXDIRDEPTH][MAX_FILENAME_LEN];
int dirstackindex = 0;

static int fgcolor = DEFAULT_FGCOLOR;
static int bgcolor = DEFAULT_BGCOLOR;

struct charCell
{
    uint32_t fgcolor;
    uint32_t bgcolor;
    char charvalue;
};

#define SCREENBUFCELLS SCREEN_ROWS *SCREEN_COLS
static charCell *screenBuffer;
;

static WORD *WorkLineRom = nullptr;

static constexpr int LEFT = 0x00000004;
static constexpr int RIGHT = 0x00000008;
static constexpr int UP = 0x00000001;
static constexpr int DOWN = 0x00000002;
static constexpr int SELECT = 0x00000002;
static constexpr int START = 0x00000001;
static constexpr int A = 0x00000020;
static constexpr int B = 0x00000010;
// static constexpr int X = 1 << 8;
// static constexpr int Y = 1 << 9;

/* Gets the file size - used to know how much to allocate */
int filesize(FILE *pFile)
{
    fseek(pFile, 0, SEEK_END);
    int lSize = ftell(pFile);
    rewind(pFile);

    return lSize;
}

static uint32_t getrandomcolor()
{
    static int colors[] = {0, 85, 170, 255};

    return RGB888_TO_RGB5551(colors[rand() % 4], colors[rand() % 4], colors[rand() % 4]);
}

static void putText(int x, int y, const char *text, int fgcolor, int bgcolor)
{

    if (text != nullptr)
    {
        auto index = y * SCREEN_COLS + x;
        auto maxLen = strlen(text);
        if (strlen(text) > SCREEN_COLS - x)
        {
            maxLen = SCREEN_COLS - x;
        }
        while (index < SCREENBUFCELLS && *text && maxLen > 0)
        {
            screenBuffer[index].charvalue = *text++;
            screenBuffer[index].fgcolor = fgcolor;
            screenBuffer[index].bgcolor = bgcolor;
            index++;
            maxLen--;
        }
    }
}

int DrawScreen(int selectedRow)
{
    surface_t *surface = display_get();

    // graphics_fill_screen(surface, 1);
    for (int y = 0; y < SCREEN_ROWS; y++)
    {
        for (int x = 0; x < SCREEN_COLS; x++)
        {
            auto index = y * SCREEN_COLS + x;
            auto cell = screenBuffer[index];
            if (y == selectedRow)
            {
                graphics_set_color(cell.bgcolor, cell.fgcolor);
            }
            else
            {
                graphics_set_color(cell.fgcolor, cell.bgcolor);
            }

            graphics_draw_character(surface, (x << 3) + 4, y << 3, cell.charvalue);
        }
    }
    if (dragonsprite)
    {
        graphics_draw_sprite_trans(surface, 80, 120, dragonsprite);
    }
    int framecount = ProcessAfterFrameIsRendered(surface, true);
    display_show(surface);
    return framecount;
}

void ClearScreen(charCell *screenBuffer, int color)
{
    for (auto i = 0; i < SCREENBUFCELLS; i++)
    {
        screenBuffer[i].bgcolor = color;
        screenBuffer[i].fgcolor = color;
        screenBuffer[i].charvalue = ' ';
    }
}

void displayRoms(Frens::RomLister romlister, int startIndex)
{
    char buffer[ROMLISTER_MAXPATH + 4];
    auto y = STARTROW;
    auto entries = romlister.GetEntries();
    ClearScreen(screenBuffer, bgcolor);
    // Show where we are actually looking. An empty list is otherwise silent
    // about whether the SD card mounted or the folder simply has no roms.
    putText(1, 0, dirstack[dirstackindex], fgcolor, bgcolor);
    putText(1, SCREEN_ROWS - 1, "A:Select B:Back START:Settings", fgcolor, bgcolor);
    putText(SCREEN_COLS - strlen(SWVERSION), SCREEN_ROWS - 1, SWVERSION, fgcolor, bgcolor);
    if (romlister.Count() == 0)
    {
        putText(1, STARTROW, "No .sms or .gg files here.", fgcolor, bgcolor);
        putText(1, STARTROW + 2, sdStatus, fgcolor, bgcolor);
        putText(1, STARTROW + 4, "Roms go in a smsPlus64 folder on", fgcolor, bgcolor);
#if NO_DFS == 0
        putText(1, STARTROW + 5, "the SD card. rom:/ above means the", fgcolor, bgcolor);
        putText(1, STARTROW + 6, "card did not mount.", fgcolor, bgcolor);
#else
        // A NODFS build carries no roms of its own, so there is no rom:/ to
        // end up in and the card is the only place a game can come from.
        putText(1, STARTROW + 5, "the SD card. This build has no", fgcolor, bgcolor);
        putText(1, STARTROW + 6, "built-in roms of its own.", fgcolor, bgcolor);
#endif
    }
    for (auto index = startIndex; index < romlister.Count(); index++)
    {
        if (y <= ENDROW)
        {
            auto info = entries[index];
            if (info.IsDirectory)
            {
                snprintf(buffer, sizeof(buffer), "D %s", info.Path);
                debugf("D %s\n", info.Path);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "R %s", info.Path);
                debugf("R %s\n", info.Path);
            }

            putText(1, y, buffer, fgcolor, bgcolor);
            y++;
        }
    }
}

void DisplayFatalError(char *error)
{
    ClearScreen(screenBuffer, bgcolor);
    putText(0, 0, "Fatal error:", fgcolor, bgcolor);
    putText(1, 3, error, fgcolor, bgcolor);
    while (true)
    {
        auto framecount = DrawScreen(-1);
    }
}

void DisplayEmulatorErrorMessage(char *error)
{
    DWORD PAD1_Latch, PAD1_Latch2, pdwSystem;
    ClearScreen(screenBuffer, bgcolor);
    putText(0, 0, "Error occured:", fgcolor, bgcolor);
    putText(0, 3, error, fgcolor, bgcolor);
    putText(0, ENDROW, "Press a button to continue.", fgcolor, bgcolor);
    while (true)
    {
        auto frameCount = DrawScreen(-1);
        processinput(&PAD1_Latch, &PAD1_Latch2, &pdwSystem, false);
        if (PAD1_Latch > 0)
        {
            return;
        }
    }
}

void showSplashScreen()
{
    dragonsprite = loaddragonsprite();
    DWORD PAD1_Latch, PAD1_Latch2, pdwSystem;
    const char *s;
    ClearScreen(screenBuffer, bgcolor);
   
    s = "SMSPlus";
    putText(SCREEN_COLS / 2 - (strlen(s) + 4) / 2, 2, s, fgcolor, bgcolor);

    putText((SCREEN_COLS / 2 - (strlen(s)) / 2) + 5, 2, "6", CRED, bgcolor);
    putText((SCREEN_COLS / 2 - (strlen(s)) / 2) + 6, 2, "4", CGREEN, bgcolor);

    s = "Sega Master System &";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 4, s, fgcolor, bgcolor);
    s = "Sega Game Gear";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 5, s, fgcolor, bgcolor);
    s = "emulator for the Nintendo 64";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 6, s, fgcolor, bgcolor);

    s = "N64 Port";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 9, s, fgcolor, bgcolor);
    s = "@frenskefrens";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 10, s, CLIGHTBLUE, bgcolor);

    s = "Built with Libdragon";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 12, s, fgcolor, bgcolor);
    s = "https://github.com/";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 13, s, CLIGHTBLUE, bgcolor);
    s = "DragonMinded/libdragon";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 14, s, CLIGHTBLUE, bgcolor);

    s = "https://github.com/";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 25, s, CLIGHTBLUE, bgcolor);
    s = "fhoedemakers/smsplus64";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 26, s, CLIGHTBLUE, bgcolor);
    int startFrame = -1;
    putText(SCREEN_COLS - strlen(SWVERSION) - 1, SCREEN_ROWS - 2, SWVERSION, fgcolor, bgcolor);
    bool first = true;
    while (true)
    {
        auto frameCount = DrawScreen(-1);
          
        if (startFrame == -1)
        {
            startFrame = frameCount;
        }

        processinput(&PAD1_Latch, &PAD1_Latch2, &pdwSystem, false);
        if (PAD1_Latch > 0 || (frameCount - startFrame) > 800)
        {
            break;
        }
        if (first || (frameCount % 30) == 0)
        {
            first = false;
            for (auto i = 0; i < SCREEN_COLS; i++)
            {
                auto col = getrandomcolor();
                putText(i, 0, " ", col, col);
                col = getrandomcolor();
                putText(i, SCREEN_ROWS - 1, " ", col, col);
            }
            for (auto i = 1; i < SCREEN_ROWS - 1; i++)
            {
                auto col = getrandomcolor();
                putText(0, i, " ", col, col);
                col = getrandomcolor();
                putText(SCREEN_COLS - 1, i, " ", col, col);
            }
        }
    }
    dragonsprite = nullptr;
}
void showLoadScreen()
{

    const char *s;
    s = "------------";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 14, s, bgcolor, fgcolor);
    s = "|Loading...|";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 15, s, bgcolor, fgcolor);
    s = "------------";
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 16, s, bgcolor, fgcolor);
    DrawScreen(-1);
}
void screenSaver()
{
    DWORD PAD1_Latch, PAD1_Latch2, pdwSystem;
    WORD frameCount;
    while (true)
    {
        frameCount = DrawScreen(-1);
        processinput(&PAD1_Latch, &PAD1_Latch2, &pdwSystem, false);

        if (PAD1_Latch > 0)
        {
            return;
        }
        if ((frameCount % 3) == 0)
        {
            auto color = getrandomcolor();
            auto row = rand() % SCREEN_ROWS;
            auto column = rand() % SCREEN_COLS;
            putText(column, row, " ", color, color);
        }
    }
}
void clearinput()
{
    DWORD PAD1_Latch, PAD1_Latch2, pdwSystem;
    while (1)
    {
        DrawScreen(-1);
        processinput(&PAD1_Latch, &PAD1_Latch2, &pdwSystem, true);
        if (PAD1_Latch == 0)
        {
            break;
        }
    }
}
#define NUMSETTINGS 7

// Settings screen, so the in-game button combinations do not have to be
// remembered. Reachable with START from the rom browser.
static void settingsScreen(const char *mountPoint)
{
    DWORD PAD1_Latch, PAD1_Latch2, pdwSystem;
    char line[SCREEN_COLS + 1];
    int row = 0;
    bool changed = false;

    // Pick up anything the in-game combinations changed, so this screen always
    // opens showing what is actually in effect.
    settings_capture();
    clearinput();

    while (1)
    {
        ClearScreen(screenBuffer, bgcolor);
        putText(1, 0, "Settings", fgcolor, bgcolor);

        snprintf(line, sizeof(line), "Frameskip  : %s", settings_frameskip_name(settings.frameskip));
        putText(1, STARTROW + 0, line, fgcolor, bgcolor);
        snprintf(line, sizeof(line), "Blink fix  : %s", settings.blinkFix ? "On" : "Off");
        putText(1, STARTROW + 1, line, fgcolor, bgcolor);
        snprintf(line, sizeof(line), "Sound      : %s", settings.sound ? "On" : "Off");
        putText(1, STARTROW + 2, line, fgcolor, bgcolor);
        snprintf(line, sizeof(line), "Frame rate : %s", settings.showFps ? "Shown" : "Hidden");
        putText(1, STARTROW + 3, line, fgcolor, bgcolor);
        snprintf(line, sizeof(line), "Profiler   : %s", settings.showProfiler ? "Shown" : "Hidden");
        putText(1, STARTROW + 4, line, fgcolor, bgcolor);
        snprintf(line, sizeof(line), "Upscale    : %s", settings.upscale ? "On" : "Off");
        putText(1, STARTROW + 5, line, fgcolor, bgcolor);
        snprintf(line, sizeof(line), "Autostart  : %s", settings.autostart ? "On" : "Off");
        putText(1, STARTROW + 6, line, fgcolor, bgcolor);

        putText(1, STARTROW + 8,  "Some games blink a character on and", fgcolor, bgcolor);
        putText(1, STARTROW + 9,  "off after it is hit. Frameskip can", fgcolor, bgcolor);
        putText(1, STARTROW + 10, "drop just the frames it is drawn on,", fgcolor, bgcolor);
        putText(1, STARTROW + 11, "so it seems to disappear instead.", fgcolor, bgcolor);
        putText(1, STARTROW + 12, "Blink fix varies which frames are", fgcolor, bgcolor);
        putText(1, STARTROW + 13, "drawn so the blink stays visible,", fgcolor, bgcolor);
        putText(1, STARTROW + 14, "at a little smoothness. Try it if a", fgcolor, bgcolor);
        putText(1, STARTROW + 15, "character vanishes when it is hit.", fgcolor, bgcolor);

        putText(1, STARTROW + 17, "Upscale fills the screen with the", fgcolor, bgcolor);
        putText(1, STARTROW + 18, "picture instead of leaving a border.", fgcolor, bgcolor);
        putText(1, STARTROW + 20, "Autostart runs the game picked in", fgcolor, bgcolor);
        putText(1, STARTROW + 21, "the flashcart menu. Turn it off if", fgcolor, bgcolor);
        putText(1, STARTROW + 22, "the last game keeps restarting.", fgcolor, bgcolor);
        putText(1, STARTROW + 24, "Saved to the SD card on exit.", fgcolor, bgcolor);

        putText(1, SCREEN_ROWS - 1, "Left/Right: change, B: Back", fgcolor, bgcolor);
        DrawScreen(STARTROW + row);
        processinput(&PAD1_Latch, &PAD1_Latch2, &pdwSystem, false);

        if ((PAD1_Latch & UP) == UP)
        {
            row = (row + NUMSETTINGS - 1) % NUMSETTINGS;
        }
        else if ((PAD1_Latch & DOWN) == DOWN)
        {
            row = (row + 1) % NUMSETTINGS;
        }
        else if ((PAD1_Latch & B) == B)
        {
            break;
        }
        else if ((PAD1_Latch & (LEFT | RIGHT | A)) != 0)
        {
            int direction = ((PAD1_Latch & LEFT) == LEFT) ? -1 : 1;
            switch (row)
            {
            case 0:
                settings.frameskip += direction;
                if (settings.frameskip > 3) settings.frameskip = -1;
                if (settings.frameskip < -1) settings.frameskip = 3;
                break;
            case 1: settings.blinkFix = !settings.blinkFix; break;
            case 2: settings.sound = !settings.sound; break;
            case 3: settings.showFps = !settings.showFps; break;
            case 4: settings.showProfiler = !settings.showProfiler; break;
            case 5: settings.upscale = !settings.upscale; break;
            case 6: settings.autostart = !settings.autostart; break;
            }
            changed = true;
        }
    }

    settings_apply();
    if (changed && !settings_save(mountPoint))
    {
        // Say so on screen rather than losing the change silently. Usually the
        // smsPlus64 folder does not exist yet, or there is no SD card and we are
        // running from the read-only rom:/ filesystem.
        debugf("Could not save settings to %s\n", mountPoint);
        ClearScreen(screenBuffer, bgcolor);
        putText(1, 0, "Settings", fgcolor, bgcolor);
        putText(1, STARTROW, "Could not save your settings.", fgcolor, bgcolor);
        putText(1, STARTROW + 2, "They stay in effect until you", fgcolor, bgcolor);
        putText(1, STARTROW + 3, "switch the console off.", fgcolor, bgcolor);
        putText(1, STARTROW + 5, "To keep them, create a folder", fgcolor, bgcolor);
        putText(1, STARTROW + 6, "named smsPlus64 on the SD card.", fgcolor, bgcolor);
        putText(1, SCREEN_ROWS - 1, "Press a button to continue", fgcolor, bgcolor);
        clearinput();
        while (1)
        {
            DrawScreen(-1);
            processinput(&PAD1_Latch, &PAD1_Latch2, &pdwSystem, false);
            if (PAD1_Latch > 0 || pdwSystem > 0)
            {
                break;
            }
        }
    }
    clearinput();
}

// Global instances of local vars in romselect() some used in Lambda expression later on
static char *selectedRomOrFolder;
static uintptr_t FLASH_ADDRESS;
static bool errorInSavingRom = false;
static char *globalErrorMessage;

//
BYTE *dirbuffer;

RomInfo menu(char *mountPoint, uintptr_t NES_FILE_ADDR, char *errorMessage, bool isFatal, bool reset)
{
    RomInfo romInfo;
    FLASH_ADDRESS = NES_FILE_ADDR;
    int firstVisibleRowINDEX = 0;
    int selectedRow = STARTROW;
    char currentDir[MAX_FILENAME_LEN];
    int totalFrames = -1;

    globalErrorMessage = errorMessage;

    DWORD PAD1_Latch, PAD1_Latch2, pdwSystem;

    int horzontalScrollIndex = 0;
    debugf("Starting Menu, Mount Point: %s\n", mountPoint);
    dirstack = (char(*)[MAX_FILENAME_LEN])calloc(MAXDIRDEPTH, sizeof(char[MAX_FILENAME_LEN]));
    if (dirstack == nullptr)
    {
        debugf("Cannot allocate memory for directory stack");
        exit(0);
    }
    dirstackindex = 0;
    strcpy(dirstack[dirstackindex], mountPoint);
    screenBuffer = (charCell *)malloc(SCREENBUFCELLS * sizeof(charCell));
    if (screenBuffer == nullptr)
    {
        debugf("Cannot allocate memory for screen buffer");
        exit(0);
    }
    size_t ramsize;
    // Borrow Emulator RAM buffer for screen.
    // screenBuffer = (charCell *)InfoNes_GetRAM(&ramsize);
    /// size_t chr_size;
    // Borrow ChrBuffer to store directory contents
    // void *buffer = InfoNes_GetChrBuf(&chr_size);
    size_t bufsize;
    dirbuffer = (BYTE *)getcachestorefromemulator(&bufsize);

    Frens::RomLister romlister(dirbuffer, bufsize);
    clearinput();
    if (strlen(errorMessage) > 0)
    {
        if (isFatal) // SD card not working, show error
        {
            DisplayFatalError(errorMessage);
        }
        else
        {
            DisplayEmulatorErrorMessage(errorMessage); // Emulator cannot start, show error
        }
    }
    else
    {
        // Show splash screen, only when not reset from emulation
        if (reset == false)
        {
            debugf("Showing splash screen\n");
            showSplashScreen();
        }
        else
        {
            //   sleep_ms(300);
        }
    }
    debugf("Listing roms\n");
    showLoadScreen();
    romlister.list(dirstack[dirstackindex]);
    displayRoms(romlister, firstVisibleRowINDEX);
    while (1)
    {

        auto index = selectedRow - STARTROW + firstVisibleRowINDEX;
        auto entries = romlister.GetEntries();

        selectedRomOrFolder = (romlister.Count() > 0) ? entries[index].Path : nullptr;
        errorInSavingRom = false;
        auto frameCount = DrawScreen(selectedRow);
        processinput(&PAD1_Latch, &PAD1_Latch2, &pdwSystem, false);

        if (PAD1_Latch > 0 || pdwSystem > 0)
        {
            totalFrames = frameCount; // Reset screenSaver
            // reset horizontal scroll of highlighted row
            horzontalScrollIndex = 0;
            putText(3, selectedRow, selectedRomOrFolder, fgcolor, bgcolor);
            if ((PAD1_Latch & UP) == UP && selectedRomOrFolder)
            {
                if (selectedRow > STARTROW)
                {
                    selectedRow--;
                }
                else
                {
                    if (firstVisibleRowINDEX > 0)
                    {
                        firstVisibleRowINDEX--;
                        displayRoms(romlister, firstVisibleRowINDEX);
                    }
                }
            }
            else if ((PAD1_Latch & DOWN) == DOWN && selectedRomOrFolder)
            {
                if (selectedRow < ENDROW && (index) < romlister.Count() - 1)
                {
                    selectedRow++;
                }
                else
                {
                    if (index < romlister.Count() - 1)
                    {
                        firstVisibleRowINDEX++;
                        displayRoms(romlister, firstVisibleRowINDEX);
                    }
                }
            }
            else if ((PAD1_Latch & LEFT) == LEFT && selectedRomOrFolder)
            {
                showLoadScreen();
                firstVisibleRowINDEX -= PAGESIZE;
                if (firstVisibleRowINDEX < 0)
                {
                    firstVisibleRowINDEX = 0;
                }
                selectedRow = STARTROW;
                displayRoms(romlister, firstVisibleRowINDEX);
            }
            else if ((PAD1_Latch & RIGHT) == RIGHT && selectedRomOrFolder)
            {
                showLoadScreen();
                if (firstVisibleRowINDEX + PAGESIZE < romlister.Count())
                {
                    firstVisibleRowINDEX += PAGESIZE;
                }
                selectedRow = STARTROW;
                displayRoms(romlister, firstVisibleRowINDEX);
            }
            else if ((PAD1_Latch & B) == B)
            {
                // go up level
                if (dirstackindex > 0)
                {
                    dirstackindex--;
                    showLoadScreen();
                    romlister.list(dirstack[dirstackindex]);
                    firstVisibleRowINDEX = 0;
                    selectedRow = STARTROW;
                    displayRoms(romlister, firstVisibleRowINDEX);
                }
            }
            else if ((pdwSystem & START) == START && (pdwSystem & SELECT) != SELECT)
            {
                settingsScreen(mountPoint);
                displayRoms(romlister, firstVisibleRowINDEX);
            }
            else if ((PAD1_Latch & A) == A && selectedRomOrFolder)
            {
                if (entries[index].IsDirectory)
                {
                    if (dirstackindex < MAXDIRDEPTH - 1)
                    {
                        dirstackindex++;
                        // if (dirstackindex == 1)
                        // {
                        //     sprintf(dirstack[dirstackindex], "%s%s", dirstack[dirstackindex - 1], selectedRomOrFolder);
                        // }
                        // else
                        // {
                        sprintf(dirstack[dirstackindex], "%s/%s", dirstack[dirstackindex - 1], selectedRomOrFolder);
                        //}
                        debugf("Pushing %s\n", dirstack[dirstackindex]);
                    }
                    else
                    {
                        debugf("Directory stack full\n");
                    }
                    showLoadScreen();
                    romlister.list(dirstack[dirstackindex]);
                    firstVisibleRowINDEX = 0;
                    selectedRow = STARTROW;
                    displayRoms(romlister, firstVisibleRowINDEX);
                }
                else
                {
                    // start game
                    debugf("Selected %s\n", selectedRomOrFolder);
                    char filetoopen[MAX_FILENAME_LEN];
                    sprintf(filetoopen, "%s/%s", dirstack[dirstackindex], selectedRomOrFolder);
                    debugf("Opening %s\n", filetoopen);
                    // Load the rom into the emulator
                    FILE *pFile = fopen(filetoopen, "rb");
                    if (pFile == nullptr)
                    {
                        snprintf(globalErrorMessage, 40, "Cannot open %s", filetoopen);
                        errorInSavingRom = true;
                        debugf("Cannot open %s\n", filetoopen);
                        break;
                    }
                    int size = filesize(pFile);
                    // A copier header makes the file an odd number of 512 byte
                    // blocks; a rom on its own never is. It is not part of the
                    // rom, so it comes off the size here, before anything is
                    // allocated. Taking it off afterwards - as this used to -
                    // left the last 512 bytes of the buffer never read while
                    // romInfo.size still counted them as rom, so whatever the
                    // allocator handed over was passed to the emulator as
                    // cartridge data.
                    int romheader = ((size / 512) & 1) ? 512 : 0;
                    if (romheader)
                    {
                        debugf("Skipping 512 byte header\n");
                        size -= romheader;
                    }
                    if (size > 0)
                    {
                        showLoadScreen();
                        debugf("Size of rom in %s is %d\n", filetoopen, size);
                        romInfo.size = size;
                        romInfo.rom = (uint8_t *)malloc(size);
                        romInfo.isGameGear = Frens::cstr_endswith(selectedRomOrFolder, ".gg");
                        strcpy(romInfo.title, selectedRomOrFolder);
                        if (romInfo.rom == nullptr)
                        {
                            snprintf(globalErrorMessage, 40, "Cannot allocate memory for rom");
                            errorInSavingRom = true;
                            debugf("Cannot allocate memory for rom\n");
                        }
                        else
                        {
                            debugf("Allocated %d bytes for rom, reading file\n", size);
                            fseek(pFile, romheader, SEEK_SET);
                            // A short read would leave the tail of the buffer
                            // uninitialised, which is the thing this block is
                            // careful not to do, so it is an error rather than
                            // something to run anyway.
                            if ((int)fread(romInfo.rom, 1, size, pFile) != size)
                            {
                                snprintf(globalErrorMessage, 40, "Cannot read rom");
                                errorInSavingRom = true;
                                debugf("Short read on %s\n", filetoopen);
                                free(romInfo.rom);
                                romInfo.rom = nullptr;
                                romInfo.size = 0;
                            }
                            else
                            {
                                fclose(pFile);
                                break;
                            }
                        }
                    }
                    else
                    {
                        debugf("Nothing to load from %s\n", filetoopen);
                    }
                    fclose(pFile);
                }
            }
        }
        // scroll selected row horizontally if textsize exceeds rowlength
        if (selectedRomOrFolder)
        {
            if ((frameCount % 30) == 0)
            {
                if (strlen(selectedRomOrFolder + horzontalScrollIndex) > VISIBLEPATHSIZE)
                {
                    horzontalScrollIndex++;
                }
                else
                {
                    horzontalScrollIndex = 0;
                }
                putText(3, selectedRow, selectedRomOrFolder + horzontalScrollIndex, fgcolor, bgcolor);
            }
        }
        if (totalFrames == -1)
        {
            totalFrames = frameCount;
        }
        if ((frameCount - totalFrames) > 800)
        {
            debugf("Starting screensaver\n");
            totalFrames = -1;
            screenSaver();
            displayRoms(romlister, firstVisibleRowINDEX);
        }
    } // while 1
      // Wait until user has released all buttons
    clearinput();
    debugf("Exiting menu\n");
    free(dirstack);
    free(screenBuffer);
    return romInfo;
}
