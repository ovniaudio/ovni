#pragma once
#include <atomic>
#include <memory>
#include "lenses/Lens.h"
#include "template/PluginEditorBase.h"
#include "ui/LensStrip.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// Editor de TELESCOPE: el header del sello (power · SAVE · presets · A/B · S·M·L) que da el chasis, la
// TIRA DE LENTES a la izquierda y la vista de la lente activa ocupando el resto.
//
// El editor es también el que hace posible la "lente a demanda": al cambiar de lente le pasa al processor
// la máscara de módulos que esa lente necesita, así lo que no se ve no se calcula.
// ========================================================================================================
class TelescopeEditor : public ovni::PluginEditorBase,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::ValueTree::Listener
{
public:
    explicit TelescopeEditor (TelescopeProcessor& p);
    ~TelescopeEditor() override;

    // Avanza la animación de la lente a mano (snapshots headless: el timer no corre sin ventana).
    void pumpLensFrames (int n);

    // ================== INSTRUMENTACIÓN PARA LOS TESTS (56c) ==================
    // El idioma puede cambiar desde CUALQUIER hilo (un preset del host, la restauración del estado), y
    // `applyLanguage()` toca componentes: tiene que correr en el message thread SIEMPRE. Eso no se puede
    // comprobar desde afuera mirando píxeles —una pantalla correcta pintada desde el hilo equivocado se
    // ve igual, y falla en el host del usuario, no acá—, así que el editor deja dicho en qué hilo aplicó
    // la última vez y cuántas veces aplicó. Es lo mínimo que hace falsable la regla.
    bool         lastApplyWasOnMessageThread() const noexcept { return appliedOnMessageThread.load(); }
    int          languageApplyCount()          const noexcept { return applyCount.load(); }
    // Lo que la TIRA está mostrando (no lo que dice el árbol): son dos cosas distintas justamente cuando
    // el listener no llega, que es el bug que este flag persigue.
    juce::String stripLanguage() const { return strip.language(); }

protected:
    void layoutBody (juce::Rectangle<int> body) override;

private:
    void parameterChanged (const juce::String& id, float value) override;

    // 56b (D-50): el idioma se escucha en el ÁRBOL y no en el control que lo cambió. Así repintan la
    // tira y la lente visible venga de donde venga el cambio: el chip del pie, un preset, o el estado
    // que el host restaura al abrir la sesión. Un callback en el chip sólo cubriría el primero.
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& prop) override;
    // El árbol entero cambió de lugar: es lo que dispara `apvts.replaceState()`, o sea la restauración
    // del estado por parte del host. Ver la nota del .cpp.
    void valueTreeRedirected (juce::ValueTree& tree) override;
    // Aplica el idioma garantizando el message thread, venga el aviso del hilo que venga.
    void applyLanguageSafely();
    void applyLanguage();
    void showLens (int index);
    std::unique_ptr<Lens> makeLens (LensId id);

    TelescopeProcessor&   proc;
    LensStrip             strip;
    std::unique_ptr<Lens> lens;
    int                   currentLens = -1;
    juce::Rectangle<int>  lensArea;

    std::atomic<bool> appliedOnMessageThread { true };
    std::atomic<int>  applyCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TelescopeEditor)
};
}
