#!/usr/bin/env python3
"""
双向串口调试（逐键透传）：串口 → stdout，stdin 每字节 → 串口。
需要「串口助手式」逐行输入 MSH 命令时，请用仓库 scripts/sifli_serial_assistant.py。
默认端口与波特率与本仓库根目录 make.sh 中 sftool 参数一致（-p / -b）。
依赖：pip install pyserial（SiFli SDK 的 tools/requirements/requirements.core.txt 已包含）。
"""
from __future__ import annotations

import argparse
import sys
import threading

try:
    import serial
except ImportError:
    print("缺少 pyserial，请执行: pip install pyserial", file=sys.stderr)
    sys.exit(1)


def serial_to_stdout(ser: serial.Serial, stop: threading.Event) -> None:
    try:
        while not stop.is_set():
            n = max(1, ser.in_waiting)
            data = ser.read(n)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
    except (OSError, serial.SerialException):
        pass


def stdin_to_serial(ser: serial.Serial, stop: threading.Event) -> None:
    try:
        while not stop.is_set():
            chunk = sys.stdin.buffer.read(1)
            if not chunk:
                stop.set()
                break
            ser.write(chunk)
            ser.flush()
    except (OSError, serial.SerialException, BrokenPipeError):
        stop.set()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="SiFli 工程串口读写调试（默认与 make.sh 中 sftool 串口参数一致）"
    )
    parser.add_argument(
        "-p",
        "--port",
        default="/dev/ttyACM0",
        help="串口设备路径（make.sh 中 sftool -p）",
    )
    parser.add_argument(
        "-b",
        "--baud",
        type=int,
        default=1000000,
        help="波特率（make.sh 中 sftool -b）",
    )
    parser.add_argument(
        "--rtscts",
        action="store_true",
        help="启用硬件流控 RTS/CTS（默认关闭）",
    )
    parser.add_argument(
        "--xonxoff",
        action="store_true",
        help="启用软件流控（默认关闭）",
    )
    args = parser.parse_args()

    try:
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            write_timeout=1.0,
            xonxoff=args.xonxoff,
            rtscts=args.rtscts,
            dsrdtr=False,
        )
        if not args.rtscts:
            ser.dtr = False
            ser.rts = False
    except serial.SerialException as e:
        print(f"无法打开串口 {args.port}: {e}", file=sys.stderr)
        sys.exit(1)

    stop = threading.Event()
    rx = threading.Thread(target=serial_to_stdout, args=(ser, stop), daemon=True)
    rx.start()

    try:
        stdin_to_serial(ser, stop)
    except KeyboardInterrupt:
        stop.set()
    finally:
        stop.set()
        ser.close()
        print("\n[已关闭串口]", file=sys.stderr)


if __name__ == "__main__":
    main()
