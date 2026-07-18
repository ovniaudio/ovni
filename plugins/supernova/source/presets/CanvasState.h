#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "params/ParameterIDs.h"

// CANVAS / CLEAR (spec 2026-07-12 §A.3) — el "estado cero" del diseñador: física quieta (INTENSITY 0
// activa el camino de quietud del kernel), FX apagados, color neutro, la imagen NÍTIDA. Desde acá se
// construye knob a knob. NO toca 'preset' (el mundo elegido queda como punto de partida) ni 'bypass'
// ni la imagen/secuencia cargadas.
namespace supernova::canvas
{
struct ParamValue { const char* id; float value; };

inline constexpr ParamValue kClearState[] =
{
    // MOVEMENT — quieto de verdad (INTENSITY 0 = gate alive del kernel)
    { params::id::MOTION, 0.0f },        { params::id::INTENSITY, 0.0f },
    { params::id::CHAOS, 0.0f },         { params::id::SPEED, 50.0f },
    { params::id::PUMP, 0.0f },          { params::id::RADIAL_GAIN, 0.0f },
    { params::id::GRAVITY, 0.0f },       { params::id::BREATHE_GAIN, 0.0f },
    // física sin knob — neutra
    { params::id::CURL_SCALE, 50.0f },   { params::id::HOME_STRENGTH, 50.0f },
    { params::id::MOMENTUM, 50.0f },     { params::id::JITTER_GAIN, 0.0f },
    // MATTER — la imagen entera, sin FX
    { params::id::SHAPE, 0.0f },         { params::id::PARTICLE_SIZE, 40.0f },
    { params::id::DENSITY, 100.0f },     { params::id::SCATTER, 0.0f },
    { params::id::TRAILS, 0.0f },        { params::id::LINKS, 0.0f },
    { params::id::CUTOUT, 0.0f },
    // CAMERA — plano, de frente
    { params::id::FIGURE, 0.0f },        { params::id::FORM, 100.0f },
    { params::id::DEPTH, 0.0f },         { params::id::ROT_X, 0.0f },
    { params::id::ROT_Y, 0.0f },         { params::id::ORBIT, 0.0f },
    { params::id::ROTATE, 0.0f },        { params::id::KALEIDO, 0.0f },
    // COLOR — los colores de TU imagen (GLOW 50 = default del param: renders 40/45/50 midieron
    // idéntico con el lienzo quieto (0% quemado), así que gana la coherencia con double-click=default)
    { params::id::PALETTE, 0.0f },       { params::id::COLOR_AMOUNT, 100.0f },
    { params::id::SAT, 50.0f },          { params::id::HUE, 0.0f },
    { params::id::HUE_CYCLE, 0.0f },     { params::id::GLOW, 50.0f },
    { params::id::BG, 0.0f },
    // dado + hotknobs (los VAR_* van DESPUÉS de los params de fila: su guard de lastWritten ve los valores
    // del lienzo ya escritos → no "restaura" tomas viejas encima de CLEAR)
    { params::id::VARIATION, 0.0f },     { params::id::EXPLODE, 0.0f },
    { params::id::VAR_MOVEMENT, 0.0f },  { params::id::VAR_MATTER, 0.0f },
    { params::id::VAR_CAMERA, 0.0f },    { params::id::VAR_COLOR, 0.0f },
};

// Escritura host-visible (gesture): el DAW la registra y el undo funciona. Compartida por CLEAR y
// por el RANDOM del ControlStrip (fuente única del patrón de escritura).
inline void setParamAsGesture (juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (apvts.getParameterRange (id).convertTo0to1 (value));
        p->endChangeGesture();
    }
}

inline void applyClearState (juce::AudioProcessorValueTreeState& apvts)
{
    for (const auto& pv : kClearState)
        setParamAsGesture (apvts, pv.id, pv.value);
}
}
