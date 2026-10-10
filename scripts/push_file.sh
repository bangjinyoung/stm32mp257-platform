#!/bin/sh
# Copy a small file to the board over the Linux console (no network needed).
# The board shell must be at a root prompt; picocom may stay open (its view shows the transfer).
# usage: push_file.sh <local file> <path on board> [tty]
# Then compare: sha256sum <local file>   vs   sha256sum <path on board> (on the board)
set -e
SRC=$1
DST=$2
TTY=${3:-/dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_0037001A3335510533383531-if01}
[ -f "$SRC" ] && [ -n "$DST" ] || { echo "usage: $0 <local file> <path on board> [tty]"; exit 1; }
SIZE=$(stat -c %s "$SRC")
[ "$SIZE" -le 1048576 ] || { echo "refusing: $SIZE B is too large for the console (max 1 MiB)"; exit 1; }

{
	printf "\nbase64 -d > %s <<'B64EOF'\n" "$DST"
	base64 -w 76 "$SRC" | while IFS= read -r line; do
		printf '%s\n' "$line"
		sleep 0.002
	done
	printf "B64EOF\nsha256sum %s\n" "$DST"
} > "$TTY"
echo "sent $SIZE B to $DST; host sha256:"
sha256sum "$SRC"
