
#include "shared.h"
#include "common.h"
#include "libdragon.h"
#include "menu.h"
#include "FrensHelpers.h"
#include "profile.h"
#include "settings.h"

#ifndef USEMENU
#include "builtinrom.h"
#endif
#include <stdarg.h>

#include "libcart/cart.h"
#define ERRORMESSAGESIZE 40
#define GAMESAVEDIR "/SAVES"

#define FRAMEBUFFERS 3

#ifndef USEMENU
#define USEMENU 1
#endif

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

// CI8 frames that the SMS+ renderer writes scanlines into directly (see
// render_line). The RDP reads one via DMA at end-of-frame and TLUTs it into
// the N64 framebuffer, off-loading the per-pixel palette lookup from the
// VR4300. Double buffered so the CPU can emulate and render the next frame
// while the RDP is still blitting this one. 16-byte aligned for RDP's DMA
// requirements; cache writeback runs before each blit.
#define CI8_FRAME_BYTES (SMS_WIDTH * SMS_HEIGHT)
static __attribute__((aligned(16))) uint8_t ci8_frame[2][CI8_FRAME_BYTES];
static int ci8_back = 0;
uint8_t *sms_line_target = ci8_frame[0];

// 32-entry palette replicated 8 times so any random 8-bit CI8 index maps
// to a valid RGBA5551 color in TMEM. Avoids per-pixel masking on the CPU.
// This is the live copy, updated by the emulator as it writes CRAM.
static __attribute__((aligned(16))) uint16_t tlut[256];

// Snapshot of the palette handed to the RDP, double buffered alongside the CI8
// frames. The RDP is still executing the queued LOAD_TLUT after
// rdpq_detach_show() returns, while the CPU has already moved on to emulating
// the next frame - and on a skipped frame nothing waits for it. Pointing the
// RDP straight at the live tlut let it read a palette that was being rewritten
// underneath it, which showed up as wrong colours on Game Gear.
static __attribute__((aligned(16))) uint16_t tlut_dma[2][256];

// Profiler accumulators, declared in profile.h and written from the emulator core.
uint32_t prof_acc[PROF_COUNT];
uint8_t prof_shown[PROF_COUNT];
static bool prof_enabled = false;

#define SOUNDISENABLED 1
int soundEnabled = SOUNDISENABLED;

char *ErrorMessage;
bool isFatalError = false;

char romName[256];

static bool fps_enabled = true;
timer_link_t *fpstimer = nullptr;
static bool hideFrameRate = false;

// Frameskip. FS_AUTO adapts to keep emulation running at 60 frames per second;
// 0 renders every frame, 1..3 skip that many frames between rendered ones.
#define FS_AUTO (-1)
#define FS_AUTO_MAX 3       // never skip more than this many frames in a row
#define FRAME_TICKS (TICKS_PER_SECOND / 60)

static int frameskip_mode = FS_AUTO;
static int skip_phase = 0;

// AUTO picks a skip level and holds it, rather than deciding frame by frame.
// Deciding per frame gives an irregular cadence - a mix of drawing every frame
// and every other frame - and uneven pacing reads as judder even when the
// average frame rate is higher. A steady 1-in-2 looks better than a jittery
// 1.7-in-2, so AUTO settles on a level and runs the same fixed cadence the
// manual modes do.
//
// The level is re-chosen a couple of times a second from the measured cost of
// a rendered frame and a skipped one, which is drift-free by construction: it
// times only work, never the waits that pace the emulator.
#define FS_TUNE_INTERVAL 30
static int auto_level = 0;
static uint32_t auto_render_ticks = 0;
static uint32_t auto_skip_ticks = 0;
static int auto_render_n = 0;
static int auto_skip_n = 0;
static int auto_tune_countdown = FS_TUNE_INTERVAL;

// Deadline for the busy-wait speed limiter, used only when sound is off and
// there is no audio queue to pace against.
static uint32_t frame_deadline = 0;

bool reset = false;

bool controller1IsInserted = false;
bool controller2IsInserted = false;

bool loadedFromFlashcartMenu = false;

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
static inline void tlut_mirror(int index)
{
    // Replicate one palette slot across all 8 TLUT copies so any 8-bit
    // CI8 index resolves to a valid color without CPU-side masking.
    uint16_t v = (uint16_t)palette444[index & 31];
    uint16_t *dst = tlut + (index & 31);
    for (int rep = 0; rep < 8; rep++, dst += 32) {
        *dst = v;
    }
}

