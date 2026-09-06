# Changelog

All notable changes to the [OVNI](https://github.com/ovniaudio/ovni) catalog are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0/).

## [0.3.1] - 2026-09-06

**A SUPERNOVA-only maintenance release, all of it about the app's audio.** The system-audio
permission is now the small one — audio, not screen — and it is asked when you pick the source;
plus a round of fixes for the things that broke around it: plugging in headphones, closing the
window while macOS was still asking, and a REOPEN that built a shell command out of the folder
name. The seven OVNI audio plugins and ORBIT are unchanged and are not part of this release.

### Changed

- **System audio capture uses Core Audio taps on macOS 14.2+: the permission is now
  "System Audio Recording Only" — no more "screen" prompt — and it is asked when you
  pick the source, not at launch.** SUPERNOVA never looked at your screen; until now it
  had to ask for screen recording because that was the only way macOS exposed system
  audio. On macOS 14.2 and later the app uses Core Audio process taps instead, so the
  prompt names exactly what it does. macOS 13 – 14.1 keep the ScreenCaptureKit path (and
  its screen permission) — that API does not exist there yet. Opening the app no longer
  pops a dialog: it starts, runs, and the top bar offers **ALLOW**; if you decline, the
  app stays usable on an input device or MIDI and the notice tells you which System
  Settings pane to open. macOS asks once — later launches re-check in silence.

### Fixed

- **REOPEN relaunches the copy you are running.** It used to hand the job to
  LaunchServices, which could resolve the bundle identifier to a *different* installed
  copy of SUPERNOVA and reopen that one instead. It now relaunches the exact bundle on
  disk, as a new instance.
- **REOPEN no longer builds a shell command out of the folder name.** The path of the app was
  pasted into a shell command, so the *name of the folder you keep SUPERNOVA in* was executed:
  a copy sitting in a directory with `$(…)` or backticks in its name — a perfectly legal folder
  name — ran that when you pressed REOPEN. The path now travels as a plain argument and is never
  parsed as code. If the relauncher itself fails to start, the app no longer quits on you.
- **Plugging in headphones no longer leaves the visual deaf.** Changing the output device — or
  its sample rate — used to silence System Audio until you reopened the app, because the capture
  stayed bound to whichever output was default when it started. It now notices the change and
  rebuilds itself, in under a second, without asking for permission again. The same self-repair
  covers any other way the capture dies; and if you switch the permission **off** in System
  Settings while the app is open, the notice comes back instead of the app pretending to listen.
- **Closing the app — or switching source — while macOS is asking no longer freezes it.** The
  system's permission dialog blocks until you answer it, and the app used to wait for that on its
  main thread: a spinning beachball for as long as the dialog stayed open. It no longer waits.
- **The top bar says when it is waiting for macOS.** If the permission was already asked for once
  but macOS has not answered yet (say you reset your permissions), the app used to show nothing
  while a system dialog appeared out of nowhere. After two seconds it now says
  *Waiting for macOS permission…*. And if the capture fails for a reason that is **not** the
  permission — no output device, a broken audio chain — the notice offers **ALLOW** to retry
  instead of leaving the app silently stuck, which is what happened before.

## [0.3.0] - 2026-09-05

**SUPERNOVA becomes a playable instrument.** A SUPERNOVA-only release: the MEDIA
session — a strip of photos and video you can cue, reorder, relink and drive from
a MIDI pad — a sequence clock that cuts on the beat, an LFO section that means
what it says, and four kaleidoscope worlds swapped for a photographic family
(still 50 worlds). The seven OVNI audio plugins and ORBIT are unchanged and are
not part of this release. macOS-only, as before.

### Added

- **MEDIA strip — see your session.** A filmstrip of numbered thumbnails between
  the visual and the knobs shows every photo/video in order, the current one with
  a countdown to the next change. Click to cue (**← / →** too), drag to reorder,
  ✕ to remove, right-click for rotate / move to start / reveal / clear, **+** to
  add. Number keys **1**–**9** / **0** cue a tile straight away. Thumbnails are
  decoded in the background with the same orientation the engine uses. **▦** in
  the top bar hides/shows it; drop a **folder** to load all its photos in name
  order; iPhone **HEIC**, WebP and TIFF are accepted (macOS). Removing,
  reordering, rotating and clearing the session are all **undoable** (Cmd+Z).
  The session — a sequence, or a single photo or video with its rotation — is
  **saved with the project and with user presets**, so reopening brings it back.
  Loading a project always wins: if the host restores one while you are playing,
  the session it carries takes over, and a cue that lands in that same instant is
  applied to the freshly loaded session instead of being written back over it.
- **Drop photos between two tiles.** The MEDIA strip is now its own drop target: files or a folder
  dropped on it are inserted **at that position** (an insertion line shows where), instead of always
  landing at the end of the session. The photo on screen keeps playing — only its number changes.
- **Missing media is marked, not dropped.** If a photo or video has moved, its tile stays in the
  session marked **missing** (red **!**, greyed name) instead of silently disappearing when the
  project reopens; the sequence skips it, and right-click offers **Relink…** for that file or
  **Relink folder…** to recover every missing item of that folder at once. A file that returns to
  its place heals itself. Nothing cuts to a missing tile — the sequence, **← / →**, the number keys
  and the MIDI cues all step over it, and clicking one opens its relink menu instead.
- **Canvas format (FORMAT chip).** The canvas now has a shape, like a project
  format: **AUTO** follows the first item (vertical photo → **9:16**, square →
  1:1, landscape → 16:9), or pick FREE / 16:9 / 9:16 / 1:1 / 4:5 / 4:3. The
  visual is drawn letterboxed to it in the editor, the app, fullscreen output and
  the Syphon feed; the EXPORT menu lists the matching preset first. **FIT / FILL**
  chooses letterbox vs. crop-to-fill. Pure layout — the render engine and its
  byte-exact goldens are untouched.
- **Sequence clock, order and transition.** The sequence can advance by
  **Seconds**, by **Beats** of the tempo (host BPM / TAP — cuts on the bar) or on
  every **Kick** with a minimum gap; **Loop** or **Shuffle** (Shuffle is the order
  the *clock* advances in — stepping by hand with **← / →** or notes 88 / 89 always
  walks the strip in order, so next-then-previous puts you back where you were); **Cut** or
  **Burst** (the old photo explodes and re-forms as the new one). Persisted with
  the project and presets. In **Beats**, a cue waits for the next bar (the armed
  tile shows a dashed outline) instead of cutting mid-phrase — the armed photo is
  decoded while it waits, so the cut lands *on* the beat — and holding **Shift**
  cuts right now. A cue asked for just *before* a downbeat lands on that downbeat:
  it carries the beat it was armed at (for MIDI, the beat of the note itself),
  not the beat of the editor tick that picked it up.
- **LFO rates: 21 beat divisions plus free-running Hz.** Every division from a
  sixteenth to four bars now carries its **triplet** (`·T`) and **dotted** (`·D`)
  variant, and a **FREE / Hz** mode (0.05–20 Hz) runs an LFO off the clock instead
  of the tempo — it keeps moving with the transport stopped or with no host.
- **Per-LFO polarity, phase and retrigger.** **BI / UNI** picks whether the LFO
  swings both ways around the knob or only adds from it; **PHASE** (0–360°) offsets
  where the cycle starts; **RETRIG** restarts the cycle on the current beat without
  disturbing your phase setting. All three persist with the project.
- **LFO depth now means what it says.** At 100 % an LFO sweeps exactly the target
  parameter's range, and the result is clamped to that range — several LFOs on the
  same target can no longer push a parameter out of bounds.
- **LFOs are recalculated on every rendered frame** instead of 30 times a second,
  so fast modulation reads as a wave rather than a staircase on a 60/120 Hz display.
- **Cue your photos from a MIDI pad.** Notes **72–87** cue tiles 1–16 of the media
  session, **88 / 89** step next / previous and **90** jumps to a random photo
  (never the one on screen) — so the session stays playable in immersive or
  fullscreen, where the strip isn't visible. Cues follow the sequence clock (in
  **Beats** they wait for the bar, like clicking a tile); **MIDI cue: now** in the
  SEQ menu makes every note cut immediately.
