#ifndef INJECTEDROMS_H
#define INJECTEDROMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How much of the start of a rom is hashed to identify it */
#define INJECTEDROM_CRC_BYTES 32768

typedef struct
{
    uint32_t crc;    /* CRC-32 of the first `length` bytes */
    uint32_t size;   /* rom size in bytes, copier header excluded */
    uint16_t length; /* bytes hashed: INJECTEDROM_CRC_BYTES, or the size when smaller */
    uint8_t type;    /* TYPE_SMS, TYPE_GG or TYPE_SG */
} InjectedRom;

/* Identify a rom a flashcart menu injected that has no header to go by, or
   whose real size matters - a Master System or Game Gear rom of 48 KB or less
   has no mapper - from its first INJECTEDROM_CRC_BYTES bytes. NULL when it is
   not in the table. */
const InjectedRom *injected_rom_find(const uint8_t *start);

#ifdef __cplusplus
}
#endif

#endif /* INJECTEDROMS_H */
