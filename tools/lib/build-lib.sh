# shellcheck shell=bash
# Shared helpers for build-binary.sh, merge-and-build.sh and flash.sh.
# Source this file; do not execute it.

# Files that must never reach a build, a patch or an archive (globs matched at any depth).
SR_SECRET_GLOBS=(
  '.enphase_token_cache.json'
  '*.token'
  '.env'
)

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
sr_init_colors() {
  local enabled=0
  if [[ -n "${FORCE_COLOR:-}" ]]; then
    enabled=1
  elif [[ -z "${NO_COLOR:-}" && -t 1 ]]; then
    enabled=1
  fi
  if [[ "$enabled" -eq 1 ]]; then
    C_RESET='\033[0m'; C_INFO='\033[1;36m'; C_OK='\033[0;32m'
    C_FAIL='\033[0;31m'; C_WARN='\033[1;33m'; C_CMD='\033[0;34m'
  else
    C_RESET=''; C_INFO=''; C_OK=''; C_FAIL=''; C_WARN=''; C_CMD=''
  fi
}
sr_init_colors

log() {
  local level="$1"; shift
  local color="$C_INFO"
  case "$level" in
    START|END|OK) color="$C_OK" ;;
    FAIL|ERROR) color="$C_FAIL" ;;
    WARN) color="$C_WARN" ;;
  esac
  printf '%b[%s] %-5s%b %s\n' "$color" "$(date '+%Y-%m-%d %H:%M:%S')" "$level" "$C_RESET" "$*"
}

die() { log ERROR "$*"; exit 1; }

run_cmd() {
  printf '%b+ %s%b\n' "$C_CMD" "$*" "$C_RESET"
  "$@"
}

run_step() {
  local label="$1"; shift
  local start_ts rc=0
  start_ts="$(date +%s)"
  log START "$label"
  run_cmd "$@" || rc=$?
  if [[ "$rc" -eq 0 ]]; then
    log END "$label ($(( $(date +%s) - start_ts ))s)"
  else
    log FAIL "$label ($(( $(date +%s) - start_ts ))s, exit $rc)"
  fi
  return "$rc"
}

# Tee all further stdout/stderr of the current shell to a log file (raw, unbuffered).
sr_tee_log() {
  local file="$1"
  mkdir -p "$(dirname "$file")"
  exec > >(tee -a "$file") 2>&1
}

# Copy a log started with sr_tee_log once everything printed so far has reached it
# (tee runs asynchronously), without ANSI color codes.
sr_copy_log() {
  local src="$1" dest="$2" marker i
  marker="Log saved: $dest"
  log INFO "$marker"
  for i in $(seq 1 50); do
    grep -qF "$marker" "$src" 2>/dev/null && break
    sleep 0.1
  done
  sed -E $'s/\x1b\\[[0-9;]*m//g' "$src" > "$dest"
}

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
sr_abspath() { (cd "$1" >/dev/null 2>&1 && pwd -P); }

sr_sha256() { shasum -a 256 "$1" | awk '{print $1}'; }

# Root of the main worktree of the repository containing $1 (shared by all its worktrees).
sr_main_worktree() {
  local common
  common="$(git -C "$1" rev-parse --path-format=absolute --git-common-dir 2>/dev/null)" || return 1
  if [[ "$(basename "$common")" == ".git" ]]; then
    dirname "$common"
  else
    printf '%s\n' "$common"
  fi
}

# Root for releases/ and .tmp-build/:
# $SR_OUTPUT_ROOT, else the main worktree of the repo containing $1, else $1.
sr_output_root() {
  if [[ -n "${SR_OUTPUT_ROOT:-}" ]]; then
    mkdir -p "$SR_OUTPUT_ROOT" && sr_abspath "$SR_OUTPUT_ROOT"
  else
    sr_main_worktree "$1" || sr_abspath "$1"
  fi
}

# Main sketch tab: the .ino defining `#define Version "..."`.
sr_find_main_ino() {
  local f
  f="$(grep -l '^#define[[:space:]]\{1,\}Version[[:space:]]\{1,\}"' "$1"/*.ino 2>/dev/null | head -n1 || true)"
  [[ -n "$f" ]] && basename "$f"
  return 0
}

# ---------------------------------------------------------------------------
# Git snapshot helpers
# ---------------------------------------------------------------------------
sr_secret_pathspecs() {
  local g
  for g in "${SR_SECRET_GLOBS[@]}"; do
    printf ':(exclude,glob)**/%s\n' "$g"
  done
}

