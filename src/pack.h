#pragma once

#include <Arduino.h>
#include <FS.h>

// Reader for the EPK1 asset container.
//
// The pack carries the Pokemon graphics and tables the companion screen draws
// with (tiles, font, borders, name tables). It is generated on a host by
// tools/pokered_pack.py from a pokered checkout and uploaded through the games
// page like a ROM, landing at GAMES_PACK_PATH. The repo never ships the assets.
//
// Layout, all little endian:
//
//   0   4   magic "EPK1"
//   4   4   section count
//   8   n*24 entries: char name[16] zero padded, u32 offset, u32 length
//           (offset is from the start of the file)
//   ...     raw section bytes
//
// Only the table of contents is held in RAM. Section bytes are read on demand
// from the open File, so a view can blit a tile without a 40 KB buffer.

namespace pack {

// Opens GAMES_PACK_PATH and parses the table of contents. False when the file
// is absent or not an EPK1 pack; every other call then behaves as if empty.
bool open();
void close();
bool isOpen();

bool has(const char *name);
size_t size(const char *name);   // 0 when absent

// Reads `len` bytes from `offset` inside the named section. Returns the number
// of bytes actually read, which is short or 0 on a bad name or a range past
// the end of the section.
// Read counters since the last statReset(), for profiling a screen draw.
extern uint32_t statReads;
extern uint32_t statMicros;
void statReset();

size_t read(const char *name, size_t offset, void *dst, size_t len);

// Section count and the name of one section, for the diagnostics line.
int count();
const char *nameAt(int index);

}  // namespace pack
