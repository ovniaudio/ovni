# OVNI 🛸

**A free, open-source catalog of spacey audio plugins — now with SUPERNOVA, an audio-reactive visual synth.**

OVNI is a small label of audio plugins with a simple interface and pro sound backed by physics. Everything is free and open source under **AGPLv3** (compatible with JUCE's open-source terms). This monorepo holds the shared library (`shared/`), the plugins (`plugins/`), and the orchestrator that builds them (`orchestrator/`).

![SUPERNOVA — 50 built-in worlds](docs/manual/images/worlds/_contact-sheet.png)

## SUPERNOVA

SUPERNOVA is the label's first **audiovisual** module: an audio-reactive **visual synthesizer / VJ instrument**. It listens to your audio and drives a real-time GPU particle field on Apple Metal.

It is **not** an audio effect. SUPERNOVA is **bit-exact audio pass-through** — it never alters the sound. Audio in equals audio out, verified by a null/identity test and a full-scale gain identity test. It's a visual instrument that happens to live in your signal chain.

**Headline features**

- **50 built-in worlds** — factory presets rendered as instant, procedural cards.
- **Drag an image, the particles become it** — Vision-based saliency + subject cutout turn any picture into a particle field.
- **Tempo-synced LFOs with a live output meter** — modulate parameters in time, watch them move.
- **MIDI-learn + MIDI CC** — map any control to your hardware.
- **Scenes and user presets** — save, recall, undo/redo.
- **Video export to MP4 / H.264, WITH SOUND** — 4 format presets, a real-time audio ring buffer muxed to an in-sync AAC track, plus free **custom duration** (1–600 s).
- **Syphon output (macOS)** — publish the texture straight into OBS, Resolume, or VDMX.

**Platform (be honest about this)**

- **macOS only, for now.** Universal binary (Apple Silicon `arm64` + Intel `x86_64`), minimum **macOS 11.0 (Big Sur)**.
- Ships as **VST3 + AU plug-ins** and a dedicated **Standalone desktop app** (a custom JUCE shell, not the generic standalone). The plug-in editor is the **same experience as the app**: same pro top bar (worlds, LFO, presets, export, Syphon, sequences), full-bleed resizable visual — only the system-audio source selector stays app-only (in a DAW the host is the source).
- The visual engine is **Apple Metal**. There is **no Windows visual renderer yet** — the WASAPI loopback audio-capture code exists, but the D3D11 render backend is future work, and video export uses AVFoundation (macOS-only). So SUPERNOVA does **not** run on Windows today.

## Quickstart

Build the universal binary from source:

```bash
cmake --preset release-universal && cmake --build build
```

`release-universal` is the distribution preset (real `arm64 + x86_64`, Release, deployment target 11.0). For faster local iteration on Apple Silicon, `cmake --preset dev && cmake --build build` builds `arm64`-only.

**Where the plug-ins land.** `COPY_PLUGIN_AFTER_BUILD` copies them to your user plug-in folders after each build:

- VST3 → `~/Library/Audio/Plug-Ins/VST3/SUPERNOVA.vst3`
- AU → `~/Library/Audio/Plug-Ins/Components/SUPERNOVA.component`

(The build-tree copies also live under `build/plugins/supernova/supernova_artefacts/Release/`.)

**The desktop app.** The standalone app captures **system audio** via ScreenCaptureKit, so on first launch macOS asks **once** for the **Screen Recording** permission — that TCC permission is how macOS gates system-audio capture (it is **not** the microphone). Inside a DAW, the plug-in reads the DAW's audio directly and needs no permission.

For a stable permission grant across rebuilds, deploy the app with the label's free self-signed cert:

```bash
./packaging/make-signing-cert.sh   # once per machine: creates "SUPERNOVA Local" in the keychain
./packaging/deploy-app.sh          # each deploy: copies to /Applications + stable signature
open /Applications/SUPERNOVA.app
```

Full story, rules, and troubleshooting: [`docs/AUDIO-TCC.md`](docs/AUDIO-TCC.md). Packaging and distribution: [`packaging/README.md`](packaging/README.md).

## Catalog status

| Module | Type | Platforms | Status |
|---|---|---|---|
| ORBIT | Audio | macOS + Windows | Released v0.2.1 |
| PULSAR | Audio | macOS + Windows | Released v0.1.1 |
| NEBULA | Audio | macOS + Windows | Released v0.1.1 |
| DUST | Audio | macOS + Windows | Released v0.1.1 |
| HALO | Audio | macOS + Windows | Released v0.1.1 |
| HORIZON | Audio | macOS + Windows | Released v0.1.1 |
| AURORA | Audio | macOS + Windows | Released v0.1.1 |
| **SUPERNOVA** | Audiovisual | macOS | **0.3.2** |

The 7 audio plugins are cross-platform (Windows VST3 already ships). SUPERNOVA shipped as **v0.2.0** on 2026-08-25 and is macOS-only; **v0.3.0** added the media session, four new worlds and the reworked LFOs, and **v0.3.1** moved the app's system-audio capture to the small "System Audio Recording" permission (no screen prompt) and made it repair itself when the output device changes, and **v0.3.2** makes the app say plainly that System Audio needs macOS 13+ on Macs where no capture route exists, instead of offering a permission it cannot get. Its source lives on branch `feat/supernova`, tagged per release (`v0.3.2`).

## License

**AGPLv3.** Free and open source, compatible with JUCE's open-source terms. Distributing your own binaries requires no paid Apple account; the $99/yr Apple Developer Program is only for signed + notarized distribution convenience, not a legal requirement (see [`packaging/README.md`](packaging/README.md)).

## Development

OVNI is built by an orchestrated set of "factory" sessions. Read **[`CONTRACT.md`](CONTRACT.md)** first — it's the set of rules that make the pieces fit together. The build was split across five parallel sessions, each with a plan under [`docs/plans/`](docs/plans/):

| Session | Builds | Plan |
|---|---|---|
| **S1** | DSP engines (`shared/engines`, `shared/dsp`) | [`docs/plans/S1-engines.md`](docs/plans/S1-engines.md) |
| **S2** | UI-kit (`shared/ui-kit`) | [`docs/plans/S2-uikit.md`](docs/plans/S2-uikit.md) |
| **S3** | Chassis: presets + template (`shared/presets`, `shared/template`) | [`docs/plans/S3-chasis.md`](docs/plans/S3-chasis.md) |
| **S4** | Orchestrator + tools + skill | [`docs/plans/S4-orquestador.md`](docs/plans/S4-orquestador.md) |
| **S5** | Integration (root + `_probe` plugin + `LAUNCH.md`) — after S1–S4 | [`docs/plans/S5-integracion.md`](docs/plans/S5-integracion.md) |

Each plan opens with a copy-paste prompt to launch its session. S5 leaves a `LAUNCH.md` describing how the **main** session drives the orchestrator (Phase 1 PULSAR → Phase 2 parallel → Phase 3).

Doctrine: **verifiable honesty** — every claim is backed by a reproducible command or in-tree evidence. Nothing is asserted "on faith."
