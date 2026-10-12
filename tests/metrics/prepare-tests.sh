#!/usr/bin/env bash
set -euo pipefail

include="${ARDUINOJSON_INCLUDE:-}"
if [[ -z "$include" ]]; then
    for candidate in "${ARDUINO_USER_DIR:-$HOME/Documents/Arduino}/libraries/ArduinoJson/src" \
                     "$HOME/Arduino/libraries/ArduinoJson/src"; do
        if [[ -f "$candidate/ArduinoJson.h" ]]; then include="$candidate"; break; fi
    done
fi
if [[ ! -f "$include/ArduinoJson.h" ]]; then
    echo "ArduinoJson headers missing. Set ARDUINOJSON_INCLUDE to the installed library's src directory." >&2
    exit 1
fi
printf '%s\n' "$include"

awk '
  /^void (DiscoveryEnergiePotentielle|PublierEnergiePotentielle)\(/ { body = 1; functions++ }
  body { print; if (/^}$/) body = 0 }
  END {
    if (functions != 2 || body) {
      print "Cannot extract energy MQTT firmware functions" > "/dev/stderr"
      exit 1
    }
  }
' "$1/EnvoiMQTT.ino" > "$2/metrics_mqtt.inc"
