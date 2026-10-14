#!/usr/bin/env bash
# Compile the firmware with arduino-cli and archive the binaries with a manifest.
#
# The sources (--workdir) and the tooling (tools/, sketch.yaml) are independent:
# any branch or worktree can be built with the tooling of the build-automation branch.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=lib/build-lib.sh
source "$SCRIPT_DIR/lib/build-lib.sh"
# Sketch root of the tooling checkout (sketch.yaml, default sources).
SKETCH_DIR="$(dirname "$SCRIPT_DIR")"

usage() {
  cat <<'USAGE'
Usage:
  build-binary.sh [options] [MAIN_INO_FILE]

Compiles the sketch found in the work directory and archives the result in
releases/<Sketch>_<BUILD_TAG>/ (app firmware .bin, .elf.gz for crash decoding,
manifest.txt, worktree.patch, build.log, SHA256SUMS).

Options:
  --workdir PATH   Sources to build (default: $WORKDIR, else the current directory if it
                   contains the main .ino, else the directory of this script)
  --profile NAME   arduino-cli profile from sketch.yaml (default: default_profile, else esp32)
  --no-profile     Ignore sketch.yaml and use $FQBN with the globally installed core/libraries
  --no-minify      Keep original resource bodies (URLs are still content-versioned)
  --no-gzip        Keep resource bodies uncompressed (default: precompress at build time)
  --define NAME[=VALUE]
                   Add a preprocessor define (repeatable), e.g. --define DIAG_HEAP for a
                   measurement build or --define HTTP_DEBUG_LOG
  -h, --help       Show this help

Environment variables:
  BUILD_TAG        Label shown in the firmware version and naming the release, used as is
                   (default: <branch>[-wip]-<YYYYMMDDTHHMM>, -wip when there are local changes)
  BUILD_GIT_SHA    Commit shown in the firmware version (default: short HEAD of the work directory,
                   suffixed with -dirty when it has local changes)
  FQBN             Board when building without profile (default: esp32:esp32:esp32:PartitionScheme=custom)
  SR_OUTPUT_ROOT   Root for releases/ and .tmp-build/ (default: main worktree of the repo)
  ARCHIVE_DIR      Archive root (default: $SR_OUTPUT_ROOT/releases)
  SR_KEEP_TMP=1    Keep the staging directory in .tmp-build/
  ASSET_PYTHON     Python with tools/requirements-assets.txt (default: tooling .venv, else python3)
  NO_COLOR / FORCE_COLOR
USAGE
}

WORK_DIR="${WORKDIR:-}"
PROFILE="${SKETCH_PROFILE:-}"
USE_PROFILE=1
MAIN_INO=""
ASSET_ARGS=()
DEFINES=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --workdir) [[ -n "${2:-}" ]] || die "--workdir requires a path"; WORK_DIR="$2"; shift 2 ;;
    --profile) [[ -n "${2:-}" ]] || die "--profile requires a name"; PROFILE="$2"; shift 2 ;;
    --no-profile) USE_PROFILE=0; shift ;;
    --no-minify) ASSET_ARGS+=(--no-minify); shift ;;
    --no-gzip) ASSET_ARGS+=(--no-gzip); shift ;;
    --define)
      [[ "${2:-}" =~ ^[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_.-]*)?$ ]] || die "--define requires NAME or NAME=VALUE"
      DEFINES+=("-D$2"); shift 2 ;;
    -h|--help) usage; exit 0 ;;
    -*) die "Unknown option: $1 (see --help)" ;;
    *) MAIN_INO="$1"; shift ;;
  esac
done

if [[ -z "$WORK_DIR" ]]; then
  if [[ -n "$(sr_find_main_ino "$PWD")" ]]; then WORK_DIR="$PWD"; else WORK_DIR="$SKETCH_DIR"; fi
fi
WORK_DIR="$(sr_abspath "$WORK_DIR")" || die "Work directory does not exist: $WORK_DIR"

[[ -n "$MAIN_INO" ]] || MAIN_INO="$(sr_find_main_ino "$WORK_DIR")"
[[ -n "$MAIN_INO" && -f "$WORK_DIR/$MAIN_INO" ]] \
  || die "Main .ino not found in $WORK_DIR (pass it explicitly, e.g. build-binary.sh Solar_Router_V17_25.ino)"
