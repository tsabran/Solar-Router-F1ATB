#!/bin/bash
# Custom Release Builder
# Usage: ./merge-and-build-release.sh [--workdir /path] [branch1 branch2 branch3 ...]
# Or: WORKDIR=/path ./merge-and-build-release.sh [branch1 branch2 branch3 ...]
# Creates/updates custom-release from custom-release-automation, rebases on
# origin/main, then merges specified branches.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INVOCATION_DIR="$(pwd)"

# Resolve work directory from --workdir parameter or WORKDIR env var, default to SCRIPT_DIR
WORK_DIR="${WORKDIR:-$SCRIPT_DIR}"
WORKDIR_SOURCE="default(script_dir)"
if [ -n "${WORKDIR:-}" ]; then
    WORKDIR_SOURCE="env(WORKDIR)"
fi

# Parse --workdir parameter if provided (must be first argument)
if [ $# -gt 0 ] && [ "$1" = "--workdir" ]; then
    if [ $# -lt 2 ]; then
        echo "Error: --workdir requires a path argument"
        exit 1
    fi
    WORK_DIR="$(cd "$2" 2>/dev/null && pwd)" || {
        echo "Error: Work directory does not exist: $2"
        exit 1
    }
    WORKDIR_SOURCE="arg(--workdir)"
    shift 2
fi

cd "$WORK_DIR"
CONFIG_FILE="$INVOCATION_DIR/release.config"
BASE_AUTOMATION_BRANCH="custom-release-automation"
WORK_RELEASE_BRANCH="custom-release"
BUILD_SCRIPT="$SCRIPT_DIR/build-binary.sh"
BRANCH_INPUT_MODE="args"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

refresh_available_branches_in_config() {
    local marker="# Autres branches disponibles (décommenter pour inclure):"
    local tmp_file selected_file branches_file line branch

    [ -f "$CONFIG_FILE" ] || return 0

    tmp_file="$(mktemp)"
    selected_file="$(mktemp)"
    branches_file="$(mktemp)"

    # Collect branches currently enabled in config (non-commented lines)
    while IFS= read -r line; do
        line="$(echo "$line" | xargs)"
        [[ -z "$line" || "$line" =~ ^# ]] && continue
        printf '%s\n' "$line" >> "$selected_file"
    done < "$CONFIG_FILE"

    git for-each-ref --format='%(refname:short)' refs/heads | sort > "$branches_file"

    # Keep file header and selected branches section up to marker
    awk -v marker="$marker" '
        { print }
        $0 == marker { found=1; exit }
        END { if (!found) print marker }
    ' "$CONFIG_FILE" > "$tmp_file"

    # Rebuild commented available branches list
    while IFS= read -r branch; do
        [ "$branch" = "$BASE_AUTOMATION_BRANCH" ] && continue
        [ "$branch" = "$WORK_RELEASE_BRANCH" ] && continue
        [ "$branch" = "main" ] && continue

        if grep -Fxq "$branch" "$selected_file"; then
            continue
        fi

        printf '# %s\n' "$branch" >> "$tmp_file"
    done < "$branches_file"

    mv "$tmp_file" "$CONFIG_FILE"
    rm -f "$selected_file" "$branches_file"
}

ensure_clean_worktree_for_destructive_ops() {
    # Any tracked, staged, or untracked change in target worktree is a hard stop.
    if [ -n "$(git status --porcelain --untracked-files=all)" ]; then
        echo -e "${RED}Error: target worktree is not clean. Refusing destructive operations.${NC}"
        echo "Work directory: $WORK_DIR"
        echo ""
        echo "Pending changes detected:"
        git status --short
        echo ""
        echo "Aborted before checkout/reset --hard to protect in-progress changes."
        exit 1
    fi
}

# Check if branches are specified
if [ $# -eq 0 ]; then
    BRANCH_INPUT_MODE="config"
    echo -e "${YELLOW}No branches specified in arguments.${NC}"
    echo -e "${YELLOW}Reading from config file: $CONFIG_FILE${NC}"
    echo ""
    
    # Check config file exists
    if [ ! -f "$CONFIG_FILE" ]; then
        echo -e "${RED}Error: Config file not found: $CONFIG_FILE${NC}"
        exit 1
    fi

    echo -e "${YELLOW}Refreshing available branches in config...${NC}"
    refresh_available_branches_in_config
    
    # Read branches from config file (portable way)
    BRANCHES_TO_MERGE=()
    while IFS= read -r line; do
        # Skip comments and empty lines
        [[ "$line" =~ ^#.*$ ]] && continue
        [[ -z "$line" ]] && continue
        # Trim whitespace
        line=$(echo "$line" | xargs)
        BRANCHES_TO_MERGE+=("$line")
    done < "$CONFIG_FILE"
    
    if [ ${#BRANCHES_TO_MERGE[@]} -eq 0 ]; then
        echo -e "${RED}Error: No branches found in config file${NC}"
        echo "Usage: $0 branch1 [branch2 branch3 ...]"
        echo "Or: Edit $CONFIG_FILE and run without arguments"
        echo ""
        echo "Available local branches:"
        git branch -v
        exit 1
    fi
elif [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
    echo "Custom Release Builder"
    echo ""
    echo "Usage:"
    echo "  $0 --workdir /path [branch1 ...]  - Use custom work directory"
    echo "  $0 branch1 [branch2 branch3 ...]  - Merge specified branches"
    echo "  $0                                  - Read branches from config"
    echo ""
    echo "Environment variables:"
    echo "  WORKDIR                             - Set work directory (can be overridden by --workdir)"
    echo ""
    echo "Branch workflow:"
    echo "  1. Checkout/create $WORK_RELEASE_BRANCH"
    echo "  2. Reset to $BASE_AUTOMATION_BRANCH"
    echo "  3. Rebase on origin/main"
    echo "  4. Merge selected branches"
    echo "  5. Build firmware binary"
    echo ""
    echo "Config file: $INVOCATION_DIR/release.config"
    echo "Edit this file to define default branches to merge"
    echo ""
    echo "Examples:"
    echo "  # Run from script directory"
    echo "  ./merge-and-build-release.sh"
    echo ""
    echo "  # Run with custom work directory"
    echo "  ./merge-and-build-release.sh --workdir /path/to/custom-release-work"
    echo ""
    echo "  # Use environment variable"
    echo "  WORKDIR=/path/to/custom-release-work ./merge-and-build-release.sh"
    exit 0
else
    BRANCHES_TO_MERGE=("$@")

    if [ -f "$CONFIG_FILE" ]; then
        echo -e "${YELLOW}Refreshing available branches in config...${NC}"
        refresh_available_branches_in_config
    fi
fi

echo -e "${BLUE}=== Custom Release Builder ===${NC}"
echo ""
echo -e "${BLUE}Work directory: $WORK_DIR${NC}"
echo -e "${BLUE}Config file: $CONFIG_FILE${NC}"
echo -e "${BLUE}Selected branches (${#BRANCHES_TO_MERGE[@]}): ${BRANCHES_TO_MERGE[*]}${NC}"
echo -e "${BLUE}Build script: $BUILD_SCRIPT${NC}"
echo ""

# Step 1: Fetch main from remote
echo -e "${YELLOW}Step 1: Fetching main branch...${NC}"
git fetch origin main
echo -e "${GREEN}✓ Fetch complete${NC}"
echo ""

# Step 2: Prepare release branch from automation branch
echo -e "${YELLOW}Step 2: Preparing branch '${WORK_RELEASE_BRANCH}' from '${BASE_AUTOMATION_BRANCH}'...${NC}"

ensure_clean_worktree_for_destructive_ops

if ! git rev-parse --verify "$BASE_AUTOMATION_BRANCH" > /dev/null 2>&1; then
    echo -e "${RED}Error: Base branch '$BASE_AUTOMATION_BRANCH' not found${NC}"
    echo "Create it first before running the script."
    exit 1
fi

if git rev-parse --verify "$WORK_RELEASE_BRANCH" > /dev/null 2>&1; then
    git checkout "$WORK_RELEASE_BRANCH"
else
    git checkout -b "$WORK_RELEASE_BRANCH"
fi

git reset --hard "$BASE_AUTOMATION_BRANCH"
echo -e "${GREEN}✓ '$WORK_RELEASE_BRANCH' reset to '$BASE_AUTOMATION_BRANCH'${NC}"
echo ""

# Step 3: Rebase release branch onto latest main
echo -e "${YELLOW}Step 3: Rebasing '$WORK_RELEASE_BRANCH' onto origin/main...${NC}"
if git rebase origin/main; then
    echo -e "${GREEN}✓ Rebase complete${NC}"
else
    echo -e "${RED}✗ Rebase conflict detected${NC}"
    echo "Resolve conflicts, then run: git rebase --continue"
    exit 1
fi
echo ""

# Step 4: Merge specified branches
echo -e "${YELLOW}Step 4: Merging branches...${NC}"
echo ""

FAILED_MERGES=()
SUCCESSFUL_MERGES=()

for branch in "${BRANCHES_TO_MERGE[@]}"; do
    echo -e "${BLUE}Merging branch: ${branch}${NC}"
    
    # Check if branch exists
    if ! git rev-parse --verify "$branch" > /dev/null 2>&1; then
        echo -e "${RED}✗ Branch '$branch' not found${NC}"
        FAILED_MERGES+=("$branch")
        echo ""
        continue
    fi
    
    # Attempt merge
    if git merge --no-edit "$branch" > /dev/null 2>&1; then
        echo -e "${GREEN}✓ Successfully merged: $branch${NC}"
        SUCCESSFUL_MERGES+=("$branch")
    else
        echo -e "${RED}✗ Merge conflict in: $branch${NC}"
        FAILED_MERGES+=("$branch")
        echo "  Resolve conflicts manually and run: git merge --continue"
        echo ""
        exit 1
    fi
    echo ""
done

# Summary
echo -e "${BLUE}=== Summary ===${NC}"
echo -e "${GREEN}Successfully merged: ${#SUCCESSFUL_MERGES[@]} branch(es)${NC}"
if [ ${#SUCCESSFUL_MERGES[@]} -gt 0 ]; then
    for branch in "${SUCCESSFUL_MERGES[@]}"; do
        echo "  - $branch"
    done
fi

if [ ${#FAILED_MERGES[@]} -gt 0 ]; then
    echo ""
    echo -e "${RED}Failed/Not found: ${#FAILED_MERGES[@]} branch(es)${NC}"
    for branch in "${FAILED_MERGES[@]}"; do
        echo "  - $branch"
    done
    echo ""
    echo -e "${RED}Build skipped because some branches failed to merge.${NC}"
    exit 1
fi

echo ""
echo -e "${YELLOW}Git log graph (origin/main..HEAD):${NC}"
git --no-pager log --graph --oneline --decorate --boundary origin/main..HEAD

echo ""
echo -e "${YELLOW}Step 5: Building firmware binary...${NC}"

if [ ! -x "$BUILD_SCRIPT" ]; then
    echo -e "${RED}Error: Build script not found or not executable: $BUILD_SCRIPT${NC}"
    exit 1
fi

if WORKDIR="$WORK_DIR" "$BUILD_SCRIPT"; then
    echo -e "${GREEN}✓ Build complete${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi

echo ""
echo -e "${BLUE}Release branch ready in: $(pwd)${NC}"
echo -e "${BLUE}Active branch: $(git branch --show-current)${NC}"
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. Review the merged code"
echo "  2. Validate generated binary in build/ and releases/"
echo "  3. Run: git log --oneline --decorate"
echo "  4. Create a tag: git tag -a v1.X.X -m 'Release notes'"
echo "  5. Push to origin: git push origin $WORK_RELEASE_BRANCH"
