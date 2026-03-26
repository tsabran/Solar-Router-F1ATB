#!/usr/bin/env bash
set -euo pipefail

# Build firmware even when the repo folder name does not match the main .ino file name.
# Arduino requires: sketch folder name == main .ino name.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="${WORKDIR:-$SCRIPT_DIR}"

# Optional: allow overriding work directory from CLI
if [[ "${1:-}" == "--workdir" ]]; then
  if [[ -z "${2:-}" ]]; then
    echo "Error: --workdir requires a path argument" >&2
    exit 1
  fi
  WORK_DIR="$(cd "$2" 2>/dev/null && pwd -P)" || {
    echo "Error: Work directory does not exist: $2" >&2
    exit 1
  }
  shift 2
fi

FQBN_DEFAULT="esp32:esp32:esp32:PartitionScheme=custom"
FQBN="${FQBN:-$FQBN_DEFAULT}"
OUT_DIR="${OUT_DIR:-$WORK_DIR/build}"
ARCHIVE_DIR="${ARCHIVE_DIR:-$WORK_DIR/../releases}"
LOG_FILE="${LOG_FILE:-$WORK_DIR/build-binary.log}"

# Canonicalize paths (resolve .., symlinks when possible) for cleaner logs and outputs.
canonpath() {
  local target="$1"
  local dir base

  if [[ -d "$target" ]]; then
    (cd "$target" >/dev/null 2>&1 && pwd -P)
    return
  fi

  dir="$(dirname "$target")"
  base="$(basename "$target")"
  if [[ -d "$dir" ]]; then
    printf '%s/%s\n' "$(cd "$dir" >/dev/null 2>&1 && pwd -P)" "$base"
  else
    # Fallback if parent does not exist yet.
    printf '%s\n' "$target"
  fi
}

OUT_DIR="$(canonpath "$OUT_DIR")"
ARCHIVE_DIR="$(canonpath "$ARCHIVE_DIR")"
LOG_FILE="$(canonpath "$LOG_FILE")"

COLOR_ENABLED=0
if [[ -n "${FORCE_COLOR:-}" ]]; then
  COLOR_ENABLED=1
elif [[ -z "${NO_COLOR:-}" && -t 1 ]]; then
  COLOR_ENABLED=1
fi

if [[ "$COLOR_ENABLED" -eq 1 ]]; then
  C_RESET='\033[0m'
  C_INFO='\033[1;36m'
  C_START='\033[0;32m'
  C_END='\033[0;32m'
  C_FAIL='\033[0;31m'
  C_WARN='\033[1;33m'
  C_TEXT_INFO='\033[1m'
  C_TEXT_START='\033[0m'
  C_TEXT_END='\033[0m'
  C_TEXT_FAIL='\033[1m'
  C_TEXT_WARN='\033[1m'
else
  C_RESET=''
  C_INFO=''
  C_START=''
  C_END=''
  C_FAIL=''
  C_WARN=''
  C_TEXT_INFO=''
  C_TEXT_START=''
  C_TEXT_END=''
  C_TEXT_FAIL=''
  C_TEXT_WARN=''
fi

log() {
  local level="$1"
  shift
  local color="$C_INFO"
  local color_text="$C_TEXT_INFO"
  case "$level" in
    START) color="$C_START"; color_text="$C_TEXT_START" ;;
    END) color="$C_END"; color_text="$C_TEXT_END" ;;
    FAIL|ERROR) color="$C_FAIL"; color_text="$C_TEXT_FAIL" ;;
    WARN) color="$C_WARN"; color_text="$C_TEXT_WARN" ;;
    INFO|*) color="$C_INFO"; color_text="$C_TEXT_INFO" ;;
  esac
  printf '%b[%s] %-5s%b %b%s%b\n' "$color" "$(date '+%Y-%m-%d %H:%M:%S')" "$level" "$C_RESET" "$color_text" "$*" "$C_RESET"
}

run_step() {
  local label="$1"
  shift
  local start_ts end_ts elapsed
  start_ts="$(date +%s)"
  log START "${label}"
  if "$@"; then
    end_ts="$(date +%s)"
    elapsed="$((end_ts - start_ts))"
    log END "${label} (${elapsed}s)"
  else
    end_ts="$(date +%s)"
    elapsed="$((end_ts - start_ts))"
    log FAIL "${label} (${elapsed}s)"
    return 1
  fi
}

