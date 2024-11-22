
#include "shared.h"
#include "common.h"
#include "libdragon.h"
#include "menu.h"
#include "FrensHelpers.h"

#ifndef USEMENU
#include "builtinrom.h"
#endif
#include <stdarg.h>

#define ERRORMESSAGESIZE 40
#define GAMESAVEDIR "/SAVES"

#define FRAMEBUFFERS 3

/* hardware definitions */
// Pad buttons
#define A_BUTTON(a) ((a) & 0x8000)
#define B_BUTTON(a) ((a) & 0x4000)
#define Z_BUTTON(a) ((a) & 0x2000)
#define START_BUTTON(a) ((a) & 0x1000)

// D-Pad
#define DU_BUTTON(a) ((a) & 0x0800)
#define DD_BUTTON(a) ((a) & 0x0400)
#define DL_BUTTON(a) ((a) & 0x0200)
#define DR_BUTTON(a) ((a) & 0x0100)

// Triggers
#define TL_BUTTON(a) ((a) & 0x0020)
#define TR_BUTTON(a) ((a) & 0x0010)

// Yellow C buttons
#define CU_BUTTON(a) ((a) & 0x0008)
#define CD_BUTTON(a) ((a) & 0x0004)
#define CL_BUTTON(a) ((a) & 0x0002)
#define CR_BUTTON(a) ((a) & 0x0001)

#define PAD_DEADZONE 5
#define PAD_ACCELERATION 10
#define PAD_CHECK_TIME 40

surface_t *_dc;

#define SOUNDISENABLED 1
int soundEnabled = SOUNDISENABLED;

char *ErrorMessage;
bool isFatalError = false;

char romName[256];

static bool fps_enabled = false;
timer_link_t *fpstimer = nullptr;
static bool hideFrameRate = false;

bool reset = false;

bool controller1IsInserted = false;
bool controller2IsInserted = false;

// Sega header https://www.smspower.org/Development/ROMHeader
struct SegaHeader
{
    // 0x7FF0
    char signature[8];
    // 0x7FF8
    uint16_t reserverd;
    // 0x7FFA
    uint16_t checksum;
    // 0x7FFC
    uint8_t product_code[2];
    // 0x7FFE
    uint8_t ProductCodeAndVersion;
    // 0x7FFF
    uint8_t sizeAndRegion;
} header;
extern "C" void sms_palette_syncGG(int index)
{
    // The GG has a different palette format
    int r = ((vdp.cram[(index << 1) | 0] >> 1) & 7) << 5;
    int g = ((vdp.cram[(index << 1) | 0] >> 5) & 7) << 5;
    int b = ((vdp.cram[(index << 1) | 1] >> 1) & 7) << 5;
#if 0
    int r444 = ((r << 4) + 127) >> 8; // equivalent to (r888 * 15 + 127) / 255
    int g444 = ((g << 4) + 127) >> 8; // equivalent to (g888 * 15 + 127) / 255
    int b444 = ((b << 4) + 127) >> 8;
    palette444[index] = (r444 << 8) | (g444 << 4) | b444;
#endif
    palette444[index] = RGB888_TO_RGB5551(r, g, b);
    return;
}

extern "C" void sms_palette_sync(int index)
{
#if 0
    // Get SMS palette color index from CRAM
    WORD r = ((vdp.cram[index] >> 0) & 3);
    WORD g = ((vdp.cram[index] >> 2) & 3);
    WORD b = ((vdp.cram[index] >> 4) & 3);
    WORD tableIndex = b << 4 | g << 2 | r;
    // Get the RGB444 color from the SMS RGB444 palette
    palette444[index] = SMSPaletteRGB444[tableIndex];
#endif

#if 1
    // Alternative color rendering below
    WORD r = ((vdp.cram[index] >> 0) & 3) << 6;
    WORD g = ((vdp.cram[index] >> 2) & 3) << 6;
    WORD b = ((vdp.cram[index] >> 4) & 3) << 6;
#if 0
    int r444 = ((r << 4) + 127) >> 8; // equivalent to (r888 * 15 + 127) / 255
    int g444 = ((g << 4) + 127) >> 8; // equivalent to (g888 * 15 + 127) / 255
    int b444 = ((b << 4) + 127) >> 8;
    palette444[index] = (r444 << 8) | (g444 << 4) | b444;
#endif
    palette444[index] = RGB888_TO_RGB5551(r, g, b);
#endif
    return;
}

