# Template de plugin — sello OVNI (`shared/template/`)

El **chasis** de un plugin del sello. Trae ya resueltos, en clases base, todo lo que comparten los plugins
y que no depende del DSP concreto:

| Archivo | Qué da |
|---|---|
| `PluginProcessorBase.{h,cpp}` | `ovni::PluginProcessorBase` — APVTS, **bypass**, **gain IN/OUT** (rampeado anti-zipper) + **IN PHASE / mono-safe** (bass-mono) **inyectados a TODO plugin**, **Program Change MIDI → preset** (AsyncUpdater, RT-safe), `getStateInformation`/`setStateInformation` (APVTS XML + `stateVersion`), atomics `uiOutPeak`/`uiClip`, `PresetManager` + `ABState` cableados, `getNumPrograms()=1`. |
| `PluginEditorBase.{h,cpp}` | `ovni::PluginEditorBase` — el **header browser** de M5: marca OVNI + designación · `[power]` `[SAVE]` `[‹ nombre › menú por categoría]` `[A/B]`, con marca de *modificado* (`*`), + el **selector de tamaño `S·M·L`** (resize 3 tamaños fijos, persistido global del sello). Usa `ovni::ui` (Theme/Fonts/Panel/Controls) + `ovni::presets`. |
| `CMakeLists.txt.in` | plantilla de CMake del plugin (la rellena `tools/scaffold.sh`). |

Un plugin nuevo **deriva** de estas bases y sólo escribe su DSP, su UI y su tabla de presets.

---

## Qué define el plugin concreto

`tools/scaffold.sh <Nombre> <Code> <Mfr>` crea `plugins/<nombre>/` con `CMakeLists.txt` (de `CMakeLists.txt.in`)
y un esqueleto de `source/`. Completá:

### 1. `source/PluginProcessor.{h,cpp}` — derivá `ovni::PluginProcessorBase`

```cpp
#include "template/PluginProcessorBase.h"
#include "engines/movement/MovementEngine.h"   // tu motor (S1)

class PluginProcessor : public ovni::PluginProcessorBase
{
public:
    PluginProcessor()
        : ovni::PluginProcessorBase ("PULSAR", createParameterLayout()) {}   // <- nombre = carpeta de User presets

    // PLUGIN: tu layout de parámetros (IDs congelados; sólo agregar al final).
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorEditor* createEditor() override;   // devuelve TU PluginEditor

protected:
    void prepareEngine (const juce::dsp::ProcessSpec& spec) override { engine.prepare (spec); }

    // PLUGIN: tu DSP. Se llama SÓLO cuando NO está en bypass; los canales sin entrada ya vienen limpios.
    void processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        ovni::engines::MovementParams mp { /* leídos del apvts */ };
        engine.process (buffer, mp);
    }

    // opcional: aporte del limiter al LED de clip
    float extraClipPush() const override { return /* dB de reducción de tu limiter -> 0..1 */ 0.0f; }

private:
    ovni::engines::MovementEngine engine;
};

// entry point de JUCE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PluginProcessor(); }
```

### 2. `source/PluginEditor.{h,cpp}` — derivá `ovni::PluginEditorBase`

```cpp
#include "template/PluginEditorBase.h"
#include "ui-kit/KnobLookAndFeel.h"   // UI-kit (S2)

class PluginEditor : public ovni::PluginEditorBase
{
public:
    explicit PluginEditor (PluginProcessor& p)
        : ovni::PluginEditorBase (p, juce::String::fromUTF8 ("MOV·01"))   // designación
    {
        // addToCanvas (NO addAndMakeVisible) de tus knobs / visualizer / meter (OutputMeter (p.uiOutPeak, p.uiClip))
        // -> así escalan con el zoom S/M/L (son hijos del Canvas, no del editor).
        setBaseSize (960, 580);   // tamaño de DISEÑO (coords base); el resize S/M/L lo escala solo. NO uses setSize.
    }

protected:
    void layoutBody (juce::Rectangle<int> body) override { /* ubicá tus controles dentro de `body` */ }
    void paintBody  (juce::Graphics& g) override        { /* superficies/labels propios (encima del header) */ }
};
```

> **BASE TÉCNICA DEL SELLO (obligatoria en TODO plugin — "como ÓRBITA").** El chasis ya inyecta
> `inGain`/`output`/`monoSafe` (NO los declares en tu layout). En tu editor **exponelos**: knobs **IN/OUT**
> (`SliderAttachment` a `"inGain"`/`"output"`) + `ovni::ui::ToggleButton(apvts,"monoSafe","IN PHASE","MONO SAFE", hue)`
> en una sección de utilidad/salida junto al `OutputMeter`. Usá el **color de familia** del motor como hue en
> knobs/seg/toggle/visualizer: **Movimiento=`theme::cyan` · FDN=`magenta` · STFT=`green` · Granular=`amber`**.
> Y el **visualizer debe reaccionar a TODAS las macros**: mandá un `std::atomic<float>` de telemetría por cada
> macro desde `processAudio` y consumilo en el `VisualizerBase` (bug real de PULSAR: `distance` no movía el pad
> porque no se mandaba su telemetría — no dejes ninguna macro sin efecto visual).

