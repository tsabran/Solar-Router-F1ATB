#!/usr/bin/env bash
set -euo pipefail

root="$1"
build="$2"
main_ino=("$root"/Solar_Router_V*.ino)
[[ ${#main_ino[@]} -eq 1 && -f "${main_ino[0]}" ]] || { echo "Expected exactly one Solar_Router_V*.ino in $root" >&2; exit 1; }
# Compile the current firmware bodies, not a second implementation of the controller.
awk '
  /^#define MODE_/ || /^#define LES_ACTIONS_LENGTH / { print }
  /^uint8_t tabPulseSinus(Total|On)\[101\]/ { table = 1; tables++ }
  table { print; if (/};/) table = 0 }
  /^(void (AppliquerRetard|GestionOverproduction)|bool RelaisSequenceValide)\(/ { body = 1; functions++ }
  body { print; if (/^}$/) body = 0 }
  END {
    if (functions != 3 || tables != 2 || body || table) {
      print "Cannot extract sequencer firmware functions/tables" > "/dev/stderr"
      exit 1
    }
  }
' "${main_ino[0]}" > "$build/sequenceur_firmware.inc"
