#!/usr/bin/env bash
# Merge a list of branches on top of a base in a temporary clone, then build and archive
# the firmware with build-binary.sh. Local branches and working trees are never modified.
#
# The tooling (this script, build-binary.sh, merge-branches.conf, sketch.yaml) comes from the
# directory of this script; the sources come from --workdir (default: current directory).
# A feature branch therefore does not need to contain the build scripts.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=tools/build-lib.sh
source "$SCRIPT_DIR/tools/build-lib.sh"

usage() {
  cat <<USAGE
Usage:
  $(basename "$0") [options] [branch ...]

Bases (combinable, --localdev is the default):
  --localdev        HEAD of the work directory + its uncommitted changes
                    (staged, unstaged, untracked non-ignored; secrets excluded)
  --localrelease    origin/main after 'git fetch origin main'
  --base REF        Any branch, tag or commit (without local changes)

Options:
  --workdir PATH    Source repository/worktree (default: current directory if it is a git
                    repo, else the directory of this script)
  --config FILE     Branch list (default: $SCRIPT_DIR/merge-branches.conf)
  --no-merge        Build the base only (ignore config and arguments)
  --dry-run         Resolve refs and test the merges in a temporary clone, without compiling
  --keep-clone      Keep the temporary clone in .tmp-build/ for inspection
  --no-ref          Do not save the merged tree as refs/builds/<tag> in the repository
  --no-fetch        --localrelease: use the current origin/main without fetching
  --fresh-core      Passed to build-binary.sh (recompile the ESP32 core)
  --list-branches   List local branches and whether the config selects them, then exit
  -h, --help        Show this help

Branches given as arguments replace the config file. Each name is resolved in the source
repository as refs/heads/NAME, then refs/remotes/origin/NAME, then any commit-ish.

Outputs (SR_OUTPUT_ROOT, default: main worktree of the repository):
  releases/<Sketch>_<mode>-<timestamp>/   firmware, manifest.txt, worktree.patch, logs
  logs/merge-and-build_<timestamp>.log    full run log (merges + compilation)
  refs/builds/<mode>-<timestamp>          exact merged tree that was compiled

Examples:
  $(basename "$0")                                   # current branch + local changes + config
  $(basename "$0") --dry-run
  $(basename "$0") --base enphase-envoy-fix ota-improve
  cd ../my-feature && $SCRIPT_DIR/$(basename "$0")   # build another worktree
USAGE
}

MODES=()
BASE_REF=""
WORK_DIR=""
CONFIG_FILE="$SCRIPT_DIR/merge-branches.conf"
NO_MERGE=0
DRY_RUN=0
KEEP_CLONE=0
SAVE_REF=1
DO_FETCH=1
LIST_BRANCHES=0
BUILD_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --localdev) MODES+=(localdev); shift ;;
    --localrelease) MODES+=(localrelease); shift ;;
    --base) [[ -n "${2:-}" ]] || die "--base requires a ref"; BASE_REF="$2"; MODES+=(base); shift 2 ;;
    --workdir) [[ -n "${2:-}" ]] || die "--workdir requires a path"; WORK_DIR="$2"; shift 2 ;;
    --config) [[ -n "${2:-}" ]] || die "--config requires a file"; CONFIG_FILE="$2"; shift 2 ;;
    --no-merge) NO_MERGE=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --keep-clone) KEEP_CLONE=1; shift ;;
    --no-ref) SAVE_REF=0; shift ;;
    --no-fetch) DO_FETCH=0; shift ;;
    --fresh-core) BUILD_ARGS+=(--fresh-core); shift ;;
    --list-branches) LIST_BRANCHES=1; shift ;;
    -h|--help) usage; exit 0 ;;
    --) shift; break ;;
    -*) die "Unknown option: $1 (see --help)" ;;
    *) break ;;
  esac
done
[[ "${#MODES[@]}" -gt 0 ]] || MODES=(localdev)

# ---------------------------------------------------------------------------
# Source repository and output locations
# ---------------------------------------------------------------------------
if [[ -z "$WORK_DIR" ]]; then
  if git -C "$PWD" rev-parse --git-dir >/dev/null 2>&1; then WORK_DIR="$PWD"; else WORK_DIR="$SCRIPT_DIR"; fi
