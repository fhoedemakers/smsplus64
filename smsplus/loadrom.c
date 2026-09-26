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

/* The cartridge type is the caller's to decide.

   This used to second-guess it from the region code in the "TMR SEGA" header,
   on the grounds that a Game Gear rom saved as .sms would otherwise have its
   colours decoded one byte per entry instead of two. Region codes are not
   reliable enough for that: in a correctly named collection, 37 Game Gear roms
   carry a Master System code (Tesserae, James Pond II, Star Wars, Wolfchild
   and a row of betas) and 18 Master System roms carry a Game Gear one (Out Run
   Europa, Predator 2, Castle of Illusion). Each of those was emulated as the
   wrong console, with exactly the broken palette the override was meant to
   prevent. The file name is right far more often, and roms that arrive without
   one - injected by a flashcart menu - are looked up in injectedroms_table.h
   before their header is consulted.

*/
/* sizeGuessed: an SG-1000 rom handed over by a flashcart menu, which says
   nothing about its size. The caller passes the 48 KB it read from cartridge
   memory - the image followed by whatever was there before - and
   sg_memory_map() maps it accordingly. */
int load_rom(uint8_t *rom, int size, int cartType, bool sizeGuessed)
{
    uint8_t *start = (uint8_t *)rom;

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
    cart.size = size;
    cart.type = cartType;
    cart.size_guessed = (cartType == TYPE_SG) && sizeGuessed;

    /* Codemasters cartridges carry their own header at $7FE0, with a checksum
       at $7FE6 and its complement at $7FE8 that add up to $10000; Mesen2 detects
       them the same way. That finds every Codemasters rom in SMS Plus GX's CRC
       list and also their betas, hacks and translations, without a table. It
       reads the rom itself, so it works for roms a flashcart menu injected too.
       Anything else up to 48K has no mapper chip, as in Mesen2: the rom fills
       $0000-$BFFF and a write to $FFFC-$FFFF reaches work RAM only. The MSX
       conversions clear all of work RAM and then enter the game through the
       MSX header at $4002, which a Sega mapper would have switched to page 0.
       A flashcart menu passes no size, so injectedroms_table.h gives these roms
       theirs. */
    cart.mapper = (size > 0xC000) ? MAPPER_SEGA : MAPPER_NONE;
    if (cartType != TYPE_SG && size > 0x8000)
    {
        int checksum = start[0x7FE6] | (start[0x7FE7] << 8);
        int complement = start[0x7FE8] | (start[0x7FE9] << 8);
        if (checksum + complement == 0x10000)
            cart.mapper = MAPPER_CODIES;
    }

    /* A partial last page counts as a page, for every console. sms_mapper_w()
       reduces every bank number modulo this, and rounding down, as this did
       for everything but SG-1000, sent that page to page 0: the Game Gear
       betas of The Lion King and Batman & Robin are a few hundred bytes short
       of 512 KB and keep graphics and code in page 31, and the Italian
       translation of Alex Kidd in Miracle World showed nothing. SG-1000 images
       were always rounded up: 8 KB, 49136 and 65535 bytes all occur.

       The partial page is mapped whole, which is only safe because every
       loader allocates up to the next 16 KB boundary: loadRomFile() pads with
       $FF, and the flashcart path reads whole pages. Any other loader has to
       do the same.

       Never zero. Only an empty image rounds to no pages, and loadRomFile()
       turns those away, but zero here is not a wrong picture but a dead
       console: gcc compiles the modulo to a divu preceded by
       "teq v1,zero,0x7", so the first bank switch a game does raises a trap
       exception. A rom under 16K has one page as far as the mapper is
       concerned, and every bank number resolves to it, which is what the
       hardware does when the cartridge has no bank lines to drive. */
    cart.pages = (size + 0x3FFF) >> 14;
    if (cart.pages == 0)
        cart.pages = 1;

    return 1;
}

