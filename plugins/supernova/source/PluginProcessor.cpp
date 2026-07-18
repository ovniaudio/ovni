#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"
#include "analysis/AnalysisThread.h"
#include "presets/PresetTypes.h"
#include "color/LookRamps.h"

namespace supernova
{
namespace pid = params::id;

juce::AudioProcessorValueTreeState::ParameterLayout SupernovaProcessor::createParameterLayout()
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    static const auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    const auto pctAttr = [] { return juce::AudioParameterFloatAttributes()
                                     .withStringFromValueFunction (pct).withLabel ("%"); };

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::INTENSITY, 1 },     "Intensity",
                                       Range { 0.0f, 100.0f, 0.01f }, 50.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::CHAOS, 1 },         "Chaos",
                                       Range { 0.0f, 100.0f, 0.01f }, 30.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::PARTICLE_SIZE, 1 }, "Size",
                                       Range { 0.0f, 100.0f, 0.01f }, 40.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::GLOW, 1 },          "Glow",
                                       Range { 0.0f, 100.0f, 0.01f }, 50.0f, pctAttr()));

    // Física por-preset (M3): automatizables, no en la franja. Default 50% = el look M2 (gravity bipolar, def 0).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::CURL_SCALE, 1 },    "Curl Scale",
                                       Range { 0.0f, 100.0f, 0.01f }, 50.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::HOME_STRENGTH, 1 }, "Home Pull",
                                       Range { 0.0f, 100.0f, 0.01f }, 50.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::GRAVITY, 1 },       "Gravity",
                                       Range { -100.0f, 100.0f, 0.01f }, 0.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::MOMENTUM, 1 },      "Momentum",
                                       Range { 0.0f, 100.0f, 0.01f }, 50.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::RADIAL_GAIN, 1 },   "Blast",
                                       Range { 0.0f, 100.0f, 0.01f }, 50.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::JITTER_GAIN, 1 },   "Shimmer",
                                       Range { 0.0f, 100.0f, 0.01f }, 50.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::BREATHE_GAIN, 1 },  "Breathe",
                                       Range { 0.0f, 100.0f, 0.01f }, 50.0f, pctAttr()));

    // CUTOUT gradual (control del USUARIO, automatizable): 0 = imagen completa, 100 = fondo borrado (queda
    // la escultura del sujeto). Sin máscara (imagen sin sujeto / fábrica) no hace nada — robusto.
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::CUTOUT, 1 },        "Cutout",
                                       Range { 0.0f, 100.0f, 0.01f }, 0.0f, pctAttr()));

    // VOCABULARIO VISUAL (capas 1-3 + paleta): la geometría y el color del mundo, automatizables.
    // MOTION en orden de curaduría (Contornos = el default elegido por Joaquín); el mapeo a motionMode del
    // shader vive en ParamMapping (kMotionModes). SHAPE = glifo del vocabulario (matriz de mockups).
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::MOTION, 1 }, "Motion",
        juce::StringArray { "Contours", "Matter", "Wave", "Vortices", "Radial" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::SHAPE, 1 },  "Shape",
        juce::StringArray { "Dot", "Disc", "Ring", "Dash", "Tri", "Quad", "Spark" }, 0));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::TRAILS, 1 },        "Trails",
                                       Range { 0.0f, 100.0f, 0.01f }, 0.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::LINKS, 1 },         "Links",
                                       Range { 0.0f, 100.0f, 0.01f }, 0.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::SAT, 1 },           "Saturation",
                                       Range { 0.0f, 100.0f, 0.01f }, 50.0f, pctAttr()));
    static const auto deg = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " \xc2\xb0"; };
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::HUE, 1 },           "Hue",
                                       Range { -180.0f, 180.0f, 0.1f }, 0.0f,
                                       juce::AudioParameterFloatAttributes()
                                           .withStringFromValueFunction (deg).withLabel ("\xc2\xb0")));
    // VARIATION: randomización CURADA dentro del preset activo (0 = preset puro). Determinista: el mismo
    // (preset, variation) siempre da el mismo mundo → automatizable y recuperable. El botón MUTATE tira el dado.
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::VARIATION, 1 },     "Variation",
                                       Range { 0.0f, 100.0f, 0.01f }, 0.0f, pctAttr()));
    // HOTKNOBS por dominio: tomas deterministas SOLO de su fila; VARIATION es el master (se suman).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::VAR_MOVEMENT, 1 },  "Movement Var",
                                       Range { 0.0f, 100.0f, 0.01f }, 0.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::VAR_MATTER, 1 },    "Matter Var",
                                       Range { 0.0f, 100.0f, 0.01f }, 0.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::VAR_CAMERA, 1 },    "Camera Var",
                                       Range { 0.0f, 100.0f, 0.01f }, 0.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::VAR_COLOR, 1 },     "Color Var",
                                       Range { 0.0f, 100.0f, 0.01f }, 0.0f, pctAttr()));

    // TIER 1 PRO (fila 3): estructura (DENSITY/SCATTER), tiempo (SPEED/PUMP), cámara y color en movimiento
    // (ROTATE/HUE CYC/KALEIDO). Defaults = comportamiento histórico EXACTO (goldens byte-idénticos).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::DENSITY, 1 },       "Density",
                                       Range { 1.0f, 100.0f, 0.01f }, 100.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::SCATTER, 1 },       "Scatter",
                                       Range { 0.0f, 100.0f, 0.01f }, 0.0f, pctAttr()));
    static const auto spd = [] (float v, int) { return "x" + juce::String (0.25f * std::pow (16.0f, v / 100.0f), 2); };
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::SPEED, 1 },         "Speed",
                                       Range { 0.0f, 100.0f, 0.01f }, 50.0f,
                                       juce::AudioParameterFloatAttributes().withStringFromValueFunction (spd)));
    static const auto dps = [] (float v, int, float scale) { return juce::String (v / 100.0f * scale, 1) + " \xc2\xb0/s"; };
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::ROTATE, 1 },        "Rotate",
                                       Range { -100.0f, 100.0f, 0.1f }, 0.0f,
                                       juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                           [] (float v, int) { return dps (v, 0, 30.0f); })));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::PUMP, 1 },          "Pump",
                                       Range { 0.0f, 100.0f, 0.01f }, 30.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::HUE_CYCLE, 1 },     "Hue Cycle",
                                       Range { -100.0f, 100.0f, 0.1f }, 0.0f,
                                       juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                           [] (float v, int) { return dps (v, 0, 60.0f); })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::KALEIDO, 1 }, "Kaleido",
        juce::StringArray { "Off", "2", "4", "6", "8" }, 0));

    // 3D + MOTOR GEOMÉTRICO (fila 4): la profundidad y la cámara orbital ("darlo vuelta") + FIGURA (los
    // hogares abandonan la imagen y se re-arman en una figura). Defaults = identidad byte-exacta.
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::DEPTH, 1 },         "Depth",
                                       Range { 0.0f, 100.0f, 0.01f }, 0.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::ROT_X, 1 },         "Rot X",
                                       Range { -180.0f, 180.0f, 0.1f }, 0.0f,
                                       juce::AudioParameterFloatAttributes()
                                           .withStringFromValueFunction (deg).withLabel ("\xc2\xb0")));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::ROT_Y, 1 },         "Rot Y",
                                       Range { -180.0f, 180.0f, 0.1f }, 0.0f,
                                       juce::AudioParameterFloatAttributes()
                                           .withStringFromValueFunction (deg).withLabel ("\xc2\xb0")));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::ORBIT, 1 },         "Orbit",
                                       Range { -100.0f, 100.0f, 0.1f }, 0.0f,
                                       juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                           [] (float v, int) { return dps (v, 0, 45.0f); })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::FIGURE, 1 }, "Figure",
        juce::StringArray { "Image", "Sphere", "Spiral", "Rings", "Grid", "Helix" }, 0));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::FORM, 1 },          "Form",
                                       Range { 0.0f, 100.0f, 0.01f }, 100.0f, pctAttr()));

    // COLOR LAB: la paleta como instrumento — banco curado (LookRamps, fuente única de los nombres).
    juce::StringArray lookNames;
    for (int i = 0; i < supernova::color::kNumLooks; ++i)
        lookNames.add (juce::String::fromUTF8 (supernova::color::kLooks[i].name));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::PALETTE, 1 },
                                                              "Palette", lookNames, 0));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::COLOR_AMOUNT, 1 },  "Color Amount",
                                       Range { 0.0f, 100.0f, 0.01f }, 100.0f, pctAttr()));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::BG, 1 },            "Background",
                                       Range { 0.0f, 100.0f, 0.01f }, 0.0f, pctAttr()));

    layout.add (std::make_unique<APB> (juce::ParameterID { pid::EXPLODE, 1 },       "Explode", false));

    // Selección de preset (RF8) — opciones desde la tabla de fábrica.
    juce::StringArray presetNames;
    for (const auto& p : ovni::presets::factoryPresets())
        presetNames.add (p.name);
    if (presetNames.isEmpty()) presetNames.add ("Default");
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::PRESET, 1 },
                                                              "Preset", presetNames, 0));

    layout.add (std::make_unique<APB> (juce::ParameterID { pid::BYPASS, 1 }, "Bypass", false));

    // SENSIBILIDAD VISUAL (fader IN de la TopBar): escala SOLO el análisis (processAudio → FIFO), jamás el
    // audio del insert (RNF1 bit-exacto se verifica en VisGainTest). El inGain del chasis queda en 0 (drive
    // real): SUPERNOVA no lo expone.
    static const auto db = [] (float v, int) { return juce::String (v, 1) + " dB"; };
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::VIS_GAIN, 1 }, "Visual Sensitivity",
                                       Range { -24.0f, 24.0f, 0.1f }, 0.0f,
                                       juce::AudioParameterFloatAttributes()
                                           .withStringFromValueFunction (db).withLabel ("dB")));
    return layout;
}

