/*
    loadrom.c --
    File loading and management.
*/

#include "shared.h"

typedef struct
{
    uint32_t crc;
    int mapper;
    int display;
    int territory;
    char *name;
} rominfo_t;

// rominfo_t game_list[] = {
//     {0x17AB6883, MAPPER_NONE, DISPLAY_NTSC, TERRITORY_EXPORT, "FA Tetris (KR)"},
//     {0x61E8806F, MAPPER_NONE, DISPLAY_NTSC, TERRITORY_EXPORT, "Flash Point (KR)"},
//     {0x192949D5, MAPPER_KOREA2, DISPLAY_NTSC, TERRITORY_EXPORT, "Janggun-iuo Adeul (KR)"},
//     {0xA05258F5, MAPPER_KOREA, DISPLAY_NTSC, TERRITORY_EXPORT, "Won-Si-In (KR)"},
//     {0x83F0EEDE, MAPPER_KOREA, DISPLAY_NTSC, TERRITORY_EXPORT, "Street Master (KR)"},
//     {0x445525E2, MAPPER_KOREA, DISPLAY_NTSC, TERRITORY_EXPORT, "Penguin Adventure (KR)"},
//     {0x29822980, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, "Cosmic Spacehead"},
//     {0xB9664AE1, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, "Fantastic Dizzy"},
//     {0xA577CE46, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, "Micro Machines"},
//     {0x8813514B, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, "Excellent Dizzy (Proto)"},
//     {0xAA140C9C, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, "Excellent Dizzy (Proto - GG)"},
//     {-1, -1, -1, -1, NULL},
// };

// int load_rom(char *filename)
// {
//     int i;
//     int size;

//     if(cart.rom)
//     {
//         free(cart.rom);
//         cart.rom = NULL;
//     }

// 	FILE *fd = NULL;

// 	fd = fopen(filename, "rb");
// 	if(!fd) return 0;

// 	/* Seek to end of file, and get size */
// 	fseek(fd, 0, SEEK_END);
// 	size = ftell(fd);
// 	fseek(fd, 0, SEEK_SET);

// 	cart.rom = malloc(size);
// 	if(!cart.rom) return 0;
// 	fread(cart.rom, size, 1, fd);

// 	fclose(fd);

//     /* Don't load games smaller than 16K */
//     if(size < 0x4000) return 0;

//     /* Take care of image header, if present */
//     if((size / 512) & 1)
//     {
//         size -= 512;
//         memmove(cart.rom, cart.rom + 512, size);
//     }

//     cart.pages = (size / 0x4000);
//     cart.crc = crc32(0L, cart.rom, size);

//     uint8_t *temprom = malloc(size * sizeof(uint8_t));
//     memcpy(temprom, cart.rom, size);
//     sha1(cart.sha1, temprom, size);
//     free(temprom);

//     /* Assign default settings (US NTSC machine) */
//     cart.mapper     = MAPPER_SEGA;
//     sms.display     = DISPLAY_NTSC;
//     sms.territory   = TERRITORY_EXPORT;

//     /* Look up mapper in game list */
//     for(i = 0; game_list[i].name != NULL; i++)
//     {
//         if(cart.crc == game_list[i].crc)
//         {
//             cart.mapper     = game_list[i].mapper;
//             sms.display     = game_list[i].display;
//             sms.territory   = game_list[i].territory;
//         }
//     }

//     system_assign_device(PORT_A, DEVICE_PAD2B);
//     system_assign_device(PORT_B, DEVICE_PAD2B);

//     return 1;
// }
/* Writes to ROM space land here. cpu_writemem16() indexes a page with the low
   13 bits of the address, so this has to be a full 8K page - it used to alias
   the 256-byte line buffer, which meant such writes ran 8K past the end of it
   and scribbled over the scanline being rendered. */
static uint8_t dummy_page[0x2000];

/* Believe the cartridge over the file name.

   The caller derives isGameGear from the file extension, which is only a hint:
   a Game Gear ROM saved as .sms is loaded as a Master System cartridge, and its
   CRAM is then decoded one byte per colour instead of two. That reads as a
   completely broken palette rather than as a misdetected cartridge, so it is
   worth correcting when the ROM says so itself.

   ROMs from about 1990 on carry a "TMR SEGA" header at 0x7FF0 whose top nibble
   of the last byte is a region code: 3 and 4 are Master System, 5 to 7 are Game
   Gear. Anything else - including the many early ROMs with no header at all -
   leaves the caller's guess alone. */
static bool header_console_type(const uint8_t *rom, int size, bool *is_game_gear)
{
    int region;

    if (size < 0x8000) return false;
    if (__builtin_memcmp(rom + 0x7FF0, "TMR SEGA", 8) != 0) return false;

    region = (rom[0x7FFF] >> 4) & 0x0F;
    if (region >= 5 && region <= 7) { *is_game_gear = true;  return true; }
    if (region == 3 || region == 4) { *is_game_gear = false; return true; }
    return false;
}

int load_rom(uint8_t *rom, int size, bool isGameGear)
{
    uint8_t *start = (uint8_t *)rom;
    bool from_header = isGameGear;

    if (header_console_type(rom, size, &from_header) && from_header != isGameGear)
    {
        printf("ROM header says %s, overriding file extension\n",
               from_header ? "Game Gear" : "Master System");
        isGameGear = from_header;
    }

    sms.use_fm = 0;
    sms.country = TYPE_OVERSEAS;
    sms.sram = sram;
    sms.dummy = dummy_page;
    /* The renderer writes scanlines straight into sms_line_target, which flips
       between two buffers, so there is no single bitmap to point at any more.
       The geometry below is kept because it describes the emulated display. */
    bitmap.data = NULL;
    bitmap.width = BMP_WIDTH;
    bitmap.height = BMP_HEIGHT;
    bitmap.pitch = BMP_WIDTH;
    bitmap.depth = 8;
    cart.rom = start;

    /* Never zero. sms_mapper_w() reduces every bank number modulo this, and a
       rom under 16K rounds down to no pages at all - which is not a wrong
       picture but a dead console: gcc compiles the modulo to a divu preceded by
       "teq v1,zero,0x7", so the first bank switch a game does raises a trap
       exception. A rom that small has one page as far as the mapper is
       concerned, and every bank number resolves to it, which is what the
       hardware does when the cartridge has no bank lines to drive.

       This is a floor, not a rounding: rounding a partial page up would let the
       mapper select a page the buffer does not hold. */
    cart.pages = (size / 0x4000);
    if (cart.pages == 0)
        cart.pages = 1;

    cart.type = isGameGear ? TYPE_GG : TYPE_SMS;
    return 1;
}

