#!/usr/bin/env python3
"""
交互式串口助手：后台线程打印设备输出，主线程逐行读取终端输入并发送到 MSH/Finsh。
默认 8N1、无流控；打开后拉低 DTR/RTS（与 batch 脚本一致，避免 CDC 误复位）。
空行发送仅回车；输入 exit / quit 或 Ctrl+C 退出。

  python3 scripts/sifli_serial_assistant.py
  python3 scripts/sifli_serial_assistant.py -p /dev/ttyACM0 -b 1000000
"""
from __future__ import annotations

import argparse
import sys
import threading

try:
    import serial
except ImportError:
    print("缺少 pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(1)


def rx_loop(ser: serial.Serial, stop: threading.Event) -> None:
    try:
        while not stop.is_set():
            n = ser.in_waiting
            chunk = ser.read(n if n > 0 else 1)
            if chunk:
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
    except (OSError, serial.SerialException):
        pass


def main() -> None:
    p = argparse.ArgumentParser(description="串口助手：终端逐行输入 MSH 命令")
    p.add_argument("-p", "--port", default="/dev/ttyACM0")
    p.add_argument("-b", "--baud", type=int, default=1000000)
    p.add_argument(
        "--eol",
        choices=("crlf", "lf", "cr"),
        default="crlf",
        help="行尾（MSH 建议 crlf）",
    )
    p.add_argument(
        "--prompt",
        default="msh> ",
        help="输入提示符（写到 stderr，减轻与设备日志混行）",
    )
    p.add_argument(
        "--raw",
        action="store_true",
        help="原始字节模式：stdin 每键直达串口（与旧版 debug 相同），不再逐行",
    )
    args = p.parse_args()

    eol_map = {"crlf": b"\r\n", "lf": b"\n", "cr": b"\r"}
    eol_b = eol_map[args.eol]

    try:
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            write_timeout=2.0,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
        ser.dtr = False
        ser.rts = False
    except serial.SerialException as e:
        print(f"无法打开 {args.port}: {e}", file=sys.stderr)
        sys.exit(1)

    stop = threading.Event()
    rx = threading.Thread(target=rx_loop, args=(ser, stop), daemon=True)
    rx.start()

    print(
        f"已打开 {args.port} @ {args.baud} 8N1，DTR/RTS 已拉低。\n"
        "设备日志在上方 stdout 实时打印；请在下方提示符后输入命令。\n"
        "空行 = 只发回车；exit / quit = 退出。\n"
        "（日志与输入可能交错，属串口助手常态。）\n",
        file=sys.stderr,
        flush=True,
    )

    try:
        if args.raw:

            def raw_tx() -> None:
                try:
                    while not stop.is_set():
                        c = sys.stdin.buffer.read(1)
                        if not c:
                            stop.set()
                            break
                        ser.write(c)
                        ser.flush()
                except (BrokenPipeError, OSError, serial.SerialException):
                    stop.set()

            raw_tx()
        else:
            while not stop.is_set():
                try:
                    sys.stderr.write(args.prompt)
                    sys.stderr.flush()
                    line = sys.stdin.readline()
                except KeyboardInterrupt:
                    print("\n[Ctrl+C]", file=sys.stderr)
                    break
                if line == "":
                    break
                text = line.rstrip("\r\n")
                low = text.strip().lower()
                if low in ("exit", "quit", ":q"):
                    break
                payload = text.encode("utf-8", errors="replace") + eol_b
                ser.write(payload)
                ser.flush()
    finally:
        stop.set()
        ser.close()
        print("[已关闭串口]", file=sys.stderr)


if __name__ == "__main__":
    main()
