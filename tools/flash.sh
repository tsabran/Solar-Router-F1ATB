#!/usr/bin/env bash
# Flash an archived build (releases/<Sketch>_<BUILD_TAG>/) over USB serial or OTA (HTTP /update).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=lib/build-lib.sh
source "$SCRIPT_DIR/lib/build-lib.sh"

usage() {
  cat <<'USAGE'
Usage:
  flash.sh --serial [PORT] [options] [RELEASE]
  flash.sh --ota [HOST]    [options] [RELEASE]

RELEASE is a release directory, its name, or its .ino.bin (default: latest in releases/).
The SHA-256 of the firmware is checked against SHA256SUMS before flashing.

  --serial [PORT]  USB flash with esptool: boot_app0 at 0xe000 (boot from app0) and the app
                   at 0x10000. NVS (WiFi) and LittleFS (router configuration) are preserved.
                   The board must already have the custom partition table (any router
                   firmware >= 14.02); for a blank board, upload once from the Arduino IDE.
                   PORT defaults to the only /dev/cu.usbserial*|SLAB*|wchusbserial*|usbmodem* port.
  --ota [HOST|URL] POST the app to <URL>/update (router web OTA), then wait for the
                   router to come back and check that /ParaFixe shows the BUILD_TAG.
                   HOST (IP or name, optional :port) means http://HOST; a URL
                   (http[s]://name[:port][/prefix]) allows a reverse proxy.
                   Defaults to OTA_HOST from flash.conf.
  --cookie NAME=ENVVAR
                   OTA: send the cookie NAME with the value of the environment variable
                   ENVVAR (e.g. auth_token=CADDY_ACTIVE_COOKIE). The value is never
                   logged nor passed on the command line. Default: OTA_COOKIE in flash.conf.
  --cookie-domain DOMAIN
                   Required with a cookie: it is only sent over https:// to DOMAIN or one
                   of its subdomains (redirects are never followed); any other target is
                   refused before the first request. Default: OTA_COOKIE_DOMAIN in flash.conf.
  --baud N         Serial speed (default: 921600)
  --timeout S      OTA: seconds to wait for the reboot (default: 120)
  --dry-run        Show what would be done
  -y, --yes        Do not ask for confirmation (flash only, not the push below)
  --push-ref       After a successful deployment, push the build ref (refs/builds/<label> or
                   the release tag) to origin without asking. By default it is proposed
                   interactively when it is not on origin yet, so that what runs on the
                   router can always be rebuilt from the shared repository.
  -h, --help       Show this help

Each real flash is logged in RELEASE/flash-<time>-<mode>-<target>.log and summarized
(before/after version, result) in releases/DEPLOYMENTS.log.

Config: flash.conf in the main worktree (not versioned, see tools/flash.conf.example), or
SR_FLASH_CONF. Keys: OTA_HOST, OTA_COOKIE, OTA_COOKIE_DOMAIN, SERIAL_PORT.
Environment: SR_OUTPUT_ROOT (default: main worktree of this repository).
USAGE
}

ORIG_ARGS="$*"
MODE=""
PORT=""
HOST=""
COOKIE_SPEC=""
COOKIE_DOMAIN=""
BAUD=921600
OTA_TIMEOUT=120
DRY_RUN=0
YES=0
PUSH_REF=0
RELEASE=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) MODE=serial; shift
              if [[ $# -gt 0 && "$1" == /dev/* ]]; then PORT="$1"; shift; fi ;;
    --ota) MODE=ota; shift
           # A host has no "_" (release names do) and no "/"; a URL has a scheme.
           if [[ $# -gt 0 && ( "$1" =~ ^[A-Za-z0-9][A-Za-z0-9.-]*(:[0-9]+)?$ || "$1" =~ ^https?:// ) ]]; then
             HOST="$1"; shift
           fi ;;
    --cookie) [[ -n "${2:-}" ]] || die "--cookie requires NAME=ENVVAR"; COOKIE_SPEC="$2"; shift 2 ;;
    --cookie-domain) [[ -n "${2:-}" ]] || die "--cookie-domain requires a DOMAIN"; COOKIE_DOMAIN="$2"; shift 2 ;;
    --baud) BAUD="$2"; shift 2 ;;
    --timeout) OTA_TIMEOUT="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    -y|--yes) YES=1; shift ;;
    --push-ref) PUSH_REF=1; shift ;;
    -h|--help) usage; exit 0 ;;
    -*) die "Unknown option: $1 (see --help)" ;;
    *) RELEASE="$1"; shift ;;
  esac
done
[[ -n "$MODE" ]] || { usage; exit 1; }

