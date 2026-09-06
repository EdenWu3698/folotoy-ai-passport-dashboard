#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
PYTHON_BIN="${AI_PASSPORT_PYTHON:-$(command -v python3 || true)}"

if [[ -z "$PYTHON_BIN" || ! -x "$PYTHON_BIN" ]]; then
  echo "需要 Python 3.11 或更高版本。" >&2
  exit 1
fi

if [[ ! -d "$SCRIPT_DIR/.venv" ]]; then
  "$PYTHON_BIN" -m venv "$SCRIPT_DIR/.venv"
fi
"$SCRIPT_DIR/.venv/bin/python" -m pip install --upgrade pip
"$SCRIPT_DIR/.venv/bin/python" -m pip install -r "$SCRIPT_DIR/requirements.txt"

if [[ ! -f "$SCRIPT_DIR/config.json" ]]; then
  cp "$SCRIPT_DIR/config.example.json" "$SCRIPT_DIR/config.json"
  echo "已创建 config.json，请先按需修改个人资料。"
fi

echo "安装完成。先运行："
echo "  $SCRIPT_DIR/run.sh preview"
echo "然后打开设备并运行："
echo "  $SCRIPT_DIR/run.sh sync"