### 3. `source/FactoryPresets.cpp` — definí tu tabla

`ovni_presets` declara `factoryPresets()` pero **no la define**: cada plugin provee la suya.

```cpp
#include "presets/PresetTypes.h"
namespace ovni::presets {
const std::vector<FactoryPreset>& factoryPresets()
{
    using C = Category;
    static const std::vector<FactoryPreset> p = {
        { "Slow Drift", C::Production,  {{"width",58},{"room",35}} },
        { "Abduction",  C::SoundDesign, {{"doppler",85},{"chaos",70}} },
        // ...
    };
    return p;
}
}
```

Los valores van en **unidades del parámetro** (los choices como índice). El `PresetManager` los aplica con
`convertTo0to1`, así un mismo número sirve para float, choice y bool.

### 4. `tests/` — incluí el test `[gain]` (puerta anti-clip del orquestador)

El chasis base es abstracto (no tiene DSP), así que la puerta anti-clip se mide **por plugin** sobre el
processor ensamblado. `tools/gain-staging-check.sh` (S4) grepea una línea `PEAK=<peak lineal>` de stdout, así
que cada plugin trae este test (taggeado `[gain]`):

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"

// Puerta anti-clip del sello: full-scale por el plugin ensamblado -> imprime PEAK=<lineal>.
// Con OUTPUT a 0 dB, la cadena anti-clip debe mantener el pico <= 1.0 (0 dBFS).
TEST_CASE ("gain staging: full-scale -> no clip", "[gain]")
{
    PluginProcessor proc;
    const double SR = 48000.0; const int N = 512;
    proc.prepareToPlay (SR, N);

    float peak = 0.0f;
    for (int blk = 0; blk < 200; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int n = 0; n < N; ++n)
                d[n] = std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (blk * N + n) / (float) SR);
        }
        proc.processBlock (buf, midi);
        peak = juce::jmax (peak, buf.getMagnitude (0, N));
    }
    std::printf ("PEAK=%.6f\n", peak);   // <- la línea que grepea gain-staging-check.sh
    REQUIRE (std::isfinite (peak));
    REQUIRE (peak <= 1.0f);
}
```

(El equivalente `[s3]` de la infra de presets ya está resuelto en `shared/presets/tests/` y no necesita DSP.)

---

## Puntos de extensión (resumen)

**Processor** (`ovni::PluginProcessorBase`)
- `processAudio(buffer, midi)` — **requerido**: tu DSP (sólo se llama fuera de bypass).
- `prepareEngine(spec)` — opcional: preparar tu motor.
- `extraClipPush()` — opcional: aporte del limiter al LED de clip.
- `bypassParamID()` — opcional: id del Bool de bypass (default `"bypass"`).
- `createEditor()` — **requerido** (de JUCE): devolvé tu editor.
- `createParameterLayout()` — tu layout (lo pasás al constructor base).

**Editor** (`ovni::PluginEditorBase`)
- `setBaseSize(w,h)` — **requerido** (en el ctor): el tamaño de DISEÑO del plugin (coords base). NO uses `setSize`.
- `addToCanvas(c)` — **requerido**: agregá tus componentes con esto (NO `addAndMakeVisible`) para que escalen con el zoom S/M/L.
- `layoutBody(body)` — **requerido**: ubicá tus controles bajo el header (coords base; el zoom es transparente).
- `paintBody(g)` — opcional: tus superficies/labels.
- `mouseDownBody(e)` — opcional: clicks fuera del header.
- `headerHeight` — opcional: alto del header (default 58).

> **RESIZE GRATIS:** el chasis da 3 tamaños fijos `S·M·L` (selector en el header, persistido global del sello en
> `~/Library/Application Support/OVNI/OVNI.settings`, clave `uiZoom`). Escala TODO el editor por `AffineTransform`
> en un `Canvas` hijo (el editor NO lleva transform propio: JUCE lo reserva para el DPI del host). El plugin no
> hace nada salvo `setBaseSize` + `addToCanvas`. Factores: S=0.8 · M=1.0 · L=1.25.

---

## Gotchas heredados (respetalos)
- **Program Change**: el `CMakeLists.txt.in` setea `NEEDS_MIDI_INPUT TRUE`. **No lo quites** o el preset por
  MIDI deja de llegar.
- **User presets** en `~/Library/Application Support/OVNI <Nombre>/User Presets/` (NUNCA `Audio/Presets` =
  root-owned). El `<Nombre>` lo fija el primer argumento del constructor base.
- **No exponer presets como programs** (`getNumPrograms()=1`): hacerlo rompe la restauración de Bool en VST3.
- **Bypass**: pass-through estéreo ya resuelto en la base; tu `processAudio` no se llama en bypass.
- **Recarga en Ableton**: cachea el binario → **Cmd+Q** para recargar.
