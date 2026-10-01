// Native stand-in for LittleFS: paths are rooted at the directory set by
// littlefs_set_root() (the stub server's --root, i.e. stub_games/).
#pragma once
#include "FS.h"
#include <sys/stat.h>

class LittleFSShim {
 public:
  void setRoot(const std::string &root) { root_ = root; }
  std::string resolve(const char *path) const {
    std::string p = path ? path : "";
    if (!p.empty() && p[0] == '/') return root_ + p;
    return root_ + "/" + p;
  }
  bool exists(const char *path) const {
    struct stat st;
    return ::stat(resolve(path).c_str(), &st) == 0;
  }
  fs::File open(const char *path, const char *mode = "r") const {
    FILE *f = fopen(resolve(path).c_str(), mode);
    return f ? fs::File(f) : fs::File();
  }
 private:
  std::string root_ = ".";
};
extern LittleFSShim LittleFS;
