#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LOG_DIR="$ROOT_DIR/Log"
BACKEND_BUILD_DIR="$ROOT_DIR/backend/build"
BACKEND_BIN="$BACKEND_BUILD_DIR/FaceScanBackend"
BACKEND_PID_FILE="$LOG_DIR/facescan-backend.pid"
FRONTEND_PID_FILE="$LOG_DIR/facescan-frontend.pid"
BACKEND_LOG_FILE="$LOG_DIR/facescan-backend.log"
FRONTEND_LOG_FILE="$LOG_DIR/facescan-frontend.log"
BACKEND_PORT="${BACKEND_PORT:-8080}"
FRONTEND_HOST="${FRONTEND_HOST:-127.0.0.1}"
FRONTEND_PORT="${FRONTEND_PORT:-5173}"
BACKEND_AS_ROOT="${BACKEND_AS_ROOT:-auto}"

ensure_sudo() {
  if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
    return
  fi

  sudo -v
}

is_running() {
  local pid_file="$1"
  if [[ ! -f "$pid_file" ]]; then
    return 1
  fi

  local pid
  pid="$(cat "$pid_file")"
  if [[ ! "$pid" =~ ^[0-9]+$ ]]; then
    rm -f "$pid_file"
    return 1
  fi

  if kill -0 "$pid" >/dev/null 2>&1; then
    return 0
  fi

  if sudo -n kill -0 "$pid" >/dev/null 2>&1; then
    return 0
  fi

  rm -f "$pid_file"
  return 1
}

listener_pid() {
  local port="$1"
  lsof -tiTCP:"$port" -sTCP:LISTEN 2>/dev/null | head -n 1 || true
}

fail_if_port_in_use() {
  local name="$1"
  local port="$2"
  local pid
  pid="$(listener_pid "$port")"
  if [[ -z "$pid" ]]; then
    return
  fi

  local command
  command="$(ps -p "$pid" -o command= 2>/dev/null || true)"
  echo "Cannot start $name: port $port is already used by PID $pid${command:+ ($command)}."
  echo "Stop the existing process or choose another port."
  exit 1
}

configured_camera_mode() {
  local config_file
  for config_file in \
    "$ROOT_DIR/backend/config/app.json" \
    "$ROOT_DIR/backend/config/app.example.json"; do
    if [[ -f "$config_file" ]]; then
      local mode
      mode="$(sed -nE 's/.*"cameraMode"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/p' "$config_file" | head -n 1)"
      if [[ -n "$mode" ]]; then
        printf '%s\n' "$mode"
        return
      fi
    fi
  done
  printf '%s\n' "mock"
}

should_run_backend_as_root() {
  case "$BACKEND_AS_ROOT" in
    1|true|yes)
      return 0
      ;;
    0|false|no)
      return 1
      ;;
    auto)
      [[ "$(configured_camera_mode)" != "mock" ]]
      ;;
    *)
      echo "Invalid BACKEND_AS_ROOT value: $BACKEND_AS_ROOT (expected auto, 1, or 0)."
      exit 1
      ;;
  esac
}

quote_shell_arg() {
  printf "%q" "$1"
}

wait_for_url() {
  local url="$1"
  local attempts="${2:-20}"
  local i
  for ((i = 0; i < attempts; ++i)); do
    if curl --fail --silent --show-error "$url" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.25
  done
  return 1
}

show_log_tail() {
  local log_file="$1"
  if [[ -f "$log_file" ]]; then
    echo "--- ${log_file#$ROOT_DIR/} ---"
    tail -n 20 "$log_file"
  fi
}

start_backend() {
  if is_running "$BACKEND_PID_FILE"; then
    echo "Backend is already running with PID $(cat "$BACKEND_PID_FILE")."
    return
  fi

  fail_if_port_in_use "backend" "$BACKEND_PORT"

  local cmake_args=(-S "$ROOT_DIR/backend" -B "$BACKEND_BUILD_DIR" -G Ninja)
  if [[ -n "${VCPKG_ROOT:-}" && -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
    cmake_args+=(-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake")
  fi
  cmake "${cmake_args[@]}"
  cmake --build "$BACKEND_BUILD_DIR"

  local backend_cmd
  local backend_pid
  local use_sudo=0
  if should_run_backend_as_root; then
    ensure_sudo
    use_sudo=1
  fi

  : >"$BACKEND_LOG_FILE"
  printf -v backend_cmd 'cd %s && { nohup %s %s >>%s 2>&1 < /dev/null & echo $!; }' \
    "$(quote_shell_arg "$ROOT_DIR")" \
    "$(quote_shell_arg "$BACKEND_BIN")" \
    "$(quote_shell_arg "$BACKEND_PORT")" \
    "$(quote_shell_arg "$BACKEND_LOG_FILE")"
  if [[ "$use_sudo" -eq 1 ]]; then
    backend_pid="$(sudo bash -lc "$backend_cmd")"
  else
    backend_pid="$(bash -lc "$backend_cmd")"
  fi
  backend_pid="${backend_pid//$'\r'/}"
  backend_pid="${backend_pid//$'\n'/}"
  if [[ ! "$backend_pid" =~ ^[0-9]+$ ]]; then
    echo "Failed to capture backend PID."
    show_log_tail "$BACKEND_LOG_FILE"
    exit 1
  fi
  printf '%s\n' "$backend_pid" >"$BACKEND_PID_FILE"

  if ! wait_for_url "http://127.0.0.1:$BACKEND_PORT/api/health"; then
    echo "Backend did not pass its health check."
    show_log_tail "$BACKEND_LOG_FILE"
    exit 1
  fi

  local privilege="current user"
  if [[ "$use_sudo" -eq 1 ]]; then
    privilege="root"
  fi
  echo "Backend started as $privilege on http://127.0.0.1:$BACKEND_PORT"
}

start_frontend() {
  if is_running "$FRONTEND_PID_FILE"; then
    echo "Frontend is already running with PID $(cat "$FRONTEND_PID_FILE")."
    return
  fi

  fail_if_port_in_use "frontend" "$FRONTEND_PORT"

  if [[ ! -d "$ROOT_DIR/frontend/node_modules" ]]; then
    npm --prefix "$ROOT_DIR/frontend" install
  fi

  : >"$FRONTEND_LOG_FILE"
  nohup npm --prefix "$ROOT_DIR/frontend" run dev -- \
    --host "$FRONTEND_HOST" --port "$FRONTEND_PORT" >"$FRONTEND_LOG_FILE" 2>&1 &
  printf '%s\n' "$!" >"$FRONTEND_PID_FILE"

  if ! wait_for_url "http://$FRONTEND_HOST:$FRONTEND_PORT/"; then
    echo "Frontend did not respond after startup."
    show_log_tail "$FRONTEND_LOG_FILE"
    exit 1
  fi

  echo "Frontend started on http://$FRONTEND_HOST:$FRONTEND_PORT"
}

mkdir -p "$LOG_DIR" "$ROOT_DIR/backend/data/images" "$ROOT_DIR/backend/data/db"

start_backend
start_frontend

echo
echo "FaceScan dev environment is up."
echo "Backend log : $BACKEND_LOG_FILE"
echo "Frontend log: $FRONTEND_LOG_FILE"
echo "Stop both with: $ROOT_DIR/scripts/macos/stop-dev.sh"
