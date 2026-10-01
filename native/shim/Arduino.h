// Native stand-in for the Arduino core: just enough for src/pokemon_state.cpp,
// src/gbgfx.cpp, src/pokemon_views.cpp and src/pack.cpp to compile on a Mac.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <chrono>

inline uint32_t millis() {
  using namespace std::chrono;
  static const auto t0 = steady_clock::now();
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
inline uint32_t micros() {
  using namespace std::chrono;
  static const auto t0 = steady_clock::now();
  return (uint32_t)duration_cast<microseconds>(steady_clock::now() - t0).count();
}
inline void delay(uint32_t) {}
inline void yield() {}

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned int v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(long long v) : s_(std::to_string(v)) {}
  String(unsigned long long v) : s_(std::to_string(v)) {}
  String(double v, int digits = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", digits, v); s_ = b; }

  size_t length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  const char *c_str() const { return s_.c_str(); }
  char charAt(size_t i) const { return i < s_.size() ? s_[i] : 0; }
  char operator[](size_t i) const { return charAt(i); }
  char &operator[](size_t i) { return s_[i]; }
  void reserve(size_t n) { s_.reserve(n); }
  void trim() {
    size_t a = s_.find_first_not_of(" \t\r\n");
    size_t b = s_.find_last_not_of(" \t\r\n");
    s_ = (a == std::string::npos) ? "" : s_.substr(a, b - a + 1);
  }
  String substring(size_t from) const { return from < s_.size() ? String(s_.substr(from)) : String(); }
  String substring(size_t from, size_t to) const {
    if (from > to) std::swap(from, to);
    if (from >= s_.size()) return String();
    if (to > s_.size()) to = s_.size();
    return String(s_.substr(from, to - from));
  }
  int indexOf(char c, size_t from = 0) const { size_t p = s_.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String &o, size_t from = 0) const { size_t p = s_.find(o.s_, from); return p == std::string::npos ? -1 : (int)p; }
  bool startsWith(const String &o) const { return s_.compare(0, o.s_.size(), o.s_) == 0; }
  bool endsWith(const String &o) const { return s_.size() >= o.s_.size() && s_.compare(s_.size() - o.s_.size(), o.s_.size(), o.s_) == 0; }
  bool equals(const String &o) const { return s_ == o.s_; }
  long toInt() const { return atol(s_.c_str()); }
  void toUpperCase() { for (auto &c : s_) c = (char)toupper((unsigned char)c); }
  void toLowerCase() { for (auto &c : s_) c = (char)tolower((unsigned char)c); }
  void remove(size_t i, size_t n = std::string::npos) { if (i < s_.size()) s_.erase(i, n); }
  void setCharAt(size_t i, char c) { if (i < s_.size()) s_[i] = c; }
  bool concat(const String &o) { s_ += o.s_; return true; }
  bool concat(char c) { s_ += c; return true; }

  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String &operator+=(const char *o) { s_ += o ? o : ""; return *this; }
  String &operator+=(char c) { s_ += c; return *this; }
  String &operator+=(int v) { s_ += std::to_string(v); return *this; }
  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator==(const char *o) const { return s_ == (o ? o : ""); }
  bool operator!=(const String &o) const { return s_ != o.s_; }
  bool operator!=(const char *o) const { return !(*this == o); }
  bool operator<(const String &o) const { return s_ < o.s_; }

  const std::string &std() const { return s_; }
 private:
  std::string s_;
};
inline String operator+(const String &a, const String &b) { String r(a); r += b; return r; }
inline String operator+(const String &a, const char *b) { String r(a); r += b; return r; }
inline String operator+(const char *a, const String &b) { String r(a); r += b; return r; }
inline String operator+(const String &a, char b) { String r(a); r += b; return r; }
inline String operator+(const String &a, int b) { String r(a); r += b; return r; }

// Serial: everything the firmware logs goes to stderr so stdout stays clean.
struct SerialShim {
  int printf(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap; va_start(ap, fmt); int n = vfprintf(stderr, fmt, ap); va_end(ap); return n;
  }
  void print(const String &s) { fputs(s.c_str(), stderr); }
  void print(const char *s) { fputs(s, stderr); }
  void println(const String &s) { fputs(s.c_str(), stderr); fputc('\n', stderr); }
  void println(const char *s) { fputs(s, stderr); fputc('\n', stderr); }
  void println() { fputc('\n', stderr); }
};
#include <stdarg.h>
extern SerialShim Serial;

#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define F(x) x
