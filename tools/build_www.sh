#!/bin/sh
# Gzip the four files the firmware embeds into www/dist/.
# The device serves these with Content-Encoding: gzip, which is transparent
# to the browser, so the page itself never knows.
set -e

here=$(cd "$(dirname "$0")" && pwd)
www="$here/../www"
dist="$www/dist"

mkdir -p "$dist"

gz() {
  src="$1"
  out="$2"
  if [ ! -f "$src" ]; then
    echo "missing: $src" >&2
    exit 1
  fi
  gzip -9 -c "$src" > "$out"
  raw=$(wc -c < "$src" | tr -d ' ')
  packed=$(wc -c < "$out" | tr -d ' ')
  printf '%-16s %8s -> %8s bytes (%d%%)\n' \
    "$(basename "$out")" "$raw" "$packed" $((packed * 100 / raw))
}

gz "$www/index.html"     "$dist/index.html.gz"
gz "$www/app.js"         "$dist/app.js.gz"
gz "$www/vendor/nes.js"  "$dist/nes.js.gz"
gz "$www/vendor/gb.js"   "$dist/gb.js.gz"

total=$(cat "$dist"/*.gz | wc -c | tr -d ' ')
printf '%-16s %21s bytes\n' "TOTAL" "$total"
