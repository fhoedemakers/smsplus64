#include "libdragon.h"
#include "libdragonsprite.h"

sprite_t  *loaddragonsprite() {
    return sprite_load_buf(libdragon_sprite, libdragon_sprite_len);
}