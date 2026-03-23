#!/usr/bin/env python3
import sys
import re
import argparse
import pandas as pd
import numpy as np

import time

import csv
from datetime import datetime
from collections import deque

# Try to import serial, but don't fail immediately if not present (only fail if used)
try:
    import serial
except ImportError:
    serial = None

# 预编译正则表达式，提高效率
VOLTAGE_PATTERN = re.compile(r'(?:adc value|voltage)[:=]\s*(\d+)', re.IGNORECASE)
STATUS_PATTERN = re.compile(r'Current status:\s*(\w+)', re.IGNORECASE)

def parse_lines(line_iterator, status_filter, is_live_stream=False, sample_interval=0, csv_writer=None):
    """
    从行迭代器中解析电压值。
    status_filter: "Charging" 或 "Discharging"
    is_live_stream: 是否为实时流（串口），如果是，则会有不同的交互提示。
    sample_interval: 采样间隔（秒），0 表示不限制。仅在实时流模式下有效。
    csv_writer: 可选的 CSV 写入对象，用于实时备份数据
    """
    voltages = []
    
    current_status = None
    collecting = False # 标记是否开始收集数据
    last_sample_time = 0 # 上次采样时间
    
    # 稳定检测参数：在一个窗口内的电压波动范围小于阈值，则视为稳定
    # STABLE_THRESHOLD_COUNT: 用于检测稳定的滑动窗口大小 (例如：600个点)
    # STABLE_VOLTAGE_DIFF: 窗口内最大电压和最小电压的差值阈值 (单位: 0.1mV)
    STABLE_THRESHOLD_COUNT = 600 
    STABLE_VOLTAGE_DIFF = 20 # 单位 0.1mV, 即 2mV
    
    # 使用双端队列作为滑动窗口
    stable_window = deque(maxlen=STABLE_THRESHOLD_COUNT)

    if is_live_stream:
        print(f"等待检测状态: {status_filter}...")
        if sample_interval > 0:
            print(f"采样间隔已设置为: {sample_interval} 秒")
    
    try:
        for line in line_iterator:
            line = line.strip()
            if not line:
                continue

            # 检查状态变更
            status_match = STATUS_PATTERN.search(line)
            if status_match:
                new_status = status_match.group(1)
                
                # 状态机逻辑
                if status_filter.lower() in new_status.lower():
                    # 进入了目标状态，开始收集
                    if not collecting:
                        print(f"\n[检测到开始状态]: {new_status}")
                        print(f"--> 开始收集数据... (当前电压点数: {len(voltages)})")
                        collecting = True
                        current_status = new_status
                else:
                    # 进入了非目标状态
                    if collecting:
                        print(f"\n[检测到状态变更]: {new_status}")
                        print("--> 停止收集数据。")
                        break 
                    current_status = new_status
                    if is_live_stream and not collecting:
                         # 实时流中，打印当前状态提示用户
                         print(f"当前状态: {new_status} (等待 {status_filter})...", end='\r')
            
            # 如果一开始就在目标状态（即没有检测到状态切换），也应该开始收集
            if not collecting and current_status is None:
                pass

            # 提取电压
            if collecting:
                match = VOLTAGE_PATTERN.search(line)
                if match:
                    # 采样间隔控制 (仅在实时流模式下生效)
                    if is_live_stream and sample_interval > 0:
                        current_time = time.time()
                        if current_time - last_sample_time < sample_interval:
                            continue # 跳过此次采样
                        last_sample_time = current_time

                    voltage = int(match.group(1))
                    if 25000 <= voltage <= 45000:
                        voltages.append(voltage)
                        
                        # 实时备份数据
                        if csv_writer:
                            csv_writer.writerow([datetime.now().strftime('%Y-%m-%d %H:%M:%S'), voltage, current_status])
                        
                        # 稳定检测逻辑 (使用滑动窗口)
                        if is_live_stream:
                            stable_window.append(voltage)
                            
                            # 仅当窗口填满后才开始检测
                            if len(stable_window) == STABLE_THRESHOLD_COUNT:
                                min_v = min(stable_window)
                                max_v = max(stable_window)
                                
                                if (max_v - min_v) <= STABLE_VOLTAGE_DIFF:
                                    print(f"\n[检测到电压稳定]: 在过去 {STABLE_THRESHOLD_COUNT} 个数据点中，电压波动范围小于或等于 {STABLE_VOLTAGE_DIFF / 10.0}mV")
                                    print("--> 判定充/放电完成，自动停止收集。")
                                    
                                    # 裁剪掉稳定窗口内的数据，只保留一个作为结束点
                                    voltages = voltages[:-STABLE_THRESHOLD_COUNT]
                                    voltages.append(voltage) # 把当前电压加回去
                                    print(f"--> 已自动裁剪尾部稳定数据 (保留最后 {len(voltages)} 个点)")
                                    break
                            
                            # 更新UI
                            print(f"已收集 {len(voltages)} 个数据点 (最新: {voltage/10000:.4f}V, 稳定窗口填充: {len(stable_window)}/{STABLE_THRESHOLD_COUNT})...", end='\r')

    except KeyboardInterrupt:
        print("\n用户中断收集。")
    
    return voltages

