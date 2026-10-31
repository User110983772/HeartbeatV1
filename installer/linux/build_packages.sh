#!/usr/bin/env bash
# Builds Heartbeat for Linux (standalone, VST3, LV2) and packages it with the factory content:
#   installer/out/heartbeat_<version>_amd64.deb             system-wide (plugins in /usr/lib, content in /usr/share/heartbeat)
#   installer/out/Heartbeat-<version>-Linux-x86_64.tar.gz   the same files plus install.sh for a per-user install
# NOBUILD=1 packages what is already built. HEARTBEAT_CONTENT / HEARTBEAT_PRESETS: see ../stage_content.sh.
set -euo pipefail
version="${VERSION:-1.0.0}"
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="$repo/installer/out"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cd "$repo"
if [ -z "${NOBUILD:-}" ]; then
  make standalone vst3 lv2 CONFIG=Release -j"$(nproc)"
fi
app="standalone/builds/linux/build/heartbeat"
vst3="plugin/builds/linux_vst/build/Heartbeat.vst3"
lv2="plugin/builds/linux_lv2/Heartbeat.lv2"
for f in "$app" "$vst3" "$lv2"; do [ -e "$f" ] || { echo "Missing $f"; exit 1; }; done

root="$work/heartbeat_${version}_amd64"
install -Dm755 "$app" "$root/usr/bin/heartbeat"
mkdir -p "$root/usr/lib/vst3" "$root/usr/lib/lv2"
cp -R "$vst3" "$root/usr/lib/vst3/"
cp -R "$lv2" "$root/usr/lib/lv2/"
install -Dm644 standalone/heartbeat.desktop "$root/usr/share/applications/heartbeat.desktop"
install -Dm644 LICENSE "$root/usr/share/doc/heartbeat/copyright"
bash "$repo/installer/stage_content.sh" "$root/usr/share/heartbeat"
find "$root" -type d -exec chmod 755 {} +
find "$root/usr/share/heartbeat" -type f -exec chmod 644 {} +

# Runtime dependencies. Names with alternatives so the package installs on distributions before and after
# Debian's 64-bit time_t renames (libasound2 -> libasound2t64). X11 libraries JUCE opens at runtime are listed too.
glibc=$(objdump -T "$root/usr/bin/heartbeat" "$root"/usr/lib/vst3/Heartbeat.vst3/Contents/*/Heartbeat.so \
        | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)
deps="libc6 (>= $glibc), libstdc++6, libgcc-s1, libasound2t64 | libasound2, libfreetype6, libcurl4t64 | libcurl4, libgl1, libx11-6, libxext6"
recommends="libxcursor1, libxinerama1, libxrandr2"
mkdir -p "$root/DEBIAN"
cat > "$root/DEBIAN/control" <<EOF
Package: heartbeat
Version: $version
Architecture: amd64
Maintainer: Fabio <dartshmoke@proton.me>
Depends: $deps
Recommends: $recommends
Section: sound
Priority: optional
Installed-Size: $(du -sk "$root/usr" | cut -f1)
Description: Heartbeat synthesizer (standalone, VST3 and LV2)
 A spectral warping wavetable, noise and sample synthesizer based on Vital,
 with factory presets, wavetables and LFOs.
EOF
mkdir -p "$out"
dpkg-deb --root-owner-group --build "$root" "$out/heartbeat_${version}_amd64.deb"

# The tarball: same files, installed per user (no root needed) or system-wide with --system.
tarroot="$work/Heartbeat-$version-Linux-x86_64"
mkdir -p "$tarroot"
cp -R "$root/usr" "$tarroot/"
cat > "$tarroot/install.sh" <<'EOF'
#!/usr/bin/env bash
# ./install.sh           install for this user (~/.local/bin, ~/.vst3, ~/.lv2, ~/.local/share/heartbeat)
# sudo ./install.sh --system   install for every user (/usr/local/bin, /usr/lib, /usr/share/heartbeat)
set -euo pipefail
src="$(cd "$(dirname "$0")" && pwd)/usr"
if [ "${1:-}" = "--system" ]; then
  install -Dm755 "$src/bin/heartbeat" /usr/local/bin/heartbeat
  mkdir -p /usr/lib/vst3 /usr/lib/lv2 /usr/share/heartbeat
  cp -R "$src/lib/vst3/Heartbeat.vst3" /usr/lib/vst3/
  cp -R "$src/lib/lv2/Heartbeat.lv2" /usr/lib/lv2/
  cp -R "$src/share/heartbeat/." /usr/share/heartbeat/
  install -Dm644 "$src/share/applications/heartbeat.desktop" /usr/share/applications/heartbeat.desktop
else
  install -Dm755 "$src/bin/heartbeat" "$HOME/.local/bin/heartbeat"
  mkdir -p "$HOME/.vst3" "$HOME/.lv2" "$HOME/.local/share/heartbeat"
  cp -R "$src/lib/vst3/Heartbeat.vst3" "$HOME/.vst3/"
  cp -R "$src/lib/lv2/Heartbeat.lv2" "$HOME/.lv2/"
  cp -R "$src/share/heartbeat/." "$HOME/.local/share/heartbeat/"
  install -Dm644 "$src/share/applications/heartbeat.desktop" "$HOME/.local/share/applications/heartbeat.desktop"
fi
echo "Heartbeat installed."
EOF
chmod 755 "$tarroot/install.sh"
tar -C "$work" -czf "$out/Heartbeat-$version-Linux-x86_64.tar.gz" "$(basename "$tarroot")"
ls -la "$out"