extern "C" void sms_render_line(int line, const uint8_t *buffer)
{
    // SMS has 192 lines
    // GG  has 144 lines
    // gg : Line starts at line 24
    // sms: Line starts at line 0
    // Emulator loops from scanline 0 to 261
    if (IS_GG)
    {
        if (line < 24 || line >= 168)
        {
            return;
        }
    }
    else
    {
        if (line >= 192)
        {
            return;
        }
    }
    // center more or less screen
    line += 24;

    // debugf("\tLine %d, ISGG: %d\n", line, IS_GG);

    if (buffer)
    {
        WORD *framebufferline = ((WORD *)(_dc)->buffer) + (line << 8) + (IS_GG ? 48 : 0);
        for (int i = screenCropX; i < BMP_WIDTH - screenCropX; i++)
        {
            framebufferline[i - screenCropX] = palette444[(buffer[i + BMP_X_OFFSET]) & 31];
        }
    }
}

void system_load_sram(void)
{
    printf("system_load_sram: TODO\n");

    // TODO
}

void system_save_sram()
{
    printf("system_save_sram: saving sram TODO\n");

    // TODO
}

void system_load_state()
{
    // TODO
}

void system_save_state()
{
    // TODO
}

int framecounter = 0;
int framedisplay = 0;
int totalfames = 0;
int ProcessAfterFrameIsRendered(surface_t *display, bool fromMenu)
{
    char buffer[15];
    if (fps_enabled)
    {
        char sound = soundEnabled ? 'S' : 'M';
        sprintf(buffer, "%c %d", sound, framedisplay);
        // debugf("Frame %d\n", totalfames);
        if (IS_GG && fromMenu == false)
        {
            graphics_draw_text(display, 48, 24, buffer);
        }
        else
        {
            graphics_draw_text(display, 10, 5, buffer);
        }
        // Frame rate calculation
    }
    framecounter++;
    return totalfames++;
}

void frameratecalc(int ovfl)
{
    // debugf("FPS: %d\n", framecounter);
    framedisplay = framecounter;
    framecounter = 0;
}
void enableordisableTimer()
{
    if (fps_enabled)
    {
        fpstimer = new_timer(TIMER_TICKS(1000000), TF_CONTINUOUS, frameratecalc);
        framecounter = framedisplay = 0;
    }
    else
    {
        if (fpstimer != nullptr)
        {
            delete_timer(fpstimer);
            fpstimer = nullptr;
        }
    }
}
#define OTHER_BUTTON1 (0b1)
#define OTHER_BUTTON2 (0b10)

static DWORD prevButtons[2]{};
static DWORD prevButtonssystem[2]{};
static DWORD prevOtherButtons[2]{};

