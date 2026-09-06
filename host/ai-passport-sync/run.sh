#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
ENV_PYTHON="${AI_PASSPORT_PYTHON:-$SCRIPT_DIR/.venv/bin/python}"

if [[ ! -x "$ENV_PYTHON" ]]; then
  ENV_PYTHON="$(command -v python3 || true)"
fi
if [[ -z "$ENV_PYTHON" || ! -x "$ENV_PYTHON" ]]; then
  echo "找不到 Python 3。请先运行 ./setup.sh，或设置 AI_PASSPORT_PYTHON。" >&2
  exit 1
fi

if (( $# == 0 )); then
  set -- sync
fi

exec "$ENV_PYTHON" "$SCRIPT_DIR/passport_helper.py" "$@"
