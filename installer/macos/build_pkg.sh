#!/usr/bin/env bash
# Builds Heartbeat for macOS (VST3, AU, standalone app; arm64 + x86_64) and packages it with the factory content:
#   installer/out/Heartbeat-<version>-macOS.pkg
# Plugins go to /Library/Audio/Plug-Ins, the app to /Applications, the factory packs to
# /Library/Application Support/Heartbeat (Heartbeat's per-machine factory folder).
# The bundles are signed ad hoc, not with a Developer ID: on first launch macOS asks to allow them
# (right-click > Open, or System Settings > Privacy & Security).
# NOBUILD=1 packages what is already built. HEARTBEAT_CONTENT / HEARTBEAT_PRESETS: see ../stage_content.sh.
set -euo pipefail
version="${VERSION:-1.0.0}"
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="$repo/installer/out"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
xcode_flags=(-configuration Release CODE_SIGNING_ALLOWED=NO ONLY_ACTIVE_ARCH=NO ARCHS="arm64 x86_64"
             MACOSX_DEPLOYMENT_TARGET=10.13)

cd "$repo"
if [ -z "${NOBUILD:-}" ]; then
  xcodebuild -project plugin/builds/osx/Heartbeat.xcodeproj -target "Heartbeat - VST3" -target "Heartbeat - AU" \
             "${xcode_flags[@]}" build
  xcodebuild -project standalone/builds/osx/Heartbeat.xcodeproj -target "Heartbeat - App" "${xcode_flags[@]}" build
fi
vst3="plugin/builds/osx/build/Release/Heartbeat.vst3"
au="plugin/builds/osx/build/Release/Heartbeat.component"
app="standalone/builds/osx/build/Release/Heartbeat.app"
for f in "$vst3" "$au" "$app"; do [ -d "$f" ] || { echo "Missing $f"; exit 1; }; done

stage() {  # stage <name> <bundle> <install folder>
  mkdir -p "$work/$1"
  ditto "$2" "$work/$1/$(basename "$2")"
  codesign --force --deep --sign - "$work/$1/$(basename "$2")"
  pkgbuild --root "$work/$1" --install-location "$3" --identifier "com.heartbeat.$1" --version "$version" \
           "$work/pkgs/$1.pkg"
}
mkdir -p "$work/pkgs"
stage vst3 "$vst3" "/Library/Audio/Plug-Ins/VST3"
stage au "$au" "/Library/Audio/Plug-Ins/Components"
stage app "$app" "/Applications"
bash "$repo/installer/stage_content.sh" "$work/content"
pkgbuild --root "$work/content" --install-location "/Library/Application Support/Heartbeat" \
         --identifier com.heartbeat.content --version "$version" "$work/pkgs/content.pkg"

cat > "$work/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Heartbeat $version</title>
  <license file="LICENSE"/>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <choices-outline>
    <line choice="vst3"/><line choice="au"/><line choice="app"/><line choice="content"/>
  </choices-outline>
  <choice id="vst3" title="VST3 plugin"><pkg-ref id="com.heartbeat.vst3"/></choice>
  <choice id="au" title="Audio Unit plugin"><pkg-ref id="com.heartbeat.au"/></choice>
  <choice id="app" title="Standalone application"><pkg-ref id="com.heartbeat.app"/></choice>
  <choice id="content" title="Factory presets, wavetables and LFOs"><pkg-ref id="com.heartbeat.content"/></choice>
  <pkg-ref id="com.heartbeat.vst3">vst3.pkg</pkg-ref>
  <pkg-ref id="com.heartbeat.au">au.pkg</pkg-ref>
  <pkg-ref id="com.heartbeat.app">app.pkg</pkg-ref>
  <pkg-ref id="com.heartbeat.content">content.pkg</pkg-ref>
</installer-gui-script>
EOF
mkdir -p "$work/resources" "$out"
cp LICENSE "$work/resources/LICENSE"
productbuild --distribution "$work/distribution.xml" --package-path "$work/pkgs" --resources "$work/resources" \
             "$out/Heartbeat-$version-macOS.pkg"
ls -la "$out"
