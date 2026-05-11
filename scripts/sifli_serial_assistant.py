#!/usr/bin/env python3
"""
交互式串口助手：后台线程打印设备输出，主线程将终端按键发到 MSH/Finsh。
默认 8N1、无流控；打开后拉低 DTR/RTS（与 batch 脚本一致，避免 CDC 误复位）。

默认（透传）：stdin 设为 cbreak；按键原样发到串口。可选「智能本地回显」：可打印字符、
回车、退格在终端显示，Tab 与方向键等 ANSI 序列不回显（避免屏幕上出现 ^[[A 乱码）。
若 MSH 也会回显字符，可能双重显示，请加 --no-local-echo。Ctrl+C 退出。

  python3 scripts/sifli_serial_assistant.py
  python3 scripts/sifli_serial_assistant.py -p /dev/ttyACM0 -b 1000000

--line：恢复旧版「本地逐行编辑」，回车整行发送（Tab/方向键仍由本机处理，到不了 MSH）。

  python3 scripts/sifli_serial_assistant.py --line

--raw：与默认透传相同（保留作兼容别名）。
"""
from __future__ import annotations

import argparse
import sys
import threading

try:
    import termios
    import tty

    _HAVE_TERMIOS = True
except ImportError:
    _HAVE_TERMIOS = False

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


def _local_echo_byte(b: int, st: list[int], *, enabled: bool) -> None:
    """按字节更新 ANSI 解析状态，并在 enabled 时对「非 Tab / 非方向键类序列」回显到 stdout。"""
    if not enabled:
        return
    s = st[0]
    if s == 0:
        if b == 9:  # Tab：转发但不回显
            return
        if b == 27:  # ESC，进入序列
            st[0] = 1
            return
        if b in (8, 127):  # BS / DEL：擦掉本地刚显示的一个字符
            sys.stdout.buffer.write(b"\x08 \x08")
            sys.stdout.buffer.flush()
            return
        if 32 <= b <= 126 or b in (10, 13):  # 可打印 + 换行
            sys.stdout.buffer.write(bytes((b,)))
            sys.stdout.buffer.flush()
        return
    if s == 1:  # ESC 后一字节
        if b == ord("["):
            st[0] = 2  # CSI，方向键多为 ESC [ A / B / C / D
        elif b == ord("O"):
            st[0] = 3  # SS3，部分终端为 ESC O A 等
        else:
            st[0] = 0
        return
    if s == 2:  # CSI：直到 final byte 0x40–0x7E
        if 0x40 <= b <= 0x7E:
            st[0] = 0
        return
    if s == 3:  # SS3：再吞一字节即结束
        st[0] = 0
        return


def stdin_passthrough_loop(
    ser: serial.Serial,
    stop: threading.Event,
    *,
    local_echo: bool = True,
) -> None:
    """逐字节转发 stdin → 串口（需已 cbreak）；可选对 Tab/方向键以外按键做本地回显。"""
    st = [0]
    try:
        while not stop.is_set():
            c = sys.stdin.buffer.read(1)
            if not c:
                stop.set()
                break
            b = c[0]
            ser.write(c)
            ser.flush()
            _local_echo_byte(b, st, enabled=local_echo)
    except (BrokenPipeError, OSError, serial.SerialException):
        stop.set()


def main() -> None:
    p = argparse.ArgumentParser(description="串口助手：MSH/Finsh 调试（默认按键透传）")
    p.add_argument("-p", "--port", default="/dev/ttyACM0")
    p.add_argument("-b", "--baud", type=int, default=1000000)
    p.add_argument(
        "--eol",
        choices=("crlf", "lf", "cr"),
        default="crlf",
        help="仅 --line：行尾（MSH 建议 crlf）",
    )
    p.add_argument(
        "--prompt",
        default="",
        help="仅 --line：输入提示符（写到 stderr）",
    )
    p.add_argument(
        "--line",
        action="store_true",
        help="逐行模式：本地编辑后回车发送（Tab/方向键不到 MSH）",
    )
    p.add_argument(
        "--raw",
        action="store_true",
        help="与默认相同：按键透传（保留别名）",
    )
    p.add_argument(
        "--no-local-echo",
        action="store_true",
        help="透传时不在本机回显（仅依赖设备回显；避免与 MSH 回显重复）",
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

    use_passthrough = not args.line
    if use_passthrough and not _HAVE_TERMIOS:
        print(
            "当前平台无 termios，无法做按键透传，已改用 --line 行为。",
            file=sys.stderr,
            flush=True,
        )
        use_passthrough = False
    if use_passthrough and not sys.stdin.isatty():
        print(
            "stdin 不是 TTY（如管道输入），已改用 --line 行为。",
            file=sys.stderr,
            flush=True,
        )
        use_passthrough = False

    local_echo = use_passthrough and not args.no_local_echo

    if use_passthrough:
        echo_note = (
            "已开启本地回显（Tab/方向键不回显）；若与设备回显重复请加 --no-local-echo。"
            if local_echo
            else "未开本地回显，显示依赖设备。"
        )
        print(
            f"已打开 {args.port} @ {args.baud} 8N1，DTR/RTS 已拉低。\n"
            f"透传模式：{echo_note}\n"
            "Ctrl+C 退出。（日志与输入可能交错，属常态。）\n",
            file=sys.stderr,
            flush=True,
        )
    else:
        print(
            f"已打开 {args.port} @ {args.baud} 8N1，DTR/RTS 已拉低。\n"
            "逐行模式：提示符后输入命令，回车发送；空行 = 只发回车；exit / quit = 退出。\n"
            "（日志与输入可能交错，属常态。）\n",
            file=sys.stderr,
            flush=True,
        )

    stdin_fd = sys.stdin.fileno()
    old_tty: list | None = None

    try:
        if use_passthrough:
            old_tty = termios.tcgetattr(stdin_fd)
            tty.setcbreak(stdin_fd)
            try:
                stdin_passthrough_loop(ser, stop, local_echo=local_echo)
            except KeyboardInterrupt:
                print("\n[Ctrl+C]", file=sys.stderr)
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
        if old_tty is not None:
            try:
                termios.tcsetattr(stdin_fd, termios.TCSAFLUSH, old_tty)
            except OSError:
                pass
        ser.close()
        print("[已关闭串口]", file=sys.stderr)


if __name__ == "__main__":
    main()
