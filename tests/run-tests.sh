#!/usr/bin/env bash
# Runner de tests C++ natifs pour Solar-Router-F1ATB
# Compile et exécute la batterie de tests en local sans aucune dépendance externe.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

CXX="${CXX:-clang++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    CXX="g++"
fi

BUILD_DIR="$SCRIPT_DIR/.build"
mkdir -p "$BUILD_DIR"
BIN="$BUILD_DIR/solar_router_tests"

INCLUDES=(-I"$SCRIPT_DIR" -I"$BUILD_DIR")
while IFS= read -r prepare; do
    bash "$prepare" "$PROJECT_ROOT" "$BUILD_DIR"
done < <(find "$SCRIPT_DIR" -type f -name prepare-tests.sh | sort)
while IFS= read -r stubs; do
    INCLUDES+=("-I$stubs")
done < <(find "$SCRIPT_DIR" -type d -name stubs | sort)

echo "🔨 Compilation de la suite de tests ($CXX)..."

# Recherche de tous les fichiers .cpp de tests
SRCS=("$SCRIPT_DIR/main_tests.cpp")
while IFS= read -r f; do
    SRCS+=("$f")
done < <(find "$SCRIPT_DIR" -type f -name "test_*.cpp")

"$CXX" -std=c++17 -Wall -Wextra -O2 \
    "${INCLUDES[@]}" \
    "${SRCS[@]}" \
    -o "$BIN"

echo "🚀 Lancement de la batterie de tests..."
cd "$PROJECT_ROOT"
"$BIN" "$@"

while IFS= read -r test; do
    node "$test" "$@"
done < <(find "$SCRIPT_DIR" -type f -name 'test_*.js' | sort)
