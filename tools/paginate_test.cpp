// Host harness for the on-device paginator in src/text_paginate.cpp.
//
// Reads UTF-8 text on stdin, paginates it on the given grid, and prints the
// pages in the same form tools/check_paginator.py prints them from the Python
// pipeline, so the two can be diffed directly:
//
//     c++ -std=c++11 -O1 -I../src tools/paginate_test.cpp \
//         src/text_paginate.cpp -o /tmp/paginate_test
//     python3 tools/check_paginator.py sample.txt
//
// This file is not part of the firmware: it lives in tools/, so PlatformIO
// never compiles it into the esp32s3 build.

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include "text_paginate.h"

int main(int argc, char **argv) {
  int cols = (argc > 1) ? atoi(argv[1]) : 74;
  int rows = (argc > 2) ? atoi(argv[2]) : 27;

  int pageNo = 0;
  textpage::Pipeline pipeline(cols, rows,
                              [&](const std::vector<std::string> &lines) {
    printf("=== page %d (%d lines)\n", pageNo++, (int)lines.size());
    for (size_t i = 0; i < lines.size(); i++) {
      std::string utf8 = textpage::encodeUtf8(lines[i]);
      printf("%s\n", utf8.c_str());
    }
  });

  unsigned char buf[997];  // deliberately not a power of two: exercises the
                           // decoder's carry across chunk boundaries
  size_t got;
  while ((got = fread(buf, 1, sizeof(buf), stdin)) > 0) {
    pipeline.feed(buf, got);
  }
  pipeline.finish();

  fprintf(stderr, "pages: %u  truncated: %d\n",
          (unsigned)pipeline.pageCount(), pipeline.truncated() ? 1 : 0);
  return 0;
}