struct controller_data gKeys;
static int rapidFireMask[2]{};
static int rapidFireCounter = 0;
void processinput(DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem, bool ignorepushed)
{
    controller_scan();
    gKeys = get_keys_pressed();

    // pwdPad1 and pwdPad2 are only used in menu and are only set on first push
    *pdwPad1 = *pdwPad2 = *pdwSystem = 0;

    int smssystem[2]{};
    unsigned long pushed, pushedsystem, pushedother;
    for (int i = 0; i < 1; i++)
    {
        if ((i == 0 && controller1IsInserted == false) ||
            (i == 1 && controller2IsInserted == false))
        {
            continue;
        }
        auto &dst = (i == 0) ? *pdwPad1 : *pdwPad2;

        auto gp = gKeys.c[i].data >> 16;

        int smsbuttons = (DL_BUTTON(gp) ? INPUT_LEFT : 0) |
                         (DR_BUTTON(gp) ? INPUT_RIGHT : 0) |
                         (DU_BUTTON(gp) ? INPUT_UP : 0) |
                         (DD_BUTTON(gp) ? INPUT_DOWN : 0) |
                         (A_BUTTON(gp) ? INPUT_BUTTON1 : 0) |
                         (B_BUTTON(gp) ? INPUT_BUTTON2 : 0) | 0;
        int otherButtons = (CL_BUTTON(gp) ? OTHER_BUTTON1 : 0) |
                           (CU_BUTTON(gp) ? OTHER_BUTTON2 : 0) | 0;
        smssystem[i] =
            (Z_BUTTON(gp) ? INPUT_PAUSE : 0) |
            (START_BUTTON(gp) ? INPUT_START : 0) |
            0;

        // if (gp.buttons & io::GamePadState::Button::SELECT) printf("SELECT\n");
        // if (gp.buttons & io::GamePadState::Button::START) printf("START\n");
        input.pad[i] = smsbuttons;

        auto p1 = smssystem[i];
        if (ignorepushed == false)
        {
            pushed = smsbuttons & ~prevButtons[i];
            pushedsystem = smssystem[i] & ~prevButtonssystem[i];
            pushedother = otherButtons & ~prevOtherButtons[i];
        }
        else
        {
            pushed = smsbuttons;
            pushedsystem = smssystem[i];
            pushedother = otherButtons;
        }
        if (p1 & INPUT_PAUSE)
        {
            if (pushedsystem & INPUT_START)
            {
                reset = true;
                debugf("Reset pressed\n");
            }
            // Toggle frame rate display
            if (pushed & INPUT_BUTTON1)
            {
                fps_enabled = !fps_enabled;
                enableordisableTimer();
                debugf("FPS: %s\n", fps_enabled ? "ON" : "OFF");
                if (fps_enabled == false)
                {
                    hideFrameRate = true;
                    debugf("Hiding frame rate\n");
                }
            }
            if (pushed & INPUT_BUTTON2)
            {

                snd.enabled = soundEnabled = !soundEnabled;
                debugf("Toggle sound (%d)\n", soundEnabled);
            }
        }
        if (p1 & INPUT_START)
        {

            if (pushed & INPUT_UP)
            {
                // screenMode(-1);
            }
            else if (pushed & INPUT_DOWN)
            {
                // screenMode(+1);
            }
        }
        prevButtons[i] = smsbuttons;
        prevButtonssystem[i] = smssystem[i];
        prevOtherButtons[i] = otherButtons;
        // return only on first push
        if (pushed)
        {
            dst = smsbuttons;
        }
        if (pushedother)
        {
            if (pushedother & OTHER_BUTTON1)
            {
                debugf("Other 1\n");
            }
            if (pushedother & OTHER_BUTTON2)
            {
                debugf("Other 2\n");
            }
        }
    }
    input.system = *pdwSystem = smssystem[0] | smssystem[1];
    // return only on first push
    if (pushedsystem)
    {
        *pdwSystem = smssystem[0] | smssystem[1];
    }
}

void process(void)
{
    DWORD pdwPad1, pdwPad2, pdwSystem; // have only meaning in menu
    while (reset == false)
    {
        // debugf("Frame %d\n", framecounter);
        processinput(&pdwPad1, &pdwPad2, &pdwSystem, false);
        _dc = display_get();
        if (hideFrameRate)
        {

            hideFrameRate = false;
            // Clear all the framebuffers
            for (int i = 0; i < FRAMEBUFFERS; i++)
            {
                debugf("Clear framebuffer %d\n", i + 1);
                graphics_fill_screen(_dc, 1);
                display_show(_dc);
                _dc = display_get();
            }
        }
        sms_frame(0);
        ProcessAfterFrameIsRendered(_dc, false);
        display_show(_dc);

        //
#if 0
       
        short *p = audio_write_begin();
        //debugf("Audio buffer length: %d\n",  snd.bufsize );
        int i = 0;
        for (int x = 0; x < snd.bufsize; x++)
        {
            // audio_buffer[x] = (snd.buffer[0][x] << 16) + snd.buffer[1][x];
            *p++ = (snd.buffer[0][x] << 16) + snd.buffer[1][x];
        }
        audio_write_end();
#endif
    }
}

void checkcontrollers()
{
    controller1IsInserted = controller2IsInserted = false;
    int controllers = get_controllers_present();
    if (controllers & CONTROLLER_1_INSERTED)
    {
        debugf("Controller 1 inserted\n");
        controller1IsInserted = true;
    }
    if (controllers & CONTROLLER_2_INSERTED)
    {
        debugf("Controller 2 inserted\n");
        controller2IsInserted = true;
    }
}

size_t find_sequence(uint8_t *buffer, size_t buffer_size, const char *sequence)
{
    size_t seq_len = strlen(sequence);
    debugf("Finding Sequence: %s\n", sequence);
    // Iterate through each position in the buffer
    for (size_t i = 0; i <= buffer_size - seq_len; i++)
    {
        // Compare the current part of the buffer with the sequence
        if (memcmp(buffer + i, sequence, seq_len) == 0)
        {
            // print found sequence in hex  at offset
            debugf("Found sequence at offset %x: ", i);

            for (size_t j = 0; j < seq_len; j++)
            {
                debugf("%c", buffer[i + j]);
            }
            debugf("\n");
            return i; // Return the starting index of the match
        }
    }
    return -1; // Return -1 if not found
}

