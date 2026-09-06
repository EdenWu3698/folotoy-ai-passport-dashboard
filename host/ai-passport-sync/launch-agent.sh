#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
LABEL="com.folotoy.ai-passport-sync"
RUNTIME_DIR="$HOME/Library/Application Support/AI Passport Sync"
PLIST_PATH="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG_DIR="$RUNTIME_DIR/logs"
ACTION="${1:-install}"

install_agent() {
  mkdir -p "$RUNTIME_DIR" "$LOG_DIR" "${PLIST_PATH:h}"
  cp "$SCRIPT_DIR/passport_helper.py" "$SCRIPT_DIR/requirements.txt" "$SCRIPT_DIR/run.sh" "$RUNTIME_DIR/"
  if [[ -f "$SCRIPT_DIR/config.json" ]]; then
    cp "$SCRIPT_DIR/config.json" "$RUNTIME_DIR/config.json"
  elif [[ ! -f "$RUNTIME_DIR/config.json" ]]; then
    cp "$SCRIPT_DIR/config.example.json" "$RUNTIME_DIR/config.json"
  fi

  local python_bin="${AI_PASSPORT_PYTHON:-$(command -v python3 || true)}"
  if [[ -z "$python_bin" || ! -x "$python_bin" ]]; then
    echo "找不到 Python 3。" >&2
    exit 1
  fi
  if [[ ! -d "$RUNTIME_DIR/.venv" ]]; then
    "$python_bin" -m venv "$RUNTIME_DIR/.venv"
  fi
  "$RUNTIME_DIR/.venv/bin/python" -m pip install -r "$RUNTIME_DIR/requirements.txt"

  "$RUNTIME_DIR/.venv/bin/python" - "$PLIST_PATH" "$LABEL" "$RUNTIME_DIR" "$LOG_DIR" <<'PY'
import plistlib
import sys

path, label, runtime, logs = sys.argv[1:]
payload = {
    "Label": label,
    "ProgramArguments": [
        f"{runtime}/run.sh", "watch", "--interval", "15", "--full-interval", "300"
    ],
    "WorkingDirectory": runtime,
    "RunAtLoad": True,
    "KeepAlive": True,
    "ThrottleInterval": 10,
    "StandardOutPath": f"{logs}/stdout.log",
    "StandardErrorPath": f"{logs}/stderr.log",
}
with open(path, "wb") as handle:
    plistlib.dump(payload, handle)
PY
  launchctl bootout "gui/$UID/$LABEL" >/dev/null 2>&1 || true
  launchctl bootstrap "gui/$UID" "$PLIST_PATH"
  echo "后台同步已安装：配额每 15 秒，完整资料每 5 分钟。"
  echo "配置：$RUNTIME_DIR/config.json"
}

uninstall_agent() {
  launchctl bootout "gui/$UID/$LABEL" >/dev/null 2>&1 || true
  [[ ! -f "$PLIST_PATH" ]] || mv "$PLIST_PATH" "$HOME/.Trash/$LABEL.plist.$(date +%s)"
  echo "后台任务已移除；运行目录和配置仍保留在：$RUNTIME_DIR"
}

case "$ACTION" in
  install|restart) install_agent ;;
  uninstall) uninstall_agent ;;
  status) launchctl print "gui/$UID/$LABEL" ;;
  *) echo "用法：$0 [install|restart|status|uninstall]" >&2; exit 2 ;;
esac
