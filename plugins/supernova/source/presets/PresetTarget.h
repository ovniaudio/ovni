#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "presets/PresetMorph.h"

// Helpers que derivan MorphSnapshots del APVTS. Compartidos por el editor (target del morph) y el PresetApplier
// (escribe el destino) → ambos derivan B idénticamente (DRY).
namespace supernova
{
// Los continuos en su valor ACTUAL del APVTS (unidades de param).
MorphSnapshot snapshotFromApvts (juce::AudioProcessorValueTreeState& apvts);

// Destino "default → overlay preset[index]" para los continuos, en unidades. Mismo semántico que
// applyFactory pero acotado a los continuos (no resetea todo, no toca preset/bypass/explode). Índice fuera de
// rango → devuelve los defaults.
MorphSnapshot presetTargetContinuous (juce::AudioProcessorValueTreeState& apvts, int presetIndex);

// Los CHOICES del mundo (motion/shape/kaleido/figure) del preset[index], como índice de choice (default 0 si
// el preset los omite / índice fuera de rango). Saltan de golpe al cambiar de preset — la identidad no se
// interpola.
struct PresetChoices { int motion = 0; int shape = 0; int kaleido = 0; int figure = 0; int palette = 0; };
PresetChoices presetTargetChoices (int presetIndex);
}
