/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */


#include "shared.h"
#include "mytypes.h"
#include "menu.h"

#include "FrensHelpers.h"

#define ERRORMESSAGESIZE 40
#define GAMESAVEDIR "/SAVES"

char *ErrorMessage;
bool isFatalError = false;

char romName[256];

static bool fps_enabled = false;
static uint32_t start_tick_us = 0;
static uint32_t fps = 0;
static char fpsString[3] = "00";
#define fpsfgcolor 0;     // black
#define fpsbgcolor 0xFFF; // white

#define MARGINTOP 24
#define MARGINBOTTOM 4

#define FPSSTART (((MARGINTOP + 7) / 8) * 8)
#define FPSEND ((FPSSTART) + 8)

bool reset = false;

// The Sega Master system color palette converted to RGB444
// so it can be used with the DVI library.
// from https://segaretro.org/Palette
WORD SMSPaletteRGB444[64] = {
    0x0, 0x500, 0xA00, 0xF00, 0x50, 0x550, 0xA50, 0xF50,
    0xA0, 0x5A0, 0xAA0, 0xFA0, 0xF0, 0x5F0, 0xAF0, 0xFF0,
    0x5, 0x505, 0xA05, 0xF05, 0x55, 0x555, 0xA55, 0xF55,
    0xA5, 0x5A5, 0xAA5, 0xFA5, 0xF5, 0x5F5, 0xAF5, 0xFF5,
    0xA, 0x50A, 0xA0A, 0xF0A, 0x5A, 0x55A, 0xA5A, 0xF5A,
    0xAA, 0x5AA, 0xAAA, 0xFAA, 0xFA, 0x5FA, 0xAFA, 0xFFA,
    0xF, 0x50F, 0xA0F, 0xF0F, 0x5F, 0x55F, 0xA5F, 0xF5F,
    0xAF, 0x5AF, 0xAAF, 0xFAF, 0xFF, 0x5FF, 0xAFF, 0xFFF};



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


extern "C" void sms_palette_syncGG(int index)
{
    // The GG has a different palette format
    int r = ((vdp.cram[(index << 1) | 0] >> 1) & 7) << 5;
    int g = ((vdp.cram[(index << 1) | 0] >> 5) & 7) << 5;
    int b = ((vdp.cram[(index << 1) | 1] >> 1) & 7) << 5;
   
    int r444 = ((r << 4) + 127) >> 8; // equivalent to (r888 * 15 + 127) / 255
    int g444 = ((g << 4) + 127) >> 8; // equivalent to (g888 * 15 + 127) / 255
    int b444 = ((b << 4) + 127) >> 8;
    palette444[index] = (r444 << 8) | (g444 << 4) | b444;
}

extern "C" void sms_palette_sync(int index)
{
#if 1
    // Get SMS palette color index from CRAM
    WORD r = ((vdp.cram[index] >> 0) & 3);
    WORD g = ((vdp.cram[index] >> 2) & 3);
    WORD b = ((vdp.cram[index] >> 4) & 3);
    WORD tableIndex = b << 4 | g << 2 | r;
    // Get the RGB444 color from the SMS RGB444 palette
    palette444[index] = SMSPaletteRGB444[tableIndex];
#endif 

#if 0
    // Alternative color rendering below
    WORD r = ((vdp.cram[index] >> 0) & 3) << 6;
    WORD g = ((vdp.cram[index] >> 2) & 3) << 6;
    WORD b = ((vdp.cram[index] >> 4) & 3) << 6;
    int r444 = ((r << 4) + 127) >> 8; // equivalent to (r888 * 15 + 127) / 255
    int g444 = ((g << 4) + 127) >> 8; // equivalent to (g888 * 15 + 127) / 255
    int b444 = ((b << 4) + 127) >> 8;
    palette444[index] = (r444 << 8) | (g444 << 4) | b444;
#endif
    return;
}

