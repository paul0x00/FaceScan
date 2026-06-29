#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LOG_DIR="$ROOT_DIR/Log"
BACKEND_PID_FILE="$LOG_DIR/facescan-backend.pid"
FRONTEND_PID_FILE="$LOG_DIR/facescan-frontend.pid"

stop_process() {
  local name="$1"
  local pid_file="$2"

  if [[ ! -f "$pid_file" ]]; then
    echo "$name is not running."
    return
  fi

  local pid
  pid="$(cat "$pid_file")"
  if [[ -z "$pid" ]]; then
    rm -f "$pid_file"
    echo "$name pid file was empty and has been cleaned up."
    return
  fi

  if kill -0 "$pid" >/dev/null 2>&1; then
    if [[ "$name" == "Backend" ]]; then
      local child_pid=""
      child_pid="$(pgrep -P "$pid" 2>/dev/null | head -n 1 || true)"
      if [[ -n "$child_pid" ]]; then
        kill "$child_pid" >/dev/null 2>&1 || true
      fi
    fi
    kill "$pid" >/dev/null 2>&1 || true
    for _ in 1 2 3 4 5; do
      if ! kill -0 "$pid" >/dev/null 2>&1; then
        break
      fi
      sleep 1
    done
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill -9 "$pid" >/dev/null 2>&1 || true
    fi
    echo "$name stopped."
  elif sudo kill -0 "$pid" >/dev/null 2>&1; then
    if [[ "$name" == "Backend" ]]; then
      local child_pid=""
      child_pid="$(sudo pgrep -P "$pid" 2>/dev/null | head -n 1 || true)"
      if [[ -n "$child_pid" ]]; then
        sudo kill "$child_pid" >/dev/null 2>&1 || true
      fi
    fi
    sudo kill "$pid" >/dev/null 2>&1 || true
    for _ in 1 2 3 4 5; do
      if ! sudo kill -0 "$pid" >/dev/null 2>&1; then
        break
      fi
      sleep 1
    done
    if sudo kill -0 "$pid" >/dev/null 2>&1; then
      sudo kill -9 "$pid" >/dev/null 2>&1 || true
    fi
    echo "$name stopped."
  else
    echo "$name was not running."
  fi

  rm -f "$pid_file"
}

stop_process "Frontend" "$FRONTEND_PID_FILE"
stop_process "Backend" "$BACKEND_PID_FILE"
