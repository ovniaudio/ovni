# OVNI 🛸 — free, open-source spatial-audio plugins

[![CI](https://github.com/ovniaudio/ovni/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/ovniaudio/ovni/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ovniaudio/ovni?label=release)](https://github.com/ovniaudio/ovni/releases/latest)
[![License: AGPLv3](https://img.shields.io/github/license/ovniaudio/ovni)](LICENSE)

Seven creative spatial-audio effects **plus SUPERNOVA, an audio-reactive visual synth** — all **free and open-source (AGPLv3)**.
macOS: VST3 + AU, universal (Apple Silicon + Intel), 11+. Windows: VST3, x64, 10+ (audio catalog — SUPERNOVA is macOS-only: VST3 + AU + a standalone app on Metal).
Simple interface, pro sound backed by real physics: HRTF, Doppler, physical reverberation.

**→ [Download the latest release](https://github.com/ovniaudio/ovni/releases/latest)** ·
**[ovniaudio.com](https://ovniaudio.com)**

---

## The plugins

This repo holds the **six audio catalog plugins and SUPERNOVA**, the audiovisual flagship. The flagship, **ORBIT**, is the engine the
rest are born from and lives in its own repo → **[github.com/ovniaudio/orbita](https://github.com/ovniaudio/orbita)**.

| Plugin | What it does |
|---|---|
| **ORBIT** ↗ | The binaural movement engine the whole family is born from. Place sound in real 3D, set it orbiting, fly it past — backed by physics. *(source in [ovniaudio/orbita](https://github.com/ovniaudio/orbita))* |
| **PULSAR** | Auto-pan that throws your sound into orbit. Chaos, Doppler and binaural width — motion that obeys physics, not an LFO. |
| **NEBULA** | Reverb of impossible, infinite spaces. An FDN cloud you sculpt — it breathes, it freezes, it never ends. |
| **DUST** | Binaural echoes. Reflections of your sound scattered as bubbles orbiting the head — from a few discrete taps to a growing cloud. |
| **HALO** | Shimmer that orbits. A pitched reverb feeds back into itself — an endless choir of octaves and fifths circling the head. |
| **HORIZON** | Spectral freeze with a pulse. Capture an instant and hold it, then re-trigger it to the beat — eternal pad to rhythmic stutter. |
| **AURORA** | Spectral panning: every frequency to its own place in the field. Your sound unfurled across the stereo — real width, mono-compatible. |
| **SUPERNOVA** | Audio-reactive **visual synth**: 262,144 GPU particles deform your image or video with the sound. VST3 + AU plus a standalone app that hears your Mac's system audio driver-free (ScreenCaptureKit); the audio path is bit-exact pass-through. macOS-only (Metal). |

Every module carries an **IN PHASE** mono-safe path, and latency is honest: zero in most
modules, 3 ms lookahead in NEBULA, one PDC-reported STFT frame in the spectral pair
(HORIZON, AURORA). If a control claims something the DSP doesn't do, that's a bug.

## Install — macOS

Grab the installer from the [latest release](https://github.com/ovniaudio/ovni/releases/latest):

> **Staged launch:** releases roll out in waves — each release carries the modules shipping
> in that wave (v0.2.0: **ORBIT + SUPERNOVA**; the rest of the catalog is coming soon at
> [ovniaudio.com](https://ovniaudio.com)).

- **`OVNI-<version>.pkg`** — all seven plugins, one double-click. It places VST3 + AU in the
  system plug-in folders (`/Library/Audio/Plug-Ins`) and asks for your password itself.
  Click **Customize** to pick specific plugins.
- **`OVNI-<PLUGIN>-<version>.pkg`** — just the one you want.

Files installed by the .pkg carry **no quarantine flag**, so your DAW loads them with no
Gatekeeper warnings. The installer itself is unsigned (no paid Apple certificate yet), so
macOS may block it on first open — once: **right-click → Open** (macOS 14 or earlier) or
**System Settings → Privacy & Security → "Open Anyway"** (macOS 15+).

Prefer manual install? **`OVNI-<version>.dmg`** has the raw bundles; that path needs the
`xattr -dr com.apple.quarantine …` step described in the `LÉEME PRIMERO` inside.

Then rescan plugins in your DAW. macOS 11+, universal (Apple Silicon + Intel).

## Install — Windows

Download **`OVNI-<version>-Windows.zip`** (all seven) or a single
**`OVNI-<PLUGIN>-<version>-Windows.zip`** — VST3, 64-bit, Windows 10+.

Before extracting: right-click the ZIP → **Properties** → tick **Unblock** → Apply. Then
extract and copy the `.vst3` folder(s) into `C:\Program Files\Common Files\VST3`. The
plugins are unsigned, so SmartScreen may warn the first time — **More info → Run anyway**.
Full steps (English + Español) are in `LEEME PRIMERO.txt` inside each ZIP. Windows is VST3
only (AU is macOS-only).

**Verify your download:** every release ships a `SHA256SUMS.txt` with the SHA-256 of every
asset — `shasum -a 256 <file>` (macOS) or `certutil -hashfile <file> SHA256` (Windows).

## Build from source

The whole catalog is one CMake/JUCE build (SUPERNOVA's Metal renderer and standalone app build on macOS only). JUCE is pinned (8.0.13) and reused from
a local checkout via `-DOVNI_JUCE_DIR`, or fetched automatically if you don't pass one;
`libmysofa` and `Catch2` are fetched automatically.

```bash
git clone https://github.com/ovniaudio/ovni
cd ovni
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DOVNI_JUCE_DIR=/path/to/JUCE   # optional — omit to auto-fetch JUCE
cmake --build build
ctest --test-dir build            # the full test battery
```

**ORBIT builds from its own repo** ([ovniaudio/orbita](https://github.com/ovniaudio/orbita),
which uses git submodules — clone it with `--recursive`).

## Layout

```
plugins/      the six catalog plugins (aurora, dust, halo, horizon, nebula, pulsar)
              plus _probe, the internal build-harness test plugin
shared/       shared DSP engines, UI kit, presets and the plugin chassis
cmake/        build helpers
packaging/    installer scripts (.pkg per plugin + full catalog, DMG, Windows ZIPs)
tests/        catalog-wide test harness
tools/        offline utilities (e.g. HRIR baking)
```

## License

**GNU AGPLv3** — see [`LICENSE`](LICENSE). Third-party attributions (JUCE, libmysofa,
Catch2, the HRIR datasets and the embedded UI fonts) are in [`NOTICE.md`](NOTICE.md).
As of v0.1.1 the whole catalog — ORBIT included — links **no proprietary libraries**.

The whole catalog is free and open-source. The brand rests on **verifiable honesty**: if a
claim in here can't be checked, that's a bug — open an issue.

🛸 **[ovniaudio.com](https://ovniaudio.com)**
