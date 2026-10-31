#!/usr/bin/env bash
# Zips the factory content for the GitHub workflow: heartbeat-content.zip with heartbeat-factory/, Presets/ and
# Melodies/.
# Usage: make_content_zip.sh [output.zip]   (sources as in stage_content.sh)
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
content="${HEARTBEAT_CONTENT:-$repo/../heartbeat-factory}"
presets="${HEARTBEAT_PRESETS:-$repo/../Presets}"
samples="${HEARTBEAT_SAMPLES:-$repo/../Melodies}"
zip_out="$(realpath -m "${1:-$repo/installer/out/heartbeat-content.zip}")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/heartbeat-factory" "$work/Presets" "$work/Melodies" "$(dirname "$zip_out")"
cp -R "$content"/. "$work/heartbeat-factory/"
cp "$presets"/*.vital "$work/Presets/"
cp "$samples"/*.wav "$work/Melodies/"
rm -f "$zip_out"
(cd "$work" && zip -qr "$zip_out" heartbeat-factory Presets Melodies)
ls -la "$zip_out"