# Write to $2 a binary patch of all working-tree changes of repo $1 against HEAD:
# staged + unstaged + untracked non-ignored files, secrets excluded. The real index is untouched.
sr_worktree_patch() {
  local repo="$1" out="$2" tmpidx l
  local -a excl=()
  while IFS= read -r l; do excl+=("$l"); done < <(sr_secret_pathspecs)
  tmpidx="$(mktemp)"
  GIT_INDEX_FILE="$tmpidx" git -C "$repo" read-tree HEAD
  GIT_INDEX_FILE="$tmpidx" git -C "$repo" add -A -- . "${excl[@]}"
  GIT_INDEX_FILE="$tmpidx" git -C "$repo" diff --cached --binary HEAD > "$out"
  rm -f "$tmpidx"
}

# NUL-separated list of files to stage from repo $1: tracked + untracked non-ignored, secrets excluded.
sr_source_files() {
  local repo="$1" l f
  local -a excl=()
  while IFS= read -r l; do excl+=("$l"); done < <(sr_secret_pathspecs)
  git -C "$repo" ls-files -z --cached --others --exclude-standard -- . "${excl[@]}" \
    | while IFS= read -r -d '' f; do
        [[ -f "$repo/$f" ]] && printf '%s\0' "$f"
      done
  return 0
}

# ---------------------------------------------------------------------------
# Arduino CLI
# ---------------------------------------------------------------------------
# Downloads from release-assets.githubusercontent.com regularly exceed the default timeout.
sr_arduino_network_timeout() {
  # `config get` returns the built-in default (1m0s); `config dump` only lists explicit settings.
  [[ -z "${ARDUINO_NETWORK_CONNECTION_TIMEOUT:-}" ]] || return 0
  if ! arduino-cli config dump 2>/dev/null | grep -q 'connection_timeout'; then
    export ARDUINO_NETWORK_CONNECTION_TIMEOUT=600s
  fi
}

# Size in bytes of the first app partition declared in a partitions.csv (K/M suffixes and hex supported).
sr_app_partition_size() {
  awk -F, '
    /^[[:space:]]*#/ { next }
    {
      t = $2; s = $5
      gsub(/[[:space:]]/, "", t); gsub(/[[:space:]]/, "", s)
      if (t != "app") next
      m = 1
      if (s ~ /[Kk]$/) { m = 1024; sub(/[Kk]$/, "", s) }
      else if (s ~ /[Mm]$/) { m = 1048576; sub(/[Mm]$/, "", s) }
      if (s ~ /^0[xX]/) {
        v = 0; s = tolower(substr(s, 3))
        for (i = 1; i <= length(s); i++) v = v * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
      } else v = s + 0
      printf "%d\n", v * m
      exit
    }' "$1"
}

# Value of KEY in a KEY=value file (read, never sourced); empty if missing.
sr_conf_get() {
  [[ -f "$1" ]] || return 0
  sed -n -E "s/^[[:space:]]*$2[[:space:]]*=[[:space:]]*\"?([^\"#]*)\"?.*$/\1/p" "$1" \
    | tail -n1 | sed -E 's/[[:space:]]+$//'
}

# Build timestamp used in labels: ISO 8601 basic date and time, e.g. 20261013T0915.
sr_label_ts() { date +%Y%m%dT%H%M; }

# Branch/ref name reduced to a label-safe word: "feature/x y" -> "feature-x-y", max 24 chars.
sr_label_part() {
  printf '%s' "$1" | sed -E -e 's#^(refs/(heads|remotes|tags)/)?(origin/)?##' -e 's#[^A-Za-z0-9._-]+#-#g' \
    -e 's#^-+##' | cut -c1-24 | sed -E 's#[-.]+$##'
}

# Build label: <base>[+N][-wip]-<timestamp>, e.g. main+3-wip-20261013T0915.
sr_build_label() {
  local base="$1" merged="$2" wip="$3" ts="$4" label
  label="$(sr_label_part "$base")"
  [[ -n "$label" ]] || label="detached"
  [[ "$merged" -eq 0 ]] || label="$label+$merged"
  [[ "$wip" -eq 0 ]] || label="$label-wip"
  printf '%s-%s\n' "$label" "$ts"
}
