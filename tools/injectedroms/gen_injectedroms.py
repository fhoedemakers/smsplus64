#!/usr/bin/env python3
"""Generate injectedroms_table.h: roms a flashcart menu can inject that the
emulator cannot identify on its own.

The EverDrive-64 X7 menu and N64FlashcartMenu copy a rom into cartridge memory
and pass nothing else - no file name, no size. smsPlus64 recognises Master
System and Game Gear roms by their "TMR SEGA" header and takes anything else
for a 48 KB SG-1000 rom. That goes wrong for these kinds of rom:

  - Master System / Game Gear roms that IsRomInjected() does not accept: no
    header at 0x7FF0, or one whose size code it does not understand. They
    would be started as SG-1000 games.
  - Roms whose header names the other console. Region codes are unreliable:
    Game Gear roms carrying a Master System code and the other way round both
    occur, and each is then emulated with the wrong screen and palette.
  - SG-1000 roms over 48 KB, which need their real size to bank-switch.
  - Master System / Game Gear roms of 48 KB or less. A header does not give
    a size that can be believed, so IsRomInjected() reads 512 KB for every
    one of them, but load_rom() goes by the size to decide that a rom this
    small has no mapper. The MSX conversions need that to start.

This script lists them by the CRC-32 of their first 32 KB (the whole rom when
it is smaller), which the emulator can compute from cartridge memory, with
their type and size. 32 KB rather than less because several families of roms
share their start: Korean conversions reuse one loader, MSX conversions share
their first 16 KB with each other and with SG-1000 releases of the same games,
and some homebrew exists as both an SG-1000 and a Master System build. With
8 KB, Bosconian, Pac-Man, Mr. Do and three more MSX and SG-1000 conversions
could not be listed; with 32 KB every rom of 48 KB or less can.

The emulator looks every injected rom up in the table before it reads the
header, so any rom can be confused with an entry, and every rom scanned counts
when entries are checked for clashes. Entries that share a CRC and a type but
differ in size are merged into one, using the largest size. For Master System
and Game Gear roms reading too much only means a small rom keeps the Sega
mapper, as every rom used to, whereas reading too little would take the
mapper away from a larger rom that starts like a small one. A CRC that
SG-1000 and other roms share, or SG-1000 roms of different sizes share, cannot
be resolved; it is left out and reported, and those roms fall back to the
header or the emulator's SG-1000 guess. So are roms under 1 KB, which are too
short to tell apart from the start of a larger one.

    tools/injectedroms/gen_injectedroms.py -o injectedroms_table.h DIR [DIR...]
    tools/injectedroms/gen_injectedroms.py DIR [DIR...]          # report only
"""
import argparse
import os
import sys
import zlib
from collections import defaultdict

CRC_BYTES = 32768       # must match INJECTEDROM_CRC_BYTES in injectedroms.h
MIN_CRC_BYTES = 1024    # roms shorter than this are too short to identify
SG_GUESSED_SIZE = 0xC000
NO_MAPPER_SIZE = 0xC000  # load_rom() gives roms up to this size no mapper
# Size codes IsRomInjected() in smsPlus64.cpp accepts
KNOWN_SIZE_CODES = {0x0, 0x1, 0x2, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF}
TYPES = {".sms": "TYPE_SMS", ".gg": "TYPE_GG", ".sg": "TYPE_SG"}


def header_type(rom):
    """The type IsRomInjected() would give this rom, or None when it does not
    recognise it by its header."""
    if len(rom) < 0x8000 or rom[0x7FF0:0x7FF8] != b"TMR SEGA":
        return None
    if (rom[0x7FFF] & 0x0F) not in KNOWN_SIZE_CODES:
        return None
    return "TYPE_GG" if 5 <= (rom[0x7FFF] >> 4) <= 7 else "TYPE_SMS"


