#!/bin/bash
# Build a raw FAT12 image of one eye design for the CYD's ffat partition,
# and optionally flash it.
#
#   ./build-eye-image.sh <eye> [--flash]
#
# <eye> is a folder under assets/eyes/ (e.g. demon). Edit
# assets/eyes/<eye>/config.eye to tweak settings, then rerun this script.
# The image lands in assets/ffat-<eye>.img. With --flash it is written to the
# board at the ffat partition offset; override the port with PORT=/dev/cu.xxx.
#
# Layout the library expects on the partition:
#   /config.eye        (copied from the eye folder)
#   /<eye>/*.bmp       (paths inside config.eye are relative to the root)
set -euo pipefail

EYE="${1:-}"
FLASH="${2:-}"
PORT="${PORT:-/dev/cu.usbserial-14240}"
OFFSET=0x290000           # ffat partition start in default_ffat.csv
SECTORS=2816              # 0x160000 bytes / 512

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/assets/eyes/$EYE"
IMG="$HERE/assets/ffat-$EYE.img"

if [[ -z "$EYE" || ! -d "$SRC" ]]; then
  echo "usage: $0 <eye> [--flash]" >&2
  echo "available eyes:" >&2
  ls "$HERE/assets/eyes" | sed 's/^/  /' >&2
  exit 1
fi
if [[ ! -f "$SRC/config.eye" ]]; then
  echo "$SRC has no config.eye" >&2
  exit 1
fi

need_kb=$(du -sk "$SRC" | cut -f1)
if (( need_kb > 1350 )); then
  echo "$EYE is ${need_kb} KB; the partition holds about 1380 KB" >&2
  exit 1
fi

MNT="$(mktemp -d)"
DEV=""
cleanup() {
  mount | grep -q " $MNT " && umount "$MNT" || true
  [[ -n "$DEV" ]] && hdiutil detach -quiet "$DEV" || true
  rmdir "$MNT" 2>/dev/null || true
}
trap cleanup EXIT

echo "building $IMG from $SRC"
rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=512 count=$SECTORS 2>/dev/null
DEV=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$IMG" | awk '{print $1}' | head -1)
newfs_msdos -F 12 -S 512 -v EYES "${DEV/disk/rdisk}" >/dev/null 2>&1
mount -t msdos "$DEV" "$MNT" 2>/dev/null
mkdir "$MNT/$EYE"
/bin/cp -X "$SRC"/* "$MNT/$EYE/"          # -X: no extended attributes
/bin/cp -X "$SRC/config.eye" "$MNT/config.eye"
dot_clean -m "$MNT"                   # drop any ._ AppleDouble files
echo "contents:"
(cd "$MNT" && find . -type f | sort | sed 's/^/  /')
umount "$MNT"
hdiutil detach -quiet "$DEV"
DEV=""
echo "wrote $IMG"

if [[ "$FLASH" == "--flash" ]]; then
  echo "flashing to $PORT at $OFFSET"
  esptool --chip esp32 --port "$PORT" --baud 460800 write_flash "$OFFSET" "$IMG"
fi
