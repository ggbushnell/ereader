// Native stand-in for the ESP32 FS layer: fs::File over a stdio FILE*.
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <memory>

namespace fs {

class File {
 public:
  File() {}
  explicit File(FILE *f) : f_(f, &fcloseIf) {}
  explicit operator bool() const { return (bool)f_; }
  size_t read(uint8_t *dst, size_t n) { return f_ ? fread(dst, 1, n, f_.get()) : 0; }
  size_t write(const uint8_t *src, size_t n) { return f_ ? fwrite(src, 1, n, f_.get()) : 0; }
  bool seek(size_t pos) { return f_ && fseek(f_.get(), (long)pos, SEEK_SET) == 0; }
  size_t position() const { return f_ ? (size_t)ftell(f_.get()) : 0; }
  size_t size() const {
    if (!f_) return 0;
    long cur = ftell(f_.get());
    fseek(f_.get(), 0, SEEK_END);
    long end = ftell(f_.get());
    fseek(f_.get(), cur, SEEK_SET);
    return (size_t)end;
  }
  void close() { f_.reset(); }
 private:
  static void fcloseIf(FILE *f) { if (f) fclose(f); }
  std::shared_ptr<FILE> f_;
};

}  // namespace fs
