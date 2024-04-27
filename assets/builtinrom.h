#pragma once

// Specify the name of the built-in ROM to include
#define BUILTINROM_SONIC_GG
extern unsigned char builtinrom[];
extern unsigned int builtinrom_len;
extern int builtinrom_isgg;
#ifdef __cplusplus
extern "C"
{
#endif

char *GetBuiltinROMName();
#ifdef __cplusplus
}
#endif