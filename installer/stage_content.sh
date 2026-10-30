#!/usr/bin/env bash
# Assembles the factory content the installers ship: every pack in the factory content folder (wavetables,
# LFOs, credits), the factory presets as the "Factory" pack and the Melodies samples as the "Melodies" pack.
# Heartbeat lists packs from its per-machine factory folder (LoadSave::getFactoryDirectory), so the result is
# copied there as is.
#
# Usage: stage_content.sh <destination>
#   HEARTBEAT_CONTENT  pack folder   (default: <repo>/../heartbeat-factory)
#   HEARTBEAT_PRESETS  preset folder (default: <repo>/../Presets)
#   HEARTBEAT_SAMPLES  sample folder (default: <repo>/../Melodies)
set -euo pipefail
dest="${1:?destination folder}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
content="${HEARTBEAT_CONTENT:-$repo/../heartbeat-factory}"
presets="${HEARTBEAT_PRESETS:-$repo/../Presets}"
samples="${HEARTBEAT_SAMPLES:-$repo/../Melodies}"
[ -d "$content" ] || { echo "No factory content at $content (set HEARTBEAT_CONTENT)"; exit 1; }
[ -d "$presets" ] || { echo "No factory presets at $presets (set HEARTBEAT_PRESETS)"; exit 1; }
[ -d "$samples" ] || { echo "No factory samples at $samples (set HEARTBEAT_SAMPLES)"; exit 1; }

rm -rf "$dest"
mkdir -p "$dest/Factory/Presets"
for pack in "$content"/*/; do
  name="$(basename "$pack")"
  [ "$name" = "tools" ] && continue
  cp -R "$pack" "$dest/$name"
done
[ -f "$content/CREDITS.md" ] && cp "$content/CREDITS.md" "$dest/"
cp "$presets"/*.vital "$dest/Factory/Presets/"
mkdir -p "$dest/Melodies/Samples"
cp "$samples"/*.wav "$dest/Melodies/Samples/"
echo "Staged $(find "$dest" -type f | wc -l | tr -d ' ') files in $dest"
