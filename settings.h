#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* User settings, persisted to <mountPoint>/settings.cfg on the SD card so
       the in-game button combinations do not have to be remembered. */
    typedef struct
    {
        int frameskip;    /* -1 automatic, else 0..3 frames skipped between drawn ones */
        int sound;        /* 0 muted, 1 on */
        int showFps;      /* 0 hidden, 1 shown */
        int showProfiler; /* 0 hidden, 1 shown */
        int autostart;    /* 1 start a rom injected by the flashcart menu, 0 always show our menu */
    } Settings;

    extern Settings settings;

    void settings_setdefaults(void);

    /* Display name for a frameskip value, shared by the menu screen and the
       in-game overlay so they cannot describe the same setting differently. */
    const char *settings_frameskip_name(int frameskip);

    /* Reads the settings file, falling back to defaults. A missing file or an
       unrecognised line is not an error - it just leaves that default in
       place, so a partly hand-edited file still works. */
    bool settings_load(const char *mountPoint);

    /* Writes the settings file. Returns false when there is nowhere to write,
       which is the normal case when running from the built-in read-only
       filesystem rather than an SD card. */
    bool settings_save(const char *mountPoint);

    /* Copy settings into the running emulator and back out again. Implemented
       in smsPlus64.cpp, next to the state they refer to. Capturing first is
       what lets the settings screen show changes made in-game with the button
       combinations. */
    void settings_apply(void);
    void settings_capture(void);

#ifdef __cplusplus
}
#endif

#endif /* _SETTINGS_H_ */