fi
REPO_ROOT="$(git -C "$WORK_DIR" rev-parse --show-toplevel 2>/dev/null)" || die "Not a git repository: $WORK_DIR"
SR_ROOT="$(sr_output_root "$REPO_ROOT")"
export SR_OUTPUT_ROOT="$SR_ROOT"
[[ "$CONFIG_FILE" = /* ]] || CONFIG_FILE="$PWD/$CONFIG_FILE"

read_config() {
  [[ -f "$CONFIG_FILE" ]] || die "Config file not found: $CONFIG_FILE"
  sed -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' "$CONFIG_FILE" | grep -v '^$' || true
}

if [[ "$LIST_BRANCHES" -eq 1 ]]; then
  selected="$(read_config)"
  echo "Config: $CONFIG_FILE"
  git -C "$REPO_ROOT" for-each-ref --sort=-committerdate \
    --format='%(refname:short)|%(objectname:short)|%(committerdate:short)|%(subject)' refs/heads \
    | while IFS='|' read -r name sha date subject; do
        mark=" "; grep -Fxq "$name" <<<"$selected" && mark="*"
        printf '%s %-40s %s %s %s\n' "$mark" "$name" "$sha" "$date" "${subject:0:60}"
      done
  echo "(* = selected in config)"
  exit 0
fi

BRANCHES=()
BRANCH_SOURCE="$CONFIG_FILE"
if [[ "$NO_MERGE" -eq 1 ]]; then
  BRANCH_SOURCE="none (--no-merge)"
else
  if [[ $# -gt 0 ]]; then
    BRANCHES=("$@")
    BRANCH_SOURCE="arguments"
  else
    while IFS= read -r b; do BRANCHES+=("$b"); done < <(read_config)
  fi
fi

RUN_TS="$(date +%Y%m%d-%H%M%S)"
RUN_LOG="$SR_ROOT/logs/merge-and-build_$RUN_TS.log"
sr_tee_log "$RUN_LOG"
log INFO "Run log:      $RUN_LOG"
log INFO "Tooling:      $SCRIPT_DIR"
log INFO "Source repo:  $REPO_ROOT"
log INFO "Output root:  $SR_ROOT"
log INFO "Modes:        ${MODES[*]}"
log INFO "Branches:     ${BRANCHES[*]:-(none)} [$BRANCH_SOURCE]"
[[ "$DRY_RUN" -eq 1 ]] && log INFO "Dry run:      no compilation, no archive, no refs/builds"

# ---------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------
# Prints "<full ref> <sha>" for a branch name, preferring local branches over origin/.
resolve_ref() {
  local name="$1" sha
  if sha="$(git -C "$REPO_ROOT" rev-parse -q --verify "refs/heads/$name^{commit}" 2>/dev/null)"; then
    echo "refs/heads/$name $sha"
  elif sha="$(git -C "$REPO_ROOT" rev-parse -q --verify "refs/remotes/origin/$name^{commit}" 2>/dev/null)"; then
    echo "refs/remotes/origin/$name $sha"
  elif sha="$(git -C "$REPO_ROOT" rev-parse -q --verify "$name^{commit}" 2>/dev/null)"; then
    echo "$name $sha"
  else
    return 1
  fi
}

MERGE_REFS=()
MERGE_SHAS=()
missing=()
for b in ${BRANCHES[@]+"${BRANCHES[@]}"}; do
  if r="$(resolve_ref "$b")"; then
    MERGE_REFS+=("${r% *}"); MERGE_SHAS+=("${r#* }")
    log INFO "  $b -> ${r% *} ${r#* }"
  else
    missing+=("$b")
  fi
done
[[ "${#missing[@]}" -eq 0 ]] || die "Unknown branch(es): ${missing[*]} (see --list-branches)"
dups="$(printf '%s\n' ${BRANCHES[@]+"${BRANCHES[@]}"} | sort | uniq -d)"
[[ -z "$dups" ]] || die "Branch listed twice: $dups"

for m in "${MODES[@]}"; do
  if [[ "$m" == localdev ]]; then
    git_dir="$(git -C "$REPO_ROOT" rev-parse --absolute-git-dir)"
    [[ -z "$(git -C "$REPO_ROOT" ls-files -u)" ]] || die "Unmerged files in $REPO_ROOT: resolve them first"
    for f in MERGE_HEAD CHERRY_PICK_HEAD REVERT_HEAD rebase-merge rebase-apply; do
      [[ ! -e "$git_dir/$f" ]] || die "Operation in progress in $REPO_ROOT ($f): finish or abort it first"
    done
  fi
done
if [[ "$DRY_RUN" -eq 0 ]]; then
  command -v arduino-cli >/dev/null || die "arduino-cli not found in PATH"
  [[ -x "$SCRIPT_DIR/build-binary.sh" ]] || die "Not executable: $SCRIPT_DIR/build-binary.sh"
fi

TOOLS_SHA="$(git -C "$SCRIPT_DIR" rev-parse HEAD 2>/dev/null || echo unknown)"
TOOLS_BRANCH="$(git -C "$SCRIPT_DIR" branch --show-current 2>/dev/null || true)"
TOOLS_DIRTY=false
if [[ -n "$(git -C "$SCRIPT_DIR" status --porcelain -- merge-and-build.sh build-binary.sh tools sketch.yaml merge-branches.conf 2>/dev/null)" ]]; then
  TOOLS_DIRTY=true
  log WARN "Tooling has uncommitted changes (recorded in the manifest)"
fi

# ---------------------------------------------------------------------------
# Temporary clones
# ---------------------------------------------------------------------------
CLONES=()
cleanup() {
  local d
  for d in ${CLONES[@]+"${CLONES[@]}"}; do
    if [[ "$KEEP_CLONE" -eq 1 ]]; then
      log INFO "Clone kept: $d"
    else
      rm -rf "$d"
    fi
  done
  rmdir "$SR_ROOT/.tmp-build" 2>/dev/null || true
}
trap cleanup EXIT

yaml_q() { printf '"%s"' "$(printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g')"; }

FAILED_MODES=()
run_build() {
  local mode="$1" tag base_desc base_sha tmp clone patch="" section results=() i b sha conflicts=0
  tag="$mode-$RUN_TS"
  [[ "$mode" == base ]] && tag="build-$RUN_TS"
  tmp="$SR_ROOT/.tmp-build/merge-$tag"
  clone="$tmp/clone"
  section="$tmp/source.yaml"

  echo
  log INFO "=== $tag ==="

  case "$mode" in
    localdev)
      base_sha="$(git -C "$REPO_ROOT" rev-parse HEAD)"
      base_desc="$(git -C "$REPO_ROOT" symbolic-ref -q HEAD || echo HEAD)"
      ;;
    localrelease)
      if [[ "$DO_FETCH" -eq 1 ]]; then
        run_cmd git -C "$REPO_ROOT" fetch origin main
      fi
      base_sha="$(git -C "$REPO_ROOT" rev-parse refs/remotes/origin/main)"
      base_desc="refs/remotes/origin/main"
      local local_main
      local_main="$(git -C "$REPO_ROOT" rev-parse -q --verify refs/heads/main || true)"
      if [[ -n "$local_main" && "$local_main" != "$base_sha" ]]; then
        log WARN "Local main ($(git -C "$REPO_ROOT" rev-parse --short main)) differs from origin/main; building origin/main"
      fi
      ;;
    base)
      local r
      r="$(resolve_ref "$BASE_REF")" || die "Unknown base: $BASE_REF"
      base_desc="${r% *}"; base_sha="${r#* }"
      ;;
  esac
  log INFO "Base: $base_desc $base_sha"

  rm -rf "$tmp"
  mkdir -p "$tmp"
  CLONES+=("$tmp")
  run_cmd git clone --quiet --local --no-checkout --no-tags "$REPO_ROOT" "$clone"
  run_cmd git -C "$clone" checkout --quiet -b "build/$tag" "$base_sha"

  if [[ "$mode" == localdev ]]; then
    patch="$tmp/worktree.patch"
    sr_worktree_patch "$REPO_ROOT" "$patch"
    if [[ -s "$patch" ]]; then
      log INFO "Local changes applied on top of the base:"
      git -C "$REPO_ROOT" apply --stat "$patch"
      run_cmd git -C "$clone" apply --index --binary "$patch" || die "Cannot apply local changes"
      run_cmd git -C "$clone" commit --quiet --no-verify \
        -m "wip: local working-tree changes of $(basename "$base_desc")"
    else
      log INFO "No local changes in $REPO_ROOT"
      rm -f "$patch"; patch=""
    fi
  fi

  for i in ${MERGE_SHAS[@]+"${!MERGE_SHAS[@]}"}; do
    b="${BRANCHES[$i]}"; sha="${MERGE_SHAS[$i]}"
    if git -C "$clone" merge-base --is-ancestor "$sha" HEAD; then
      log INFO "= $b ($(git -C "$clone" rev-parse --short "$sha")) already included"
      results+=("already-included")
      continue
    fi
    if run_cmd git -C "$clone" merge --no-ff --no-edit \
         -m "Merge branch '$b' ($(git -C "$clone" rev-parse --short "$sha")) into build/$tag" "$sha"; then
      log OK "+ $b merged"
      results+=("merged")
    else
      log FAIL "Merge conflict with $b (${MERGE_REFS[$i]} $sha). Conflicting files:"
      git -C "$clone" diff --name-only --diff-filter=U | sed 's/^/    /'
      git -C "$clone" merge --abort
      results+=("conflict")
      conflicts=$((conflicts + 1))
      # In dry-run, keep testing the following branches to report every conflict at once.
      [[ "$DRY_RUN" -eq 1 ]] || break
    fi
  done

  echo
  git -C "$clone" --no-pager log --graph --oneline --decorate -n 25 "$base_sha^!" HEAD 2>/dev/null \
    || git -C "$clone" --no-pager log --graph --oneline -n 25

  local build_sha build_ref=null
  build_sha="$(git -C "$clone" rev-parse HEAD)"
  if [[ "$conflicts" -gt 0 ]]; then
    log FAIL "$tag: $conflicts conflict(s). Resolve them on the source branches (or merge them in a dedicated branch) and retry."
    FAILED_MODES+=("$tag")
    return 0
  fi
  if [[ "$DRY_RUN" -eq 0 && "$SAVE_REF" -eq 1 ]]; then
    run_cmd git -C "$REPO_ROOT" fetch --quiet --no-tags "$clone" "build/$tag:refs/builds/$tag"
    build_ref="refs/builds/$tag"
  fi

  {
    echo "source:"
    echo "  mode: $(yaml_q "$mode")"
    echo "  repo: $(yaml_q "$(sr_main_worktree "$REPO_ROOT")")"
    echo "  worktree: $(yaml_q "$REPO_ROOT")"
    echo "  base_ref: $(yaml_q "$base_desc")"
    echo "  base_sha: $(yaml_q "$base_sha")"
    if [[ -n "$patch" ]]; then
      echo "  worktree_patch: \"worktree.patch\""
      echo "  worktree_patch_sha256: $(yaml_q "$(sr_sha256 "$patch")")"
      echo "  worktree_patch_files:"
      git -C "$REPO_ROOT" apply --numstat "$patch" | awk '{printf "    - \"%s\"\n", $3}'
    else
      echo "  worktree_patch: null"
    fi
    echo "  merge_list_from: $(yaml_q "$BRANCH_SOURCE")"
    if [[ "${#results[@]}" -eq 0 ]]; then
      echo "  merges: []"
    else
      echo "  merges:"
      for i in "${!results[@]}"; do
        echo "    - branch: $(yaml_q "${BRANCHES[$i]}")"
        echo "      ref: $(yaml_q "${MERGE_REFS[$i]}")"
        echo "      sha: $(yaml_q "${MERGE_SHAS[$i]}")"
        echo "      result: $(yaml_q "${results[$i]}")"
      done
    fi
    echo "  build_commit: $(yaml_q "$build_sha")"
    if [[ "$build_ref" == null ]]; then echo "  build_ref: null"; else echo "  build_ref: $(yaml_q "$build_ref")"; fi
    echo "tooling:"
    echo "  dir: $(yaml_q "$SCRIPT_DIR")"
    echo "  branch: $(yaml_q "$TOOLS_BRANCH")"
    echo "  sha: $(yaml_q "$TOOLS_SHA")"
    echo "  dirty: $TOOLS_DIRTY"
    echo "  run_log: $(yaml_q "$RUN_LOG")"
  } > "$section"

  if [[ "$DRY_RUN" -eq 1 ]]; then
    log OK "$tag: dry run OK, would build commit $build_sha"
    cat "$section"
    return 0
  fi

  local result_file="$tmp/result" release_dir
  if env BUILD_TAG="$tag" BUILD_GIT_SHA="$(git -C "$clone" rev-parse --short=8 HEAD)" \
       SR_SOURCE_SECTION="$section" SR_SOURCE_PATCH="$patch" SR_RESULT_FILE="$result_file" \
       "$SCRIPT_DIR/build-binary.sh" --workdir "$clone" ${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"}; then
    release_dir="$(cat "$result_file")"
    cp "$RUN_LOG" "$release_dir/merge-and-build.log"
    log OK "$tag: $release_dir"
  else
    log FAIL "$tag: build failed (see $RUN_LOG)"
    FAILED_MODES+=("$tag")
  fi
}

for m in "${MODES[@]}"; do
  run_build "$m"
done

echo
if [[ "${#FAILED_MODES[@]}" -gt 0 ]]; then
  die "Failed: ${FAILED_MODES[*]} (log: $RUN_LOG)"
fi
log OK "All done (log: $RUN_LOG)"
