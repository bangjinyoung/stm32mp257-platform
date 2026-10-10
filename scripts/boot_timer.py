#!/usr/bin/env python3
"""Reboot the board N times over the console VCP and log every line with a host timestamp.

usage: boot_timer.py <tty> <log file> [runs]

Each run sends "reboot", waits for the login banner, then runs systemd-analyze.
A summary of stage times (host clock, relative to the reboot command) is appended
at the end of the log. Host timestamps include USB/VCP buffering latency.
"""

import datetime
import os
import subprocess
import sys
import time

MARKERS = [
    ("tfa", b"NOTICE:  CPU:"),
    ("uboot", b"U-Boot 20"),
    ("kernel_start", b"Starting kernel"),
    ("linux", b"Linux version"),
    ("login", b"login:"),
]
RUN_TIMEOUT_S = 180


def now():
    return time.monotonic()


def stamp():
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def run_once(fd, log, run):
    t0 = now()
    seen = {}
    pending = b""
    log.write(f"{stamp()} ### run {run}: send reboot\n".encode())
    os.write(fd, b"reboot\n")
    while now() - t0 < RUN_TIMEOUT_S:
        try:
            chunk = os.read(fd, 512)
        except BlockingIOError:
            chunk = b""
        if not chunk:
            time.sleep(0.01)
            continue
        pending += chunk
        while b"\n" in pending:
            line, pending = pending.split(b"\n", 1)
            line = line.rstrip(b"\r")
            log.write(stamp().encode() + b" " + line + b"\n")
            for name, pat in MARKERS:
                if name not in seen and pat in line:
                    seen[name] = now() - t0
        if "login" in seen:
            break
    if "login" in seen:
        time.sleep(5)
        os.write(fd, b"\nsystemd-analyze\n")
        end = now() + 4
        while now() < end:
            try:
                chunk = os.read(fd, 512)
            except BlockingIOError:
                chunk = b""
            if chunk:
                for line in chunk.split(b"\n"):
                    log.write(stamp().encode() + b" " + line.rstrip(b"\r") + b"\n")
            else:
                time.sleep(0.05)
    return seen


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit("usage: boot_timer.py <tty> <log file> [runs]")
    tty, log_path = sys.argv[1], sys.argv[2]
    runs = int(sys.argv[3]) if len(sys.argv) == 4 else 3
    subprocess.run(["stty", "-F", tty, "115200", "raw", "-echo", "-hupcl"], check=True)
    fd = os.open(tty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    results = []
    with open(log_path, "ab", buffering=0) as log:
        os.write(fd, b"\n")
        time.sleep(1)
        for run in range(1, runs + 1):
            results.append(run_once(fd, log, run))
        log.write(b"### summary (seconds from reboot command, host clock)\n")
        for run, seen in enumerate(results, 1):
            cols = " ".join(f"{name}={seen.get(name, float('nan')):.3f}" for name, _ in MARKERS)
            log.write(f"run {run}: {cols}\n".encode())
            print(f"run {run}: {cols}")


if __name__ == "__main__":
    main()
