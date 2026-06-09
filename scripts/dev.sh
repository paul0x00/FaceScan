#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$ROOT_DIR/Log"
BACKEND_BUILD_DIR="$ROOT_DIR/backend/build"
BACKEND_BIN="$BACKEND_BUILD_DIR/facescan_backend"
BACKEND_PID_FILE="$LOG_DIR/facescan-backend.pid"
FRONTEND_PID_FILE="$LOG_DIR/facescan-frontend.pid"
BACKEND_LOG_FILE="$LOG_DIR/facescan-backend.log"
FRONTEND_LOG_FILE="$LOG_DIR/facescan-frontend.log"
BACKEND_PORT="${BACKEND_PORT:-8080}"
FRONTEND_HOST="${FRONTEND_HOST:-127.0.0.1}"

is_running() {
  local pid_file="$1"
  if [[ ! -f "$pid_file" ]]; then
    return 1
  fi

  local pid
  pid="$(cat "$pid_file")"
  if [[ -z "$pid" ]]; then
    return 1
  fi

  if kill -0 "$pid" >/dev/null 2>&1; then
    return 0
  fi

  rm -f "$pid_file"
  return 1
}

start_backend() {
  if is_running "$BACKEND_PID_FILE"; then
    echo "Backend is already running with PID $(cat "$BACKEND_PID_FILE")."
    return
  fi

  cmake -S "$ROOT_DIR/backend" -B "$BACKEND_BUILD_DIR" -G Ninja
  cmake --build "$BACKEND_BUILD_DIR"

  nohup "$BACKEND_BIN" "$BACKEND_PORT" >"$BACKEND_LOG_FILE" 2>&1 &
  echo $! >"$BACKEND_PID_FILE"
  sleep 1

  if ! is_running "$BACKEND_PID_FILE"; then
    echo "Failed to start backend. Check $BACKEND_LOG_FILE"
    exit 1
  fi

  echo "Backend started on http://127.0.0.1:$BACKEND_PORT"
}

start_frontend() {
  if is_running "$FRONTEND_PID_FILE"; then
    echo "Frontend is already running with PID $(cat "$FRONTEND_PID_FILE")."
    return
  fi

  if [[ ! -d "$ROOT_DIR/frontend/node_modules" ]]; then
    npm --prefix "$ROOT_DIR/frontend" install
  fi

  nohup npm --prefix "$ROOT_DIR/frontend" run dev -- --host "$FRONTEND_HOST" >"$FRONTEND_LOG_FILE" 2>&1 &
  echo $! >"$FRONTEND_PID_FILE"
  sleep 1

  if ! is_running "$FRONTEND_PID_FILE"; then
    echo "Failed to start frontend. Check $FRONTEND_LOG_FILE"
    exit 1
  fi

  echo "Frontend started on http://$FRONTEND_HOST:5173"
}

mkdir -p "$LOG_DIR" "$ROOT_DIR/backend/data/images" "$ROOT_DIR/backend/data/db"

start_backend
start_frontend

echo
echo "FaceScan dev environment is up."
echo "Backend log : $BACKEND_LOG_FILE"
echo "Frontend log: $FRONTEND_LOG_FILE"
echo "Stop both with: $ROOT_DIR/scripts/stop-dev.sh"
