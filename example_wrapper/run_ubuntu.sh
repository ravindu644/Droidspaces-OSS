#!/bin/bash
set -euo pipefail

cd "$(dirname "$(realpath "$0")")"

DROIDSPACES="/bin/droidspaces"
CONTAINER_NAME="Ubuntu 22.04"
CONTAINER_HOSTNAME="ubuntu"
ROOTFS_PATH="${PWD}/Ubuntu-22-04"
SUDO_HOME="${SUDO_HOME:-/home/ravindu644}"
SHARED_STORAGE="${SUDO_HOME}/Droidspaces"
TOOLCHAIN_PATH="${SUDO_HOME}/toolchains"

# Auto elevate if not root
if [ "$EUID" -ne 0 ]; then
  exec sudo "$0" "$@"
fi

# Validate binary
command -v "${DROIDSPACES}" >/dev/null 2>&1 || {
  echo "Error: droidspaces not found at ${DROIDSPACES}"
  exit 1
}

# Validate rootfs
[ -d "${ROOTFS_PATH}" ] || {
  echo "Error: rootfs not found at ${ROOTFS_PATH}"
  exit 1
}

mkdir -p "${SHARED_STORAGE}" "${TOOLCHAIN_PATH}"

# Common arguments
COMMON_ARGS=(
  --name "${CONTAINER_NAME}"
  --rootfs "${ROOTFS_PATH}"
  --bind "${SHARED_STORAGE}:/mnt/Shared"
  --bind "${TOOLCHAIN_PATH}:/home/kernel-builder/toolchains"
  --hostname "${CONTAINER_HOSTNAME}"
)

# Get container PID
get_pid() {
  "${DROIDSPACES}" --name "${CONTAINER_NAME}" pid 2>/dev/null || echo "NONE"
}

# Check if running
is_running() {
  local pid
  pid="$(get_pid)"
  [[ "$pid" != "NONE" && "$pid" =~ ^[0-9]+$ ]]
}

# Commands
start_container() {
  echo "Starting container (foreground)..."
  exec "${DROIDSPACES}" "${COMMON_ARGS[@]}" start -f
}

restart_container() {
  echo "Restarting container..."
  "${DROIDSPACES}" "${COMMON_ARGS[@]}" restart
}

enter_container() {
  echo "Entering container..."
  exec "${DROIDSPACES}" --name "${CONTAINER_NAME}" enter "$@"
}

run_container() {
  echo "Running command in container..."
  exec "${DROIDSPACES}" --name "${CONTAINER_NAME}" run "$@"
}

stop_container() {
  echo "Stopping container..."
  "${DROIDSPACES}" --name "${CONTAINER_NAME}" stop
}

# Default behavior
default_action() {
  if is_running; then
    echo "Container is already running. Entering right now..."
    enter_container "$@"
  else
    start_container
  fi
}

# CLI handling
case "${1:-}" in
  start)
    if is_running; then
      echo "Container already running. Use 'enter' to attach."
    else
      start_container
    fi
    ;;
  restart)
    restart_container
    ;;
  enter)
    shift
    if is_running; then
      enter_container "$@"
    else
      echo "Container not running. Starting in foreground..."
      start_container
    fi
    ;;
  run)
    shift
    if is_running; then
      run_container "$@"
    else
      echo "Container not running. Start it first."
      exit 1
    fi
    ;;
  stop)
    if is_running; then
      stop_container
    else
      echo "Container is not running."
    fi
    ;;
  status)
    if is_running; then
      echo "Container is running (PID: $(get_pid))"
    else
      echo "Container is not running"
    fi
    ;;
  *)
    default_action "$@"
    ;;
esac
