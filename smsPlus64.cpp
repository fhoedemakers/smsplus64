/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "shared.h"
#include "mytypes.h"
#include "libdragon.h"
#include "menu.h"

#include "FrensHelpers.h"

#define ERRORMESSAGESIZE 40
#define GAMESAVEDIR "/SAVES"

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

char *ErrorMessage;
bool isFatalError = false;

char romName[256];

static bool fps_enabled = true;

bool reset = false;

bool controller1IsInserted = false;
bool controller2IsInserted = false;

// The Sega Master system color palette converted to RGB444
// so it can be used with the DVI library.
// from https://segaretro.org/Palette

#if 0
WORD SMSPaletteRGB444[64] = {
    0x0, 0x500, 0xA00, 0xF00, 0x50, 0x550, 0xA50, 0xF50,
    0xA0, 0x5A0, 0xAA0, 0xFA0, 0xF0, 0x5F0, 0xAF0, 0xFF0,
    0x5, 0x505, 0xA05, 0xF05, 0x55, 0x555, 0xA55, 0xF55,
    0xA5, 0x5A5, 0xAA5, 0xFA5, 0xF5, 0x5F5, 0xAF5, 0xFF5,
    0xA, 0x50A, 0xA0A, 0xF0A, 0x5A, 0x55A, 0xA5A, 0xF5A,
    0xAA, 0x5AA, 0xAAA, 0xFAA, 0xFA, 0x5FA, 0xAFA, 0xFFA,
    0xF, 0x50F, 0xA0F, 0xF0F, 0x5F, 0x55F, 0xA5F, 0xF5F,
    0xAF, 0x5AF, 0xAAF, 0xFAF, 0xFF, 0x5FF, 0xAFF, 0xFFF};
#endif

bool initSDCard()
{
    printf("Mounting SDcard");
    return true;
}
int sampleIndex = 0;
void processaudio(int offset)
{
    int samples = 4; // 735/192 = 3.828125 192*4=768 735/3=245

    if (offset == (IS_GG ? 24 : 0))
    {
        sampleIndex = 0;
    }
    else
    {
        sampleIndex += samples;
        if (sampleIndex >= 735)
        {
            return;
        }
    }
    short *p1 = snd.buffer[0] + sampleIndex;
    short *p2 = snd.buffer[1] + sampleIndex;
#if 0
    while (samples)
    {
        auto &ring = dvi_->getAudioRingBuffer();
        auto n = std::min<int>(samples, ring.getWritableSize());
        auto n = samples;
        if (!n)
        {
            return;
        }
        auto p = ring.getWritePointer();
        int ct = n;
        while (ct--)
        {
            int l = (*p1++ << 16) + *p2++;
            // works also : int l = (*p1++ + *p2++) / 2;
            int r = l;
            // int l = *wave1++;
            *p++ = {static_cast<short>(l), static_cast<short>(r)};
        }
        ring.advanceWritePointer(n);
        samples -= n;
    }
#endif
}

#define RGB888_TO_RGB5551(r, g, b) (((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1)
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
    // Audio needs to be processed per scanline
    processaudio(line);
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
    char buffer[10];
    if (fps_enabled)
    {
        sprintf(buffer, "%d", framedisplay);
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
        }
        if (p1 & INPUT_START)
        {
            // Toggle frame rate display
            if (pushed & INPUT_BUTTON1)
            {
                fps_enabled = !fps_enabled;
                debugf("FPS: %s\n", fps_enabled ? "ON" : "OFF");
            }
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
        sms_frame(0);
        ProcessAfterFrameIsRendered(_dc, false);
        display_show(_dc);
    }
}
void frameratecalc(int ovfl)
{
    // debugf("FPS: %d\n", framecounter);
    framedisplay = framecounter;
    framecounter = 0;
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
/// @brief
/// Start emulator. Emulator does not run well in DEBUG mode, lots of red screen flicker. In order to keep it running fast enough, we need to run it in release mode or in
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

    ErrorMessage = errMSG;

    printf("Starting Master System Emulator\n");

    debug_init(DEBUG_FEATURE_LOG_ISVIEWER | DEBUG_FEATURE_LOG_USB);
    debugf("Starting SmsPlus 64, a Sega Master System emulator for the Nintendo 64\n");
    debugf("Built on %s %s using libdragon\n", __DATE__, __TIME__);
    debugf("Now running %s\n", GetBuiltinROMName());
    debugf("Trying to mount SD card...");
    if (!debug_init_sdfs("sd:/", -1))
    {
        debugf("Error opening SD, trying rom filesystem...");
        if (dfs_init(DFS_DEFAULT_LOCATION) != DFS_ESUCCESS)
        {
            debugf("rom filesystem failed to start!\n");
            debugf("Exit program\n");
            isFatalError = true;
            strcpy(ErrorMessage, "Error opening SD card and rom filesystem.");
        }
        else
        {
            debugf("rom filesystem mounted\n");
            strcpy(mountPoint, "rom://");
        }
    }
    else
    {
        debugf("SD card mounted\n");
        strcpy(mountPoint, "sd:/smsPlus64");
    }
    // register_VI_handler(vblCallback);
    controller_init();
    timer_init();
    new_timer(TIMER_TICKS(1000000), TF_CONTINUOUS, frameratecalc);

    while (true)
    {
        checkcontrollers();

#if 1
        display_init(RESOLUTION_640x480, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
        menu(mountPoint, 0, ErrorMessage, isFatalError, reset);
        display_close();
#endif
        /* Initialize display */
        display_init(RESOLUTION_256x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
        checkcontrollers();
        if ((isFatalError = !initSDCard()) == false)
        {
        }
        reset = false;
        debugf("Now playing: %s\n", romName);
        load_rom(fileSize, isGameGear);
        // Initialize all systems and power on
        system_init(SMS_AUD_RATE);
        // load state if any
        // system_load_state();
        system_reset();
        debugf("Starting game\n");
        process();
        romName[0] = 0;
        display_close();
    }
    return 0;
}
