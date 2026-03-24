#!/usr/bin/env bash
# 一键：编译烧录 + 自动向串口发送 GH3018 调试命令（无需人工开串口工具）
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PORT="${SIFLI_SERIAL_PORT:-/dev/ttyACM0}"
BAUD="${SIFLI_SERIAL_BAUD:-1000000}"

echo "=== 1/2 编译与烧录 (make.sh) ==="
bash "$ROOT/make.sh"

echo ""
echo "=== 等待 USB 串口重新就绪 (${PORT}) ==="
for i in $(seq 1 30); do
  if [[ -e "$PORT" ]]; then
    sleep 1
    break
  fi
  sleep 0.5
done
sleep 2

BATCH="$ROOT/.cursor/skills/sifli-sf32-watch-sdk/scripts/sifli_serial_batch.py"
if [[ ! -f "$BATCH" ]]; then
  echo "缺少 $BATCH" >&2
  exit 1
fi

echo "=== 2/2 串口自动执行 GH3018：open → 心率 → peek → stop ==="
# --min-capture：首条等 init；第二条至少采集这么久再因静默结束（需佩戴/贴合传感器才易出 HR）
python3 "$BATCH" -p "$PORT" -b "$BAUD" --boot-wait 1 --settle 4 \
  --min-capture "10,50,0,0" --max-wait 90 --silence 1.2 ${SIFLI_SERIAL_PULSE_DTR:+--pulse-dtr} \
  "hbd -open" \
  "hbd -hb" \
  "hbd -peek" \
  "hbd -stop"

echo ""
echo "=== 完成。交互调试请用: python3 \"$ROOT/scripts/sifli_serial_assistant.py\" ==="
