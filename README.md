# OVNI 🛸 — free, open-source spatial-audio plugins for macOS

Seven creative spatial-audio effects — **free and open-source (AGPLv3)**, VST3 + AU,
universal for macOS 11+ (Apple Silicon + Intel). Simple interface, pro sound backed by
real physics: HRTF, Doppler, physical reverberation.

**→ [Download the whole catalog](https://github.com/ovniaudio/ovni/releases/latest)** ·
**[ovniaudio.com](https://ovniaudio.com)**

One download (`OVNI-v0.1.0.dmg`) installs all seven plugins.

---

## The plugins

This repo holds the **six catalog plugins**. The flagship, **ORBIT**, is the engine the
rest are born from and lives in its own repo → **[github.com/ovniaudio/orbita](https://github.com/ovniaudio/orbita)**.

| Plugin | What it does |
|---|---|
| **ORBIT** ↗ | The binaural movement engine the whole family is born from. Place sound in real 3D, set it orbiting, fly it past — backed by physics. *(source in [ovniaudio/orbita](https://github.com/ovniaudio/orbita))* |
| **PULSAR** | Auto-pan that throws your sound into orbit. Chaos, Doppler and binaural width — motion that obeys physics, not an LFO. |
| **NEBULA** | Reverb of impossible, infinite spaces. An FDN cloud you sculpt — it breathes, it freezes, it never ends. |
| **DUST** | Binaural echoes. Reflections of your sound scattered as bubbles orbiting the head — from a few discrete taps to a growing cloud. |
| **HALO** | Shimmer that orbits. A pitched reverb feeds back into itself — an infinite choir of octaves and fifths circling the head. |
| **HORIZON** | Spectral freeze with a pulse. Capture an instant and hold it, then re-trigger it to the beat — eternal pad to rhythmic stutter. |
| **AURORA** | Spectral panning: every frequency to its own place in the field. Your sound unfurled across the stereo — real width, mono-compatible. |

Every plugin is mono-safe by design and ships zero- or low-latency (PDC-compensated where a
spectral block is used).

## Install

The plugins are **unsigned** (no paid Apple Developer certificate), so on first use macOS
Gatekeeper will complain. Two ways past it:

1. **Right-click → Open** the plugin bundle in Finder the first time, then confirm. *(For
   plugin bundles this doesn't always surface an override — if it doesn't, use option 2.)*
2. **Clear the quarantine flag** in Terminal (reliable), e.g.:
   ```bash
   xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/PULSAR.vst3"
   xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/PULSAR.component"
   ```
   Repeat per plugin, or run it once over the whole folder.

Then rescan plugins in your DAW. macOS 11+ · Apple Silicon + Intel · Windows soon.

## Build from source

The six catalog plugins are one CMake/JUCE build. You need a JUCE checkout; `libmysofa`
and `Catch2` are fetched automatically.

```bash
git clone https://github.com/ovniaudio/ovni
cd ovni
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DOVNI_JUCE_DIR=/path/to/JUCE
cmake --build build
```

Run the tests with `ctest --test-dir build`. **ORBIT builds from its own repo**
([ovniaudio/orbita](https://github.com/ovniaudio/orbita), which uses git submodules —
clone it with `--recursive`).

## Layout

```
plugins/      the six catalog plugins (aurora, dust, halo, horizon, nebula, pulsar)
shared/       shared DSP engines, UI kit, presets and the plugin chassis
cmake/        build helpers
packaging/    DMG / installer scripts
tools/        offline utilities (e.g. HRIR baking)
```

## License

**GNU AGPLv3** — see [`LICENSE`](LICENSE). Third-party attributions (JUCE, libmysofa,
Catch2, the HRIR datasets, and Intel IPP for ORBIT) are in [`NOTICE`](NOTICE.md).

The whole catalog is free and open-source. The brand rests on **verifiable honesty**: if a
control claims something the DSP doesn't do, that's a bug — open an issue.

🛸 **[ovniaudio.com](https://ovniaudio.com)**