SupernovaProcessor::SupernovaProcessor()
    : ovni::PluginProcessorBase ("SUPERNOVA", createParameterLayout())
{
    // El MidiMapper clampa el índice de preset al tamaño REAL de la tabla de fábrica (nota/PC fuera de rango).
    MidiMapper::Config cfg;
    cfg.presetCount = juce::jmax (1, (int) ovni::presets::factoryPresets().size());
    midiMapper = MidiMapper (cfg);

    // Aplica el param 'preset' (choice) a los continuos del APVTS (RF8) — a-prueba-de-feedback (§PresetApplier).
    presetApplier = std::make_unique<PresetApplier> (apvts);

    pVisGain = apvts.getRawParameterValue (pid::VIS_GAIN);   // sensibilidad visual (fader IN de la barra)

    analysisThread = std::make_unique<AnalysisThread> (audioFifo, analysisFrames);
}

SupernovaProcessor::~SupernovaProcessor()
{
    if (analysisThread) analysisThread->stopThread (1000);
}

void SupernovaProcessor::prepareEngine (const juce::dsp::ProcessSpec& spec)
{
    monoScratch.assign ((size_t) juce::jmax (1u, spec.maximumBlockSize), 0.0f);
    audioRingSr     = spec.sampleRate > 0 ? spec.sampleRate : 48000.0;
    audioRingFrames = (int) (audioRingSr * kAudioRingSeconds);
    audioRing.assign ((size_t) audioRingFrames * 2, 0.0f);
    audioRingWriteFrame.store (0);
    audioFifo.reset();
    midiTriggerQueue.reset();
    ccQueue.reset();
    beatClock.reset();
    audioTimeSec = 0.0;
    if (analysisThread) analysisThread->prepare (spec.sampleRate, 11);
}

