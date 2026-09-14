#include "pack.h"

#include <LittleFS.h>

#include "config.h"

namespace {

struct Entry {
  char name[17];
  uint32_t offset;
  uint32_t length;
};

const int MAX_SECTIONS = 48;

fs::File file;
Entry toc[MAX_SECTIONS];
int sections = 0;
bool opened = false;

uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

int find(const char *name) {
  if (!opened || !name) return -1;
  for (int i = 0; i < sections; i++) {
    if (strcmp(toc[i].name, name) == 0) return i;
  }
  return -1;
}

}  // namespace

namespace pack {

bool open() {
  close();
  if (!LittleFS.exists(GAMES_PACK_PATH)) return false;
  file = LittleFS.open(GAMES_PACK_PATH, "r");
  if (!file) return false;

  uint8_t head[8];
  if (file.read(head, 8) != 8 || memcmp(head, "EPK1", 4) != 0) {
    file.close();
    return false;
  }
  uint32_t n = le32(head + 4);
  if (n == 0 || n > (uint32_t)MAX_SECTIONS) {
    file.close();
    return false;
  }

  uint32_t fileSize = (uint32_t)file.size();
  sections = 0;
  for (uint32_t i = 0; i < n; i++) {
    uint8_t rec[24];
    if (file.read(rec, 24) != 24) {
      file.close();
      sections = 0;
      return false;
    }
    Entry &e = toc[sections];
    memcpy(e.name, rec, 16);
    e.name[16] = 0;
    e.offset = le32(rec + 16);
    e.length = le32(rec + 20);
    // A truncated or lying entry is dropped rather than taking the pack down.
    if (e.offset > fileSize || e.length > fileSize - e.offset) continue;
    sections++;
  }
  if (!sections) {
    file.close();
    return false;
  }
  opened = true;
  Serial.printf("pack: %s open, %d sections\n", GAMES_PACK_PATH, sections);
  return true;
}

void close() {
  if (file) file.close();
  opened = false;
  sections = 0;
}

bool isOpen() { return opened; }

bool has(const char *name) { return find(name) >= 0; }

size_t size(const char *name) {
  int i = find(name);
  return i < 0 ? 0 : (size_t)toc[i].length;
}

size_t read(const char *name, size_t offset, void *dst, size_t len) {
  int i = find(name);
  if (i < 0 || !dst || !len) return 0;
  const Entry &e = toc[i];
  if (offset >= e.length) return 0;
  size_t room = (size_t)e.length - offset;
  if (len > room) len = room;
  size_t target = (size_t)e.offset + offset;
  uint32_t t0 = micros();
  if (file.position() != target && !file.seek(target)) return 0;
  size_t n = file.read((uint8_t *)dst, len);
  statReads++;
  statMicros += micros() - t0;
  return n;
}

uint32_t statReads = 0;
uint32_t statMicros = 0;

void statReset() { statReads = 0; statMicros = 0; }

int count() { return sections; }

const char *nameAt(int index) {
  if (index < 0 || index >= sections) return "";
  return toc[index].name;
}

}  // namespace pack