- **A photo sequence is exported the way it plays**: the clip follows the sequence
  **clock** (Seconds / Beats / Kick), its **order** (Loop / Shuffle) and its
  **Burst** transition, starting from the photo on screen — and the visuals react
  at the speed you saw them, with the muxed audio covering the same stretch.

### Changed

- **Four worlds swapped for a photographic family.** The kaleidoscope presets
  (Kaleidoscope, Hypnosis, Cathedral, Hologram) mirrored your picture into a
  mandala and the material stopped reading. Their slots now hold **Parallax,
  Lantern, Meridian and Vantage** — built on the same idea as Satellite: the
  image is preserved and reinterpreted with depth, parallax, a slow orbit, a
  subject cut-out or light, with faithful (or subtly graded) color. Still 50
  worlds. The **KALEIDO** control itself is untouched — turn it on in any world.
- **Rotating a photo is instant.** The ⟳ button used to re-read the file, decode
  it again and re-run both Vision passes on every quarter turn — hundreds of
  milliseconds with a 12-megapixel photo. The decode is now cached upright and
  rotations are derived from it (a measured 45× on four turns). As a bonus, a
  photo reopened from a project at a given rotation and the same photo rotated by
  hand now produce exactly the same image.

### Fixed

- **Sessions saved with a comma decimal separator survive.** LFO banks, MIDI-learn
  maps and scenes are written and read independently of the process locale, so a
  host that switches the C locale (common in Qt/GTK apps) can no longer destroy a
  saved performance setup.