void SupernovaProcessor::releaseResources()
{
    if (analysisThread) analysisThread->stopThread (1000);
}

// Bracket alrededor del restore del chasis: evita que el PresetApplier re-aplique el preset sobre los continuos
// recién restaurados (que pueden traer tweaks del usuario post-preset) → round-trip exacto.
void SupernovaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (presetApplier) presetApplier->beginStateRestore();
    ovni::PluginProcessorBase::setStateInformation (data, sizeInBytes);
    if (presetApplier) presetApplier->endStateRestore();
    // Phase B: los mapeos MIDI/LFOs/escenas NO se deserializan acá — setStateInformation puede correr fuera del
    // message thread (el host no lo garantiza) y esos objetos (std::vector/string) los lee/escribe SOLO el editor
    // (message thread). El editor los re-hidrata desde apvts.state en su hydrate (ctor + al ver cambiar el
    // stateStamp) → sin data race. Los datos ya están en apvts.state (el chasis restauró el ValueTree).
    ++stateStampCount;   // el editor (si está abierto) re-hidrata secuencia + performance-state en su próximo tick
}

// RNF1: SOLO lee. Mezcla mono de la entrada → FIFO. NUNCA escribe `buffer` → audio bit-exacto.
void SupernovaProcessor::processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // MIDI → triggers (RF5). NoteOn: explosión/rayo → cola visual; nota→preset → chasis (RT-safe). El
    // Program-Change lo aplica el chasis en processBlock ANTES de esto (no lo re-procesamos acá). Todo RT-safe:
    // mapNoteOn es puro, push es lock-free drop-on-full, requestFactoryPreset difiere al message thread.
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isController())   // MIDI-learn (Phase B): CC crudo → cola → editor lo rutea a un param
        {
            ccQueue.push ({ m.getControllerNumber(), m.getControllerValue() });
            continue;
        }
        if (! m.isNoteOn()) continue;
        const auto ev = midiMapper.mapNoteOn (m.getNoteNumber(), m.getVelocity());
        if (ev.type == MidiTriggerType::PresetChange) requestFactoryPreset (ev.preset);
        else if (ev.isValid())                        midiTriggerQueue.push (ev);
    }

    // TEMPO SYNC (Phase B · BeatClock): avanza el reloj con el playhead del host (dentro del DAW la fase se
    // engancha al transport); en standalone corre libre + tap. Publica snapshots atómicos para el editor.
    {
        const double sr = getSampleRate();
        const int    ns = buffer.getNumSamples();
        const double blockDt = (sr > 0.0 && ns > 0) ? (double) ns / sr : 0.0;
        audioTimeSec += blockDt;
        if (tapPending.exchange (false)) beatClock.tap (audioTimeSec);

        BeatClock::HostInfo host;
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                if (auto bpm = pos->getBpm())       { host.valid = true; host.bpm = *bpm; }
                if (auto ppq = pos->getPpqPosition()) host.ppqPosition = *ppq;
                host.isPlaying = pos->getIsPlaying();
            }
        }
        beatClock.advance (blockDt > 0.0 ? blockDt : 0.001, host);
        pubBpm.store (beatClock.bpm());
        pubBeatPhase.store (beatClock.beatPhase());
        pubPhaseBeats.store (beatClock.phaseInBeats());
        pubPlaying.store (host.isPlaying);
    }

    const int n   = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n <= 0 || (int) monoScratch.size() < n) return;

    if (nch >= 2)
    {
        const float* L = buffer.getReadPointer (0);
        const float* R = buffer.getReadPointer (1);
        for (int i = 0; i < n; ++i) monoScratch[(size_t) i] = 0.5f * (L[i] + R[i]);
    }
    else if (nch == 1)
    {
        const float* M = buffer.getReadPointer (0);
        for (int i = 0; i < n; ++i) monoScratch[(size_t) i] = M[i];
    }
    else return;

    // SENSIBILIDAD VISUAL (visGain): escala la MEZCLA MONO que va al análisis — el buffer del insert no se
    // toca (RNF1). Ganancia por-bloque (el análisis promedia ventanas: el zipper es inaudible e invisible).
    if (pVisGain != nullptr)
    {
        const float vg = juce::Decibels::decibelsToGain (pVisGain->load());
        if (! juce::exactlyEqual (vg, 1.0f))
            juce::FloatVectorOperations::multiply (monoScratch.data(), vg, n);
    }

    audioFifo.push (monoScratch.data(), n);   // lock-free, drop-on-full

    // EXPORT CON SONIDO: anillo RT (~12s) del MISMO audio que alimenta el análisis. Escritura a buffer
    // pre-alocado + índice atómico (release) — sin locks ni allocs; el buffer NO se modifica (RNF1 intacto).
    if (audioRingFrames > 0 && ! audioRing.empty())
    {
        const float* Lr = buffer.getReadPointer (0);
        const float* Rr = nch >= 2 ? buffer.getReadPointer (1) : Lr;
        int w = audioRingWriteFrame.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            audioRing[(size_t) w * 2 + 0] = Lr[i];
            audioRing[(size_t) w * 2 + 1] = Rr[i];
            if (++w >= audioRingFrames) w = 0;
        }
        audioRingWriteFrame.store (w, std::memory_order_release);
    }
}

std::vector<float> SupernovaProcessor::audioRingSnapshot (double& srOut) const
{
    // Desenrolla el anillo (viejo→nuevo). Corre en el message thread mientras el audio thread sigue
    // escribiendo: el tear posible queda en la COSTURA (≤1 bloque) — que coincide con el punto de loop.
    srOut = audioRingSr;
    if (audioRingFrames <= 0 || audioRing.empty()) return {};
    const int w = audioRingWriteFrame.load (std::memory_order_acquire);
    std::vector<float> out ((size_t) audioRingFrames * 2, 0.0f);
    const size_t tailFloats = (size_t) (audioRingFrames - w) * 2;
    std::memcpy (out.data(), audioRing.data() + (size_t) w * 2, tailFloats * sizeof (float));
    if (w > 0) std::memcpy (out.data() + tailFloats, audioRing.data(), (size_t) w * 2 * sizeof (float));
    return out;
}

juce::AudioProcessorEditor* SupernovaProcessor::createEditor() { return new SupernovaEditor (*this); }

} // namespace supernova

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new supernova::SupernovaProcessor(); }
