/*
 * ed64pro.h - EverDrive-64 PRO support for smsPlus64.
 *
 * The PRO is the one supported cartridge that libcart cannot drive. It has no
 * raw SD interface at all: the card and its FAT filesystem belong to an
 * on-cartridge MCU, so libdragon's block-device backed "sd:/" cannot be made
 * to work on it. What this does instead is talk to that MCU directly and
 * present the card as an ordinary libdragon filesystem, so the game browser,
 * the rom loader and the settings file carry on unchanged.
 *
 * It also answers the other half of the difference. The X-series and the
 * SummerCart64 menus copy the chosen Sega rom into cartridge memory for the
 * emulator to find; the PRO menu hands over the file's path instead.
 */
#ifndef ED64PRO_H
#define ED64PRO_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Probe for the cartridge. Safe on any hardware, including an emulator and an
 * iQue, and bounded in time. Call once, before anything else touches the
 * cartridge - in particular before libcart's cart_init(), which mistakes a
 * PRO for a Series X and then drives SD registers it does not have. */
bool     ed64pro_detect(void);

/* What detect() decided. False everywhere except on a PRO. */
bool     ed64pro_present(void);

/* The raw device id, for the log. The published documentation only gives the
 * top half (0xED64), so this is worth recording. */
uint32_t ed64pro_edid(void);

/* Mount the card under a libdragon prefix, e.g. "sd:/". */
bool     ed64pro_attach_fs(const char *prefix);

/* Whether large reads are being staged through cartridge memory. False means
 * the self-test failed and everything is going through the command fifo,
 * which works but is slow. Diagnostics only. */
bool     ed64pro_fastpath(void);

/* True if the named directory exists on the card. Path is relative to the
 * card root, e.g. "smsPlus64". */
bool     ed64pro_dir_exists(const char *path);

/* The file the Everdrive menu launched this app with, relative to the card
 * root. Returns false, and an empty string, when there is none. */
bool     ed64pro_app_file(char *out, size_t cap);

/* Forget that file, so a return to the game browser does not start it again.
 * Best effort: the in-memory guard is what actually makes this stick. */
void     ed64pro_clear_app_file(void);

#ifdef __cplusplus
}
#endif

#endif /* ED64PRO_H */
