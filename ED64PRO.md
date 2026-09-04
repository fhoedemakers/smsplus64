# EverDrive-64 PRO support

Notes on how smsPlus64 drives the [EverDrive-64 PRO](https://krikzz.com/our-products/cartridges/everdrive-64-pro.html),
and why it does not go through libcart the way the other flashcarts do.
Resolves [#13](https://github.com/fhoedemakers/smsplus64/issues/13).

## The two symptoms

1. Started on its own from the Everdrive menu, the game browser was empty and reported
   `SD not mounted. Cart: Series X EverDrive-64`.
2. Copied to `/ED64/edapp/sms/` and `/ED64/edapp/gg/` so the Everdrive menu could launch it
   for a `.sms` or `.gg` file, the chosen game did not start and the emulator fell back to
   its own browser, which was broken for reason 1.

Both follow from the same thing: the PRO is a different machine from every cartridge the
emulator supported, and it was being taken for one of them.

## Why the card was not found

`main()` called libcart's `cart_init()`, which probes 64drive, then the X-series, then the
V3, then the SummerCart64. Its X-series probe accepts any cartridge where

```c
io_read(0x1F800014) >> 16 == 0xED64
```

On the PRO that address is the `EDID` register of an entirely different register block, and
it answers `0xED64xxxx` as well. libcart therefore reported `CART_EDX`, after which
`edx_card_init()` bit-banged SD registers at `0x1F8080xx` that this cartridge does not have,
`f_mount` failed, and `debug_init_sdfs()` returned false.

libdragon's own `usb.c` avoids this by comparing the whole 32-bit word against
`EDX_VERSION 0xED640013`. libcart compares only the top half.

The mistake could not simply be corrected inside libcart, because **the PRO exposes no raw
SD sector interface at all**. Its card and FAT filesystem belong to a controller on the
cartridge, which serves whole files rather than sectors. libdragon's `sd:/` stack is built on
a block device (`cart_card_rd_dram`), and `fat_disks[]` in libdragon's `debug.c` is `static`,
so a replacement block device cannot be substituted either.

## Why a game picked in the Everdrive menu did not start

For the X-series and the SummerCart64 the menu copies the chosen Sega rom into cartridge
memory at cartridge base + 2 MB, and `IsRomInjected()` looks for the `TMR SEGA` signature
there.

The PRO copies nothing. It remembers the path of the file the user picked, and the
application asks for it with `ed_dev_get_path(path, DEV_PATH_APPF)` — documented in krikzz's
sources as "target file for emulator (if any)". The file name is the whole handover.

## What was implemented

The PRO is detected before anything else touches the cartridge, its controller is driven
directly, and its card is presented to the rest of the emulator as an ordinary libdragon
filesystem under `sd:/`. The game browser, the rom loader and the settings file are unchanged.

### Files

| File | Contents |
| --- | --- |
| `ed64pro_drv.c` / `.h` | The cartridge command protocol. A trimmed port of `edio/everdrive.c` from [ed64-pro-pub](https://github.com/krikzz/ed64-pro-pub) (MIT, © krikzz). |
| `ed64pro.c` / `.h` | Detection, the `sd:/` filesystem, the cartridge-memory read window, and the launched-file path. |

### Detection

Three gates, in order, no writes until the first two pass, and every wait bounded so an
unfamiliar cartridge fails the probe rather than locking the console:

1. `sys_bbplayer()` first — reading PI addresses outside the rom bus-errors on an iQue.
2. `EDID >> 16 == 0xED64`, then `SYSSTAT`: it carries a constant nibble (`(v & 0xF0) == 0xA0`)
   and a strobe bit that inverts on every read. Requiring both, over up to 64 reads, is what
   separates a PRO from a Series X, which has nothing mapped at that address.
3. A `CMD_STATUS` handshake that must answer `0x5A`, with a deliberately short 500 ms
   deadline because this also decides whether the cartridge is a PRO at all.

This runs *before* `debug_init()` as well as before `cart_init()`: `DEBUG_FEATURE_LOG_USB`
makes libdragon run a cartridge probe of its own that writes unlock keys chosen for other
carts. On a PRO, `cart_init()` is skipped entirely and `cart_type` stays `CART_NULL`.

### The `sd:/` filesystem

A libdragon `filesystem_t` attached with `attach_filesystem("sd:/", &fs)`, implementing
`open, close, read, write, lseek, fstat, stat, findfirst, findnext2, mkdir, unlink` on top of
the cartridge's `CMD_FS` commands, with the controller's FatFs result codes mapped to `errno`.

Three details worth recording:

- **Path forms differ per callback.** libdragon strips the whole prefix for `open` and
  `unlink` but leaves a leading slash on for `stat`, `findfirst`, `findnext2` and `mkdir`;
  and the browser builds paths with `"%s/%s"`, which yields `sd://name` when it is rooted at
  the card. One normaliser strips every leading slash, collapses the rest and rejects `..`.
- **One open file and one loaded directory, cartridge-wide.** Read handles are bound to the
  cartridge lazily and re-bound when used again; a second handle is refused only when a write
  is involved, because re-opening a file created with "truncate" would discard what had just
  been written. Directory walks carry their index in `dir_t`'s opaque cookie and reload if a
  generation counter says something displaced the listing.
- **A directory error returns -2, not -1.** libdragon reads -1 from `findfirst` as "the
  directory exists and is empty".

### Reading roms

A size threshold for a fast path cannot work here: newlib's `fread` always goes through its
own 1024-byte buffer, so the browser's single 512 KB read arrives as 512 separate 1 KB calls.

Instead a 64 KB window of the file is staged in cartridge memory with `ed_fs_file_read_fci`
— the controller streams the card straight into cartridge SDRAM with the N64 idle — and
served from there by plain PI DMA, refilled when the read position leaves it. A 512 KB rom
costs 8 cartridge transfers instead of 512 round trips through a fifo that moves one payload
byte per 32-bit PI word. Files below 32 KB stay on the fifo.

Staging sits at 16 MB into cartridge SDRAM, not at the 2 MB the injected-rom convention uses:
`build_dfs.sh` packs `filesystem/` into the rom, which currently produces a 2.6 MB z64, so
staging at 2 MB would overwrite the running code. At mount time it is checked by writing two
patterns, reading them back, and confirming the first 64 bytes of the running rom did not
change — a read-back on its own would pass on an address that aliases back onto us. On
failure it retries at 4 MB, and failing that everything falls back to the fifo. A transfer
that errors later does the same rather than failing the load.

### Starting a game from the Everdrive menu

`startEd64ProRom()` reads `DEV_PATH_APPF`, checks the extension, and loads the file through
the same filesystem and the same `loadRomFile()` the browser uses, so both paths agree about
the 512-byte copier header. It takes its one chance per power-on whatever comes of it: the
loop in `main()` returns here every time a game ends, and the cartridge would still be naming
the same file — so without that, leaving a game would restart it, and so would holding Z to
ask for the browser. This is the equivalent of `killInjectedRomHeader()` on the other carts,
and unlike that it does not need the cartridge to agree to forget anything.

Holding Z and the saved `autostart` setting both work as they do elsewhere. Because the
settings must be loaded before `autostart` can be honoured, the card is mounted *before* the
rom is read on this cart — the opposite of the X-series ordering, which exists only because
libcart reconfigures the cartridge interface when it mounts.

## Regression check

The PRO probe runs on every cartridge, before `cart_init()`, so it had to be confirmed not to
disturb the carts that already worked. A SummerCart64 and an EverDrive-64 X7 both run
normally with the same binary and report their own cartridge type in a debug build, which
also confirms the probe rejects an X-series: its `EDID` answers `0xED64` in the top half just
as the PRO's does, and it is the `SYSSTAT` gate that separates them.

The EverDrive-64 V3 and the 64drive have not been re-tested; both were already listed as
untested or partly working in the README.

## Ported around

Three things in the reference sources were changed deliberately, and should not be copied
back in from upstream:

- **Unbounded waits.** The reference fifo reader loops on "bytes available" forever and the
  wait-for-controller spins on a status bit with no way out. Every wait here has a deadline
  that resets whenever bytes actually arrive; running out latches a fault that later commands
  refuse, so a card pulled mid-game becomes an error rather than a frozen console.
- **Unbounded received strings.** `ed_rx_string()` writes a terminator at a controller-supplied
  offset before reading the body, with no bound. The port truncates to the caller's buffer and
  drains the remainder so the fifo stays in step.
- **`ed_epo_rd` / `ed_epo_wr`.** Both advance their cartridge address by the whole remaining
  length rather than by the block just transferred, so they walk the wrong addresses for
  anything over one block. They are not ported; nothing here needs them.

## Incidental fixes

Reachable before this work, but made materially more likely by it:

- `killInjectedRomHeader()` ran unconditionally on the way to the browser, and on a cartridge
  where `GetRomAddress()` is 0 — an emulator, and now the PRO — it issued DMA writes to PI
  address `0x7FF0`, which is not cartridge space.
- `RomLister::list()` computed `max_entries` and never checked it, so a directory with more
  than about 400 entries wrote past the end of the emulator's tile cache. Browsing a card root
  is now the fallback on the PRO, which makes that easy to hit.
- A rom that failed to open returned an uninitialised `RomInfo` from `menu()` straight into
  `load_rom()`. The browser now stays up and reports the error.

## Build note

`smsplus/system.h` shadowed libdragon's `system.h`, which is where `attach_filesystem()` and
`filesystem_t` live. Promoting libdragon's directory with `-I` does not help — gcc drops a
`-I` naming a directory already on its standard search path — so the project's own include
directories moved to `-iquote`. Every project header is included in quotes, and a quoted
include always searches the including file's own directory first, so `smsplus/shared.h` still
gets `smsplus/system.h`.