def parse_log_file(log_file, status_filter):
    print(f"正在解析日志文件: {log_file} ...")
    with open(log_file, 'r', encoding='utf-8', errors='ignore') as f:
        voltages = parse_lines(f, status_filter, is_live_stream=False)
    
    # 文件模式下的回退逻辑：如果没找到状态标记，尝试全量提取
    if not voltages:
        print("警告：未在日志中检测到明确的状态起始标记 (Current status: ...)")
        print("尝试提取所有符合范围的电压数据...")
        voltages = []
        with open(log_file, 'r', encoding='utf-8', errors='ignore') as f_fallback:
            for line in f_fallback:
                 match = VOLTAGE_PATTERN.search(line)
                 if match:
                    voltage = int(match.group(1))
                    if 25000 <= voltage <= 45000:
                        voltages.append(voltage)
    return voltages

def parse_serial_port(port, baudrate, status_filter, sample_interval=0):
    if not serial:
        print("错误：未安装 pyserial 库。请运行: pip install pyserial")
        sys.exit(1)
        
    print(f"正在连接串口: {port} @ {baudrate} ...")
    print("提示：按 Ctrl+C 可以随时停止并使用已收集的数据生成曲线。")
    
    # 生成备份文件名
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    backup_file = f"battery_backup_{status_filter}_{timestamp}.csv"
    print(f"数据将实时备份到: {backup_file}")
    
    f_csv = open(backup_file, 'w', newline='', encoding='utf-8')
    writer = csv.writer(f_csv)
    writer.writerow(['Timestamp', 'Voltage(0.1mV)', 'Status'])
    
    try:
        with serial.Serial(port, baudrate, timeout=1) as ser:
            # 定义一个生成器，持续读取串口行
            def serial_lines():
                while True:
                    try:
                        line_bytes = ser.readline()
                        if line_bytes:
                            line = line_bytes.decode('utf-8', errors='replace')
                            # 可选：打印原始日志以便调试
                            # print(line.strip()) 
                            yield line
                    except Exception as e:
                        print(f"读取错误: {e}")
                        break
            
            voltages = parse_lines(serial_lines(), status_filter, is_live_stream=True, sample_interval=sample_interval, csv_writer=writer)
            
            # 确保数据写入磁盘
            f_csv.flush()
            return voltages
            
    except serial.SerialException as e:
        print(f"串口错误: {e}")
        return []
    finally:
        f_csv.close()