[[ "$MAIN_INO" == *.ino ]] || die "Main file must end with .ino: $MAIN_INO"
SKETCH_NAME="${MAIN_INO%.ino}"
FW_VERSION="$(sed -n 's/^#define[[:space:]]\{1,\}Version[[:space:]]\{1,\}"\([^"]*\)".*/\1/p' "$WORK_DIR/$MAIN_INO" | head -n1)"

IS_GIT=0
git -C "$WORK_DIR" rev-parse --git-dir >/dev/null 2>&1 && IS_GIT=1

if [[ -z "${BUILD_TAG:-}" ]]; then
  # Default label: <branch>[-wip]-<timestamp>; -wip when uncommitted changes are built.
  if [[ "$IS_GIT" -eq 1 ]]; then
    wip=0
    [[ -z "$(git -C "$WORK_DIR" status --porcelain --untracked-files=normal 2>/dev/null)" ]] || wip=1
    BUILD_TAG="$(sr_build_label "$(git -C "$WORK_DIR" branch --show-current)" 0 "$wip" "$(sr_label_ts)")"
  else
    BUILD_TAG="nogit-$(sr_label_ts)"
  fi
fi
[[ "$BUILD_TAG" =~ ^[A-Za-z0-9._+-]+$ ]] || die "BUILD_TAG may only contain [A-Za-z0-9._+-]: $BUILD_TAG"

SR_ROOT="$(sr_output_root "$WORK_DIR")"
ARCHIVE_DIR="${ARCHIVE_DIR:-$SR_ROOT/releases}"
ARTIFACT="${SKETCH_NAME}_${BUILD_TAG}"
RELEASE_DIR="$ARCHIVE_DIR/$ARTIFACT"
# Outside TMP_ROOT, which is recreated after logging starts; copied into the release.
LOG_FILE="$SR_ROOT/.tmp-build/$ARTIFACT.build.log"
TMP_ROOT="$SR_ROOT/.tmp-build/$ARTIFACT"
STAGE_DIR="$TMP_ROOT/$SKETCH_NAME"

[[ ! -e "$RELEASE_DIR" ]] || die "Release directory already exists: $RELEASE_DIR"

BUILD_OK=0
cleanup() {
  if [[ "$BUILD_OK" -ne 1 && -d "$RELEASE_DIR" ]]; then
    rm -rf "$RELEASE_DIR"
    log FAIL "Build failed, partial release removed"
  fi
  if [[ "${SR_KEEP_TMP:-0}" == "1" ]]; then
    log INFO "Staging kept: $TMP_ROOT (log: $LOG_FILE)"
  else
    rm -rf "$TMP_ROOT" "$LOG_FILE"
    rmdir "$SR_ROOT/.tmp-build" 2>/dev/null || true
  fi
}
trap cleanup EXIT

rm -f "$LOG_FILE"
sr_tee_log "$LOG_FILE"
command -v arduino-cli >/dev/null || die "arduino-cli not found in PATH"
command -v python3 >/dev/null || die "python3 not found in PATH"

# ---------------------------------------------------------------------------
# Staging: Arduino requires the sketch folder name to match the main .ino.
# Only tracked + untracked non-ignored files are staged, secrets excluded.
# ---------------------------------------------------------------------------
rm -rf "$TMP_ROOT"
mkdir -p "$STAGE_DIR"
if [[ "$IS_GIT" -eq 1 ]]; then
  sr_source_files "$WORK_DIR" | run_step "staging (git-listed files)" \
    rsync -a --from0 --files-from=- "$WORK_DIR/" "$STAGE_DIR/"
else
  excludes=(--exclude .git --exclude .tmp-build --exclude build --exclude releases --exclude logs)
  for g in "${SR_SECRET_GLOBS[@]}"; do excludes+=(--exclude "$g"); done
  run_step "staging (rsync)" rsync -a "${excludes[@]}" "$WORK_DIR/" "$STAGE_DIR/"
fi
[[ -f "$STAGE_DIR/$MAIN_INO" ]] || die "Staged main file not found: $STAGE_DIR/$MAIN_INO"

# ---------------------------------------------------------------------------
# Source description for the manifest
# ---------------------------------------------------------------------------
SOURCE_SECTION="${SR_SOURCE_SECTION:-}"
SOURCE_PATCH="${SR_SOURCE_PATCH:-}"
if [[ -z "$SOURCE_SECTION" ]]; then
  SOURCE_SECTION="$TMP_ROOT/source.yaml"
  if [[ "$IS_GIT" -eq 1 ]]; then
    SOURCE_PATCH="$TMP_ROOT/worktree.patch"
    sr_worktree_patch "$WORK_DIR" "$SOURCE_PATCH"
    head_sha="$(git -C "$WORK_DIR" rev-parse HEAD)"
    branch="$(git -C "$WORK_DIR" branch --show-current)"
    {
      echo "source:"
      echo "  mode: \"build-binary\""
      echo "  worktree: \"$WORK_DIR\""
      echo "  branch: \"${branch:-(detached)}\""
      echo "  head_sha: \"$head_sha\""
      echo "  describe: \"$(git -C "$WORK_DIR" describe --always --dirty --tags 2>/dev/null)\""
      if [[ -s "$SOURCE_PATCH" ]]; then
        echo "  worktree_patch: \"worktree.patch\""
        echo "  worktree_patch_sha256: \"$(sr_sha256 "$SOURCE_PATCH")\""
        echo "  worktree_patch_files:"
        git -C "$WORK_DIR" apply --numstat "$SOURCE_PATCH" | awk '{printf "    - \"%s\"\n", $3}'
      else
        echo "  worktree_patch: null"
      fi
    } > "$SOURCE_SECTION"
    if [[ -z "${BUILD_GIT_SHA:-}" ]]; then
      BUILD_GIT_SHA="$(git -C "$WORK_DIR" rev-parse --short=8 HEAD)"
      [[ -s "$SOURCE_PATCH" ]] && BUILD_GIT_SHA="$BUILD_GIT_SHA-dirty"
    fi
  else
    printf 'source:\n  mode: "build-binary"\n  worktree: "%s"\n  git: false\n' "$WORK_DIR" > "$SOURCE_SECTION"
  fi
fi
BUILD_GIT_SHA="${BUILD_GIT_SHA:-unknown}"
[[ "$BUILD_GIT_SHA" =~ ^[A-Za-z0-9._+-]+$ ]] || die "BUILD_GIT_SHA may only contain [A-Za-z0-9._+-]: $BUILD_GIT_SHA"

# ---------------------------------------------------------------------------
# Board / profile
# ---------------------------------------------------------------------------
FQBN="${FQBN:-esp32:esp32:esp32:PartitionScheme=custom}"
PROFILE_ARGS=()
if [[ "$USE_PROFILE" -eq 1 ]]; then
  if [[ ! -f "$STAGE_DIR/sketch.yaml" && -f "$SKETCH_DIR/sketch.yaml" ]]; then
    cp "$SKETCH_DIR/sketch.yaml" "$STAGE_DIR/sketch.yaml"
    log INFO "sketch.yaml taken from tooling: $SKETCH_DIR/sketch.yaml"
  fi
  if [[ -f "$STAGE_DIR/sketch.yaml" ]]; then
    [[ -n "$PROFILE" ]] || PROFILE="$(sed -n 's/^default_profile:[[:space:]]*//p' "$STAGE_DIR/sketch.yaml" | tr -d '"'\''[:space:]')"
    PROFILE="${PROFILE:-esp32}"
    FQBN="$(awk -v p="$PROFILE" '
      $0 ~ "^  " p ":[[:space:]]*$" { inp = 1; next }
      inp && /^  [^ ]/ { inp = 0 }
      inp && /^    fqbn:/ { sub(/^    fqbn:[[:space:]]*/, ""); print; exit }' "$STAGE_DIR/sketch.yaml")"
    [[ -n "$FQBN" ]] || die "Profile '$PROFILE' (or its fqbn) not found in sketch.yaml"
    PROFILE_ARGS=(--profile "$PROFILE")
  else
    log WARN "No sketch.yaml: building with the globally installed core and libraries"
    USE_PROFILE=0
    PROFILE=""
  fi
else
  rm -f "$STAGE_DIR/sketch.yaml"
  PROFILE=""
fi
[[ "${#PROFILE_ARGS[@]}" -gt 0 ]] || PROFILE_ARGS=(--fqbn "$FQBN")

if [[ "$FQBN" == *PartitionScheme=custom* && ! -f "$STAGE_DIR/partitions.csv" ]]; then
  die "PartitionScheme=custom requires partitions.csv at the sketch root"
fi

# ---------------------------------------------------------------------------
# Version flags in build_opt.h (overrides any BUILD_TAG / BUILD_GIT_SHA already present)
# ---------------------------------------------------------------------------
BUILD_OPT="$STAGE_DIR/build_opt.h"
touch "$BUILD_OPT"
{ grep -v -e '-DBUILD_TAG=' -e '-DBUILD_GIT_SHA=' "$BUILD_OPT" || true; } > "$BUILD_OPT.tmp"
printf -- '-DBUILD_TAG=\\"%s\\"\n-DBUILD_GIT_SHA=\\"%s\\"\n' "$BUILD_TAG" "$BUILD_GIT_SHA" >> "$BUILD_OPT.tmp"
mv "$BUILD_OPT.tmp" "$BUILD_OPT"

log INFO "Sketch:      $MAIN_INO (Version $FW_VERSION)"
log INFO "Work dir:    $WORK_DIR"
log INFO "Build tag:   $BUILD_TAG"
log INFO "Git sha:     $BUILD_GIT_SHA"
log INFO "FQBN:        $FQBN"
log INFO "Profile:     ${PROFILE:-(none)}"
log INFO "Release dir: $RELEASE_DIR"

# Older source branches without the cache helper remain buildable with this tooling.
ASSET_REPORT="$TMP_ROOT/assets.json"
if [[ -f "$STAGE_DIR/StaticAssetCache.h" ]]; then
  if [[ -z "${ASSET_PYTHON:-}" ]]; then
    ASSET_PYTHON=python3
    [[ ! -x "$SKETCH_DIR/.venv/bin/python" ]] || ASSET_PYTHON="$SKETCH_DIR/.venv/bin/python"
  fi
  run_step "prepare embedded assets" "$ASSET_PYTHON" "$SCRIPT_DIR/prepare-assets.py" \
    --stage "$STAGE_DIR" --report "$ASSET_REPORT" "${ASSET_ARGS[@]}"
fi

# ---------------------------------------------------------------------------
# Compile
# ---------------------------------------------------------------------------
sr_arduino_network_timeout
# A per-build --build-path also makes arduino-cli recompile the core instead of reusing the
# cached core.a, so the "Compile Date/Time" printed at boot is the real build time.
COMPILE_JSON="$TMP_ROOT/compile.json"
COMPILE_CMD=(arduino-cli compile "${PROFILE_ARGS[@]}" --build-path "$TMP_ROOT/build-path"
  --output-dir "$TMP_ROOT/out" --json "$STAGE_DIR")
if [[ "${#DEFINES[@]}" -gt 0 ]]; then
  # Empty by default on esp32:esp32; the board defines stay in build.extra_flags.
  COMPILE_CMD+=(--build-property "compiler.cpp.extra_flags=${DEFINES[*]}"
    --build-property "compiler.c.extra_flags=${DEFINES[*]}")
  log INFO "Defines:     ${DEFINES[*]}"
fi
compile_rc=0
compile_start="$(date +%s)"
log START "arduino-cli compile (output shown at the end)"
printf '%b+ %s%b\n' "$C_CMD" "${COMPILE_CMD[*]}" "$C_RESET"
"${COMPILE_CMD[@]}" > "$COMPILE_JSON" || compile_rc=$?
python3 - "$COMPILE_JSON" <<'PY' || true
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print(open(sys.argv[1]).read())
    sys.exit(0)
for k in ("compiler_out", "compiler_err"):
    if d.get(k): print(d[k], end="" if d[k].endswith("\n") else "\n")
if d.get("error"): print("error:", d["error"])
PY
[[ "$compile_rc" -eq 0 ]] || die "arduino-cli compile failed (exit $compile_rc)"
log END "arduino-cli compile ($(( $(date +%s) - compile_start ))s)"

BIN_FILE="$TMP_ROOT/out/$MAIN_INO.bin"
[[ -f "$BIN_FILE" ]] || die "Expected firmware file not found: $BIN_FILE"

# arduino-cli checks against upload.maximum_size (16 MB for the custom scheme), not partitions.csv.
BIN_SIZE="$(wc -c < "$BIN_FILE" | tr -d ' ')"
APP_SIZE=0
if [[ -f "$STAGE_DIR/partitions.csv" ]]; then
  APP_SIZE="$(sr_app_partition_size "$STAGE_DIR/partitions.csv")"
  if [[ "${APP_SIZE:-0}" -gt 0 ]]; then
    pct=$(( BIN_SIZE * 100 / APP_SIZE ))
    log INFO "Firmware size: $BIN_SIZE / $APP_SIZE bytes app partition ($pct%, $(( APP_SIZE - BIN_SIZE )) bytes free)"
    [[ "$BIN_SIZE" -le "$APP_SIZE" ]] || die "Firmware does not fit in the app partition of partitions.csv"
    [[ "$pct" -lt 95 ]] || log WARN "Firmware uses $pct% of the app partition"
  fi
fi

# ---------------------------------------------------------------------------
# Archive
# ---------------------------------------------------------------------------
mkdir -p "$RELEASE_DIR"

ARTIFACTS=()
if [[ -f "$ASSET_REPORT" ]]; then
  cp "$ASSET_REPORT" "$RELEASE_DIR/assets.json"
  ARTIFACTS+=(assets.json)
fi
# Bootloader, partition table and merged image are not archived: they only depend on the
# platform and partitions.csv, and flashing the merged image would erase the configuration.
cp "$BIN_FILE" "$RELEASE_DIR/$ARTIFACT.ino.bin"
ARTIFACTS+=("$ARTIFACT.ino.bin")
# The exact .elf is required to decode crash backtraces/coredumps of this firmware.
if [[ -f "$TMP_ROOT/out/$MAIN_INO.elf" ]]; then
  gzip -9 -c "$TMP_ROOT/out/$MAIN_INO.elf" > "$RELEASE_DIR/$ARTIFACT.ino.elf.gz"
  ARTIFACTS+=("$ARTIFACT.ino.elf.gz")
fi
if [[ -n "$SOURCE_PATCH" && -s "$SOURCE_PATCH" ]]; then
  cp "$SOURCE_PATCH" "$RELEASE_DIR/worktree.patch"
  ARTIFACTS+=(worktree.patch)
fi

manifest_args=()
for a in "${ARTIFACTS[@]}"; do manifest_args+=(--artifact "$a"); done
python3 "$SCRIPT_DIR/lib/write-manifest.py" \
  --out "$RELEASE_DIR/manifest.txt" \
  --compile-json "$COMPILE_JSON" \
  --stage-dir "$STAGE_DIR" \
  --release-dir "$RELEASE_DIR" \
  --sketch "$SKETCH_NAME" \
  --version "$FW_VERSION" \
  --build-tag "$BUILD_TAG" \
  --git-sha "$BUILD_GIT_SHA" \
  --fqbn "$FQBN" \
  --profile "$PROFILE" \
  --app-partition-size "${APP_SIZE:-0}" \
  --source-section "$SOURCE_SECTION" \
  "${manifest_args[@]}"

(cd "$RELEASE_DIR" && shasum -a 256 "${ARTIFACTS[@]}" manifest.txt > SHA256SUMS)
BUILD_OK=1
[[ -n "${SR_RESULT_FILE:-}" ]] && printf '%s\n' "$RELEASE_DIR" > "$SR_RESULT_FILE"

log OK "Build done: $RELEASE_DIR"
for f in "${ARTIFACTS[@]}" manifest.txt SHA256SUMS; do log INFO "  - $f"; done
log INFO "Firmware sha256: $(sr_sha256 "$RELEASE_DIR/$ARTIFACT.ino.bin")"
sr_copy_log "$LOG_FILE" "$RELEASE_DIR/build.log"
