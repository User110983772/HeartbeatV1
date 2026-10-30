# Heartbeat installers

Each installer contains the plugin(s), the standalone app and the factory content. The content is every pack in
`../heartbeat-factory` (wavetables, LFOs and credits), the presets in `../Presets` as the `Factory` pack and
the samples in `../Melodies` as the `Melodies` pack, all found next to the project folder. Heartbeat lists factory packs from a per-machine folder and the
user's own folder:

| | Plugins | App | Factory content |
|---|---|---|---|
| Windows | `C:\Program Files\Common Files\VST3` | `C:\Program Files\Heartbeat` | `C:\ProgramData\Heartbeat` |
| macOS | `/Library/Audio/Plug-Ins/{VST3,Components}` | `/Applications` | `/Library/Application Support/Heartbeat` |
| Linux (.deb) | `/usr/lib/{vst3,lv2}` | `/usr/bin/heartbeat` | `/usr/share/heartbeat` |

## Windows

Install Inno Setup 6 (`winget install JRSoftware.InnoSetup`), then run `installer\windows\build_installer.bat`.
It builds the VST3 and the standalone app in Release (with the copy-to-Program-Files step turned off, so it
doesn't need administrator rights), then writes `installer\out\Heartbeat-1.0.0-Windows-Setup.exe`. The
installer adds the Visual C++ runtime when it's missing. `build_installer.bat nobuild` packages existing builds.

## Linux

`installer/linux/build_packages.sh` builds the standalone app, VST3 and LV2 and writes a `.deb` plus a
`.tar.gz` with `install.sh` (per user, or `--system`).

## macOS

Needs a Mac with Xcode: `installer/macos/build_pkg.sh` writes `installer/out/Heartbeat-1.0.0-macOS.pkg`.
The bundles are signed ad hoc, so macOS asks before opening them the first time.

## GitHub Actions

`.github/workflows/build-installers.yml` builds all three on GitHub's runners (from the Actions tab, or by
pushing a `v*` tag). The content isn't in the repository: run `installer/make_content_zip.sh`, upload the
zip somewhere downloadable (such as an asset of a GitHub release) and set the repository variable
`FACTORY_CONTENT_URL` to its URL.
