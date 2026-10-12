#!/usr/bin/env bash
# Flash an archived build (releases/<Sketch>_<BUILD_TAG>/) over USB serial or OTA (HTTP /update).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=tools/build-lib.sh
source "$SCRIPT_DIR/tools/build-lib.sh"

usage() {
  cat <<'USAGE'
Usage:
  flash.sh --serial [PORT] [options] [RELEASE]
  flash.sh --ota HOST      [options] [RELEASE]

RELEASE is a release directory, its name, or its .ino.bin (default: latest in releases/).
The SHA-256 of the firmware is checked against SHA256SUMS before flashing.

  --serial [PORT]  USB flash with esptool, same layout as the Arduino IDE:
                   bootloader 0x1000, partitions 0x8000, boot_app0 0xe000, app 0x10000.
                   NVS (WiFi) and LittleFS (router configuration) are preserved.
                   PORT defaults to the only /dev/cu.usbserial*|SLAB*|wchusbserial*|usbmodem* port.
  --ota HOST       POST the app to http://HOST/update (router web OTA), then wait for the
                   router to come back and check that its version shows the BUILD_TAG.
  --baud N         Serial speed (default: 921600)
  --timeout S      OTA: seconds to wait for the reboot (default: 120)
  --dry-run        Show what would be done
  -y, --yes        Do not ask for confirmation
  -h, --help       Show this help

Environment: SR_OUTPUT_ROOT (default: main worktree of this repository).
USAGE
}

MODE=""
PORT=""
HOST=""
BAUD=921600
OTA_TIMEOUT=120
DRY_RUN=0
YES=0
RELEASE=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) MODE=serial; shift
              if [[ $# -gt 0 && "$1" == /dev/* ]]; then PORT="$1"; shift; fi ;;
    --ota) [[ -n "${2:-}" ]] || die "--ota requires a host"; MODE=ota; HOST="$2"; shift 2 ;;
    --baud) BAUD="$2"; shift 2 ;;
    --timeout) OTA_TIMEOUT="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    -y|--yes) YES=1; shift ;;
    -h|--help) usage; exit 0 ;;
    -*) die "Unknown option: $1 (see --help)" ;;
    *) RELEASE="$1"; shift ;;
  esac
done
[[ -n "$MODE" ]] || { usage; exit 1; }

SR_ROOT="$(sr_output_root "$SCRIPT_DIR")"
ARCHIVE_DIR="${ARCHIVE_DIR:-$SR_ROOT/releases}"

# ---------------------------------------------------------------------------
# Release selection and integrity
# ---------------------------------------------------------------------------
if [[ -z "$RELEASE" ]]; then
  RELEASE="$(ls -1td "$ARCHIVE_DIR"/*/ 2>/dev/null | head -n1 || true)"
  [[ -n "$RELEASE" ]] || die "No release found in $ARCHIVE_DIR"
elif [[ -f "$RELEASE" ]]; then
  RELEASE="$(dirname "$RELEASE")"
elif [[ ! -d "$RELEASE" && -d "$ARCHIVE_DIR/$RELEASE" ]]; then
  RELEASE="$ARCHIVE_DIR/$RELEASE"
fi
[[ -d "$RELEASE" ]] || die "Release not found: $RELEASE"
RELEASE="$(sr_abspath "$RELEASE")"
ARTIFACT="$(basename "$RELEASE")"
APP_BIN="$RELEASE/$ARTIFACT.ino.bin"
[[ -f "$APP_BIN" ]] || die "Firmware not found: $APP_BIN"
MANIFEST="$RELEASE/manifest.txt"
manifest_get() { sed -n "s/^$1: \"\(.*\)\"$/\1/p" "$MANIFEST" 2>/dev/null | head -n1; }
BUILD_TAG="$(manifest_get build_tag)"

if [[ -f "$RELEASE/SHA256SUMS" ]]; then
  (cd "$RELEASE" && shasum -a 256 -c --quiet SHA256SUMS 2>/dev/null | grep -v ': OK$' || true) \
    | grep -q . && die "Checksum mismatch in $RELEASE (see SHA256SUMS)"
  log OK "SHA256SUMS verified"
else
  log WARN "No SHA256SUMS in $RELEASE"
fi

log INFO "Release:   $RELEASE"
log INFO "Build tag: ${BUILD_TAG:-unknown}"
log INFO "Firmware:  $(wc -c < "$APP_BIN" | tr -d ' ') bytes, sha256 $(sr_sha256 "$APP_BIN")"

confirm() {
  [[ "$YES" -eq 1 || "$DRY_RUN" -eq 1 ]] && return 0
  [[ -t 0 ]] || die "Not interactive: use --yes to confirm"
  local answer
  read -r -p "$1 [y/N] " answer
  [[ "$answer" == y || "$answer" == Y ]] || die "Aborted"
}

