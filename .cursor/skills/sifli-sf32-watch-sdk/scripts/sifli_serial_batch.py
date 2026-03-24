#!/usr/bin/env python3
"""
非交互串口：依次发送 Finsh/MSH 命令并打印回显。
默认端口与波特率：与仓库 make.sh（sftool）及 build_sf32lb52-watch_hcpu/rtconfig.h
中 RT_SERIAL_DEFAULT_BAUDRATE（1000000）一致。
"""
from __future__ import annotations

import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("缺少 pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(1)


def read_until_idle(
    ser: serial.Serial,
    silence: float,
    max_total: float,
    early_no_data_exit: float | None = None,
    min_elapsed_before_silence_break: float = 0.0,
) -> bytes:
    """
    读到「一段」响应：在收到至少一个字节后，若 silence 秒内无新数据则结束。
    若从未收到字节，则一直等到 max_total（避免把「等设备首字节」误判为静默结束）。

    early_no_data_exit: 若设置，在「从未收到任何字节」时，经过该秒数后提前结束（用于丢弃唤醒期缓冲）。
    min_elapsed_before_silence_break: 从本函数开始至少经过该秒数后，才允许因「静默」结束；
        用于 gh30x 初始化等长时间无 UART 输出的阻塞调用，避免误截断。
    """
    buf = b""
    start = time.monotonic()
    last_rx = time.monotonic()
    saw_any = False
    while time.monotonic() - start < max_total:
        n = ser.in_waiting
        if n > 0:
            chunk = ser.read(n)
        else:
            # 无缓冲时短阻塞，便于慢速首包与 CDC 驱动及时性
            chunk = ser.read(1)
        if chunk:
            buf += chunk
            last_rx = time.monotonic()
            saw_any = True
        else:
            if saw_any and (time.monotonic() - last_rx >= silence):
                if (time.monotonic() - start) >= min_elapsed_before_silence_break:
                    break
            if (
                not saw_any
                and early_no_data_exit is not None
                and (time.monotonic() - start >= early_no_data_exit)
            ):
                break
    return buf


def sniff_port(ser: serial.Serial, seconds: float) -> None:
    """连续读取若干秒原始字节到 stdout（用于确认是否有日志流）。"""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        n = ser.in_waiting
        chunk = ser.read(n if n > 0 else 1)
        if chunk:
            sys.stdout.buffer.write(chunk)
            sys.stdout.flush()


def open_serial(port: str, baud: int, retries: int, delay: float) -> serial.Serial:
    """
    打开后立即拉低 DTR/RTS，避免 Linux CDC 默认拉高导致 MCU 复位或线路状态异常，
    进而出现「只有日志、命令无回显」的现象。
    """
    last_err: Exception | None = None
    for i in range(retries):
        try:
            ser = serial.Serial(
                port=port,
                baudrate=baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.08,
                write_timeout=2.0,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False,
            )
            ser.dtr = False
            ser.rts = False
            return ser
        except serial.SerialException as e:
            last_err = e
            if i + 1 < retries:
                time.sleep(delay)
    raise last_err  # type: ignore[misc]


def main() -> None:
    p = argparse.ArgumentParser(description="批量发送串口命令（Finsh/MSH）")
    p.add_argument("-p", "--port", default="/dev/ttyACM0")
    p.add_argument("-b", "--baud", type=int, default=1000000)
    p.add_argument("--boot-wait", type=float, default=0.0, help="打开串口前先等待秒数（烧录后枚举）")
    p.add_argument("--open-retries", type=int, default=20)
    p.add_argument("--open-delay", type=float, default=0.5)
    p.add_argument(
        "--silence",
        type=float,
        default=1.2,
        help="收到数据后，连续无新数据达该秒数则结束本条命令（日志密集时请加大，默认 1.2）",
    )
    p.add_argument("--max-wait", type=float, default=8.0, help="单条命令最长等待秒数")
    p.add_argument(
        "--post-cmd-wait",
        type=float,
        default=0.15,
        metavar="SEC",
        help="写出命令后先固定等待 SEC 秒再开始采集（给 MSH 调度时间，减轻与日志交错导致的截断）",
    )
    p.add_argument(
        "--min-capture",
        default="",
        metavar="LIST",
        help="逗号分隔、与命令顺序对应的最短采集秒数，如 12,0,0 专给首条阻塞命令；"
        "仅写一个数时只作用于第一条命令，其余为 0",
    )
    p.add_argument(
        "--pulse-dtr",
        action="store_true",
        help="打开串口后拉低再拉高 DTR（部分 USB 转串口可触发 MCU 复位）",
    )
    p.add_argument(
        "--sniff",
        type=float,
        metavar="SEC",
        default=0.0,
        help="在清缓冲/发命令前，先只读串口 SEC 秒并打印（确认是否有持续日志）",
    )
    p.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="在 stderr 打印打开后的串口参数（波特率、8N1、流控）",
    )
    p.add_argument(
        "--settle",
        type=float,
        default=0.5,
        metavar="SEC",
        help="打开串口并置低 DTR/RTS 后等待 SEC 秒再发命令（默认 0.5；烧录后建议 2～5）",
    )
    p.add_argument(
        "--flush-rx",
        action="store_true",
        help="发送命令前丢弃已缓冲的 RX（默认关闭：flush 会导致本板 MSH 无法接收后续命令）",
    )
    p.add_argument(
        "--wake",
        action="store_true",
        help="发命令前先发送一次回车，便于出现 msh 提示符（默认关闭）",
    )
    p.add_argument(
        "--eol",
        choices=("crlf", "lf", "cr"),
        default="crlf",
        help="命令行尾（MSH 建议 crlf，默认 crlf）",
    )
    p.add_argument(
        "commands",
        nargs="*",
        help="Finsh 命令列表；可与 --sniff 组合；仅 sniff 时可留空",
    )
    args = p.parse_args()

    if args.sniff <= 0 and not args.commands:
        p.error("请至少指定一条命令，或使用 --sniff SEC")

    def min_capture_list(spec: str, ncmds: int) -> list[float]:
        if not (spec or "").strip():
            return [0.0] * ncmds
        parts = [float(x.strip()) for x in spec.split(",") if x.strip() != ""]
        if not parts:
            return [0.0] * ncmds
        if len(parts) == 1 and ncmds > 1:
            out = [0.0] * ncmds
            out[0] = parts[0]
            return out
        while len(parts) < ncmds:
            parts.append(0.0)
        return parts[:ncmds]

    min_caps = min_capture_list(args.min_capture, len(args.commands))

    if args.boot_wait > 0:
        time.sleep(args.boot_wait)

    try:
        ser = open_serial(args.port, args.baud, args.open_retries, args.open_delay)
    except serial.SerialException as e:
        print(f"无法打开 {args.port}: {e}", file=sys.stderr)
        sys.exit(1)

    if args.verbose:
        print(
            f"[serial] port={ser.port!s} baud={ser.baudrate} "
            f"bytesize={ser.bytesize} parity={ser.parity} stopbits={ser.stopbits} "
            f"rtscts={ser.rtscts} xonxoff={ser.xonxoff} dsrdtr={getattr(ser, 'dsrdtr', None)}",
            file=sys.stderr,
            flush=True,
        )

    eol_map = {"crlf": b"\r\n", "lf": b"\n", "cr": b"\r"}
    eol_bytes = eol_map[args.eol]

    try:
        if args.pulse_dtr:
            ser.dtr = False
            time.sleep(0.15)
            ser.dtr = True
            time.sleep(0.5)
            ser.dtr = False
            ser.rts = False
        if args.sniff > 0:
            print(f"[serial] sniff {args.sniff}s raw RX -> stdout", file=sys.stderr, flush=True)
            sniff_port(ser, args.sniff)
        if not args.commands:
            return

        if args.settle > 0:
            time.sleep(args.settle)

        if args.flush_rx:
            ser.reset_input_buffer()

        if args.wake:
            ser.write(eol_bytes)
            ser.flush()
            time.sleep(0.05)
            read_until_idle(ser, silence=0.2, max_total=1.0, early_no_data_exit=0.3)

        for idx, cmd in enumerate(args.commands):
            print(f"\n### >> {cmd}", file=sys.stderr, flush=True)
            ser.write(cmd.strip().encode("utf-8", errors="replace") + eol_bytes)
            ser.flush()
            if args.post_cmd_wait > 0:
                time.sleep(args.post_cmd_wait)
            chunk = read_until_idle(
                ser,
                silence=args.silence,
                max_total=args.max_wait,
                min_elapsed_before_silence_break=min_caps[idx],
            )
            if not chunk:
                print(
                    "(本条命令未收到任何字节：勿使用 --flush-rx；可加大 --settle 或试 --wake)",
                    file=sys.stderr,
                    flush=True,
                )
            sys.stdout.buffer.write(chunk)
            sys.stdout.flush()
    finally:
        ser.close()


if __name__ == "__main__":
    main()