SR_ROOT="$(sr_output_root "$SCRIPT_DIR")"
ARCHIVE_DIR="${ARCHIVE_DIR:-$SR_ROOT/releases}"

# flash.conf is bash: OTA_HOST, OTA_COOKIE, OTA_COOKIE_DOMAIN, SERIAL_PORT.
FLASH_CONF="${SR_FLASH_CONF:-$SR_ROOT/flash.conf}"
OTA_HOST="" OTA_COOKIE="" OTA_COOKIE_DOMAIN="" SERIAL_PORT=""
# shellcheck source=/dev/null
[[ ! -f "$FLASH_CONF" ]] || source "$FLASH_CONF"
if [[ "$MODE" == ota && -z "$HOST" ]]; then
  HOST="$OTA_HOST"
  [[ -n "$HOST" ]] || die "--ota: no HOST given and no OTA_HOST in $FLASH_CONF (see tools/flash.conf.example)"
fi
if [[ "$MODE" == ota ]]; then
  if [[ "$HOST" =~ ^https?:// ]]; then
    BASE_URL="${HOST%/}"
  else
    BASE_URL="http://$HOST"
  fi
  [[ -n "$COOKIE_SPEC" ]] || COOKIE_SPEC="$OTA_COOKIE"
  COOKIE_NAME=""
  COOKIE_ENV=""
  if [[ -n "$COOKIE_SPEC" ]]; then
    [[ "$COOKIE_SPEC" =~ ^([A-Za-z0-9_.-]+)=([A-Za-z_][A-Za-z0-9_]*)$ ]] \
      || die "--cookie/OTA_COOKIE must be NAME=ENVVAR, got: $COOKIE_SPEC"
    COOKIE_NAME="${BASH_REMATCH[1]}"
    COOKIE_ENV="${BASH_REMATCH[2]}"
    # The cookie is a credential: refuse to send it anywhere but https://<allowed domain>.
    [[ -n "$COOKIE_DOMAIN" ]] || COOKIE_DOMAIN="$OTA_COOKIE_DOMAIN"
    COOKIE_DOMAIN="$(printf '%s' "${COOKIE_DOMAIN#.}" | tr 'A-Z' 'a-z')"
    [[ "$COOKIE_DOMAIN" =~ ^[a-z0-9-]+(\.[a-z0-9-]+)+$ ]] \
      || die "A cookie requires --cookie-domain/OTA_COOKIE_DOMAIN (e.g. example.org), got: '${COOKIE_DOMAIN}'"
    [[ "$BASE_URL" =~ ^https://([A-Za-z0-9.-]+)(:[0-9]+)?(/[A-Za-z0-9._~/-]*)?$ ]] \
      || die "Refusing to send cookie $COOKIE_NAME: target must be https://<host>[:port][/path], got: $BASE_URL"
    url_host="$(printf '%s' "${BASH_REMATCH[1]}" | tr 'A-Z' 'a-z')"
    [[ "$url_host" == "$COOKIE_DOMAIN" || "$url_host" == *".$COOKIE_DOMAIN" ]] \
      || die "Refusing to send cookie $COOKIE_NAME to $url_host (allowed: $COOKIE_DOMAIN and its subdomains)"
    printenv "$COOKIE_ENV" >/dev/null \
      || die "Environment variable $COOKIE_ENV is not set/exported (cookie $COOKIE_NAME)"
  fi
fi
if [[ "$MODE" == serial && -z "$PORT" ]]; then
  PORT="$SERIAL_PORT"
fi

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
# Nested keys of the "source:" section (e.g. build_ref, repo).
manifest_src() { sed -n "s/^  $1: \"\(.*\)\"$/\1/p" "$MANIFEST" 2>/dev/null | head -n1; }

# ---------------------------------------------------------------------------
# Deployment log: releases/<release>/flash-<time>-<target>.log for each real
# attempt, plus one line per attempt in releases/DEPLOYMENTS.log.
# ---------------------------------------------------------------------------
DEPLOY_LOG="$ARCHIVE_DIR/DEPLOYMENTS.log"
DEPLOY_STARTED=0
DEPLOY_RESULT=FAIL
VERSION_BEFORE=""
VERSION_AFTER=""
if [[ "$DRY_RUN" -eq 0 ]]; then
  FLASH_TS="$(date +%Y%m%d-%H%M%S)"
  RAW_LOG="$RELEASE/.flash-$FLASH_TS.raw"
  sr_tee_log "$RAW_LOG"
  log INFO "Command:   flash.sh $ORIG_ARGS"
  log INFO "Target:    $MODE ${HOST:-${PORT:-auto}}"
fi

deploy_finish() {
  local rc=$? target flash_log
  trap - EXIT
  [[ "$DRY_RUN" -eq 0 ]] || exit "$rc"
  if [[ "$DEPLOY_STARTED" -eq 0 ]]; then
    rm -f "$RAW_LOG"
    exit "$rc"
  fi
  [[ "$rc" -eq 0 ]] && DEPLOY_RESULT=OK
  target="${HOST:-$PORT}"
  flash_log="$RELEASE/flash-$FLASH_TS-$MODE-$(printf '%s' "${target#*://}" | sed -e 's#/*$##' -e 's#[^A-Za-z0-9.-]#_#g').log"
  printf '%s\t%s\t%s\t%s\tbefore=%s\tafter=%s\trelease=%s\tsha256=%s\tlog=%s\n' \
    "$(date '+%Y-%m-%dT%H:%M:%S%z')" "$DEPLOY_RESULT" "$MODE" "$target" \
    "${VERSION_BEFORE:--}" "${VERSION_AFTER:--}" "$ARTIFACT" "$(sr_sha256 "$APP_BIN")" \
    "$ARTIFACT/$(basename "$flash_log")" >> "$DEPLOY_LOG"
  log INFO "Deployment recorded in $DEPLOY_LOG"
  sr_copy_log "$RAW_LOG" "$flash_log"
  rm -f "$RAW_LOG"
  exit "$rc"
}
trap deploy_finish EXIT

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
log INFO "Git ref:   $(manifest_src build_ref || true)"
case "$BUILD_TAG" in
  *-wip-*) log WARN "Work-in-progress build: it includes uncommitted local changes (see worktree.patch)" ;;
esac

confirm() {
  [[ "$YES" -eq 1 || "$DRY_RUN" -eq 1 ]] && return 0
  [[ -t 0 ]] || die "Not interactive: use --yes to confirm"
  local answer
  read -r -p "$1 [y/N] " answer
  [[ "$answer" == y || "$answer" == Y ]] || die "Aborted"
}

# A -wip build must be acknowledged even with --yes, unless SR_ALLOW_WIP=1.
confirm_wip() {
  case "$BUILD_TAG" in *-wip-*) ;; *) return 0 ;; esac
  [[ "${SR_ALLOW_WIP:-0}" != 1 ]] || return 0
  [[ -t 0 ]] || die "$BUILD_TAG is a -wip build: confirm interactively or set SR_ALLOW_WIP=1"
  local answer
  read -r -p "$BUILD_TAG includes uncommitted changes. Deploy it anyway? [y/N] " answer
  [[ "$answer" == y || "$answer" == Y ]] || die "Aborted"
}

# After a successful deployment: make sure the build ref is on origin (asked, never implied by --yes).
offer_push_ref() {
  local ref repo answer
  ref="$(manifest_src build_ref || true)"
  if [[ -z "$ref" ]]; then
    log WARN "No build ref in the manifest (--no-ref build): it cannot be rebuilt from git"
    return 0
  fi
  repo="$(manifest_src repo || true)"
  [[ -d "$repo" ]] || repo="$SR_ROOT"
  git -C "$repo" rev-parse -q --verify "$ref" >/dev/null || { log WARN "$ref not found in $repo"; return 0; }
  if [[ -n "$(git -C "$repo" ls-remote origin "$ref" 2>/dev/null)" ]]; then
    log INFO "$ref is on origin"
    return 0
  fi
  if [[ "$PUSH_REF" -eq 0 ]]; then
    if [[ ! -t 0 ]]; then
      log WARN "$ref is only local. Publish it with: git -C $repo push origin $ref"
      return 0
    fi
    read -r -p "Push $ref to origin? [y/N] " answer
    [[ "$answer" == y || "$answer" == Y ]] || { log INFO "Not pushed. Later: git -C $repo push origin $ref"; return 0; }
  fi
  if run_cmd git -C "$repo" push origin "$ref"; then
    log OK "$ref pushed to origin"
  else
    log WARN "Push failed (the deployment itself succeeded): git -C $repo push origin $ref"
  fi
}

# ---------------------------------------------------------------------------
# Serial
# ---------------------------------------------------------------------------
flash_serial() {
  local platform_dir boot_app0 esptool

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
    0xe000 "$boot_app0" 0x10000 "$APP_BIN")
  log INFO "Port:      $PORT"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    printf '%b+ %s%b\n' "$C_CMD" "${cmd[*]}" "$C_RESET"
    return 0
  fi
  confirm "Flash $ARTIFACT on $PORT?"
  confirm_wip
  DEPLOY_STARTED=1
  run_step "esptool write-flash" "${cmd[@]}"
}

# ---------------------------------------------------------------------------
# OTA (router web server)
# ---------------------------------------------------------------------------
# curl with the optional auth cookie, given through a curl config on a file descriptor so the
# value never appears in argv (ps) nor in the logs.
ota_curl() {
  if [[ -n "$COOKIE_NAME" ]]; then
    curl --proto =https --max-redirs 0 -K <(printenv "$COOKIE_ENV" | sed -e 's/[\\"]/\\&/g' -e "s/^/cookie = \"$COOKIE_NAME=/" -e 's/$/"/') "$@"
  else
    curl "$@"
  fi
}

# Prints "<http_code> <VersionTexte>" (code 000 when unreachable, version empty if not JSON).
router_probe() {
  local tmp code
  tmp="$(mktemp)"
  code="$(ota_curl -sS --max-time 10 -o "$tmp" -w '%{http_code}' "$BASE_URL/ParaFixe" 2>/dev/null)" || true
  printf '%s %s\n' "${code:-000}" \
    "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("VersionTexte",""))' "$tmp" 2>/dev/null || true)"
  rm -f "$tmp"
}

auth_hint() {
  local auth="no cookie sent, see --cookie"
  [[ -z "$COOKIE_NAME" ]] || auth="check the cookie $COOKIE_NAME from \$$COOKIE_ENV"
  case "$1" in
    3??|401|403) printf ' (HTTP %s: authentication rejected? %s)' "$1" "$auth" ;;
    000) printf ' (unreachable)' ;;
    *) printf ' (HTTP %s)' "$1" ;;
  esac
}