- **Undo restores your LFO bank, MIDI-learn map and scenes.** They were captured in
  the undo snapshot but never re-read on undo/redo, so they stayed put.
- **A click in the instant a project loads no longer overwrites its settings.**
  Toggling the canvas format, FIT/FILL, the MEDIA strip or *MIDI cue: now* inside
  the 33 ms between the host restoring a project and the editor picking it up
  used to overwrite the setting that had just been restored — and it did not
  correct itself. The host wins, as it already did for the session itself.

## [0.2.0] - 2026-08-25

**Introduces SUPERNOVA — the 8th OVNI module and the first audiovisual one.**
SUPERNOVA is an audio-reactive visual synthesizer / VJ instrument: it listens to
audio and drives a real-time GPU particle field on Apple Metal. It is a *visual*
instrument, not an audio effect — the audio is passed through bit-exact and is
never altered. macOS-only for now; the seven existing OVNI audio plugins are
unchanged by this release.

### Added

- **SUPERNOVA visual synthesizer.** New audiovisual module that turns incoming
  audio into a live GPU particle field. Ships as a **VST3 + AU** plug-in and a
  dedicated **Standalone desktop app**, as a Universal binary (Apple Silicon
  arm64 + Intel x86_64), minimum macOS 11.0 (Big Sur). The render engine is
  Apple Metal.
- **Bit-exact audio pass-through.** SUPERNOVA never touches the sound — the audio
  it receives is the audio it outputs, sample-for-sample. Guaranteed by a
  null/identity test and a full-scale gain identity test.
- **50 built-in worlds** with **instant world cards** and a **world browser**
  showing live thumbnails, so every factory preset is a distinct, ready-to-play
  look you can page through.
- **Drag any image and the particles form it.** Subject detection uses Apple
  Vision saliency plus a subject **cutout** (erase the background, keep the
  particle sculpture); cutout is an automatable knob for full user control.
- **Photo sequences and video as a color source.** Drop multiple images to build
  an auto-rotating sequence (EXIF auto-orientation, per-photo rotate, aspect FIT,
  background prefetch); feed a video (AVFoundation) to drive the palette live.
- **Cinematic visual engine.** Colorist grade (lift + contrast + split-tone),
  enveloping multi-scale bloom halo, and an optics pack (grain, dither, chromatic
  aberration) in the composite — each **defaults to 0 and is byte-exact off**, so
  the classic look is preserved until you dial them in. Backed by an HDR linear
  accumulation pipeline with ACES filmic tone-mapping.
- **Desktop app with system-audio capture.** The Standalone app captures system
  audio via ScreenCaptureKit (macOS 13+) so it can react to anything playing on
  the Mac; inside a DAW the plug-in reads the host audio directly. The app has its
  own chrome, an immersive mode, and true fullscreen to a chosen monitor.
- **The plug-in editor IS the app.** The VST3/AU editor ships the same pro
  two-row top bar as the desktop app — worlds navigation and browser, LFO panel,
  user PRESETS, video EXPORT, Syphon, photo-sequence controls, input meter and
  the IN sensitivity fader — with the visual filling the whole window
  (free-form resizable, size remembered per instance, no fixed-zoom chrome).
  Only app-specific controls (SOURCE selector, MIC/MIDI setup, always-on-top)
  stay app-only: inside a DAW the host is the source and owns the window.
- **Visual sensitivity that never touches the sound.** The IN fader is a new
  `visGain` parameter that scales *only* the analysis feed (what the visuals
  react to). The insert audio stays bit-exact at any fader position — verified
  by a dedicated test — in both the plug-in and the app.
- **Tempo-synced modulation.** BeatClock tempo sync with tempo-locked **LFOs** and
  a **live LFO output meter**, plus a pro LFO panel with a waveform-shape strip.
- **MIDI-learn and MIDI CC.** Right-click any knob to learn a CC; MIDI notes can
  fire directional explosions and lightning, and notes / program changes can
  recall presets.
