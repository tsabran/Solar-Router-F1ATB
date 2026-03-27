#!/bin/bash
# Local Build Script
# Usage: ./merge-and-build.sh [--localdev] [--localrelease] [branch1 ...]
# Or:    ./merge-and-build.sh   (reads from merge-branches.conf, defaults to --localdev)
#
# Modes (combinable):
#   --localdev     (default) Temp clone of the current branch with working-tree
#                  changes applied on top. Merges branches. VERSION_SUFFIX=localdev.
#   --localrelease Fetch origin/main into local repo first (so the clone has it),
#                  then temp clone starting from origin/main. Merges branches.
#                  VERSION_SUFFIX=localrelease.
#
# Both modes use a temporary clone and never touch any local branch or working tree.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INVOCATION_DIR="$(pwd)"

# --------------------------------------------------------------------------- #
# Flag parsing  (flags must come before branch names)
# --------------------------------------------------------------------------- #
BUILD_LOCALDEV=false
BUILD_LOCALRELEASE=false

while [ $# -gt 0 ]; do
    case "$1" in
        --localdev)     BUILD_LOCALDEV=true;     shift ;;
        --localrelease) BUILD_LOCALRELEASE=true; shift ;;
        --help|-h)
            echo "Local Build Script"
            echo ""
            echo "Builds firmware in a temporary clone. Never touches local branches."
            echo ""
            echo "Usage:"
            echo "  $0 [--localdev] [--localrelease] [branch1 ...]"
            echo "  $0                  (reads branches from merge-branches.conf)"
            echo ""
            echo "Modes (combinable, --localdev is default):"
            echo "  --localdev      Current branch + working-tree changes, merge branches,"
            echo "                  build (VERSION_SUFFIX=localdev)."
            echo "  --localrelease  Fetch origin/main locally first, then temp clone"
            echo "                  starting from origin/main, merge branches,"
            echo "                  build (VERSION_SUFFIX=localrelease)."
            echo ""
            echo "Config file: $INVOCATION_DIR/merge-branches.conf"
            echo ""
            echo "Examples:"
            echo "  $0"
            echo "  $0 --localdev"
            echo "  $0 --localrelease"
            echo "  $0 --localdev --localrelease"
            exit 0
            ;;
        *) break ;;
    esac
done

# Default to localdev if no mode specified
[ "$BUILD_LOCALDEV" = false ] && [ "$BUILD_LOCALRELEASE" = false ] && BUILD_LOCALDEV=true

CONFIG_FILE="$INVOCATION_DIR/merge-branches.conf"
BUILD_SCRIPT="$SCRIPT_DIR/build-binary.sh"
BASE_AUTOMATION_BRANCH="custom-release-automation"
WORK_RELEASE_BRANCH="custom-release"
BRANCH_INPUT_MODE="args"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

run_cmd() {
    echo -e "${BLUE}+ $*${NC}" >&2
    "$@"
}