flash_ota() {
  local probe code before after deadline resp
  log INFO "Router:    $BASE_URL"
  [[ -z "$COOKIE_NAME" ]] || log INFO "Auth:      cookie $COOKIE_NAME from \$$COOKIE_ENV (only for $COOKIE_DOMAIN)"
  probe="$(router_probe)"
  code="${probe%% *}"
  before="${probe#* }"
  VERSION_BEFORE="$before"
  if [[ -z "$before" ]]; then
    log WARN "Cannot read the current version from $BASE_URL/ParaFixe$(auth_hint "$code")"
    case "$code" in
      3??|401|403|000) [[ "$DRY_RUN" -eq 1 ]] || die "Router not reachable through $BASE_URL, nothing sent" ;;
    esac
  fi
  log INFO "Current:   ${before:-unknown}"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    printf '%b+ curl%s -F update=@%s %s/update%b\n' "$C_CMD" \
      "${COOKIE_NAME:+ --cookie $COOKIE_NAME=\$$COOKIE_ENV}" "$APP_BIN" "$BASE_URL" "$C_RESET"
    return 0
  fi
  confirm "Upload $ARTIFACT to $BASE_URL/update (the router reboots)?"
  confirm_wip
  DEPLOY_STARTED=1
  log START "OTA upload"
  local rc=0 body
  body="$(mktemp)"
  code="$(ota_curl -sS --max-time 300 -o "$body" -w '%{http_code}' \
    -F "update=@$APP_BIN;filename=$ARTIFACT.ino.bin;type=application/octet-stream" \
    "$BASE_URL/update")" || rc=$?
  resp="$(head -c 200 "$body" | tr -d '\r\n')"
  rm -f "$body"
  # 6/7 = host not resolved / connection refused: nothing was sent.
  case "$rc" in 6|7) die "Cannot reach $BASE_URL/update (curl exit $rc)" ;; esac
  case "$code" in
    3??|401|403) die "Upload refused$(auth_hint "$code")" ;;
    # Gateway errors come from a reverse proxy that could not reach the router: not flashed.
    502|503|504) die "Upload not delivered (HTTP $code from the proxy): the router kept its firmware, retry" ;;
  esac
  # Otherwise the router may reboot before the response is complete: rely on the version check.
  log INFO "Response:  ${resp:-(none)} (HTTP ${code:-000}, curl exit $rc)"
  [[ "$resp" != FAIL ]] || die "The router reported FAIL (see its Telnet/serial log)"

  log INFO "Waiting for reboot (max ${OTA_TIMEOUT}s)..."
  sleep 5
  deadline=$(( $(date +%s) + OTA_TIMEOUT ))
  while [[ "$(date +%s)" -lt "$deadline" ]]; do
    probe="$(router_probe)"
    after="${probe#* }"
    [[ -z "$after" ]] || VERSION_AFTER="$after"
    # VersionTexte is "<version> (<label> <sha>)": match the whole label (custom.1 != custom.10).
    if [[ -n "$after" && ( -z "$BUILD_TAG" || "$after" == *"($BUILD_TAG "* ) ]]; then
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
[[ "$DRY_RUN" -eq 1 ]] || offer_push_ref