// debug_init_sdfs is only available when NDEBUG is not defined
// We need sdfs to access the everdrive SD filesystem. So make it also available when NDEBUG is defined.
// #ifdef 	NDEBUG
#undef debug_init_sdfs
#ifdef __cplusplus
extern "C"
{
#endif
    bool debug_init_sdfs(const char *prefix, int npart);
    bool init_sdfs(const char *prefix, int npart)
    {
        return debug_init_sdfs(prefix, npart);
    }
#ifdef __cplusplus
}
#endif

// create a wrapper for debugf to stdout 
void debugstdout(const char *fmt, ...)
{
#ifndef NDEBUG
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    //vfprintf(stderr, fmt, args);
    va_end(args); 
#endif
}
// Checks if a rom is injected by the everdrive/N64Flashcart menu
// The rom is injected at 0xB0200000 and has a Sega header
// The Sega header is at 0x7FF0
// Some roms have a 512 byte header, so we also check at 0x7FF0 + 512
bool IsRomInjected(RomInfo *info, bool withOffset)
{
    bool rval = false;
    int offset = withOffset ? 512 : 0;
    debugstdout("Searching for Sega header at %x\n", 0xB0200000 + 0x7FF0 + offset);
    dma_read_async(&header, 0xB0200000 + 0x7FF0 + offset, sizeof(header));
    dma_wait();
    if (strncmp(header.signature, "TMR SEGA", 8) == 0)
    {
        info->isGameGear = false;
        uint8_t romsize = header.sizeAndRegion & 0b00001111;
        uint8_t region = (header.sizeAndRegion >> 4) & 0b00001111;
        // https://www.smspower.org/Development/ROMHeader
        switch (romsize)
        {
        case 0:                      // 256KB
            info->size = 512 * 1024; // 512KB and 1MB Roms are reported in the header as 256KB.
                                     // Setting Rom size to 512KB also works for 256KB roms.
            break;                   // Setting rom size to 1MB for 256 or 512KB games does not work.
                                     // Only a small set of roms are 1MB.
        case 1:
            info->size = 512 * 1024;
            break;
        case 2:
            info->size = 1024 * 1024;
            break;
        case 0xa:
            info->size = 8 * 1024;
            break;
        case 0xb:
            info->size = 16 * 1024;
            break;
        case 0xc:
            info->size = 32 * 1024;
            break;
        case 0xd:
            info->size = 48 * 1024;
            break;
        case 0xe:
            info->size = 64 * 1024;
            break;
        case 0xf:
            info->size = 128 * 1024;
            break;
        default:
            debugstdout("Unknown romsize %x\n", romsize);
            info->size = 0; // unknown size
            break;
        }
        info->isGameGear = false;
        debugstdout("Romsize %x = %d bytes\n", romsize, info->size);
        debugstdout("Region: %x - ", region);
        switch (region)
        {
        case 3:
            debugstdout("SMS Japan\n");
            info->isGameGear = false;
            break;
        case 4:
            debugstdout("SMS Export\n");
            info->isGameGear = false;
            break;
        case 5:
            debugstdout("GG USA\n");
            info->isGameGear = true;
            break;
        case 6:
            debugstdout("GG Export\n");
            info->isGameGear = true;
            break;
        case 7:
            debugstdout("GG International\n");
            info->isGameGear = true;
            break;
        default:
            debugstdout("Unknown\n");
            break;
        }
        if (info->size > 0)
        {
            rval = true;
        }
    }
    return rval;
}

