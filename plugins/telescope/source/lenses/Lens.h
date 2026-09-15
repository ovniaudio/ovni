#pragma once
#include "analysis/AnalysisFrame.h"
#include "lenses/LensIds.h"
#include "lenses/Strings.h"
#include "ui-kit/VisualizerBase.h"

// ========================================================================================================
// Lens — la interfaz que cumplen las 13 lentes de TELESCOPE.
//
// Una lente ES un ovni::ui::VisualizerBase: de ahí saca gratis la capa estática horneada a resolución
// física (nítida en Retina), la animación a fps fijos, la pausa de repaint en reposo y el respeto por
// reduced-motion. Lo único propio de cada lente es CÓMO dibuja el mismo análisis.
//
// `requiredModules()` es lo que hace posible la "lente a demanda": el editor pone esa máscara en el
// AnalysisThread, así lo que no se ve no se calcula.
// ========================================================================================================
namespace telescope
{
class Lens : public ovni::ui::VisualizerBase
{
public:
    explicit Lens (int fps = 30) : ovni::ui::VisualizerBase (fps) {}
    ~Lens() override = default;

    // El flag de reduced-motion del sello vive protected en VisualizerBase. TELESCOPE lo re-expone acá:
    // alguien tiene que poder fijarlo desde afuera (el host/app al leer la preferencia del SO, y los
    // tests de accesibilidad al verificar que la lente efectivamente deja de animar).
    static void setReducedMotion (bool on) noexcept { ovni::ui::VisualizerBase::setGlobalReducedMotion (on); }
    static bool reducedMotion() noexcept            { return ovni::ui::VisualizerBase::globalReducedMotionFlag(); }

    virtual juce::String  name() const = 0;
    virtual LensId        id() const = 0;
    virtual juce::uint32  requiredModules() const = 0;

    // ================== EL IDIOMA, PARA LAS TRECE (D-50, prompt 56b) ==================
    // Hasta el 56 sólo SCOPE, FIELD y la tira leían de Strings.h y las otras diez dibujaban literales en
    // castellano, así que el plugin no cumplía D-50 ("inglés por defecto y posibilidad en todos los
    // idiomas"). El helper vive acá y no copiado trece veces: cada lente sólo dice DE DÓNDE sale su
    // estado (una línea), y `tr()` es el mismo para todas.
    //
    // El default devuelve un árbol vacío a propósito: `languageOf` de un árbol sin la propiedad da `en`,
    // que es el default de D-50. Así una lente sin procesador (PlaceholderLens) compila y rotula en
    // inglés en vez de obligar a inventarle un estado.
    virtual const juce::ValueTree& stateTree() const
    {
        static const juce::ValueTree none;
        return none;
    }

    juce::String tr (strings::Key k) const { return strings::get (k, strings::languageOf (stateTree())); }

    // La misma clave en minúsculas, para las lecturas bajo el cursor y las leyendas chicas, que en las
    // trece lentes van en caja baja. (En alemán deja el sustantivo en minúscula — está en la lista de
    // "revisión de nativo pendiente" junto con el resto de las tablas de/fr/it/pt.)
    juce::String trLower (strings::Key k) const { return tr (k).toLowerCase(); }
};
}
