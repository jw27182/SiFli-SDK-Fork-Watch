#!/usr/bin/env python3
"""
GH3018 RAW数据采集脚本
从串口读取GH3018传感器的原始PPG数据并保存到文件。

用法:
  python3 scripts/gh3018_capture_raw.py -o output.txt
  python3 scripts/gh3018_capture_raw.py -o output.txt -d 60  # 采集60秒
  python3 scripts/gh3018_capture_raw.py -o output.txt -n 5000  # 采集5000个样本
  python3 scripts/gh3018_capture_raw.py -o output.txt -p /dev/ttyACM1

数据格式 (每行):
  RAW[i]=0xXXXXXXXX ppg=XXXXX gain=X adj=X
"""

from __future__ import annotations

import argparse
import re
import sys
import time
import threading
from datetime import datetime

try:
    import serial
except ImportError:
    print("缺少 pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(1)


class GH3018Capture:
    """GH3018 RAW数据采集器"""
    
    # 匹配RAW数据的正则表达式
    # 格式: RAW[0]=0x187B2301 ppg=63046 gain=0 adj=0
    RAW_PATTERN = re.compile(
        r'RAW\[(\d+)\]=0x([0-9A-Fa-f]+)\s+ppg=(\d+)\s+gain=(\d+)\s+adj=(\d+)'
    )
    
    def __init__(self, port: str, baud: int, output_file: str):
        self.port = port
        self.baud = baud
        self.output_file = output_file
        self.ser = None
        self.stop_event = threading.Event()
        self.raw_lines = []
        self.total_samples = 0
        self.lock = threading.Lock()
        
    def connect(self) -> bool:
        """连接串口"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1,
                write_timeout=2.0,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False,
            )
            self.ser.dtr = False
            self.ser.rts = False
            print(f"已连接: {self.port} @ {self.baud} 8N1", file=sys.stderr)
            return True
        except serial.SerialException as e:
            print(f"无法打开 {self.port}: {e}", file=sys.stderr)
            return False
    
    def disconnect(self):
        """断开串口"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("已关闭串口", file=sys.stderr)
    
    def rx_thread(self):
        """接收线程：读取串口数据并解析RAW数据"""
        buffer = ""
        while not self.stop_event.is_set():
            try:
                if self.ser.in_waiting > 0:
                    chunk = self.ser.read(self.ser.in_waiting)
                    text = chunk.decode('utf-8', errors='replace')
                    buffer += text
                    
                    # 按行处理
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        
                        # 检查是否是RAW数据
                        match = self.RAW_PATTERN.search(line)
                        if match:
                            with self.lock:
                                self.raw_lines.append(line)
                                self.total_samples += 1
                                
                                # 实时显示进度
                                idx = int(match.group(1))
                                if idx == 0:  # 每帧开始时显示
                                    print(f"\r已采集: {self.total_samples} 样本", 
                                          end='', file=sys.stderr)
                else:
                    time.sleep(0.01)
            except (OSError, serial.SerialException):
                self.stop_event.set()
                break
    
    def save_data(self):
        """保存数据到文件"""
        with self.lock:
            lines_to_save = self.raw_lines.copy()
        
        with open(self.output_file, 'w') as f:
            # 写入头部信息
            f.write(f"# GH3018 RAW Data Capture\n")
            f.write(f"# Time: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"# Port: {self.port} @ {self.baud}\n")
            f.write(f"# Samples: {len(lines_to_save)}\n")
            f.write(f"#\n")
            
            for line in lines_to_save:
                f.write(line + '\n')
        
        print(f"\n数据已保存到: {self.output_file}", file=sys.stderr)
        print(f"总样本数: {len(lines_to_save)}", file=sys.stderr)
    
    def capture_by_duration(self, duration_sec: int):
        """按时间采集"""
        print(f"开始采集，持续 {duration_sec} 秒...", file=sys.stderr)
        print("请确保设备正在输出RAW数据（可通过MSH命令开启）", file=sys.stderr)
        print("按 Ctrl+C 提前停止\n", file=sys.stderr)
        
        # 启动接收线程
        rx = threading.Thread(target=self.rx_thread, daemon=True)
        rx.start()
        
        try:
            start_time = time.time()
            while not self.stop_event.is_set():
                elapsed = time.time() - start_time
                remaining = duration_sec - elapsed
                if remaining <= 0:
                    break
                print(f"\r已采集: {self.total_samples} 样本 | "
                      f"剩余: {remaining:.0f}秒", 
                      end='', file=sys.stderr)
                time.sleep(0.5)
        except KeyboardInterrupt:
            print("\n用户中断", file=sys.stderr)
        
        self.stop_event.set()
        rx.join(timeout=2)
        self.save_data()
    
    def capture_by_count(self, target_count: int):
        """按样本数量采集"""
        print(f"开始采集，目标: {target_count} 个样本...", file=sys.stderr)
        print("请确保设备正在输出RAW数据（可通过MSH命令开启）", file=sys.stderr)
        print("按 Ctrl+C 提前停止\n", file=sys.stderr)
        
        # 启动接收线程
        rx = threading.Thread(target=self.rx_thread, daemon=True)
        rx.start()
        
        try:
            while not self.stop_event.is_set():
                with self.lock:
                    current = self.total_samples
                print(f"\r已采集: {current} / {target_count} 样本", 
                      end='', file=sys.stderr)
                if current >= target_count:
                    break
                time.sleep(0.5)
        except KeyboardInterrupt:
            print("\n用户中断", file=sys.stderr)
        
        self.stop_event.set()
        rx.join(timeout=2)
        self.save_data()
    
    def capture_interactive(self):
        """交互式采集（按Enter停止）"""
        print("开始采集...", file=sys.stderr)
        print("请确保设备正在输出RAW数据（可通过MSH命令开启）", file=sys.stderr)
        print("按 Enter 停止采集\n", file=sys.stderr)
        
        # 启动接收线程
        rx = threading.Thread(target=self.rx_thread, daemon=True)
        rx.start()
        
        try:
            input()  # 等待用户按Enter
        except KeyboardInterrupt:
            print("\n用户中断", file=sys.stderr)
        
        self.stop_event.set()
        rx.join(timeout=2)
        self.save_data()


def main():
    parser = argparse.ArgumentParser(
        description="GH3018 RAW数据采集脚本",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s -o data.txt                    # 交互式采集，按Enter停止
  %(prog)s -o data.txt -d 60              # 采集60秒
  %(prog)s -o data.txt -n 5000            # 采集5000个样本
  %(prog)s -o data.txt -p /dev/ttyACM1    # 指定串口
  
数据格式:
  RAW[i]=0xXXXXXXXX ppg=XXXXX gain=X adj=X
        """
    )
    
    parser.add_argument("-o", "--output", required=True,
                        help="输出文件路径")
    parser.add_argument("-p", "--port", default="/dev/ttyACM0",
                        help="串口设备 (默认: /dev/ttyACM0)")
    parser.add_argument("-b", "--baud", type=int, default=1000000,
                        help="波特率 (默认: 1000000)")
    parser.add_argument("-d", "--duration", type=int, default=0,
                        help="采集时间(秒)，0表示手动停止")
    parser.add_argument("-n", "--count", type=int, default=0,
                        help="采集样本数量，0表示手动停止")
    
    args = parser.parse_args()
    
    # 创建采集器
    capture = GH3018Capture(args.port, args.baud, args.output)
    
    # 连接串口
    if not capture.connect():
        sys.exit(1)
    
    try:
        # 根据参数选择采集模式
        if args.duration > 0:
            capture.capture_by_duration(args.duration)
        elif args.count > 0:
            capture.capture_by_count(args.count)
        else:
            capture.capture_interactive()
    finally:
        capture.disconnect()


if __name__ == "__main__":
    main()
