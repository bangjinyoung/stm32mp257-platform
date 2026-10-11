#!/bin/sh
# Reboot into U-Boot, add the mp2-button DT node to the in-RAM DTB, boot Linux, and
# optionally push a module to /tmp. Nothing is written to the card (no saveenv).
# picocom must be running with --logfile; the console output is read from that log.
# usage: uboot_mp2_dt.sh <picocom log> [module.ko] [tty]
# Env: FROM_UBOOT=1 if the board already sits at the STM32MP> prompt.
#      NO_GPIO=1 to leave out button-gpios (probe must fail).
set -eu
LOG=${1:?usage: $0 <picocom log> [module.ko] [tty]}
KO=${2:-}
# first ST-LINK V3 console; pass [tty] when more than one board is attached
TTY=${3:-$(ls /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_*-if01 | head -n 1)}
DIR=$(dirname "$0")
GPIOC=/soc@0/pinctrl@44240000/gpio@44260000
ROOT=PARTUUID=e91c4e10-16e6-4c0e-bd0e-77becf4a3582
KERNEL=/Image-tune.gz

# Count matches of a pattern in the log written since byte offset $1.
count_since() { tail -c +"$1" "$LOG" | tr -d '\r' | grep -a -c -E "$2" || true; }

# Wait (max $3 s) until pattern $2 appears in the log after offset $1.
wait_for() {
	end=$(($(date +%s) + $3))
	until [ "$(count_since "$1" "$2")" -gt 0 ]; do
		[ "$(date +%s)" -lt "$end" ] || { echo "timeout waiting for: $2"; exit 1; }
		sleep 0.2
	done
}

# Send one U-Boot command and wait for the next prompt.
uboot() {
	off=$(($(stat -c %s "$LOG") + 1))
	printf '%s\n' "$1" > "$TTY"
	wait_for "$off" 'STM32MP> *$' 20
	if [ "$(count_since "$off" 'Usage:|Error|error|not found|ERROR')" -gt 0 ]; then
		echo "U-Boot command failed: $1"; tail -c +"$off" "$LOG" | tr -d '\r' | tail -5; exit 1
	fi
}

off=$(($(stat -c %s "$LOG") + 1))
if [ "${FROM_UBOOT:-0}" != 1 ]; then
	printf '\nreboot\n' > "$TTY"
	echo "$(date +%T) reboot sent, stopping autoboot"
	end=$(($(date +%s) + 150))
	until [ "$(count_since "$off" 'STM32MP> *$')" -gt 0 ]; do
		[ "$(date +%s)" -lt "$end" ] || { echo "timeout: no U-Boot prompt"; exit 1; }
		printf ' ' > "$TTY"
		sleep 0.1
	done
	printf '\003' > "$TTY"   # drop the queued spaces
	sleep 0.5
fi
echo "$(date +%T) U-Boot prompt"

uboot 'load mmc 0:8 ${fdt_addr_r} /stm32mp257f-dk.dtb'
uboot 'fdt addr ${fdt_addr_r}'
uboot 'fdt resize 4096'
uboot "fdt get value gp $GPIOC phandle"
uboot 'fdt mknode / mp2-button'
uboot 'fdt set /mp2-button compatible "bjy,mp2-button"'
if [ "${NO_GPIO:-0}" = 1 ]; then
	echo "NO_GPIO=1: node without button-gpios (negative test)"
else
	uboot 'fdt set /mp2-button button-gpios "<${gp} 0xb 0x0>"'
fi
uboot 'fdt print /mp2-button'
uboot "load mmc 0:8 \${kernel_addr_r} $KERNEL"
uboot 'load mmc 0:8 ${ramdisk_addr_r} /st-image-resize-initrd'
uboot 'setenv rdsize ${filesize}'
uboot "setenv bootargs \"root=$ROOT rootwait rw earlycon console=ttySTM0,115200\""
echo "$(date +%T) DT node added, booting $KERNEL"

off=$(($(stat -c %s "$LOG") + 1))
printf '%s\n' 'booti ${kernel_addr_r} ${ramdisk_addr_r}:${rdsize} ${fdt_addr_r}' > "$TTY"
wait_for "$off" 'root@[^ ]+:.*# *$' 180
sleep 5   # let the boot messages settle
echo "$(date +%T) Linux shell"

off=$(($(stat -c %s "$LOG") + 1))
printf '\ncat /proc/device-tree/mp2-button/compatible; echo; ls /sys/bus/platform/devices/ | grep mp2\n' > "$TTY"
wait_for "$off" '^mp2-button *$' 10
echo "$(date +%T) mp2-button node and platform device present"

if [ -n "$KO" ]; then
	"$DIR/push_file.sh" "$KO" "/tmp/$(basename "$KO")" "$TTY"
fi