def generate_curve(voltages, is_charging):
    """
    根据采集到的电压列表生成 0-100% 的曲线表。
    """
    if not voltages:
        print("错误：未提取到有效的电压数据。")
        return None

    # 数据平滑处理（移动平均）
    window_size = 20
    if len(voltages) > window_size:
        # 使用居中窗口，并移除因窗口计算产生的 NaN 值
        voltages_smooth = pd.Series(voltages).rolling(window=window_size, center=True).mean().dropna().tolist()
    else:
        voltages_smooth = voltages

    total_points = len(voltages_smooth)
    if total_points < 101:
        print(f"警告：数据点过少 ({total_points})，生成的曲线可能不平滑。建议增加采样点。")
        # 如果点数过少，直接使用原始点进行插值，避免过度平滑导致数据失真
        voltages_smooth = voltages
        total_points = len(voltages_smooth)

    # 核心逻辑：生成一个统一的、电压从高到低排序的电压->百分比映射表
    # 这个表代表了电池在不同电压下的“健康状态”
    
    # 1. 无论充放电，都将电压从高到低排序，作为我们曲线的基础
    # 这代表了从 100% 电量到 0% 电量的电压状态
    curve_voltages = sorted(voltages_smooth, reverse=True)

    # 2. 创建一个代表“时间”或“容量”的线性序列 (0% -> 100%)
    # 我们假设采集到的点在时间上是均匀的
    time_percent_map = np.linspace(0, 100, total_points)

    # 3. 根据充放电模式，决定如何将时间/容量百分比映射到排序后的电压上
    if is_charging:
        # 充电过程：时间 0% 对应低电压，时间 100% 对应高电压
        # 由于 curve_voltages 是高->低排序，我们需要将时间百分比反转来匹配
        # 即：高电压(curve_voltages[0]) -> 时间100%, 低电压(curve_voltages[-1]) -> 时间0%
        voltage_to_time_percent = {int(vol): pct for vol, pct in zip(curve_voltages, reversed(time_percent_map))}
    else:
        # 放电过程：时间 0% 对应高电压，时间 100% 对应低电压
        # curve_voltages 的顺序与时间顺序一致
        # 即：高电压(curve_voltages[0]) -> 时间0%, 低电压(curve_voltages[-1]) -> 时间100%
        voltage_to_time_percent = {int(vol): pct for vol, pct in zip(curve_voltages, time_percent_map)}

    # 4. 生成最终的 101 点 C 语言表 (100% -> 0%)
    # 我们需要找到与 100, 99, ..., 0 每个百分比最接近的电压值
    curve_table = []
    
    # 为了查找方便，反转映射：时间百分比 -> 电压
    time_percent_to_voltage = {pct: vol for vol, pct in voltage_to_time_percent.items()}
    
    # 获取所有记录的时间百分比，并排序
    recorded_percents = sorted(time_percent_to_voltage.keys())

    for i in range(101):
        target_percent = 100 - i
        
        # 使用 numpy.interp 进行线性插值，找到目标百分比对应的电压
        # np.interp(查询点, 已知点x, 已知点y)
        # 这里，我们用“时间百分比”作为x轴，电压作为y轴
        # 注意：np.interp 要求 x 轴（第二个参数）是单调递增的。
        
        # 放电时，recorded_percents 是 0->100, 电压是 高->低
        # 充电时，recorded_percents 是 0->100, 电压是 低->高
        # 所以需要根据模式调整y轴
        
        if is_charging:
            # 充电：时间百分比 0->100 对应电压 低->高
            y_values = sorted(time_percent_to_voltage.values())
        else:
            # 放电：时间百分比 0->100 对应电压 高->低
            y_values = sorted(time_percent_to_voltage.values(), reverse=True)

        # 插值
        interpolated_voltage = np.interp(target_percent, recorded_percents, y_values)
        
        curve_table.append((target_percent, int(interpolated_voltage)))

    return curve_table

def print_c_array(curve_table, table_name, output_file=None):
    """
    打印 C 语言格式的数组，如果指定了 output_file 则同时写入文件。
    """
    output_lines = []
    output_lines.append(f"\n// 生成的 C 语言代码片段 - {table_name}")
    output_lines.append(f"const battery_lookup_point_t {table_name}[] =")
    output_lines.append("{")
    for percent, voltage in curve_table:
        output_lines.append(f"    {{ {percent:>3}, {voltage:>5} }},  // {voltage/10000:.4f}V")
    output_lines.append("};")
    
    # 打印到控制台
    for line in output_lines:
        print(line)
        
    # 写入文件
    if output_file:
        try:
            with open(output_file, 'w', encoding='utf-8') as f:
                for line in output_lines:
                    f.write(line + '\n')
            print(f"\n[成功] C 代码已保存到文件: {output_file}")
        except Exception as e:
            print(f"\n[错误] 保存文件失败: {e}")

def main():
    parser = argparse.ArgumentParser(description='电池曲线生成工具')
    parser.add_argument('--logfile', help='包含电压日志的文件路径')
    parser.add_argument('--port', help='串口端口 (例如 COM3 或 /dev/ttyUSB0)')
    parser.add_argument('--baud', type=int, default=1000000, help='波特率 (默认: 1000000)')
    parser.add_argument('--mode', choices=['charge', 'discharge'], required=True, help='生成充电曲线还是放电曲线')
    parser.add_argument('--interval', type=int, default=0, help='采样间隔 (秒)，默认 0 (实时采集)')
    parser.add_argument('--output', help='保存 C 代码的文件路径 (默认自动生成文件名)')
    
    args = parser.parse_args()
    
    if not args.logfile and not args.port:
        parser.error("必须指定 --logfile 或 --port 其中之一")
    
    is_charging = (args.mode == 'charge')
    status_str = "Charging" if is_charging else "Discharging"
    table_name = "charging_curve_table" if is_charging else "discharge_curve_table"
    
    # 自动生成输出文件名
    if not args.output:
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        args.output = f"battery_curve_{status_str}_{timestamp}.c"

    if args.port:
        voltages = parse_serial_port(args.port, args.baud, status_str, args.interval)
    else:
        voltages = parse_log_file(args.logfile, status_str)
    
    if not voltages:
        print("\n未找到有效数据。")
        return
        
    print(f"\n提取到 {len(voltages)} 个电压数据点。")
    print(f"电压范围: {min(voltages)} - {max(voltages)}")
    
    curve = generate_curve(voltages, is_charging)
    
    if curve:
        print_c_array(curve, table_name, args.output)

if __name__ == "__main__":
    main()
