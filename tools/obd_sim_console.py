#!/usr/bin/env python3
"""Console REPL for simulator mode over USB serial."""

from __future__ import annotations

import argparse
import sys
import time

import serial
import serial.tools.list_ports


DEFAULT_PORT = "COM5"
DEFAULT_BAUD = 115200


def list_ports() -> list[str]:
    return [p.device for p in serial.tools.list_ports.comports()]


def print_available_ports() -> None:
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return
    print("Available ports:")
    for p in ports:
        print(f"  - {p.device}: {p.description}")


def send_command(ser: serial.Serial, cmd: str, wait_s: float = 0.15) -> None:
    text = cmd.strip()
    if not text:
        return
    ser.write((text + "\n").encode("utf-8"))
    ser.flush()
    time.sleep(wait_s)
    while True:
        line = ser.readline()
        if not line:
            break
        print(line.decode("utf-8", errors="replace").rstrip())


def run_send_sequence(ser: serial.Serial, sequence: str) -> None:
    commands = [c.strip() for c in sequence.split(";") if c.strip()]
    for cmd in commands:
        print(f"> {cmd}")
        send_command(ser, cmd)


def run_repl(ser: serial.Serial) -> int:
    print("Connected. Type 'help' for firmware commands, 'quit' to exit.")
    while True:
        try:
            cmd = input("sim> ").strip()
        except (KeyboardInterrupt, EOFError):
            print()
            return 0
        if cmd.lower() in {"quit", "exit"}:
            return 0
        send_command(ser, cmd)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="OBD simulator serial console")
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial port (e.g. COM5)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Baud rate")
    parser.add_argument(
        "--send",
        default="",
        help='One-shot commands separated by ";" (example: "connect; egt 600; regen 2")',
    )
    parser.add_argument(
        "--list-ports", action="store_true", help="List available ports and exit"
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.list_ports:
        print_available_ports()
        return 0

    if args.port not in list_ports():
        print(f"Warning: {args.port} not currently listed. Continuing anyway...")

    try:
        with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
            if args.send:
                run_send_sequence(ser, args.send)
                return 0
            return run_repl(ser)
    except serial.SerialException as exc:
        print(f"Serial error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
