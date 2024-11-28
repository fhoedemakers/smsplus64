// You must use your own roms to replace the built-in rom.
// Convert a rom file to a C array: xxd -i rom.nes > rom.c
// Modify rom.c as follows:
// 1. Change the array name to builtinrom
// 2. Change the array length to builtinrom_len
// 3. Add #define BUILTINROM_NAME and give the game a name.
//
// Modify this file to include the new rom based on the #define
// Modify builtinrom.h and change the #define of the rom you want to use.
// Only one rom can be used at a time.
#if USEMENU == 0
#include "builtinrom.h"
#ifdef BUILTINROM_SONIC_GG
#include "sonic_gg.c"
#endif
#ifdef BUILTINROM_ALADDIN_GG
#include "aladdin_gg.c"
#endif
#ifdef BUILTINROM_SONIC_SMS
#include "sonic_sms.c"
#endif

char *GetBuiltinROMName()
{
    return BUILTINROMNAME;
}
#endif