extern "C" void sms_palette_syncGG(int index)
{
    // The GG has a different palette format
    int r = ((vdp.cram[(index << 1) | 0] >> 1) & 7) << 5;
    int g = ((vdp.cram[(index << 1) | 0] >> 5) & 7) << 5;
    int b = ((vdp.cram[(index << 1) | 1] >> 1) & 7) << 5;
    palette444[index] = RGB888_TO_RGB5551(r, g, b);
    tlut_mirror(index);
}

extern "C" void sms_palette_sync(int index)
{
    WORD r = ((vdp.cram[index] >> 0) & 3) << 6;
    WORD g = ((vdp.cram[index] >> 2) & 3) << 6;
    WORD b = ((vdp.cram[index] >> 4) & 3) << 6;
    palette444[index] = RGB888_TO_RGB5551(r, g, b);
    tlut_mirror(index);
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

int framecounter = 0;    // emulated frames this second
int drawncounter = 0;    // frames actually rendered and shown this second
int framedisplay = 0;    // emulated frames per second, last full second
int drawndisplay = 0;    // rendered frames per second, last full second
int totalfames = 0;

// Frameskip mode for the OSD. AUTO also reports the level it settled on.
static const char *frameskip_label()
{
    static char label[4];
    int n = 0;
    if (frameskip_mode == FS_AUTO)
    {
        label[n++] = 'A';
        label[n++] = (char)('0' + auto_level);
    }
    else
    {
        label[n++] = (char)('0' + frameskip_mode);
    }
    label[n] = '\0';
    return label;
}

int ProcessAfterFrameIsRendered(surface_t *display, bool fromMenu)
{
    char buffer[40];
    if (fps_enabled)
    {
        char sound = soundEnabled ? 'S' : 'M';
        // Same spot for both consoles. The emulated picture starts at row 24
        // (Master System) or row 48 (Game Gear), so rows 5..21 are clear
        // either way. Game Gear used to draw at (48, 24) instead, which was
        // not visible on hardware.
        int x = 10;
        int y = 5;
        graphics_set_color(CBLACK, CWHITE);
        if (fromMenu)
        {
            sprintf(buffer, "%c %04d", sound, framedisplay);
        }
        else
        {
            // console / sound / emulated fps / displayed fps / frameskip mode.
            // The console character is what the emulator actually decided the
            // cartridge is, which decides the CRAM format among other things -
            // a Master System ROM run as Game Gear has its palette read two
            // bytes per colour instead of one and comes out completely wrong.
            char console = IS_GG ? 'G' : 'S';
            sprintf(buffer, "%c%c %03d/%02d %s", console, sound,
                    framedisplay, drawndisplay, frameskip_label());
        }
        graphics_draw_text(display, x, y, buffer);

        if (prof_enabled && fromMenu == false)
        {
            // Percentage of wall clock spent in each phase over the last second.
            sprintf(buffer, "Z%02d R%02d B%02d A%02d I%02d",
                    prof_shown[PROF_Z80], prof_shown[PROF_RENDER],
                    prof_shown[PROF_BLIT], prof_shown[PROF_AUDIO],
                    prof_shown[PROF_IDLE]);
            graphics_draw_text(display, x, y + 9, buffer);
        }
    }
    drawncounter++;
    return totalfames++;
}

void frameratecalc(int ovfl)
{
    framedisplay = framecounter;
    drawndisplay = drawncounter;
    framecounter = drawncounter = 0;

    // Fold the tick accumulators into whole percent of the elapsed second.
    for (int i = 0; i < PROF_COUNT; i++)
    {
        uint32_t pct = prof_acc[i] / (TICKS_PER_SECOND / 100);
        prof_shown[i] = (uint8_t)(pct > 99 ? 99 : pct);
        prof_acc[i] = 0;
    }
}
void enableordisableTimer()
{
    if (fps_enabled)
    {
        // Guard against a second timer: this is called again whenever the
        // setting changes, and new_timer() would otherwise leak the old one and
        // halve the reported frame rate by counting twice a second.
        if (fpstimer == nullptr)
        {
            fpstimer = new_timer(TIMER_TICKS(1000000), TF_CONTINUOUS, frameratecalc);
        }
        framecounter = framedisplay = drawncounter = drawndisplay = 0;
        // frameratecalc() is what drains these; start from a clean slate so the
        // first second is not inflated by whatever piled up while it was off.
        for (int i = 0; i < PROF_COUNT; i++)
        {
            prof_acc[i] = 0;
        }
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
// Settings <-> running emulator. The settings screen captures first so it
// shows whatever the in-game button combinations last set, then applies on the
// way out, so both ways of changing a setting agree.
extern "C" void settings_apply(void)
{
    frameskip_mode = settings.frameskip;
    soundEnabled = settings.sound;
    snd.enabled = soundEnabled;
    prof_enabled = (settings.showProfiler != 0);

    if (fps_enabled && !settings.showFps)
    {
        // Turning the overlay off leaves its pixels on every framebuffer.
        hideFrameRate = true;
    }
    fps_enabled = (settings.showFps != 0);
    enableordisableTimer();

    // Start the frameskip decision from scratch so a changed level takes effect
    // immediately instead of finishing the previous cadence.
    skip_phase = 0;
    auto_level = 0;
    auto_render_ticks = auto_skip_ticks = 0;
    auto_render_n = auto_skip_n = 0;
    auto_tune_countdown = FS_TUNE_INTERVAL;
}

extern "C" void settings_capture(void)
{
    settings.frameskip = frameskip_mode;
    settings.sound = soundEnabled ? 1 : 0;
    settings.showFps = fps_enabled ? 1 : 0;
    settings.showProfiler = prof_enabled ? 1 : 0;
}

#define OTHER_BUTTON1 (0b1)
#define OTHER_BUTTON2 (0b10)
#define OTHER_BUTTON3 (0b100)

// Where saved settings live. Kept at file scope so the in-game overlay can
// write them too, not just the menu.
char mountPoint[24] = "";

// Set by the Z + C-Right combination, acted on by the main loop.
static bool settingsRequested = false;

// Why the browser is looking where it is. Shown when it has nothing to list, so
// a failed SD card is distinguishable from an empty folder.
char sdStatus[48] = "";

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
                           (CU_BUTTON(gp) ? OTHER_BUTTON2 : 0) |
                           (CR_BUTTON(gp) ? OTHER_BUTTON3 : 0) | 0;
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
                if (!loadedFromFlashcartMenu)
                {
                    reset = true;
                    debugf("Reset pressed\n");
                }
                else
                {
                    debugf("Reset ignored\n");
                }
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
            // Cycle frameskip: AUTO -> off -> 1 -> 2 -> 3 -> AUTO
            if (pushedother & OTHER_BUTTON1)
            {
                if (frameskip_mode == FS_AUTO)
                    frameskip_mode = 0;
                else if (frameskip_mode >= FS_AUTO_MAX)
                    frameskip_mode = FS_AUTO;
                else
                    frameskip_mode++;
                skip_phase = 0;
                auto_render_ticks = auto_skip_ticks = 0;
                auto_render_n = auto_skip_n = 0;
                auto_tune_countdown = FS_TUNE_INTERVAL;
                frame_deadline = TICKS_READ();
                debugf("Frameskip: %s\n", frameskip_label());
            }
            // Toggle the phase profiler overlay
            if (pushedother & OTHER_BUTTON2)
            {
                prof_enabled = !prof_enabled;
                debugf("Profiler: %s\n", prof_enabled ? "ON" : "OFF");
            }
            // Open the settings overlay. Handled by the main loop rather than
            // here, so it does not run from inside input processing.
            if (pushedother & OTHER_BUTTON3)
            {
                settingsRequested = true;
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
    }
    input.system = *pdwSystem = smssystem[0] | smssystem[1];
    // return only on first push
    if (pushedsystem)
    {
        *pdwSystem = smssystem[0] | smssystem[1];
    }
}

// Frames skipped between drawn ones, for whichever mode is active.
static int frameskip_level()
{
    return (frameskip_mode == FS_AUTO) ? auto_level : frameskip_mode;
}

// Re-choose the AUTO level from what rendering and skipping actually cost.
// Called once per frame; does its work every FS_TUNE_INTERVAL frames.
static void auto_frameskip_tune()
{
    if (--auto_tune_countdown > 0)
        return;
    auto_tune_countdown = FS_TUNE_INTERVAL;

    if (auto_render_n == 0)
        return;

    uint32_t render_cost = auto_render_ticks / (uint32_t)auto_render_n;
    // At level 0 there are no skipped frames to measure. Treating them as free
    // makes the estimate optimistic, so AUTO may step up one level too eagerly
    // - but that immediately produces real samples and the next pass corrects
    // it, so it settles either way.
    uint32_t skip_cost = auto_skip_n ? (auto_skip_ticks / (uint32_t)auto_skip_n) : 0;

    // Cheapest cadence whose predicted average frame cost fits in the budget.
    // Stepping up halves the frames drawn, so it needs to buy more than a
    // rounding error: a game running at 58fps is 3% short of the budget and is
    // far better left alone than cut to 29.
    uint32_t step_up_limit = FRAME_TICKS + (FRAME_TICKS / 16);
    int level = 0;
    while (level < FS_AUTO_MAX)
    {
        uint32_t avg = (render_cost + (uint32_t)level * skip_cost) / (uint32_t)(level + 1);
        if (avg <= step_up_limit)
            break;
        level++;
    }

    // Stepping down needs clear headroom, otherwise a level sitting right on
    // the budget oscillates against the one above it every tuning pass, which
    // is exactly the judder this is meant to avoid.
    if (level < auto_level)
    {
        uint32_t avg = (render_cost + (uint32_t)level * skip_cost) / (uint32_t)(level + 1);
        if (avg > FRAME_TICKS - (FRAME_TICKS / 16))
            level = auto_level;
    }

    auto_level = level;
    auto_render_ticks = auto_skip_ticks = 0;
    auto_render_n = auto_skip_n = 0;
}

// In-game settings overlay. The menu's settings screen uses a 38 column text
// grid that does not fit the 256 pixel wide framebuffer a game runs in, so this
// is a compact version of the same four settings. Emulation is paused while it
// is open.
static void inGameSettings()
{
    DWORD pad1, pad2, sys;
    char line[40];
    const int rows = 5;
    int row = 0;
    bool changed = false;

    // The RDP may still be blitting the last frame into a buffer we are about
    // to draw over.
    rspq_wait();
    settings_capture();

    while (true)
    {
        surface_t *dc = display_get();
        graphics_fill_screen(dc, CBLACK);

        graphics_set_color(CWHITE, CBLACK);
        graphics_draw_text(dc, 24, 32, "SETTINGS");

        for (int i = 0; i < rows; i++)
        {
            switch (i)
            {
            case 0:
                sprintf(line, "Frameskip  %s", settings_frameskip_name(settings.frameskip));
                break;
            case 1:
                sprintf(line, "Sound      %s", settings.sound ? "On" : "Off");
                break;
            case 2:
                sprintf(line, "Frame rate %s", settings.showFps ? "Shown" : "Hidden");
                break;
            case 3:
                sprintf(line, "Profiler   %s", settings.showProfiler ? "Shown" : "Hidden");
                break;
            default:
                sprintf(line, "Autostart  %s", settings.autostart ? "On" : "Off");
                break;
            }
            // Selected row is inverted.
            graphics_set_color(i == row ? CBLACK : CWHITE, i == row ? CWHITE : CBLACK);
            graphics_draw_text(dc, 24, 56 + i * 12, line);
        }

        graphics_set_color(CWHITE, CBLACK);
        graphics_draw_text(dc, 24, 124, "Up/Down     select");
        graphics_draw_text(dc, 24, 136, "Left/Right  change");
        graphics_draw_text(dc, 24, 148, "B           close");

        display_show(dc);
        processinput(&pad1, &pad2, &sys, false);

        if (pad1 & INPUT_UP)
        {
            row = (row + rows - 1) % rows;
        }
        else if (pad1 & INPUT_DOWN)
        {
            row = (row + 1) % rows;
        }
        else if (pad1 & INPUT_BUTTON2)
        {
            break;
        }
        else if (pad1 & (INPUT_LEFT | INPUT_RIGHT | INPUT_BUTTON1))
        {
            int direction = (pad1 & INPUT_LEFT) ? -1 : 1;
            switch (row)
            {
            case 0:
                settings.frameskip += direction;
                if (settings.frameskip > 3) settings.frameskip = -1;
                if (settings.frameskip < -1) settings.frameskip = 3;
                break;
            case 1: settings.sound = !settings.sound; break;
            case 2: settings.showFps = !settings.showFps; break;
            case 3: settings.showProfiler = !settings.showProfiler; break;
            default: settings.autostart = !settings.autostart; break;
            }
            changed = true;
        }
    }

    settings_apply();
    if (changed && !settings_save(mountPoint))
    {
        debugf("Could not save settings to '%s'\n", mountPoint);
    }
    // This drew over framebuffers the emulator only partly repaints, so have
    // the main loop clear them before resuming.
    hideFrameRate = true;
}

void process(void)
{
    DWORD pdwPad1, pdwPad2, pdwSystem; // have only meaning in menu

    // Start both CI8 frames blank so nothing from a previous game shows through.
    __builtin_memset(ci8_frame, 0, sizeof(ci8_frame));
    ci8_back = 0;
    sms_line_target = ci8_frame[0];

    // Clear every framebuffer once. The RDP only ever draws the emulated
    // picture - rows 24..215 for the Master System, and a 160x144 window for
    // the Game Gear - so the border around it keeps whatever the menu or the
    // previous game left in memory.
    for (int i = 0; i < FRAMEBUFFERS; i++)
    {
        surface_t *clear = display_get();
        graphics_fill_screen(clear, CBLACK);
        display_show(clear);
    }
    // Start by drawing every frame and let the tuner settle from there, so a
    // game that can keep up never skips.
    skip_phase = 0;
    auto_level = 0;
    auto_render_ticks = auto_skip_ticks = 0;
    auto_render_n = auto_skip_n = 0;
    auto_tune_countdown = FS_TUNE_INTERVAL;
    frame_deadline = TICKS_READ();

    while (reset == false)
    {
        processinput(&pdwPad1, &pdwPad2, &pdwSystem, false);

        if (settingsRequested)
        {
            settingsRequested = false;
            inGameSettings();
            frame_deadline = TICKS_READ(); // do not try to catch up on the pause
            continue;
        }

        bool render_frame = (skip_phase == 0);

        if (render_frame)
        {
            PROF_BEGIN(PROF_IDLE);

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

            // Make sure the RDP is done before we start overwriting a CI8
            // buffer it might still be reading. In the steady state it is
            // reading the *other* buffer, so this costs nothing; it only bites
            // if a blit somehow ran over a whole frame.
            //
            // This is a full sync because there is no cheap way to wait on just
            // the RDP's texture DMA: rspq syncpoints track the RSP, which runs
            // ahead of the RDP. Watch PROF_IDLE - if this turns out to cost
            // real time, the fix is a per-buffer rdpq_detach_cb() completion
            // flag rather than a global wait.
            rspq_wait();

            PROF_END(PROF_IDLE);
        }

        // Everything from here to the end of the blit is the frame's real
        // work; the waits around it are pacing, not cost.
        uint32_t work_start = TICKS_READ();

        sms_frame(render_frame ? 0 : 1);

        if (render_frame)
        {
            PROF_BEGIN(PROF_BLIT);

            uint8_t *frame = ci8_frame[ci8_back];

            // Draw the overlay before handing the framebuffer to the RDP, so
            // CPU and RDP never touch it at the same time.
            ProcessAfterFrameIsRendered(_dc, false);

            // RDP TLUTs the CI8 emulator output into the RGBA5551 framebuffer.
            // CPU writes to the frame went through the d-cache; flush before
            // the RDP reads via DMA.
            data_cache_hit_writeback(frame, CI8_FRAME_BYTES);

            // Snapshot the palette for the RDP. It has to be a copy, not the
            // live tlut, because the emulator keeps writing CRAM while the RDP
            // is still reading. rdpq_tex_upload_tlut() issues a LOAD_TLUT that
            // DMAs straight out of RDRAM and does no writeback of its own, so
            // the snapshot has to be flushed by hand. Only 512 bytes.
            uint16_t *palette = tlut_dma[ci8_back];
            __builtin_memcpy(palette, tlut, sizeof(tlut));
            data_cache_hit_writeback(palette, sizeof(tlut));

            surface_t ci8_surface = surface_make_linear(frame, FMT_CI8, SMS_WIDTH, SMS_HEIGHT);
            rdpq_attach(_dc, NULL);
            rdpq_set_mode_copy(false);
            rdpq_mode_tlut(TLUT_RGBA16);
            rdpq_tex_upload_tlut(palette, 0, 256);

            if (IS_GG)
            {
                // GG visible window: cols 48..207, rows 24..167, blitted to fb at
                // (48, 48) so the 160x144 image is centered horizontally and
                // vertically inside the 256x240 framebuffer.
                rdpq_blitparms_t parms = {};
                parms.s0 = 48;
                parms.t0 = 24;
                parms.width = 160;
                parms.height = 144;
                rdpq_tex_blit(&ci8_surface, 48, 48, &parms);
            }
            else
            {
                // SMS: full 256x192 image at fb (0, 24).
                rdpq_tex_blit(&ci8_surface, 0, 24, NULL);
            }

            // Schedules display_show() to happen once the RDP is done, instead
            // of parking the VR4300 on rspq_wait() for the whole blit.
            rdpq_detach_show();

            // Flip: the next frame renders into the other buffer while the RDP
            // reads this one.
            ci8_back ^= 1;
            sms_line_target = ci8_frame[ci8_back];

            PROF_END(PROF_BLIT);
        }

        // How much of the frame budget this frame actually consumed.
        int32_t work = TICKS_SINCE(work_start);

        framecounter++;

        /* Push one frame of stereo samples. Blocking, so the audio queue
           draining at 44.1kHz is what paces the emulator to 60 frames per
           second - skipped frames never reach display_get(), so the
           framebuffer backpressure alone no longer regulates speed. */
        if (snd.enabled && snd.buffer)
        {
            PROF_BEGIN(PROF_IDLE);
            audio_push(snd.buffer, snd.bufsize, true);
            PROF_END(PROF_IDLE);
        }
        else
        {
            // No audio queue to pace against; hold the frame here instead.
            PROF_BEGIN(PROF_IDLE);
            frame_deadline += FRAME_TICKS;
            if (TICKS_DISTANCE(frame_deadline, TICKS_READ()) > (int32_t)(FRAME_TICKS * 4))
            {
                // Fell a long way behind (menu, ROM load, reset): resync rather
                // than trying to catch up on a backlog we cannot make up.
                frame_deadline = TICKS_READ();
            }
            while (TICKS_BEFORE(TICKS_READ(), frame_deadline))
            {
            }
            PROF_END(PROF_IDLE);
        }

        // Feed the AUTO tuner what this frame actually cost.
        if (render_frame)
        {
            auto_render_ticks += (uint32_t)work;
            auto_render_n++;
        }
        else
        {
            auto_skip_ticks += (uint32_t)work;
            auto_skip_n++;
        }
        if (frameskip_mode == FS_AUTO)
        {
            auto_frameskip_tune();
        }

        skip_phase = (skip_phase + 1) % (frameskip_level() + 1);
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

uint32_t GetRomAddress()
{
    switch (cart_type)
    {
    case CART_CI:
    case CART_SC:
        return 0x10200000;
    case CART_EDX:
    case CART_ED:
        return 0xB0200000;
    default:
        return 0; // This is an emulator or something else, load the built in ROM.
    }
}
// create a wrapper for debugf to stdout
void debugstdout(const char *fmt, ...)
{
#ifndef NDEBUG
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    // vfprintf(stderr, fmt, args);
    va_end(args);
#endif
}
// Checks if a rom is injected by the everdrive/N64Flashcart menu
// The rom is injected at GetRomAddress() and has a Sega header
// The Sega header is at 0x7FF0
// Some roms have a 512 byte header, so we also check at 0x7FF0 + 512
bool IsRomInjected(RomInfo *info, bool withOffset)
{
    bool rval = false;
    int offset = withOffset ? 512 : 0;
    debugstdout("Searching for Sega header at %x\n", GetRomAddress() + 0x7FF0 + offset);
    dma_read_async(&header, GetRomAddress() + 0x7FF0 + offset, sizeof(header));
    dma_wait();
    if (strncmp(header.signature, "TMR SEGA", 8) == 0)
    {
        debugstdout("  --->Sega header found\n");
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
static const char *format_cart_type()
{
    switch (cart_type)
    {
    case CART_CI:
        return "64drive";

    case CART_EDX:
        return "Series X EverDrive-64";

    case CART_ED:
        return "Series V EverDrive-64";

    case CART_SC:
        return "SummerCart64";

    default: // Probably emulator
        return "Emulator?";
    }
}

// Blank the "TMR SEGA" signature the flashcart menu leaves in cartridge space
// when it injects a rom. It survives a reset, so without this the same game is
// detected and started again on every boot and the menu is unreachable except
// by holding Z. Called as soon as the rom has been copied to RAM, so the next
// boot comes up in the menu; picking a game from the flashcart menu writes a
// fresh header and works as before.
static void killInjectedRomHeader()
{
    __builtin_memset(&header, 0, sizeof(header));
    debugf("Clearing injected rom header\n");
    dma_write_raw_async(&header, GetRomAddress() + 0x7FF0, sizeof(header));
    dma_wait();
    dma_write_raw_async(&header, GetRomAddress() + 0x7FF0 + 512, sizeof(header));
    dma_wait();
}

// Mount the rom filesystem and the SD card, then read the saved settings from
// beside the roms. Runs before the injected-rom check so that settings are in
// effect however the emulator was started, and so the autostart setting can be
// honoured at all.
static void mountFilesystemsAndLoadSettings(bool *dfsStarted, char *mountPoint)
{
    if (*dfsStarted)
    {
        return;
    }
    debugstdout("Mounting rom file system...");
    if (dfs_init(DFS_DEFAULT_LOCATION) != DFS_ESUCCESS)
    {
        debugstdout("rom filesystem failed to start!\n");
        isFatalError = true;
        strcpy(ErrorMessage, "Error opening rom filesystem.");
        return;
    }
    *dfsStarted = true;
    debugstdout("mounted.\nTrying to mount SD card...");
    // -1 asks for the first usable partition. Some cards only come up when a
    // partition is named explicitly, so fall back to trying the first two
    // before giving up.
    bool sdMounted = init_sdfs("sd:/", -1);
    for (int part = 0; !sdMounted && part < 2; part++)
    {
        debugstdout("Retrying SD with partition %d\n", part);
        sdMounted = init_sdfs("sd:/", part);
    }
    if (!sdMounted)
    {
        debugstdout("Error opening SD, using rom:/ filesystem...\n");
        strcpy(mountPoint, "rom:/");
        // The browser shows this when it has nothing to list. Without it, a
        // failed card is indistinguishable from an empty folder.
        snprintf(sdStatus, sizeof(sdStatus), "SD not mounted. Cart: %s", format_cart_type());
    }
    else
    {
        debugstdout("SD card mounted\n");
        strcpy(mountPoint, "sd:/smsPlus64");
        snprintf(sdStatus, sizeof(sdStatus), "SD ok. Cart: %s", format_cart_type());
    }
    // Saved settings live beside the roms. Defaults are used when there is no
    // file yet, or when running from the read-only rom:/ filesystem.
    if (settings_load(mountPoint))
    {
        debugstdout("Loaded settings from %s\n", mountPoint);
    }
    else
    {
        debugstdout("No saved settings, using defaults\n");
    }
    settings_apply();
}

int main()
{

    char errMSG[ERRORMESSAGESIZE];
    errMSG[0] = romName[0] = 0;
    int fileSize = 0;
    bool isGameGear = false;
    size_t tmpSize;

    bool dfsStarted = false;
    ErrorMessage = errMSG;
    RomInfo info;

    debug_init(DEBUG_FEATURE_LOG_ISVIEWER | DEBUG_FEATURE_LOG_USB);
    debugf("Starting SMSPlus64, a Sega Master System emulator for the Nintendo 64 - https://github.com/fhoedemakers/smsplus64\n");
    debugf("Built on %s %s using libdragon - https://github.com/DragonMinded/libdragon\n", __DATE__, __TIME__);

    cart_init();
    controller_init();
    timer_init();
    rdpq_init();
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

        // #ifndef NDEBUG
        //  console must be initialized for the user to press Z to skip to menu
        console_init();
        console_set_render_mode(RENDER_MANUAL);
        console_clear();
// #endif
#ifndef NDEBUG
#if 0
        // allocate MB of memory for the rom
        int size = 2 * 1024 * 1024;
        uint8_t *rom = (uint8_t *)malloc(size);
        if (rom == nullptr)
        {
            debugstdout("Error allocating memory for rom\n");
            strcpy(ErrorMessage, "Error allocating memory for rom");
            console_render();
            break;
        }
        // read the rom from the filesystem
        debugstdout("Searching for rom at range GetRomAddress() - %x\n", GetRomAddress() + size);
        dma_read_async(rom, GetRomAddress(), size);
        dma_wait();
        debugstdout("Rom read\n");
        // check if the rom is a game gear rom);
        int offs = 0; 
        if ((offs =find_sequence(rom, size, "TMR SEGA")) != -1)
        {
            debugstdout("Rom found at offset %x, (%x) \n", offs, GetRomAddress() + offs);
        }
        else
        {
            debugstdout("Rom not found\n");
        }
        free(rom);
#endif
#endif
        // Wait for Z button to be pressed until counter is reached
        bool zPressed = false;
        debugstdout("Press Z button to skip to menu\n");
        int counter = 0;
        while (counter < 10)
        {
            zPressed = get_keys_pressed().c[0].Z;
            if (zPressed)
            {
                break;
            }
            wait_ms(10);
            controller_scan();
            counter++;
        }
        // Check whether rom is started via Everdrive/N64Flashcartmenu
        // Those roms are injected at GetRomAddress() and have a Sega header
        if (!zPressed)
        {
            debugstdout("Detecting flash cart type\n");
            if (cart_type != CART_NULL)
            {
                debugstdout("Cart type: %s\n", format_cart_type());
                debugstdout("Cart size: %d\n", cart_size);
                debugstdout("Cart ROM address: %x\n", GetRomAddress());
                debugstdout("Check if game is started via Everdrive/FlashCartMenu\n");
            } else {
                debugstdout("No flash cart detected\n");
            }
        }
        else
        {
            debugstdout("Z button pressed, skipping to menu\n");
        }
        loadedFromFlashcartMenu = false;
        if (!zPressed && cart_type != CART_NULL && (loadedFromFlashcartMenu = IsRomInjected(&info, false)) == false)
        {
            if ((loadedFromFlashcartMenu = IsRomInjected(&info, true)) == true)
            {
                offset = 512;
            }
            else
            {
                debugstdout("No Sega header found\n");
            }
        }

        // Mount early only when there is an injected rom to make a decision
        // about, because autostart is a saved setting. Otherwise leave mounting
        // where it has always been, in the menu branch below: doing it earlier
        // stopped the SD card mounting on an EverDrive X7, though a SummerCart64
        // was unaffected.
        if (loadedFromFlashcartMenu)
        {
            mountFilesystemsAndLoadSettings(&dfsStarted, mountPoint);
            if (!settings.autostart)
            {
                debugstdout("Autostart disabled, going to the menu\n");
                loadedFromFlashcartMenu = false;
            }
        }

        if (loadedFromFlashcartMenu)
        {
            debugstdout("Allocating memory for rom\n");
            info.rom = (uint8_t *)malloc(info.size);
            debugstdout("Reading rom at %x\n", GetRomAddress() + offset);
            dma_read_async(info.rom, GetRomAddress() + offset, info.size);
            debugstdout("Waiting for dma\n");
            dma_wait();
            strcpy(info.title, "Everdrive/Flashcart");
            // The rom is safely in RAM now, so drop the header that got us here
            // and the next boot will come up in the menu instead of replaying
            // this game.
            killInjectedRomHeader();
#ifndef NDEBUG
            debugstdout("Press A button to continue\n");
            controller_scan();
            console_render();
            while (!get_keys_pressed().c[0].A)
            {
                wait_ms(10);
                controller_scan();
            }
#endif
            console_close();
        }
        else
        {
            debugstdout("Will start menu\n");
            mountFilesystemsAndLoadSettings(&dfsStarted, mountPoint);
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
#if USEMENU == 1

            killInjectedRomHeader();
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