extern "C" void sms_render_line(int line, const uint8_t *buffer)
{
    // DVI top margin has #MARGINTOP lines
    // DVI bottom margin has #MARGINBOTTOM lines
    // DVI usable screen estate: MARGINTOP .. (240 - #MARGINBOTTOM)
    // SMS has 192 lines
    // GG  has 144 lines
    // gg : Line starts at line 24
    // sms: Line starts at line 0
    // Emulator loops from scanline 0 to 261
    // Audio needs to be processed per scanline

    processaudio(line);
    // Adjust line number to center the emulator display
    line += MARGINTOP;
    // Only render lines that are visible on the screen, keeping into account top and bottom margins
    if (line < MARGINTOP || line >= 240 - MARGINBOTTOM)
        return;

    auto b = dvi_->getLineBuffer();
    uint16_t *sbuffer;
    if (buffer)
    {
        uint16_t *sbuffer = b->data() + 32 + (IS_GG ? 48 : 0);
        for (int i = screenCropX; i < BMP_WIDTH - screenCropX; i++)
        {
            sbuffer[i - screenCropX] = palette444[(buffer[i + BMP_X_OFFSET]) & 31];
        }
    }
    else
    {
        sbuffer = b->data() + 32;
        __builtin_memset(sbuffer, 0, 512);
    }
    // Display frame rate
    if (fps_enabled && line >= FPSSTART && line < FPSEND)
    {
        WORD *fpsBuffer = b->data() + 40;
        int rowInChar = line % 8;
        for (auto i = 0; i < 2; i++)
        {
            char firstFpsDigit = fpsString[i];
            char fontSlice = getcharslicefrom8x8font(firstFpsDigit, rowInChar);
            for (auto bit = 0; bit < 8; bit++)
            {
                if (fontSlice & 1)
                {
                    *fpsBuffer++ = fpsfgcolor;
                }
                else
                {
                    *fpsBuffer++ = fpsbgcolor;
                }
                fontSlice >>= 1;
            }
        }
    }
    // dvi_->setLineBuffer(line, b);
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
int ProcessAfterFrameIsRendered()
{

    // Frame rate calculation
    if (fps_enabled)
    {
        
    }
    return framecounter++;
}

static DWORD prevButtons[2]{};
static DWORD prevButtonssystem[2]{};
static DWORD prevOtherButtons[2]{};

#define OTHER_BUTTON1 (0b1)
#define OTHER_BUTTON2 (0b10)

#define NESPAD_SELECT (0x04)
#define NESPAD_START (0x08)
#define NESPAD_UP (0x10)
#define NESPAD_DOWN (0x20)
#define NESPAD_LEFT (0x40)
#define NESPAD_RIGHT (0x80)
#define NESPAD_A (0x01)
#define NESPAD_B (0x02)

static int rapidFireMask[2]{};
static int rapidFireCounter = 0;
void processinput(DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem, bool ignorepushed)
{
    // pwdPad1 and pwdPad2 are only used in menu and are only set on first push
    *pdwPad1 = *pdwPad2 = *pdwSystem = 0;

    int smssystem[2]{};
    unsigned long pushed, pushedsystem, pushedother;
    for (int i = 0; i < 2; i++)
    {
        int nespadbuttons = 0;
        auto &dst = (i == 0) ? *pdwPad1 : *pdwPad2;
        int smsbuttons = 0;
        int otherButtons = 0;
#if 0
        auto &gp = io::getCurrentGamePadState(i);
        int smsbuttons = (gp.buttons & io::GamePadState::Button::LEFT ? INPUT_LEFT : 0) |
                         (gp.buttons & io::GamePadState::Button::RIGHT ? INPUT_RIGHT : 0) |
                         (gp.buttons & io::GamePadState::Button::UP ? INPUT_UP : 0) |
                         (gp.buttons & io::GamePadState::Button::DOWN ? INPUT_DOWN : 0) |
                         (gp.buttons & io::GamePadState::Button::A ? INPUT_BUTTON1 : 0) |
                         (gp.buttons & io::GamePadState::Button::B ? INPUT_BUTTON2 : 0) | 0;
        int otherButtons = (gp.buttons & io::GamePadState::Button::X ? OTHER_BUTTON1 : 0) |
                           (gp.buttons & io::GamePadState::Button::Y ? OTHER_BUTTON2 : 0) | 0;
        smssystem[i] =
            (gp.buttons & io::GamePadState::Button::SELECT ? INPUT_PAUSE : 0) |
            (gp.buttons & io::GamePadState::Button::START ? INPUT_START : 0) |
            0;
#endif
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
                printf("Reset pressed\n");
            }
        }
        if (p1 & INPUT_START)
        {
            // Toggle frame rate display
            if (pushed & INPUT_BUTTON1)
            {
                fps_enabled = !fps_enabled;
                printf("FPS: %s\n", fps_enabled ? "ON" : "OFF");
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
                printf("Other 1\n");
            }
            if (pushedother & OTHER_BUTTON2)
            {
                printf("Other 2\n");
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
        processinput(&pdwPad1, &pdwPad2, &pdwSystem, false);
        sms_frame(0);
        ProcessAfterFrameIsRendered();
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
    
    size_t tmpSize;

    ErrorMessage = errMSG;
   
    printf("Starting Master System Emulator\n");


    if ((isFatalError = !initSDCard()) == false)
    {
        
    }
    
    while (true)
    {
#if 0
        menu(SMS_FILE_ADDR, ErrorMessage, isFatalError, reset);
#endif
        reset = false;
        printf("Now playing: %s\n", romName);
        load_rom(fileSize, isGameGear);
        // Initialize all systems and power on
        system_init(SMS_AUD_RATE);
        // load state if any
        // system_load_state();
        system_reset();
        printf("Starting game\n");
        process();
        romName[0] = 0;
    }

    return 0;
}
