#include <stdio.h>
#include <string.h>
#include "settings.h"

Settings settings;

/* Plain text rather than a packed struct: it survives a version change, and it
   can be read or fixed with a text editor on any PC. */
#define SETTINGS_FILE "settings.cfg"

void settings_setdefaults(void)
{
    settings.frameskip = -1; /* automatic */
    settings.sound = 1;
    settings.showFps = 1;
    settings.showProfiler = 0;
}

static void settings_path(char *out, size_t size, const char *mountPoint)
{
    snprintf(out, size, "%s/%s", mountPoint, SETTINGS_FILE);
}

static int clamp(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

bool settings_load(const char *mountPoint)
{
    char path[128];
    char line[64];
    FILE *file;

    settings_setdefaults();
    if (mountPoint == NULL || mountPoint[0] == '\0') return false;

    settings_path(path, sizeof(path), mountPoint);
    file = fopen(path, "r");
    if (file == NULL) return false;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        int value;
        if (sscanf(line, "frameskip=%d", &value) == 1)
            settings.frameskip = clamp(value, -1, 3);
        else if (sscanf(line, "sound=%d", &value) == 1)
            settings.sound = (value != 0);
        else if (sscanf(line, "fps=%d", &value) == 1)
            settings.showFps = (value != 0);
        else if (sscanf(line, "profiler=%d", &value) == 1)
            settings.showProfiler = (value != 0);
    }
    fclose(file);
    return true;
}

bool settings_save(const char *mountPoint)
{
    char path[128];
    FILE *file;

    if (mountPoint == NULL || mountPoint[0] == '\0') return false;

    settings_path(path, sizeof(path), mountPoint);
    file = fopen(path, "w");
    if (file == NULL) return false; /* read-only rom:/ filesystem, or no card */

    fprintf(file, "# smsPlus64 settings\n");
    fprintf(file, "# frameskip: -1 automatic, 0..3 frames skipped between drawn ones\n");
    fprintf(file, "frameskip=%d\n", settings.frameskip);
    fprintf(file, "sound=%d\n", settings.sound);
    fprintf(file, "fps=%d\n", settings.showFps);
    fprintf(file, "profiler=%d\n", settings.showProfiler);
    fclose(file);
    return true;
}