refresh_available_branches_in_config() {
    local marker="# Autres branches disponibles (décommenter pour inclure):"
    local tmp_file selected_file branches_file line branch

    [ -f "$CONFIG_FILE" ] || return 0

    tmp_file="$(mktemp)"
    selected_file="$(mktemp)"
    branches_file="$(mktemp)"

    while IFS= read -r line; do
        line="$(echo "$line" | xargs)"
        [[ -z "$line" || "$line" =~ ^# ]] && continue
        printf '%s\n' "$line" >> "$selected_file"
    done < "$CONFIG_FILE"

    git -C "$SCRIPT_DIR" for-each-ref --format='%(refname:short)' refs/heads | sort > "$branches_file"

    awk -v marker="$marker" '
        { print }
        $0 == marker { found=1; exit }
        END { if (!found) print marker }
    ' "$CONFIG_FILE" > "$tmp_file"

    while IFS= read -r branch; do
        [ "$branch" = "$BASE_AUTOMATION_BRANCH" ] && continue
        [ "$branch" = "$WORK_RELEASE_BRANCH" ] && continue
        [ "$branch" = "main" ] && continue
        grep -Fxq "$branch" "$selected_file" && continue
        printf '# %s\n' "$branch" >> "$tmp_file"
    done < "$branches_file"

    mv "$tmp_file" "$CONFIG_FILE"
    rm -f "$selected_file" "$branches_file"
}

# --------------------------------------------------------------------------- #
# Branch resolution
# --------------------------------------------------------------------------- #
if [ $# -eq 0 ]; then
    BRANCH_INPUT_MODE="config"
    echo -e "${YELLOW}No branches specified in arguments.${NC}"
    echo -e "${YELLOW}Reading from config file: $CONFIG_FILE${NC}"
    echo ""

    if [ ! -f "$CONFIG_FILE" ]; then
        echo -e "${RED}Error: Config file not found: $CONFIG_FILE${NC}"
        exit 1
    fi

    echo -e "${YELLOW}Refreshing available branches in config...${NC}"
    refresh_available_branches_in_config

    BRANCHES_TO_MERGE=()
    while IFS= read -r line; do
        [[ "$line" =~ ^#.*$ ]] && continue
        [[ -z "$line" ]] && continue
        line=$(echo "$line" | xargs)
        BRANCHES_TO_MERGE+=("$line")
    done < "$CONFIG_FILE"

    if [ ${#BRANCHES_TO_MERGE[@]} -eq 0 ]; then
        echo -e "${RED}Error: No branches found in config file${NC}"
        echo "Usage: $0 branch1 [branch2 branch3 ...]"
        echo "Or: Edit $CONFIG_FILE and run without arguments"
        echo ""
        echo "Available local branches:"
        git -C "$SCRIPT_DIR" branch -v
        exit 1
    fi
else
    BRANCHES_TO_MERGE=("$@")

    if [ -f "$CONFIG_FILE" ]; then
        echo -e "${YELLOW}Refreshing available branches in config...${NC}"
        refresh_available_branches_in_config
    fi
fi

# Resolve repo root and current branch (needed for localdev working-tree snapshot)
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
CURRENT_BRANCH="$(git -C "$REPO_ROOT" branch --show-current)"
if [ -z "$CURRENT_BRANCH" ]; then
    echo -e "${RED}Error: HEAD is detached. Checkout a branch before running this script.${NC}"
    exit 1
fi

if [ ! -x "$BUILD_SCRIPT" ]; then
    echo -e "${RED}Error: Build script not found or not executable: $BUILD_SCRIPT${NC}"
    exit 1
fi

# --------------------------------------------------------------------------- #
# Cleanup trap for all temp clones
# --------------------------------------------------------------------------- #
TEMP_DIRS=()
cleanup_all() {
    if [ ${#TEMP_DIRS[@]} -gt 0 ]; then
        echo ""
        echo -e "${YELLOW}Cleaning up temporary clones...${NC}"
        for d in "${TEMP_DIRS[@]}"; do rm -rf "$d"; done
    fi
}
 trap cleanup_all EXIT

# --------------------------------------------------------------------------- #
# Helper: merge BRANCHES_TO_MERGE in current directory
# --------------------------------------------------------------------------- #
do_merge_branches() {
    local failed=() succeeded=() local_ref

    for branch in "${BRANCHES_TO_MERGE[@]}"; do
        echo -e "${BLUE}Merging branch: ${branch}${NC}"

        local_ref=""
        if git rev-parse --verify "$branch" > /dev/null 2>&1; then
            local_ref="$branch"
        elif git rev-parse --verify "origin/$branch" > /dev/null 2>&1; then
            local_ref="origin/$branch"
        fi

        if [ -z "$local_ref" ]; then
            echo -e "${RED}✗ Branch '$branch' not found${NC}"
            failed+=("$branch")
            echo ""
            continue
        fi

        if run_cmd git merge --no-edit "$local_ref" > /dev/null 2>&1; then
            echo -e "${GREEN}✓ Successfully merged: $branch${NC}"
            succeeded+=("$branch")
        else
            echo -e "${RED}✗ Merge conflict in: $branch${NC}"
            failed+=("$branch")
            echo "  Resolve conflicts on the source branch before retrying."
            echo ""
            exit 1
        fi
        echo ""
    done

    echo -e "${BLUE}=== Summary ===${NC}"
    echo -e "${GREEN}Successfully merged: ${#succeeded[@]} branch(es)${NC}"
    for b in "${succeeded[@]}"; do echo "  - $b"; done

    if [ ${#failed[@]} -gt 0 ]; then
        echo ""
        echo -e "${RED}Failed/Not found: ${#failed[@]} branch(es)${NC}"
        for b in "${failed[@]}"; do echo "  - $b"; done
        echo ""
        echo -e "${RED}Build skipped because some branches failed to merge.${NC}"
        exit 1
    fi

    echo ""
    echo -e "${YELLOW}Git log graph:${NC}"
    git --no-pager log --graph --oneline --decorate --boundary \
        "HEAD~$((${#succeeded[@]} + 2))..HEAD" 2>/dev/null \
        || git --no-pager log --graph --oneline --decorate -10
}

# --------------------------------------------------------------------------- #
# Helper: run a build in a temp clone
#   $1 = mode: localdev | localrelease
# --------------------------------------------------------------------------- #
run_local_build() {
    local mode="$1"
    local temp_dir temp_clone step=1
    temp_dir="$(mktemp -d)"
    TEMP_DIRS+=("$temp_dir")
    temp_clone="$temp_dir/clone"

    echo -e "${BLUE}=== Build: $mode ===${NC}"
    echo ""
    echo -e "${BLUE}Repo:         $REPO_ROOT${NC}"
    echo -e "${BLUE}Branch:       $CURRENT_BRANCH${NC}"
    echo -e "${BLUE}Config file:  $CONFIG_FILE${NC}"
    echo -e "${BLUE}Branches (${#BRANCHES_TO_MERGE[@]}): ${BRANCHES_TO_MERGE[*]}${NC}"
    echo -e "${BLUE}Build script: $BUILD_SCRIPT${NC}"
    echo ""

    # For localrelease: fetch origin/main into the SOURCE repo before cloning,
    # because the clone is local-only and has no GitHub remote access.
    if [ "$mode" = "localrelease" ]; then
        echo -e "${YELLOW}Step $step: Fetching origin/main and updating local main...${NC}"
        run_cmd git -C "$REPO_ROOT" fetch origin main

        LOCAL_MAIN="$(git -C "$REPO_ROOT" rev-parse main 2>/dev/null || true)"
        ORIGIN_MAIN="$(git -C "$REPO_ROOT" rev-parse origin/main)"

        if [ -z "$LOCAL_MAIN" ]; then
            echo -e "${YELLOW}⚠ Local branch 'main' does not exist, skipping sync.${NC}"
        elif [ "$LOCAL_MAIN" = "$ORIGIN_MAIN" ]; then
            echo -e "${GREEN}✓ main is already up to date with origin/main${NC}"
        elif git -C "$REPO_ROOT" merge-base --is-ancestor "$LOCAL_MAIN" "$ORIGIN_MAIN"; then
            # main is strictly behind origin/main: safe to fast-forward
            echo -e "${YELLOW}New commits on origin/main:${NC}"
            git -C "$REPO_ROOT" --no-pager log --oneline --decorate "${LOCAL_MAIN}..origin/main"
            run_cmd git -C "$REPO_ROOT" branch -f main origin/main
            echo -e "${GREEN}✓ main fast-forwarded to origin/main${NC}"
        else
            echo -e "${RED}Error: local 'main' has diverged from 'origin/main'.${NC}"
            echo "  local:  $(git -C "$REPO_ROOT" log -1 --oneline main)"
            echo "  origin: $(git -C "$REPO_ROOT" log -1 --oneline origin/main)"
            echo ""
            echo "This is likely a mistake. Fix it before building a localrelease:"
            echo "  git checkout main && git reset --hard origin/main"
            exit 1
        fi
        echo ""; (( step++ )) || true
    fi

    # Clone
    echo -e "${YELLOW}Step $step: Cloning local repo into temporary directory...${NC}"
    run_cmd git clone --local --no-single-branch --no-tags "$REPO_ROOT" "$temp_clone"
    echo -e "${GREEN}✓ Clone ready${NC}"
    echo ""; (( step++ )) || true

    cd "$temp_clone"
    trap 'cd "$REPO_ROOT"' RETURN

    if [ "$mode" = "localdev" ]; then
        # Apply current working-tree changes on top of the cloned branch
        echo -e "${YELLOW}Step $step: Applying local working-tree changes...${NC}"
        run_cmd rsync -a --delete \
            --exclude '.git' \
            --exclude 'build/' \
            --exclude 'build-binary.log' \
            --exclude '.tmp-build/' \
            "$REPO_ROOT/" "$temp_clone/"
        if [ -n "$(git status --porcelain)" ]; then
            run_cmd git add -A
            run_cmd git commit -m "wip: local working-tree changes" --quiet
            echo -e "${GREEN}✓ Local changes committed in clone${NC}"
        else
            echo -e "${GREEN}✓ No local changes to apply (working tree is clean)${NC}"
        fi
        echo ""; (( step++ )) || true

    elif [ "$mode" = "localrelease" ]; then
        # Start from origin/main (already up to date thanks to pre-clone fetch)
        echo -e "${YELLOW}Step $step: Starting from origin/main...${NC}"
        run_cmd git checkout -b release-temp origin/main
        echo -e "${GREEN}✓ Checked out origin/main${NC}"
        echo ""; (( step++ )) || true
    fi

    # Merge
    echo -e "${YELLOW}Step $step: Merging branches...${NC}"
    echo ""
    do_merge_branches
    (( step++ )) || true

    # Build
    echo ""
    echo -e "${YELLOW}Step $step: Building firmware binary ($mode)...${NC}"

    if run_cmd env VERSION_SUFFIX="$mode" WORKDIR="$temp_clone" "$BUILD_SCRIPT"; then
        echo -e "${GREEN}✓ Build complete ($mode)${NC}"
    else
        echo -e "${RED}✗ Build failed ($mode)${NC}"
        exit 1
    fi

    echo ""
}

# --------------------------------------------------------------------------- #
# Execute requested modes
# --------------------------------------------------------------------------- #
[ "$BUILD_LOCALDEV" = true ]     && run_local_build "localdev"
[ "$BUILD_LOCALRELEASE" = true ] && run_local_build "localrelease"
exit 0