mkdir -p "$(dirname "$LOG_FILE")"
exec > >(tee -a "$LOG_FILE") 2>&1
log INFO "Log file: $LOG_FILE"

usage() {
  cat <<'EOF'
Usage:
  ./build-binary.sh [--workdir /path] [MAIN_INO_FILE]

Examples:
  ./build-binary.sh
  ./build-binary.sh --workdir /path/to/worktree
  ./build-binary.sh Solar_Router_V17_16.ino
  WORKDIR=/path/to/worktree ./build-binary.sh
  FQBN=esp32:esp32:esp32:PartitionScheme=custom ./build-binary.sh
  OUT_DIR=./build/custom ./build-binary.sh

Environment variables:
  WORKDIR Work directory used for sources/build files
  FQBN    Board target for arduino-cli
  OUT_DIR Output directory for binaries
  ARCHIVE_DIR Directory where timestamped .ino.bin copies are stored
  LOG_FILE Path to execution log file (default: ./build-binary.log)
  NO_COLOR Disable colored output
  FORCE_COLOR Force colored output
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

cd "$WORK_DIR"

MAIN_INO="${1:-}"
if [[ -z "$MAIN_INO" ]]; then
  # Pick the sketch tab that defines firmware version; this is the main tab in this project.
  MAIN_INO="$(grep -l '^#define[[:space:]]\+Version[[:space:]]\+"' ./*.ino | head -n1 | xargs -n1 basename)"
fi

if [[ -z "$MAIN_INO" || ! -f "$MAIN_INO" ]]; then
  log ERROR "Unable to find main .ino file."
  log INFO "Tip: pass it explicitly, e.g. ./build-binary.sh Solar_Router_V17_16.ino"
  exit 1
fi

SKETCH_NAME="${MAIN_INO%.ino}"
if [[ "$SKETCH_NAME" == "$MAIN_INO" ]]; then
  log ERROR "Main file must end with .ino"
  exit 1
fi

TMP_ROOT="$WORK_DIR/.tmp-build"
STAGE_DIR="$TMP_ROOT/$SKETCH_NAME"

cleanup() {
  rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

mkdir -p "$STAGE_DIR" "$OUT_DIR"

# Stage all files into a temp sketch folder named exactly like the main .ino file.
run_step "rsync staging" rsync -a --delete \
  --exclude '.git' \
  --exclude '.tmp-build' \
  --exclude 'build' \
  --exclude 'releases' \
  "$WORK_DIR/" "$STAGE_DIR/"

if [[ ! -f "$STAGE_DIR/$MAIN_INO" ]]; then
  log ERROR "Staged main file not found: $STAGE_DIR/$MAIN_INO"
  exit 1
fi

log INFO "Build target: ${MAIN_INO} (FQBN: ${FQBN})"
log INFO "Work directory: ${WORK_DIR}"
log INFO "Staging strategy: compile from ${STAGE_DIR} so sketch folder matches ${SKETCH_NAME}.ino"
log INFO "Outputs: build=${OUT_DIR} archive=${ARCHIVE_DIR}"

run_step "arduino-cli compile" arduino-cli compile \
  --fqbn "$FQBN" \
  --output-dir "$OUT_DIR" \
  "$STAGE_DIR"

BIN_FILE="$OUT_DIR/${MAIN_INO}.bin"
if [[ ! -f "$BIN_FILE" ]]; then
  log ERROR "Expected firmware file not found: $BIN_FILE"
  exit 1
fi

TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
BIN_COPY_NAME="${MAIN_INO%.ino}_${TIMESTAMP}.ino.bin"
mkdir -p "$ARCHIVE_DIR"
BIN_COPY_PATH="$ARCHIVE_DIR/$BIN_COPY_NAME"
cp "$BIN_FILE" "$BIN_COPY_PATH"

log INFO "Build done. Artifacts:"
ls -1 "$OUT_DIR" | sed 's/^/  - /'
log INFO "Timestamped copy: $BIN_COPY_PATH"