def scan(dirs):
    """Yield (path, type, size, identified by its header, first CRC_BYTES)."""
    for top in dirs:
        for root, _, files in os.walk(top):
            for name in sorted(files):
                ext = os.path.splitext(name)[1].lower()
                if ext not in TYPES:
                    continue
                path = os.path.join(root, name)
                with open(path, "rb") as f:
                    rom = f.read()
                # Copier headers: the emulator looks for the rom at offset 0
                # and 512, so the CRC is taken over the rom itself. The test
                # is loadRomFile()'s: a whole number of 1 KB plus 512 bytes.
                # An odd number of 512-byte blocks alone also matches roms
                # that are not a whole number of blocks, and their start is
                # code, not a header.
                if ext != ".sg" and len(rom) % 1024 == 512:
                    rom = rom[512:]
                typ = TYPES[ext]
                # Identified correctly by its header
                known = typ != "TYPE_SG" and header_type(rom) == typ
                yield path, typ, len(rom), known, rom[:CRC_BYTES]


def crc(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def build_table(roms):
    """Return (entries, dropped). entries: (length, crc, type, size, path)."""
    # Every injected rom is looked up before its header is read
    reach = [r for r in roms if r[4]]
    wanted_roms = [r for r in reach
                   if (not r[3] and (r[1] != "TYPE_SG" or r[2] > SG_GUESSED_SIZE))
                   or (r[3] and r[2] <= NO_MAPPER_SIZE)]

    # A rom shorter than CRC_BYTES is hashed whole, and at run time that many
    # bytes of whatever was injected are compared - so every rom that reaches
    # the lookup has to be checked against each such length, not just roms of
    # the same size. Very short roms are left out: too little to go by.
    dropped = [((len(r[4]), crc(r[4])), [(r[1], r[2], r[0])])
               for r in wanted_roms if len(r[4]) < MIN_CRC_BYTES]
    wanted_roms = [r for r in wanted_roms if len(r[4]) >= MIN_CRC_BYTES]
    lengths = {len(r[4]) for r in wanted_roms}

    groups = defaultdict(list)   # (length, crc) -> [(type, size, path)]
    for path, typ, size, _, head in reach:
        for length in lengths:
            if len(head) >= length:
                groups[(length, crc(head[:length]))].append((typ, size, path))
    wanted = {(len(r[4]), crc(r[4])): r[0] for r in wanted_roms}

    entries = []
    for key in sorted(wanted):
        members = groups[key]
        types = {t for t, _, _ in members}
        sg_sizes = {s for t, s, _ in members if t == "TYPE_SG"}
        if len(types) > 1 or len(sg_sizes) > 1:
            dropped.append((key, members))
            continue
        typ = types.pop()
        size = max(s for _, s, _ in members)
        entries.append((key[0], key[1], typ, size, wanted[key]))
    # Shortest hashed length first, then by CRC, so the lookup can carry each
    # length's CRC on to the next
    entries.sort(key=lambda e: (e[0], e[1]))
    return entries, dropped


def write_table(out, entries, dirs):
    out.write("/* Generated by tools/injectedroms/gen_injectedroms.py - do not edit.\n"
              "   Scanned:\n")
    for d in dirs:
        out.write(f"     {d}\n")
    out.write("\n   Roms a flashcart menu can inject that smsPlus64 cannot identify on\n"
              "   its own, or whose size it has to know. See injectedroms.c. */\n\n"
              "static const InjectedRom injectedRoms[] =\n{\n")
    for length, value, typ, size, path in entries:
        path = path.replace("*/", "*_")
        out.write(f"    {{0x{value:08X}, {size:7}, {length:4}, {typ}}}, /* {path} */\n")
    out.write("};\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("-o", "--output", help="write the table here")
    args = ap.parse_args()

    entries, dropped = build_table(scan(args.dirs))

    for length, value, typ, size, path in entries:
        print(f"{value:08X} {length:4} {typ:8} {size:7}  {path}")
    for (length, value), members in dropped:
        print(f"LEFT OUT {value:08X} ({length} bytes):", file=sys.stderr)
        for typ, size, path in sorted(members):
            print(f"    {typ:8} {size:7}  {os.path.basename(path)}", file=sys.stderr)
    counts = defaultdict(int)
    for e in entries:
        counts[e[2]] += 1
    print(f"{len(entries)} entries {dict(counts)}, {len(dropped)} left out", file=sys.stderr)

    if args.output:
        with open(args.output, "w") as out:
            write_table(out, entries, args.dirs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