# ---------------------------------------------------------------------------
# Serial
# ---------------------------------------------------------------------------
flash_serial() {
  local boot="$RELEASE/$ARTIFACT.ino.bootloader.bin" parts="$RELEASE/$ARTIFACT.ino.partitions.bin"
  local platform_dir boot_app0 esptool
  [[ -f "$boot" && -f "$parts" ]] || die "Bootloader/partitions images missing in $RELEASE"

  platform_dir="$(awk '/^  board_platform:/{f=1} f && /install_dir:/{gsub(/.*install_dir: "|"$/, ""); print; exit}' "$MANIFEST" 2>/dev/null || true)"
  boot_app0="$platform_dir/tools/partitions/boot_app0.bin"
  if [[ ! -f "$boot_app0" ]]; then
    boot_app0="$(ls -1 "$HOME"/Library/Arduino15/packages/esp32/hardware/esp32/*/tools/partitions/boot_app0.bin \
      "$HOME"/.arduino15/packages/esp32/hardware/esp32/*/tools/partitions/boot_app0.bin 2>/dev/null | tail -n1 || true)"
  fi
  [[ -f "$boot_app0" ]] || die "boot_app0.bin not found (ESP32 platform not installed?)"

  esptool="$(command -v esptool || true)"
  if [[ -z "$esptool" ]]; then
    esptool="$(ls -1 "$HOME"/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool \
      "$HOME"/Library/Arduino15/internal/esp32_esptool_py_*/esptool \
      "$HOME"/.arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | tail -n1 || true)"
  fi
  [[ -x "$esptool" ]] || die "esptool not found"

  if [[ -z "$PORT" ]]; then
    local ports=()
    while IFS= read -r p; do ports+=("$p"); done < <(ls -1 /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART* \
      /dev/cu.wchusbserial* /dev/cu.usbmodem* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true)
    [[ "${#ports[@]}" -eq 1 ]] || die "Found ${#ports[@]} serial port(s) (${ports[*]:-none}): pass --serial PORT"
    PORT="${ports[0]}"
  fi

  local cmd=("$esptool" --chip esp32 --port "$PORT" --baud "$BAUD" --before default-reset --after hard-reset
    write-flash -z --flash-mode keep --flash-freq keep --flash-size keep
    0x1000 "$boot" 0x8000 "$parts" 0xe000 "$boot_app0" 0x10000 "$APP_BIN")
  log INFO "Port:      $PORT"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    printf '%b+ %s%b\n' "$C_CMD" "${cmd[*]}" "$C_RESET"
    return 0
  fi
  confirm "Flash $ARTIFACT on $PORT?"
  run_step "esptool write-flash" "${cmd[@]}"
}

# ---------------------------------------------------------------------------
# OTA (router web server)
# ---------------------------------------------------------------------------
router_version() {
  curl -fsS --max-time 5 "http://$HOST/ParaFixe" 2>/dev/null \
    | python3 -c 'import json,sys; print(json.load(sys.stdin).get("VersionTexte",""))' 2>/dev/null || true
}

flash_ota() {
  local before after deadline resp
  log INFO "Router:    http://$HOST"
  before="$(router_version)"
  [[ -n "$before" ]] || log WARN "Cannot read the current version from http://$HOST/ParaFixe"
  log INFO "Current:   ${before:-unknown}"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    printf '%b+ curl -F update=@%s http://%s/update%b\n' "$C_CMD" "$APP_BIN" "$HOST" "$C_RESET"
    return 0
  fi
  confirm "Upload $ARTIFACT to http://$HOST/update (the router reboots)?"
  log START "OTA upload"
  local rc=0
  resp="$(curl -fsS --max-time 300 -F "update=@$APP_BIN;filename=$ARTIFACT.ino.bin;type=application/octet-stream" \
    "http://$HOST/update")" || rc=$?
  # 6/7/28 = host not resolved / connection refused / timeout: nothing was sent.
  case "$rc" in 6|7|28) die "Cannot reach http://$HOST/update (curl exit $rc)" ;; esac
  # Otherwise the router may reboot before the response is complete: rely on the version check.
  log INFO "Response:  ${resp:-(none)} (curl exit $rc)"
  [[ "$resp" != FAIL ]] || die "The router reported FAIL (see its Telnet/serial log)"

  log INFO "Waiting for reboot (max ${OTA_TIMEOUT}s)..."
  sleep 5
  deadline=$(( $(date +%s) + OTA_TIMEOUT ))
  while [[ "$(date +%s)" -lt "$deadline" ]]; do
    after="$(router_version)"
    if [[ -n "$after" && ( -z "$BUILD_TAG" || "$after" == *"$BUILD_TAG"* ) ]]; then
      log OK "Router is running: $after"
      return 0
    fi
    sleep 3
  done
  die "Router did not report ${BUILD_TAG:-a version} within ${OTA_TIMEOUT}s (last: ${after:-no answer})"
}

case "$MODE" in
  serial) flash_serial ;;
  ota) flash_ota ;;
esac