// #endif
/// @brief
/// Start emulator.
/// RelWithDebugInfo mode.
/// @return
int main()
{

    char errMSG[ERRORMESSAGESIZE];
    errMSG[0] = romName[0] = 0;
    int fileSize = 0;
    bool isGameGear = false;
    char mountPoint[20];
    size_t tmpSize;
    bool startedFromEverDriveMenu = false;
    bool dfsStarted = false;
    ErrorMessage = errMSG;
    RomInfo info;

    debug_init(DEBUG_FEATURE_LOG_ISVIEWER | DEBUG_FEATURE_LOG_USB );
    debugf("Starting SMSPlus64, a Sega Master System emulator for the Nintendo 64 - https://github.com/fhoedemakers/smsplus64\n");
    debugf("Built on %s %s using libdragon - https://github.com/DragonMinded/libdragon\n", __DATE__, __TIME__);

    controller_init();
    timer_init();
    enableordisableTimer();
    struct controller_data output;
    get_accessories_present(&output);
    int accessory = identify_accessory(0);
    switch (accessory)
    {
    case ACCESSORY_MEMPAK:
        debugf("Accessory: Memory Pak\n");
        break;
    case ACCESSORY_RUMBLEPAK:
        debugf("Accessory: Rumble Pak\n");
        break;
    case ACCESSORY_TRANSFERPAK:
        debugf("Accessory: Transfer Pak\n");
        break;
    case ACCESSORY_VRU:
        debugf("Accessory: VRU\n");
        break;
    default:
        debugf("Accessory: None\n");
        break;
    }
    while (true)
    {
        int offset = 0;
        checkcontrollers();
       
    #ifndef NDEBUG
        console_init();
        console_set_render_mode(RENDER_MANUAL);
        console_clear();
    #endif
        // Check whether rom is started via Everdrive/N64Flashcartmenu
        // Those roms are injected at 0xB0200000 and have a Sega header
        debugstdout("Check if game is started via Everdrive/FlashCartMenu\n");
        if ((startedFromEverDriveMenu = IsRomInjected(&info, false)) == false)
        {
            if ((startedFromEverDriveMenu = IsRomInjected(&info, true)) == true)
            {
                offset = 512;
            }
            else
            {
                debugstdout("No Sega header found\n");
            }
        }

        if (startedFromEverDriveMenu)
        {
            debugstdout("Allocating memory for rom\n");
            info.rom = (uint8_t *)malloc(info.size);
            debugstdout("Reading rom at 0xB0200000\n");
            dma_read_async(info.rom, 0xB0200000 + offset, info.size);
            debugstdout("Waiting for dma\n");
            dma_wait();
            strcpy(info.title, "Everdrive/Flashcart");
        }
        else
        {
            debugstdout("Will start menu\n");
            if (dfsStarted == false)
            {
                debugstdout("Mounting rom file system...");
                if (dfs_init(DFS_DEFAULT_LOCATION) == DFS_ESUCCESS)
                {
                    dfsStarted = true;
                    debugstdout("mounted.\nTrying to mount SD card...");
                    if (!init_sdfs("sd:/", -1))
                    {
                        debugstdout("Error opening SD, using rom:/ filesystem...\n");
                        strcpy(mountPoint, "rom:/");
                    }
                    else
                    {
                        debugstdout("SD card mounted\n");
                        strcpy(mountPoint, "sd:/smsPlus64");
                    }
                }
                else
                {
                    debugstdout("rom filesystem failed to start!\n");
                    debugstdout("Exit program\n");
                    isFatalError = true;
                    strcpy(ErrorMessage, "Error opening rom filesystem.");
                }
            }
#ifndef NDEBUG
            debugstdout("Press A button to continue\n");
            controller_scan();
            console_render();
            while (!get_keys_pressed().c[0].A)
            {
                wait_ms(10);
                controller_scan();
            }

            console_close();
#endif
#ifdef USEMENU
            display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
            info = menu(mountPoint, 0, ErrorMessage, isFatalError, reset);
            display_close();
#else

            info.rom = builtinrom;
            info.size = builtinrom_len;
            info.isGameGear = builtinrom_isgg;
            strcpy(info.title, GetBuiltinROMName());
#endif
        }
        /* Initialize display */
        display_init(RESOLUTION_256x240, DEPTH_16_BPP, FRAMEBUFFERS, GAMMA_NONE, FILTERS_RESAMPLE);
        checkcontrollers();
        // dump info
        debugf("Starting game:\n");
        debugf("- ROM: %s\n", info.title);
        debugf("- Size: %d\n", info.size);
        debugf("- Address: %p\n", info.rom);
        debugf("- isGameGear: %d\n", info.isGameGear);
        reset = false;
        debugf("Init audio\n");
        audio_init(44100, 4);
        load_rom(info.rom, info.size, info.isGameGear);
        // Initialize all systems and power on
        system_init(SMS_AUD_RATE);
        // load state if any
        // system_load_state();
        system_reset();
        debugf("Starting game\n");
        process();
        romName[0] = 0;
        display_close();
        debugf("Closing audio\n");
        audio_close();
#ifdef USEMENU
        debugf("Freeing rom\n");
        free(info.rom);
#endif
    }
    return 0;
}