- **Scenes, user presets, and Undo/Redo.** Save and recall your own presets
  (undoable), snapshot scenes, and undo/redo destructive actions like RANDOM and
  CLEAR — they are no longer one-way doors.
- **Video export to MP4 / H.264, with sound.** Four format presets, a free custom
  duration from 1 to 600 seconds, and an in-sync AAC audio track muxed from a 12-second
  real-time audio ring buffer, so exported clips carry the audio that drove them.
- **Syphon output (macOS).** Publishes the final texture to OBS, Resolume, and
  VDMX for live use.
- **Preset MORPH.** Switching presets interpolates instead of hard-cutting.
- **Per-world physics and audio reactivity.** Onset triggers a radial explosion,
  bass drives turbulence, highs add jitter, and RMS drives a breathing motion,
  with per-preset physics parameters; reactivity is level-independent (AGC).
- **Expanded visual vocabulary.** Domain hotknobs (MOVEMENT / MATTER / CAMERA /
  COLOR), a RANDOM dice, a VARIATION master control, a PLEXUS live-connections
  layer, energy-conserving trails, a 3D cutout/figure geometry mode, and a
  gradient-map color lab.
- **Engine-rendered app icon** — the OVNI wordmark broken into particle dust,
  rendered by SUPERNOVA's own engine.
- **Windows system-audio capture groundwork.** WASAPI loopback capture is written,
  but there is no Windows visual renderer yet (the D3D11 backend is future work),
  so SUPERNOVA remains macOS-only for now.
- **Verification and packaging.** Byte-exact golden-frame regression, an offscreen
  render tool for HD stills/animation, pluginval strictness-8 passing on VST3 and
  AU, and packaging that ships `SUPERNOVA.app` inside the DMG alongside the audio
  plugins.

### Changed

- **The app no longer looks like the plug-in.** The Standalone app has a complete,
  purpose-built chrome instead of the generic plug-in editor shell.
- **All user-facing strings are in English** (tooltips, permission banner, dialog
  titles, navigation, settings).
- **Knob rows are ordered by measured impact.** Row layout is driven by a
  spatial + motion impact metric rather than guesswork.
- **CLEAR is instant.** It teleports the canvas to its home state (positions home,
  velocity zero, accumulators cleared).
- **Immersive mode actually grows the view** — hiding the knobs now expands the
  canvas instead of leaving dead space.
- **The figure is invariant to view size** — enlarging the window no longer makes
  the field look sparser or softer.
- **State hydration hardened** — MIDI / LFO / scene deserialization runs on the
  message-thread editor hydrate instead of inside `setStateInformation`.

### Fixed

- **Video export crashed on the first frame** — `VideoExporter.mm` was compiled
  without ARC. (Critical.)
- **System-audio permission prompt never appeared** — a background-thread capture
  request was covering it; the app now requests correctly and connects **live** on
  grant, with no relaunch.
- **System-audio capture** now uses the correct two-call `AudioBufferList` pattern.
- **Stable local code-signing identity** so the macOS Screen-Recording permission
  survives rebuilds instead of re-prompting on every launch, and a stray second
  `SUPERNOVA.app` no longer shadows it.
- **Crash on close with Syphon active** (SIGSEGV) — the callback is drained before
  the server is released; and a **crash when enabling Syphon in Ableton** (the
  shader is now embedded at runtime).
- **Reactivity is contained** — the image is never lost and never leaves a hole,
  and it is independent of input level.
- **Zoom never opens larger than the screen** — the zoom factor is clamped to the
  display work area (fixed a field bug where fullscreen clipped SUPERNOVA on a
  laptop).
- **Tap tempo works without audio** — a silence heartbeat and a valid sample rate
  keep the clock alive.
- **Cutout at 100%** no longer kills the soft-mask background.
- **EXIF parser out-of-bounds read** hardened, and a degenerate one-item photo
  sequence no longer resurrects itself.
- **Tool flags no longer override presets**, and the trackpad wheel no longer
  picks up scroll inertia.

## [0.1.1]

Catalog release: the seven cross-platform OVNI audio plugins — ORBIT, PULSAR,
NEBULA, DUST, HALO, HORIZON, and AURORA — with per-plugin installers (individual
macOS `.pkg` files plus a complete bundle, and Windows ZIPs).

## [0.1.0]

Initial catalog release: the first public build of the seven OVNI audio plugins
(VST3 + AU on macOS, VST3 on Windows), free and open source under AGPLv3.

---

Repository: <https://github.com/ovniaudio/ovni>
