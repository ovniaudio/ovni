// [telescope][presets] — los nombres de los presets de fábrica llegan al host (parámetro "preset") y al gestor
// como UTF-8, no como Latin-1.
//
// Hallazgo de la obrera del prompt 67, verificado por la auditora (15-sep): `FactoryPreset::name` es un
// `const char*` con UTF-8 crudo ("Loudness · libre", el "·" son los bytes C2 B7) y llegaba a juce::String por el
// constructor `String (const char*)`, que decodifica byte a byte (CharPointer_ASCII): el host mostraba
// "Loudness Â· libre" en el parámetro y el menú de presets lo mismo. El camino correcto es
// juce::String::fromUTF8 en cada conversión (PluginProcessor.cpp, PluginEditorBase.cpp, PresetManager.cpp).
// Rojo antes del fix, verde después; y si algún día la tabla queda toda en ASCII, el REQUIRE final lo dice.
#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include "PluginProcessor.h"
#include "presets/PresetTypes.h"

TEST_CASE ("telescope: los nombres de los presets de fábrica son UTF-8 en el parámetro y en el gestor",
           "[telescope][presets]")
{
    telescope::TelescopeProcessor proc;
    const auto& all = ovni::presets::factoryPresets();
    REQUIRE (! all.empty());

    juce::AudioParameterChoice* presetParam = nullptr;
    for (auto* p : proc.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p); r != nullptr && r->getParameterID() == "preset")
            presetParam = dynamic_cast<juce::AudioParameterChoice*> (r);
    REQUIRE (presetParam != nullptr);
    REQUIRE (presetParam->choices.size() == (int) all.size());

    bool sawNonAscii = false;
    for (size_t i = 0; i < all.size(); ++i)
    {
        const auto expected = juce::String::fromUTF8 (all[i].name);
        sawNonAscii = sawNonAscii || ! juce::CharPointer_ASCII::isValidString (all[i].name, 1024);
        INFO ("preset " << i << ": " << all[i].name);

        // lo que ve el host en el parámetro "preset"
        CHECK (presetParam->choices[(int) i] == expected);
        CHECK (! presetParam->choices[(int) i].containsChar ((juce::juce_wchar) 0x00C2));   // "Â": un byte de UTF-8 leído como Latin-1

        // lo que ve la UI (‹ nombre ›) después de aplicar el preset
        proc.presets().applyFactory ((int) i);
        CHECK (proc.presets().current().name == expected);
        CHECK (! proc.presets().current().name.containsChar ((juce::juce_wchar) 0x00C2));
    }

    // La tabla de 0.1.0 tiene "·" en los cuatro nombres: si esto deja de ser cierto, el test ya no prueba la
    // decodificación y hay que saberlo (no es un fallo del producto: es un aviso al que edite la tabla).
    REQUIRE (sawNonAscii);
}
