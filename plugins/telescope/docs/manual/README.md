# TELESCOPE 🔭 — manual

*An audio analyser that also concludes. Free and open source (AGPLv3), by [OVNI Audio](https://ovniaudio.com).
Version 0.1.0. Español: [`README.es.md`](README.es.md).*

![The fourteen views of TELESCOPE](images/contact-sheet.png)

---

## What this plugin is

Thirteen **lenses** over one analysis engine, plus a rules engine that turns what it measured into
sentences — each one carrying the number that produced it and the id of the rule that fired.

**It does not touch your audio.** TELESCOPE reads L and R, pushes them to its own analysis thread and
writes nothing back: **0 samples of latency, 0 tail, bit-exact output**. That is not a claim, it is a test:
`tests/PassThroughTest.cpp` (`[telescope][null]`) runs deterministic stereo noise through the plugin with
bypass on and off, with each of the thirteen lenses selected, in blocks of 1, 7, 64 and 4096 samples, and
from a mono source — `NULL_MISMATCHES=0` in all five cases. A second test (`[gain][telescope]`) sends a
full-scale signal through and checks the output samples are the *same* samples.

Analysis runs on its own thread. The audio never waits for a measurement.

### What it is not

- **It does not localise sources.** FIELD and POLAR LEVEL show pan direction by energy. There is no HRTF,
  no ITD, no azimuth — recovering where a sound *was* from a finished stereo mix has no unique solution.
- **Stereo only.** No surround, no Ambisonics.
- **No speech-intelligibility or dialogue metric.**
- **No AI, no network, no telemetry.** VERDICT is a deterministic table of thresholds. The same signal
  gives the same report, word for word, and nothing leaves your machine.
- **"No findings" is not "it's finished."** Read the VERDICT section: this one matters.

---

## Installing on macOS

The installer is a `.pkg`: double-click it, and macOS's own Installer puts the plug-ins where your DAW
looks for them (`/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/Components`). It asks for your
admin password itself. Nothing lands in quarantine, so there is no `xattr` command to run afterwards.

The package is **signed with a Developer ID and notarized by Apple**, so Gatekeeper opens it with a
double-click and asks nothing else. If macOS ever warns you about *this* `.pkg`, do not bypass the warning:
it means the file is not the one we published — download it again from the release and check its SHA-256
against `SHA256SUMS.txt`.

Requires **macOS 11.0 (Big Sur) or newer**. The binary is universal: Apple Silicon and Intel.
On Windows 10+ it is a VST3 x64 in an unsigned ZIP, in the same release; how to install it is in the `LEEME PRIMERO.txt` inside.

---

## The window

- **The strip on the left** lists the thirteen lenses. Click one to open it. Only the modules that the
  open lens needs are computed, so the ones you are not looking at cost nothing — except loudness, which
  is *always* on, so the integrated value, LRA, histogram and clip count never have a hole in them because
  you were looking somewhere else.
- **S · M · L** at the top right resize the window. The layouts are not the same drawing scaled: each size
  has its own metrics.
- **LANGUAGE**, at the foot of the strip, switches the whole plug-in. See [Language](#language).
- **Hovering** over any plot gives you a readout — always from the *data*, never from the pixel colour.
  Where there is no data, there is no readout: nothing is invented to fill the box.

---

# The lenses

## 1 · LOUDNESS

![LOUDNESS](images/loudness.png)

**What it measures.** How loud the programme is, to the broadcast standard.

| Reading | Definition |
|---|---|
| **INTEGRATED** (LUFS) | ITU-R BS.1770, double gate: absolute −70 LUFS, relative 10 LU below the mean of what passes |
| **MOMENTARY** (LUFS) | 400 ms window |
| **SHORT-TERM** (LUFS) | 3 s window, at 10 Hz |
| **LRA** (LU) | EBU Tech 3342: gates at −70 and −20 LU, P95 − P10 of the short-term values |
| **TP MAX** (dBTP) | true peak, BS.1770 Annex 2, measured *before* the K-weighting |
| **L / R** (dBTP) | the same true peak, **per channel** — same FIR, same raw signal, one maximum each |
| **M MAX / S MAX** | maxima since the last RESET |

**How to read it.** Four bars: **MOM** and **SHORT** in LUFS, **L** and **R** in dBTP, all on the same
0 … −60 scale. The number is the integrated value. The peak bars carry the clip threshold from DYNAMICS as
a dotted amber line, their own peak tick and their maximum since the RESET. The history below shows the
last three minutes of short-term with momentary behind it. If you pick a streaming target, its line is
drawn across the LUFS bars and the history, and the distance to your integrated value is written out.

**While a window is still filling** — 400 ms for momentary, 3 s for short-term — the bar shows the
*partial* value with a dimmed fill and a thin outline, and the number is greyed. It is the same
calculation over the hops there are, not an estimate: when the window fills it matches the official
number to the bit. The integrated value has no partial, and will not: the gate is the gate.

**Controls.** `RESET` (empties everything and starts over) · `PAUSE` / `RESUME` (the engine keeps draining
its bus but stops integrating, so the discard counter does not lie) · `TARGET` (the streaming platform).

**What it is not.** The K-weighting is recalculated for your sample rate from the analogue prototype —
the standard only publishes the 48 kHz table. That table is how we check the recalculation: evaluated at
48 kHz it reproduces the published coefficients to 8.9 × 10⁻¹⁶. And the true peak is oversampled 4×, which
has a **known maximum under-read** the standard itself tabulates: EBU test 3341-17 reads −6.3160 dBTP
against a real −6.0. That is inside the standard's own +0.2/−0.4 dBTP tolerance, and it is a real
under-read: if your master measures −1.0 dBTP here, treat it as a little higher.

---

## 2 · DYNAMICS

![DYNAMICS](images/dynamics.png)

**What it measures.** Not how loud the mix is — that is LOUDNESS — but how much headroom is left in it.

| Reading | Definition |
|---|---|
| **PSR** (dB) | max true peak of the **last 3 s** − short-term. The live reading. Valid only once there is a short-term value |
| **PLR** (dB) | max true peak **since RESET** − integrated. The peak-to-loudness ratio of AES TD1004. Valid only once the integrated value has passed the gate |
| Histogram | short-term since RESET in **61 bins of 1 LU**; bin *i* is centred on (*i* − 60) LUFS |
| Clips | events above the threshold in dBTP (default −1.0, range −3…0) since RESET |
| Timeline | 10 minutes, one mark per second that had events, now on the right |

**How to read it.** A steady sine reads PSR = PLR = 0.0 dB, and that is the right answer, not a bug: for a
997 Hz sine the K-weighting adds exactly the +0.691 dB that the BS.1770 constant subtracts, so LUFS = dBFS
peak. A steady tone has no peak headroom over its own level.

**One clip event is not one sample over the line.** A 997 Hz sine that overshoots for 50 ms crosses the
threshold about fifty times — once per cycle — for what any ear calls *one* clip. An event opens on the
first sample above the threshold and does not close until 100 ms below it. Measured: 10 bursts of 50 ms,
1 s apart → **10 events**.

**Controls.** `RESET` · `PAUSE` / `RESUME` · the clip threshold. **Changing the threshold restarts the
count** and the timeline: a counter that mixed events measured against two different ceilings would mean
nothing.

**What it is not.** The 8 dB reference line is a *reference*, not a verdict. The lens draws it, labels it
and shows where your number falls. It does not colour the number red and it does not write a conclusion.
That is VERDICT's job, with the rule visible.

---

## 3 · SPECTRUM

![SPECTRUM](images/spectrum.png)

**What it measures.** The spectrum, with an exact dB reference: each bin is in **dBFS referred to a
full-scale sine**,

> dB_k = 20 · log₁₀( 2 · |X_k| / (N · CG) )  with CG = Σw / N (the window's coherent gain)

so a sine of amplitude A centred on a bin reads exactly 20·log₁₀(A). This is not a calibration by eye: the
coherent-gain correction is exact and is checked with all three windows (−20.0000 dB with Hann,
Blackman-Harris and Kaiser).

**How to read it.** Two consequences of that definition are worth knowing before you look at the screen:

- **Full-scale white noise does not read 0 dBFS per bin.** Its energy is spread over N/2+1 bins, so each
  bin reads far lower. No serious analyser says otherwise; we also write it down.
- **Changing the FFT size moves the noise floor, not the tones.** Doubling N splits each bin in two: a
  *tone* still reads its amplitude (all its energy lands in one bin) but the *noise* drops 3 dB per bin.
  A big FFT "cleans" the floor without anything having changed in the audio.

**Controls.** `FFT` (1 024 – 32 768) · `WINDOW FN` (Hann 4.00 bins / −31.5 dB · Blackman-Harris 8.00 /
−92.0 · Kaiser β=9 6.06 / −66.3, all measured from this code) · `OVERLAP` · `CHANNEL` (L · R · M · S ·
L+R) · `BANDS` (free / ⅓-octave ISO 266, 30 bands / Bark, 24 bands) · `SLOPE` · `AVERAGE` (none /
exponential τ 0.1–10 s / infinite) · `HOLD` (peak-hold decay, 0–60 dB/s, default 12; measured 12.03) ·
`RANGE` · `SMOOTHING` (off / 1/24 / 1/12 / 1/6 octave, default 1/12).

**`SMOOTHING` is a screen setting, not a measurement one.** It averages the curve that is *drawn* over a
fixed width in octaves — the same idea as SPAN's — and touches nothing else: the readout under the cursor,
the peak hold and every number this lens reports keep reading the raw bins. An analyser that smooths the
number it reports has stopped being useful for measuring. On a log frequency axis a fraction of an octave
is a fixed width in pixels, which is why it costs a running sum and nothing more.

**Why the default slope is 3.** An FFT bin measures a *fixed* width (Δf = sr/N) while music and hearing
work in octaves, whose width grows with frequency. Pink noise — equal energy per octave, the neutral
reference — falls 3 dB per octave in a per-bin spectrum. With slope = 3 pink reads **flat**, which is what
you expect of a reference. Measured: −0.0072 dB/oct over 10 s of pink noise between 100 Hz and 10 kHz.

**What it is not.** The slope is a *display* transform: it does not touch the data, and the number under
your cursor is the tilted one. It is **not** applied to the band modes — a ⅓-octave band already
integrates a width proportional to frequency, so applying both would tilt pink noise 3 dB/oct upwards and
draw a staircase where there is a straight line.

**A band with no bin in it is marked differently from a band at zero.** With a 4 096-point FFT at 48 kHz
the bin is 11.7 Hz wide and the 40 Hz band is 9.3: not one bin fits. That is not "no energy", it is *at
this resolution this band cannot be measured*, and it is drawn as what it is. For bass, use a big FFT.

---

## 4 · SPECTROGRAM

![SPECTROGRAM](images/spectrogram.png)

**What it measures.** Level over time. Frequency in Y (log, 20 Hz – 20 kHz, 512 rows), time in X
(10 / 30 / 60 s of history, now on the right), **level in the colour**, mapped over `[−range, 0]`.

**How to read it.** The colour ramp is the label's, and it is **monotonic in luminance** — 0 inversions
across all 256 entries. That is required, not decorative: here the colour *encodes* dB, so if two entries
crossed in brightness, two different levels would look equally loud.

**Controls.** `HISTORY` (10 / 30 / 60 s) · `RANGE` (60 / 90 / 120 dB) · `CHANNEL` · `PALETTE`. The FFT
settings are SPECTRUM's — it is the same knob, not a copy.

**The palette is one setting for four lenses.** SPECTROGRAM, STEREO SPECTROGRAM (its level axis only — the
phase stays bipolar), WATERFALL and FIELD share the ramp you pick, because it is the same decision seen
from four lenses. `ovni` is the label's own ramp; `inferno` and `viridis` are the exact tables published by
Smith and van der Walt (2015, CC0, credited in `NOTICE.md`); `spectrum` is the blue → cyan → green → yellow
→ red → white one. The first three are **monotonic in luminance** — brighter always means louder, for any
pair of values — and `spectrum` deliberately is not: there, what orders the level is the *hue*, which is a
sequence the eye also reads without a legend.

**Three things that come from the data, not the drawing.**

- It uses the **instantaneous** power, never the averaged one: a spectrogram *is* the evolution over time,
  and averaging would erase exactly what it shows.
- **Each row takes the maximum of the bins in its cell**; only when a cell contains no bin at all (bass,
  where rows are denser than bins) is it interpolated in dB between neighbours. Always interpolating would
  flatten narrow peaks by up to 4 dB, which is the thing a spectrogram exists to show.
- **Changing the FFT size, overlap, channel, range or history clears the display.** Mixing columns
  measured under two different mappings would be a picture that lies about what already happened. Changing
  only the *history* clears the ring but does **not** restart the analysis: stretching the window to look
  further back cannot cost you the number you were reading.

**What it is not.** The hover readout is re-read from the ring with the same grouping the column was
painted with — not matched against the pixel's colour. The palette rounds to 8 bits and has ten pairs of
entries with the same colour, so "nearest colour" lied by one step (0.35 dB at range 90) at those levels.
If the column under the cursor has already scrolled out of the ring, there is no readout.

**Reduced motion leaves this lens running.** Its X axis *is* time; freezing it would not be less motion,
it would be showing less data.

---

## 5 · WATERFALL

![WATERFALL](images/waterfall.png)

**What it measures.** The same data as the spectrogram, put in depth: frequency in X (log, 20 Hz –
20 kHz), **level as height**, time in Z with now at the front and the past receding.

**How to read it.** The spectrogram puts level in the *colour*, and the eye compares colours badly: two
greens 6 dB apart look almost the same, and a narrow 12 dB peak goes unnoticed. Here level is **height**,
which the eye compares better than anything else. The cost is what the spectrogram does better — with 120
lines overlapping, a short event can hide behind a later one. They are complementary; that is why both
exist.

**Controls.** `LINES` (60 / 90 / 120) · `TILT` · `HISTORY` · `RANGE`. The back line is always the oldest
column still in the ring and the front line the one just written; the spacing is integer arithmetic over
the write index, so the same ring always gives the same columns. If the ring holds fewer columns than the
lines you asked for, it draws the ones there are — repeating a column would draw relief the signal does
not have.

**What it is not.** There is no GPU. Metal only exists on macOS and JUCE's OpenGL context is contested by
several hosts' own drawing; an analyser that shows two fewer lenses on Windows — or in the wrong host —
is not the same product. The projection is **oblique in software**: no divide by z, three multiplies per
point. Oblique rather than perspective because a real perspective divides by z, and that divide breaks two
things worth more than realism in an instrument: that the same distance in frequency measures the same at
any depth, and that the projection stays monotonic.

The occlusion is drawn front-to-back with a horizon, and the result on screen is **identical** to the
literal painter's algorithm — which is provable here, and proved in `[waterfall][horizon]`, because the
projection is strictly decreasing in z. The cost with all 120 lines is in the
[data sheet](../ficha.md), together with the conditions it was measured under — against a budget of 4 ms
median and 8 ms p95.

The hover readout gives frequency and dB **of the front line only** — the one that can be read without
ambiguity, since in depth one pixel column falls across several lines at once.

---

## 6 · CQT

![CQT](images/cqt.png)

**What it measures.** The spectrum by **note**. An FFT spreads its bins at equal distances in *hertz*; the
constant-Q transform (Brown 1991; Brown & Puckette 1992) spreads them at equal distances in *octaves* and
gives each bin its own window:

```text
B     = 24 bins per octave (two per semitone: one quarter-tone per bin)
f_min = 27.5 Hz  (A0, the lowest note on a piano)
f_max = min (20 kHz, 0.45·fs)          → 229 bins at 48 kHz
Q     = 1 / (2^(1/B) − 1) = 34.127     → the same Q across nine octaves
N_k   = round (Q · fs / f_k)           one Hann window per bin
```

**How to read it.** One bar per bin over a note axis — the *odd* bins, the quarter-tones between notes,
are drawn fainter — with a 114-key keyboard drawn to scale underneath, so every bar stands on its own key.
Below that, the chromagram in twelve bars with the tonic in amber and the key with its two numbers.

**Controls.** `CHANNEL` · `CHROMA` (the chromagram's smoothing: 0.5 / 2 / 5 s). Peak hold shares
SPECTRUM's decay setting — the same knob.

**The bass is inherently late, and the lens says so.** Each bin looks `N_k` samples back, and that halves
every octave: A0 needs 59 567 samples (**1.2410 s** at 48 kHz), A2 0.3103 s, A4 0.0776 s, A6 0.0194 s.
There is no way to have quarter-tone resolution at 27.5 Hz without listening for over a second — that is
physics, not an implementation choice. TELESCOPE **declares** it, in the frame and in the `A0 · 1.24 s`
label at the foot of this lens and SPIRAL, instead of hiding it. The kernel support is anchored to the
**end** of the block, so only the bass pays its window: centred, even the 20 kHz bins would be looking
0.68 s into the past.

**What it is not — the key is never stated alone.** The lens always shows three things together —
`A minor · confidence 0.83 · 91 % of the time` — for two measurable reasons:

1. **Confidence has a floor.** It is the maximum of 24 correlations against the Krumhansl & Kessler (1982)
   profiles, so even a perfectly flat chromagram wins one of them by chance. 6 s of pink noise scores
   **≈ 0.54** in the reference run; with 12 s it falls to ≈ 0.38. What actually gives noise away is the **share of time**: 37 %
   and 3 % against the 98–100 % of a real key.
2. **Some music is genuinely ambiguous.** A bare C-E-G triad, all at the same level, with no bass,
   correlates 0.790 with C major and **0.809 with E minor** — E minor wins. That is not an error: the
   minor profile weights its ♭6 at 3.98, and those three notes in a real bar could be either. Double the
   tonic in the bass — how a root-position triad is normally played — and C major wins with **0.845**
   against 0.613. An instrument that printed "C major" flat out in the first case would be choosing for
   you.

Silence reports **no key** (tonic and mode at −1, confidence 0), because "I don't know" is a result and
"C major, confidence 0" would be worse than saying nothing.

**And the sidelobe floor is published.** The kernels are pruned at 0.0054 · max|K_k| (the paper's
threshold), which buys a floor: measured on a lone A4, the worst sidelobe sits **67.5 dB below** the peak.
There is no absolute silence around a loud note, and that thing on screen has a name and a number.

---

## 7 · SPIRAL

![SPIRAL](images/spiral.png)

**What it measures.** The same constant-Q, **coiled**: one turn per octave, so equal notes end up at the
same angle.

```text
angle  = pitch class   ·  C at the top, clockwise, one turn = one octave
radius = octave        ·  A0 inside, the highest bin outside, linear per octave
spike  = magnitude     ·  brightness and thickness follow level; below the range floor, nothing is drawn
```

**How to read it.** A C in C2, another in C4 and another in C6 stop being three distant bars on a long
axis and become three spikes **aligned on one radius**. That is the thing CQT cannot show and this can:
the octave structure of what is playing, at a glance. In the centre, the chroma wheel: twelve sectors at
the **same angle** as the spikes of their class, tonic in amber, key inside.

**Controls.** `CHANNEL` · `CHROMA`.

**What it is not.** The spiral is Archimedean, so a circle crosses it at exactly one point — the C ray.
The guide circles are **not** "octave n": they are the **radius where octave n starts**, and that is where
the label sits. Reading them as octave rings would be reading one octave too much.

---

## 8 · SCOPE

![SCOPE](images/scope.png)

**What it measures.** The three classic ways of *looking* at stereo, over a sliding window of
**100 / 300 / 1000 ms** (default 300):

| Output | Formula | What it says |
|---|---|---|
| **CORR** | ΣLR / √(ΣLL·ΣRR) | +1 mono · 0 decorrelated · −1 out of phase |
| **WIDTH** | √(ΣSS / ΣMM) | 0 mono · 1 two independent sources · ↑ the side dominates |
| **BALANCE** (dB) | 10·log₁₀(ΣRR / ΣLL) | + = R louder, − = L louder |
| **MONO LOSS** (dB) | 10·log₁₀(ΣMM) − 10·log₁₀((ΣLL+ΣRR)/2) | 0 if L=R · −3.01 if independent · −∞ if L=−R |

with M = (L+R)/2 and S = (L−R)/2, accumulated in `double` per 100 ms hop. This is exactly the maths the
label uses to measure ORBIT and PULSAR, fed with the same deterministic pink noise, so TELESCOPE's numbers
are **directly comparable** with theirs. One declared difference: here balance is R over L; ORBIT's
harness prints L over R — the same number with the sign flipped.

**How to read it.** `LISSAJOUS` draws the goniometer, and **it auto-scales and says so**: the outer ring is
the hop's peak (smoothed, fast attack, slow release), labelled underneath — *"lissajous · edge = peak
−24.8 dBFS"*. Without that, a mix at −20 dBFS is a dot in the middle and the goniometer stops doing what
it is for: reading the *shape* of the stereo, not the level. The gain stops at −40 dBFS: stretching the
cloud of a signal that is not there would be exactly the kind of lie this plug-in does not tell. `POLAR`
keeps the angle and makes the radius level in dB, from −60 to the edge.

The **oscilloscope** shows 40 ms (1 920 samples at 48 kHz; the buffer tops out at 2 048, so above
~51.2 kHz the visible window shortens). With `TRIGGER` it starts at the first rising zero crossing of M
within the first 20 ms; without it, at the start of the hop, and it swims — which is the truth of what
arrives. It is drawn with a min/max envelope per pixel column, so peaks are not lost.

**Controls.** the mode (`LISSAJOUS` / `POLAR SAMPLE` / `POLAR LEVEL`) · `TRIGGER` · `WINDOW`
(100 / 300 / 1000 ms).

**What it is not — the edge cases, and never NaN or infinity** (the UI draws these numbers):

| Situation | What is published |
|---|---|
| No signal (ΣLL+ΣRR ≈ 0) | everything at 0 and *"no signal"* — which is **not** the same as "perfect mono" |
| One channel muted (ΣLL·ΣRR ≈ 0) | `corr = 0`: there is no **defined** correlation, not a correlation that equals zero |
| L = −R (ΣMM ≈ 0) | `width` at its ceiling **10** and `monoLoss` at its floor **−60 dB** |
| Extreme imbalance | `balance` clamped to **±60 dB** |

---

## 9 · SCOPE — POLAR LEVEL

![POLAR LEVEL](images/polar-level.png)

**What it measures.** SCOPE's third mode, and the one built for mixing: a half-circle with **mono at the
top**, **L and R on the base**, energy drawn as **one ray per degree**, the instantaneous cloud on top and
the correlation meter alongside. (Through 0.1.0's first visual pass this mode was called HEMISPHERE; the
name *hemisphere* is still used here for the **fold**, which is a different thing — the mode is named for
what it shows, the fold for what it does.)

```text
θ = 90° + 2·atan2(R − L, R + L)     ≡     2·atan2(R, L)      (mod 360°)

only L → 0°   ·   mono (L = R) → 90°   ·   only R → 180°   ·   L = −R → 270°
```

**One ray per degree, in two layers.** The engine publishes 360 bins of one degree each, and all 181 of
them from 0° to 180° are drawn — each a 1° wedge from the origin, clipped to [0°, 180°] so nothing crosses
the base. Two layers sit on those rays:

| Layer | What it is |
|---|---|
| **Average** (filled) | the envelope averaged **in time**, τ = 0.3 s, in energy, bin by bin |
| **Peak** (thin outline with glow) | the peak-hold with its decay — 12 / 24 / 48 dB/s, your choice |

The averaging is in **time**, never in **angle**: averaging in angle would invent width the measurement
does not have. (Through 0.1.0's first visual pass the envelope went through a circular ±2° running
*maximum* before being drawn, which turned every spike into a flat 5° plateau — the staircase was the
width of the filter, not of the data.)

**How to read it.** The goniometer's cloud says *where* there are samples; these rays say *how much*
there is in each direction, which is the question a mix asks ("is the bass centred?", "how much material
do I have out of phase?"). The factor of **2** is what makes the upper half-circle cover all in-phase
stereo: the real quadrant of a vector (L, R) with both components positive spans 90°, and here it is
opened to 180°. A consequence worth having: the angle is **linear in panning**, so distances on screen read
as distances in pan.

**Out of phase folds onto the base.** The base sits at the **foot** of the panel — the panel *is* the
half-circle, there is nothing below it. What is out of phase (θ above 180°) is folded onto the base with
θ' = 360° − θ, which sends each direction to the place it belongs *by panning* (θ = 190°, which is R with
its phase flipped, lands at 170°, right next to R; θ = 270°, which is L = −R, lands at 90°, in the middle),
painted in the alert colour over the in-phase lobe. **Nothing is lost by folding**: the number is printed
next to it — `out of phase N %`, measured over the samples as Σ(l² + r²) for the pairs with l·r < 0 over
the total. The emphasis is proportional to the measured mono loss: every mix with decorrelated sources has
instantaneous samples out of phase — that is normal — and painting them as an emergency would teach you to
distrust the meter.

**The radius is level relative to the strongest direction** — 0 dB at the edge, −6 dB at half the radius,
−12 dB at a quarter, with arcs at −6 / −12 / −18. That is what makes the dominant direction form a **lobe**
instead of a fan. (Until 0.1.0's visual pass the radius mapped −60…0 dB linearly, and with any real music
every direction fell inside the top 20 dB — that is, between 0.67 and 1.0 of the radius — so the envelope
hugged the outer arc and the lens said "all wide" no matter what.) `LIN` in the footer switches to a dB
scale with a −24 dB floor for whoever prefers to read it that way.

**The four numbers are here too.** WIDTH, BALANCE, MONO LOSS and the out-of-phase percentage sit above the
half-circle: the column that carries them in Lissajous and polar is taken here by the vertical correlation
meter, and until 0.1.0's visual pass they simply were not drawn in this mode.

**Controls.** the mode selector · `PEAK DECAY` (12 / 24 / 48 dB/s; measured 0.00 % error) · `LIN` / `dB`
(the radial scale). The decay is the lens's memory, not the engine's: the engine publishes the hop's
envelope over **all** its samples, not the 2 048 decimated ones the goniometer draws — an envelope that
skips the transient would under-read exactly where it matters.

**What it is not. It is not localisation.** It is pan direction by instantaneous energy, not where a sound
comes from in a room: no HRTF, no ITD, nothing of the kind. Same honesty as FIELD's label.

**Verified** (`[telescope][hemis]`): mono lights **1 of the 181 rays** above −20 dB relative and
independent noise lights all **181** above −12 — an aiming needle and a fine comb, which is what the two
signals are; mono peaks at **90°** with the rest of the circle at the floor;
only-L / only-R give clean lobes at **0°** / **180°**; L = −R gives **270°** with the upper hemisphere's
maximum *at the floor, without exception* — and, once folded, **100.0 % out of phase with zero pixels of
data below the base**; independent noise gives a 360° fan whose 0–90 and 90–180
sectors match within **0.844 dB**; a 22.5° constant-power pan lands at **45°**, which is also what FIELD's
energy panning gives via θ = arccos(−pan); and full-scale mono reads **+3.0103 dB**, because L and R add in
quadrature.

---

## 10 · BAND CORRELATION

![BAND CORRELATION](images/band-correlation.png)

**What it measures.** SCOPE's five sums, **per ⅓-octave band** (ISO 266, the usual 30).

Why: a broadband correlation meter, on a mix with mono bass and open highs, gives an intermediate number
that says nothing. Measured on the house test signal — an 80 Hz sine in L = R plus high-passed pink noise
above 2 kHz with R = −L — broadband gives **corr +0.24 and mono −2.1 dB**, while the 80 Hz band gives
**+1.00** and the 4 kHz band **−1.00**. The broadband meter is not wrong; there are two different things
happening and it cannot see them.

```text
ΣLL = Σ|L_k|²   ΣRR = Σ|R_k|²   ΣLR = Σ Re(L_k·R_k*)   ΣMM = Σ|M_k|²   ΣSS = Σ|S_k|²

corr_b     = ΣLR / √(ΣLL·ΣRR)                        width_b    = √(ΣSS / ΣMM)
balance_b  = 10·log₁₀(ΣRR / ΣLL)                     monoLoss_b = 10·log₁₀ΣMM − 10·log₁₀((ΣLL+ΣRR)/2)
```

**And it agrees with the time-domain meter, which is the proof that the two are the same calculation.**
Broadband computed from the bins against the `Stereo` module in the time domain, over independent pink
noise: corr differs by **0.00076**, width by 0.00077, balance by 0.0288 dB, mono loss by **0.0033 dB**
(criteria: 0.02 and 0.1 dB). If they disagreed, one of them would be wrong.

**Controls.** `WINDOW` (0.3 / 1 / 3 s) · `ROW` (which of the four figures is on top).
`K = round(seconds × frames per second)` frames whose positions are fixed in the stream: the determinism
is inherited, and the 30 bands come out **identical to the bit** whether the audio arrives in blocks of 1
or 4 096. What is published is the **effective** window (0.2987 / 1.0027 / 3.0080 s measured), not the one
you asked for.

**What it is not. A band with no bin in it is not zero.** With a 4 096-point FFT at 48 kHz the 40 Hz band
is 9.3 Hz wide and the bin is 11.7: none fits. That is not "zero correlation", it is *at this resolution
it cannot be measured*, and the lens marks it differently. The hover readout says **how many bins**
measured each band, because in the bass it can be one.

**And the scatter in the bass is not a defect of the module.** A correlation estimator over noise has
σ ≈ 1/√(2·BW·T), and a ⅓-octave band has BW = 0.2316·fc. With a 1 s window the 10 kHz band gathers 2 316
degrees of freedom (σ = 0.015) and the 50 Hz band gathers 12 (σ = 0.21). There is less signal per second
down there, no analyser can invent it — but it can avoid hiding it.

---

## 11 · STEREO SPECTROGRAM

![STEREO SPECTROGRAM](images/stereo-spectrogram.png)

**What it measures.** The sonogram with the colour's meaning changed: **the colour is the phase**
(per-bin coherence) — red out of phase, green wide, white mono — and **the brightness is the level**, with
the same dB mapping over `[−range, 0]`.

**How to read it.** A cell with no energy is **black**, not "dim red": without that, the noise floor —
where phase is pure chance — would paint the screen in colours that mean nothing. Coherence is
`coh_k = Σ Re(L_k·R_k*) / √(Σ|L_k|²·Σ|R_k|²)` smoothed over BAND CORRELATION's window, and it is 0
**by definition** when a channel has no energy in that bin: there are not two phases to compare. In
silence the cell sits at the centre of the scale (128), meaning *undefined* — a 0 would say "out of
phase", which over silence would be an invented alarm.

**Controls.** `HISTORY` · `RANGE` · `WINDOW`.

**What it is not. The smoothing is not cosmetic.** The coherence of a single frame, over two independent
sources, is spread across all of `[−1, +1]`: the drawing would be confetti. Over the window it settles
around 0 **with scatter** (measured: mean 125.15 of 255, sd 16.32, i.e. ≈ −0.02 ± 0.13 in coherence),
which is what a coherence estimator actually does. With L = R it reads **255 in every cell with energy**
and with L = −R it reads **0**: perfect mono is +1, not "nearly".

The **energy**, in contrast, is *not* smoothed: it is a spectrogram's brightness and its X axis is time.
And careful comparing it against SPECTRUM: here **both channels** are summed (`E_k = |L_k|² + |R_k|²`), so
a mono signal reads 3.01 dB above what the L channel alone reads.

---

## 12 · FIELD

![FIELD](images/field.png)

**What it measures.** Energy per **pan direction × frequency**, 64 columns × 96 rows, with decay and a
temporal trail, in 2.5D. Per STFT bin:

```text
pan_k = (ΣRR_k − ΣLL_k) / (ΣRR_k + ΣLL_k) ∈ [−1, +1]
        −1 = L only   ·   0 = centre (or no energy)   ·   +1 = R only
```

With the constant-power law (L = cos θ·x, R = sin θ·x) the number is exact and round: `pan = −cos 2θ`.
Verified bin by bin with a worst error of **4.9·10⁻⁸**: 0° → −1.000 · 22.5° → −0.707 · 45° → 0.000 ·
67.5° → +0.707 · 90° → +1.000.

**How to read it — what it shows and nothing else can.** The four broadband figures of a mix with two hard
sources in opposite channels are nearly identical to those of decorrelated noise (`corr ≈ 0`,
`width ≈ 1`): to a correlation meter the two "sound equally wide". In FIELD one is two blobs in opposite
corners and the other an even cloud from side to side. That difference is the whole point of the lens.

**Controls.** `DECAY` (0.5 / **1** / 2 s; measured 0.512 / 1.003 / 2.005) · `WINDOW` (0.3 / 1 / 3 s, the
same as lenses 10 and 11). Per frame, `grid *= exp(−dt/τ)` and then each bin adds its energy — an
exponential average, so with no new signal the grid falls to 1/e in exactly τ. This is not decoration: at
21 ms per frame, without it the lens would be confetti.

**What it is NOT — and this one cannot be switched off.**

**It does not measure where the sources are.** From a finished stereo mix that problem has no unique
solution, and it is exactly the trap ORBIT fell into: its implemented interaural delay was 8 % of the
physical one and had the sign inverted, and the plug-in called it "position" (audit of 2026-09-03). An
analyser from the same label cannot repeat that mistake in the lens that most invites it. So, permanently:

- the axis is labelled **L … C … R**, never `−90° … +90°` as if it were binaural azimuth;
- under the axis, fixed: **"energy by pan direction · not localisation"**;
- the readout gives the pan as a **percentage**, not in degrees.

**It also does not tell mono from out-of-phase.** L = R and L = −R have the same energy in both channels,
so both give `pan = 0` and both are drawn in the centre. That is correct — FIELD measures level balance,
not phase — and it is exactly what lenses 10 and 11 do show. The three are read together.

**Two honest limits of the resolution.** Below ~1.5 kHz a grid row is *narrower than an STFT bin* (at
100 Hz the row is ~7.5 Hz and a 4 096-point bin at 48 kHz is 11.72 Hz), so a bass tone falls between two
bins that land in different rows and its energy is split: it reads lower than a treble tone of the same
amplitude. Measured with two identical tones: 5 kHz reads 0.0 dB rel, 100 Hz reads −2.9. And the width of
the cloud depends on the window: with decorrelated material the per-bin pan scatters around 0 with the
*estimator's* spread (0.137 measured at 1 s). `WINDOW` changes how wide the cloud opens. What it is not,
is a measurement of stereo width in degrees.

**The dB in the readout is RELATIVE** to the grid's maximum, and it is labelled `dB rel`. The cell
accumulates energy through an exponential average, so its absolute value depends on τ and the frame rate;
converting it to dBFS would need a division that is only valid in steady state. An approximate absolute
number in a meter is worse than an exact relative one — and the absolute level is already given by
SPECTRUM and SPECTROGRAM, which measure it without approximating.

---

## 13 · TONAL BALANCE

![TONAL BALANCE](images/tonal-balance.png)

**What it measures.** Not "how loud is it?" but **"what colour is it, compared to this other thing?"**.
Load a reference track — yours, or a commercial one — TELESCOPE analyses it **whole, offline**, and the
lens compares your live programme against it.

**It compares tilt, not level.** Both ⅓-octave curves are normalised by subtracting **their own integrated
LUFS**:

```text
norm[b]  = curve[b] − integrated LUFS          (each side with its own)
delta[b] = live_norm[b] − ref_norm[b]
```

The same pink noise at −14 and at −20 LUFS gives **delta 0 in every band** (verified: worst case over 24
bands between 50 Hz and 10 kHz is 0.66 dB, and that residue is the noise's own variance over 10 s). Without
the normalisation the graph would answer "which one is louder", which the loudness meter already says.

**And the consequence, said out loud: the delta sums to zero.** If you add 6 dB to everything above 2 kHz,
**the loudness goes up with it**, and the delta does not show "+6 up top and 0 below". It shows this
(measured, not estimated):

| | delta |
|---|---|
| the shelf applied | +6.00 dB above 2 kHz |
| how much the integrated loudness rose | +4.30 dB |
| delta the lens draws ≥ 8 kHz | **+1.70 dB** |
| delta the lens draws ≤ 500 Hz | **−4.30 dB** |

Both numbers are the **same truth**: the difference between the two regions is still exactly 6 dB. What
changed is that it is now counted **at equal volume**, which is how you compare a mix against a reference.
The identity is checked band by band, with a worst error of **0.006 dB** over 21 bands.

**How to read it.** Above, the two normalised curves on SPECTRUM's log grid, each labelled with its name
and its integrated value, on a **fixed** scale of +6 to −42 dB — a self-adjusting scale would make two
captures of the same mix incomparable, which is exactly what the lens is for. Below, the delta as ±12 dB
bars with a **labelled ±3 dB reference band**. That band is a *reference, not a verdict*: it does not say
"this is wrong", it says how much and where. One vertex **per band** (30), not per pixel: more would be
inventing resolution the data does not have. A band that only measures one of the two sides is **not**
drawn at delta 0 — that would say "perfect match" — the curve breaks and its bar is greyed.

**What counts as drawable, and the two ways a line used to break.** A band is drawn when its raw value
sits **at or above −90 dBFS** *and* its normalised value **fits inside the plot** (above −42 LU). A band
is comparable when both sides are drawable. Where the reference has nothing to say the readout says
`no reference in this band`; where it has something to say but the number falls off the bottom of the
plot, it says `reference below the plot range (−42 LU) in this band` — which is a different fact and
deserves a different sentence.

Two separate defects used to break the line, and neither was the music:

- **the −200 dB floor.** Until 0.1.0's visual pass the threshold was the analysis guard value of −200 dB,
  not a level: a band reading −123 dBFS — the analysis's own rounding — counted as a measurement, so the
  curve drew it a hundred decibels below the plot (a vertical line straight down to the edge) and the
  delta against it came out at +106 dB, a number that cannot exist between two programmes.
- **the FFT grid.** Band power used to take **whole bins**: `ceil(lo/binHz)` to `ceil(hi/binHz)`, and when
  both rounded to the same integer the band came out **empty** while holding plenty of energy. With a
  4096-point FFT at 48 kHz the bin is 11.719 Hz and the 40 Hz band is 9.26 Hz wide — 0.79 of a bin — so
  30–50 Hz showed a hole in *both* curves. At 44.1 kHz the hole moved to 25 Hz. Each bin now contributes
  **in proportion to how much of the band it overlaps**, so no band below Nyquist is ever empty:

  ```text
  overlap_k = max(0, min(hi, (k+½)·binHz) − max(lo, (k−½)·binHz))
  P_band    = ( Σ_k P_k · overlap_k / binHz ) · (hi − lo) / Σ_k overlap_k
  ```

  The readout carries the consequence: the bin count is **fractional**, and a band narrower than one bin
  (`0.8 bins at this FFT`) is that bin's density, not an independent measurement.

**Controls.** `RESET` (restarts the programme's average and does **not** unload the reference: you throw
away what was measured, not what you configured) · `LOAD` (or drag a file onto the lens) · `REMOVE`.

**What it is not.** The state stores the **path**, not the numbers. Reopen a session and, if the file is
still there, it is **re-analysed**; if it is gone, the lens says `reference not found: <name>` and draws no
curve. Saving 30 numbers in the preset and drawing them as if they were the file would be showing a
reference that no longer exists, with no way for anyone to notice.

SPECTRUM does **not** apply this lens's covered-width correction: its ⅓-octave bars are the raw sum, which
is an RTA's convention. In the bass bands the two readings differ, and that is deliberate — the one here
is the one you can compare across sample rates.

---

## 14 · VERDICT

![VERDICT](images/verdict.png)

Twelve lenses show. This one **says**. And the iron rule is that **every sentence carries the number that
supports it and the id of the rule that produced it**, both visible at once.

**What it is.** A deterministic rules engine: no AI, no network, no model — a table of thresholds
(`source/data/Rules.h`) and the conditions that read them. The same signal gives the same report, word for
word, today and in a year. It runs on your machine and sends nothing anywhere. The readable twin of the
table — every threshold with its reasoning — is in
[`../telescope-diccionario.md`](../telescope-diccionario.md).

**How to read it.** First a **headline**, fixed under the header: the count of what was measured, with no
adjectives — *"15 checks within range · 3 to look at, the first at 0:20"*. Then four sections: **within
range** · **how it will feel** · **where it translates** · **what to check, and where**. A **finding** is a
⚠ or a ●. The ○ lines — the key, a box that comes out ✓, the distance to a platform — are informative and
are not defects, and they are not counted as "to look at".

**Within range comes first.** Every rule that was evaluated and did **not** fire gets one short line with
the number it measured and the rule's own limit: *"Transients have room: PSR 9.4 dB (floor 8.0 dB)"*,
*"Low mids 200-500 Hz: -0.6 dB vs the trend (muddy from +3.0 dB)"*. A rule that could not be evaluated
says nothing — "within range" is a measurement, not a default. When the lens is too short for the whole
list, the section collapses to a single line that names each rule (the editor's S / M / L zooms scale the
whole canvas, so there the lens has the same size in all three and the section stays expanded).

**How a finding reads.** The number first, the mixing term in brackets, and where to look at the end —
never what to do: *"630 Hz dips 26.0 dB below its own average (deepest band of a 400-1000 Hz stretch that
dips between 0:20 and 0:35). Check what plays in that range there."* A dip that spans several contiguous
bands at the same time is **one** finding, not one per band: the number is the deepest band's, and the time
window is the whole stretch's (from the first band that goes down to the last one that comes back). A
measured dip is a ⚠: ● is kept for what is objectively broken (clip bursts, DC, bands that cancel in mono). The grey evidence line carries the rule's
thresholds next to the measured number: `hole · -25.96 dB / 15 s · rule 6 dB / 10 s`.

**What it compares against.** Section 1's rules ask whether a region is in excess or missing. Compared to
*what* — and the sentence always says which one it used:

| Situation | Against what | What it means |
|---|---|---|
| **with a reference loaded** | the reference, compared by **shape** (each curve minus its own broadband level) | "this region does not look like the mix you chose as a target" |
| **without a reference** | the **material's own trend**: a least-squares line over the programme's shape in log frequency, between 50 Hz and 16 kHz | "this region does not look like the rest of your own mix" |

The second is deliberate. The usual alternative — hiding a "curve of a good mix" inside the code and
measuring against it — is opinion dressed as measurement. Against your own trend there is nothing to have
an opinion about. **In exchange it is a blunt instrument**: a mix with a strong spectral intention can
fire a rule with nothing to fix. **Loading a reference is strictly better.**

**Controls.** `RESET` · `MODE` (live / FILE) · `FILE` → `LOAD` (or drop a file) · `LANGUAGE` (on the lens
strip: the language belongs to the plug-in, not to this lens).

**What it is not.**

- **It does not opine.** It is forbidden from saying "sounds professional", "moving" or "ready". If it
  ever does, that is a bug — and `VERDICT[tono]` sweeps the six phrase tables, word by word, for exactly
  that kind of word.
- **The per-device checks are generic, and it says so.** The six boxes (phone, headphones, laptop, car,
  club with mono sub, hi-fi) are **not** measurements of any real speaker, not simulations, and **there is
  no response curve of any kind inside the plug-in**: each box is a definition of what class of system it
  is — where it starts to respond, where it rolls off, what happens to the stereo — and its check looks at
  your material against that definition. They are good for saying "this mix puts almost everything where a
  phone does not reach". That is a forecast, and it comes out labelled as a forecast.
- **The footer cannot be removed:** *"Measurement, not taste. Device checks are generic. Re-run the
  analysis after every change."*

### "No findings" is not "it's finished"

![VERDICT with no findings](images/verdict-no-findings.png)

When it finds nothing, what it says is: *"Nothing outside the ranges of these rules. They measure; what
they can't hear is yours."* It does not say "ready". The difference is the whole product: the rules cover
what they cover, and what is not in the table is not measured.

### Where the "where" comes from

`analysis/SecondHistory.h` keeps 10 minutes at 1 Hz since the last RESET: per second, the 30 ⅓-octave
bands with their level, correlation and mono loss, the minimum and maximum short-term, the true peak, the
clips, the broadband stereo, the DC and the key. **The rows of a file analysis are the rows of the live
analysis, equal to the bit** (`HISTORY[identidad]`, 60 of 60 rows over a one-minute track). That is why
"there is a hole between 0:20 and 0:35" means the same thing looking at the file as listening to it.

**The spectral part of the row is only paid for with the lens open.** With VERDICT and TONAL BALANCE
closed, the rows still store loudness and DC — which are cheap — and **declare that they have no
spectrum**, rather than publishing thirty zeros that would look like a measurement. In practice: if you
open VERDICT halfway through a track, the report starts there. For exact times over a whole track, use
**FILE** mode.

---

# The rest

## Loading a reference, and analysing a file

Drag an audio file onto TONAL BALANCE, or onto VERDICT in FILE mode.

**The offline analysis is identical to the live one — not "similar": identical to the bit.** The engine
decides its hop and frame positions by counting samples since the reset, not by the host's block size, and
the offline path uses the *same classes*, not a second implementation. Verified over the complete path
against the same WAV read by `FileAnalyzer`: integrated, LRA, true peak and the M/S maxima equal to the
bit; the 30 measured ⅓-octave bands equal to the bit; blocks of 4 096 vs 512 vs 64 vs 7 vs 1, equal to the
bit.

**Formats:** WAV, AIFF, FLAC and Ogg always; on macOS also everything CoreAudio reads (MP3, AAC, ALAC), and
on Windows WMA and MP3. That is what JUCE gives out of the box — there is no in-house decoder, and
therefore no format that "almost" works. **Any** dragged file is accepted, not only the known extensions:
if it cannot be read, the message says so. Refusing an AIFF named `.dat` in silence is worse than trying.

**The sample rate is the file's.** Nothing is resampled: the engine prepares itself at the file's rate and
measures it as it is. Neither the ⅓-octave bands nor LUFS depend on the sample rate, so a 44.1 k reference
is directly comparable against a 48 k session. Resampling would only add one more filter between the file
and its own measurement.

**Speed:** 60 s of audio in about a quarter of a second on an M4 (243 ms with the machine quiet, 633 ms under load; the test requires under 6 s), with progress and cancel.

## Reduced motion

TELESCOPE honours the system's reduced-motion setting, per lens and honestly:

- **SCOPE**: the goniometer leaves no trail — only the current hop, a coherent still frame. POLAR LEVEL has
  no memory: both layers — average and peak — are the hop.
- **SPECTRUM**: no smoothing between frames; the frame is drawn as it arrives.
- **FIELD**: the trail is not drawn (only the current grid) and the normalisation's smoothing is off.
- **SPECTROGRAM, STEREO SPECTROGRAM and WATERFALL keep running.** Their time axis *is* the data; freezing
  them would not be less motion, it would be less information. And there is nothing smoothed to switch
  off — each column is a ring column as it was written.

## Language

![The plug-in in Spanish](images/language-es.png)

`LANGUAGE`, at the foot of the strip, switches the **whole** plug-in — all thirteen lenses, not three of
them. It shows the endonym ("Español", "Deutsch"), not the code: someone who does not read English does
not know their language is called `de`.

Six languages today: **`en` and `es` reviewed**; **`pt`, `fr`, `de` and `it` translated with the
industry's terms and pending review by a native speaker.** English is the default. The fallback is
**per key**: if a language is missing a phrase, that phrase comes out in English and the rest of the
language is untouched — never blank, because a blank label in a meter is worse than one in the wrong
language: you cannot tell a bug from a value that does not exist. The full key-by-language matrix is in
[`../strings-matrix.md`](../strings-matrix.md).

**Note names follow each language's convention**, which is not a translation but **three** different
living systems: letters (C D E) in the English-speaking world, solfège (Do Re Mi) in the Romance
languages, and the German one — letters except in two places, where B is **B♭** and the note above it is
**H**. Showing "B" to a German reader where a B natural sounds would name a different note, a semitone up.

The note name **with an octave** (`A4`, `C#3`) stays in the English convention on purpose, and is not an
inconsistency: there an *axis* is being labelled, and analysers' axes the world over say C1, not Do1.

## Where the numbers come from

Every number in this manual is produced by a test over a synthetic signal, in the repository, runnable:

```bash
cmake --preset dev
ninja -C build telescope_VST3 telescope_AU telescope_Standalone OvniTelescopeTests
ctest --test-dir build -R telescope --output-on-failure
```

The tag-by-tag table is in the plug-in's [`README.md`](../../README.md) ("Verificación"), and the full
technical write-up of every decision is in that same document. The paint budget of each lens — 4 ms median
and 8 ms p95 at size L — is measured by `[budget]` and published together with the load factor `k` of the
machine that measured it, so a number never travels without the conditions it was taken under.

---

## Licence

**AGPLv3**, like the whole OVNI catalog. Source: <https://github.com/ovniaudio/ovni>.
Third-party notices: [`NOTICE.md`](../../../../NOTICE.md).
Questions: <hello@ovniaudio.com> · <https://ovniaudio.com>
