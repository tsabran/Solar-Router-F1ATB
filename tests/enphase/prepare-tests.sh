#!/usr/bin/env bash
set -euo pipefail
main_ino=("$1"/Solar_Router_V*.ino)
[[ ${#main_ino[@]} -eq 1 && -f "${main_ino[0]}" ]] || { echo "Expected exactly one Solar_Router_V*.ino in $1" >&2; exit 1; }
awk '
  /^struct WeightedStat/ { body = 1; found++ }
  body { print; if (/^};/) body = 0 }
  /^WeightedStat Envoy/ || /^unsigned long EnvoyCompte/ { print }
  END { if (found != 1 || body) exit 1 }
' "${main_ino[0]}" > "$2/envoy_stats.inc"
awk '
  /^void filtre_puissance\(/ { body = 1; found++ }
  body { print; if (/^}$/) body = 0 }
  END { if (found != 1 || body) exit 1 }
' "$1/Stockage.ino" > "$2/envoy_filter.inc"
awk '
  /^void handle(MainJS[123]|Brute|BruteJS[12]|AjaxHisto48h|AjaxData10mn|CommunCSS)\(/ { body = 1; found++ }
  body { print; if (/^}$/) body = 0 }
  END { if (found != 9 || body) exit 1 }
' "$1/Server.ino" > "$2/envoy_web.inc"
awk '
  /^    if \(Source_data == "Enphase"\)/ { body = 1; found++ }
  body && /^    if \(Source_data == "SmartG"\)/ { body = 0 }
  body { print }
  END { if (found != 1 || body) exit 1 }
' "$1/Server.ino" > "$2/envoy_ajax.inc"
awk '
  /^String (IP2String|ULtoHex)\(/ { body = 1; found++ }
  body { print; if (/^}$/) body = 0 }
  END { if (found != 2 || body) exit 1 }
' "$1/commonFx.ino" > "$2/envoy_common.inc"
awk '
  /^void Append(Int|Float2)\(/ { body = 1; found++ }
  body { print; if (/^}$/) body = 0 }
  END { if (found != 2 || body) exit 1 }
' "$1/commonFx.ino" > "$2/envoy_append.inc"
