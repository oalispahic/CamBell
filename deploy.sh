#!/usr/bin/env bash
set -euo pipefail

# ── Paths ──────────────────────────────────────────────────────
SRC="/home/marexserver/Documents/PlatformIO/Projects/CamTest/.pio/build/esp32cam/firmware.bin"
FW_DIR="/home/marexserver/Documents/marexdevserver/backend/firmware"
DST="$FW_DIR/latest.bin"
META="$FW_DIR/meta.json"

# ── Version arg ────────────────────────────────────────────────
if [ $# -lt 1 ]; then
  echo "Usage: $0 <version-string>"
  echo "e.g.:  $0 \"BETA 1.2\""
  exit 1
fi
VERSION="$1"

# ── Sanity checks ──────────────────────────────────────────────
if [ ! -f "$SRC" ]; then
  echo "ERROR: build output not found at:"
  echo "  $SRC"
  echo "Did you run 'pio run' first?"
  exit 1
fi

if [ ! -d "$FW_DIR" ]; then
  echo "ERROR: firmware dir not found at:"
  echo "  $FW_DIR"
  exit 1
fi

# ── Copy ───────────────────────────────────────────────────────
cp "$SRC" "$DST"

# ── Hash + size ────────────────────────────────────────────────
MD5=$(md5sum "$DST" | awk '{print $1}')
SIZE=$(stat -c%s "$DST")

# ── Write meta.json ────────────────────────────────────────────
cat > "$META" <<EOF
{
  "version": "$VERSION",
  "size": $SIZE,
  "md5": "$MD5"
}
EOF

echo "Deployed firmware:"
echo "  version : $VERSION"
echo "  size    : $SIZE bytes"
echo "  md5     : $MD5"
echo "  -> $DST"
echo "  -> $META"