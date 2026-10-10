#!/usr/bin/env python3
"""Append every line from the board's VCP to a log file with a host timestamp."""

import datetime
import os
import subprocess
import sys
import time


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: vcp_logger.py <tty> <log file>")

    tty, log_path = sys.argv[1], sys.argv[2]
    subprocess.run(["stty", "-F", tty, "115200", "raw", "-echo", "-hupcl"], check=True)

    fd = os.open(tty, os.O_RDONLY | os.O_NOCTTY)
    pending = b""
    with open(log_path, "ab", buffering=0) as log:
        while True:
            chunk = os.read(fd, 256)
            if not chunk:
                # A replugged ST-LINK leaves the old tty returning EOF forever.
                if not os.path.exists(tty):
                    sys.exit("tty gone: " + tty)
                time.sleep(0.05)
                continue
            pending += chunk
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                log.write(stamp.encode() + b" " + line.rstrip(b"\r") + b"\n")


if __name__ == "__main__":
    main()
