#!/usr/bin/env python3
"""Send one command to the firmware's USB CDC diagnostic port."""
import argparse
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("port", help="e.g. /dev/ttyACM0 or COM5")
parser.add_argument("command", nargs="*", default=["status"])
parser.add_argument("--watch", action="store_true", help="repeat once per second")
args = parser.parse_args()
command = " ".join(args.command) or "status"
if "\n" in command or "\r" in command or len(command) > 126:
    parser.error("use a single command of at most 126 characters")
if args.watch and command not in {"status", "slots", "adc"}:
    parser.error("--watch supports status, slots or adc only")
try:
    with serial.Serial(args.port, 115200, timeout=0.05, dsrdtr=False) as port:
        port.dtr = True
        time.sleep(0.1)
        while True:
            port.write((command + "\n").encode("ascii"))
            deadline = time.monotonic() + 0.8
            while time.monotonic() < deadline:
                data = port.read(1024)
                if data:
                    print(data.decode("utf-8", errors="replace"), end="", flush=True)
            if not args.watch:
                break
            time.sleep(0.2)
except KeyboardInterrupt:
    pass
except (serial.SerialException, UnicodeEncodeError) as error:
    parser.exit(1, f"{error}\n")
