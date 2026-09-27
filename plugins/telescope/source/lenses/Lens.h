#pragma once
#include "analysis/AnalysisFrame.h"
#include "lenses/LensIds.h"
#include "lenses/Look.h"
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

    // ================== T2 (F2 de la 0.2): LA LENTE, ALINEADA AL PÍXEL FÍSICO ==================
    // La base hornea la capa estática a escala física y la devuelve al plano lógico con `scale (1/s)`; las
    // cachés raster (Raster.h) hacen lo mismo. Lo que llega al dispositivo es una TRASLACIÓN pura, y el
    // renderer la copia sólo si cae en un píxel ENTERO. A escala no entera el origen de la lente no cae
    // ahí (M a 1.25: x = 182 × 1.25 = 227.5 px) y las imágenes se re-muestreaban en cada cuadro: 3 a 6 ms
    // en vez de 0.3 a 1 en esta Mac (19.6 en Windows), y todo medio píxel borroso — rejilla y rótulos de
    // los ejes incluidos, que son justo lo que un usuario no llegaba a leer.
    //
    // El arreglo: antes de dibujar, la lente se corre el RESTO fraccionario de su origen físico (menos de
    // medio píxel físico, capa estática y viva juntas) y cae entera. Para saber dónde cae hace falta un
    // ANCLA: un componente cuyo origen está en un píxel entero del dispositivo. En el plugin es el editor
    // (TelescopeEditor::showLens), que el host pone en el origen de la ventana. Sin ancla —una lente
    // suelta, como la de [budget], pintada en el origen de su propia imagen— no se corre nada.
    //
    // No alcanza con pedir el filtro bajo (se probó primero): en CoreGraphics, sin interpolación y con una
    // traslación fraccionaria, igual va por el camino general, y cuesta el doble que la copia.
    void setPixelAnchor (juce::Component* anchor) noexcept { pixelAnchor = anchor; }

    // El corrimiento, en coordenadas LÓGICAS de la lente, que lleva su origen al píxel físico entero más
    // cercano dada la escala física `physScale` del Graphics con que se la pinta. Público para los tests.
    juce::Point<float> pixelAlignment (float physScale) const
    {
        auto* anchor = pixelAnchor.getComponent();
        if (anchor == nullptr || physScale <= 0.0f || ! anchor->isParentOf (this)) return {};

        // El origen de la lente en coordenadas del ancla (atraviesa el zoom S/M/L del Canvas), y la escala
        // acumulada del ancla a la lente: `physScale` sobre esa escala es lo que el host pone debajo del
        // ancla (el DPI de la pantalla).
        const auto o = anchor->getLocalPoint (this, juce::Point<float>());
        const auto u = anchor->getLocalPoint (this, juce::Point<float> (1.0f, 0.0f)) - o;
        const float chain = u.getDistanceFromOrigin();
        if (chain <= 0.0f) return {};
        const float host = physScale / chain;

        // El origen, en píxeles físicos. Un resto de menos de 1/64 de píxel es error de float y no una
        // posición (S a 1.25: 145.6f × 1.25 = 181.99999): corregirlo corría la lente 1e-5 y CoreGraphics
        // rasterizaba el texto distinto — cambiaban píxeles de una lente que ya caía entera.
        const auto rest = [] (float v)
        {
            const float r = std::round (v) - v;
            return std::abs (r) < 1.0f / 64.0f ? 0.0f : r;
        };
        return { rest (o.x * host) / physScale, rest (o.y * host) / physScale };
    }

    // Test-only (F2 de la 0.2, [contraste]): la capa estática se dibuja DIRECTO sobre el Graphics que pinta
    // la lente, sin pasar por la imagen horneada. El instrumento de contraste envuelve el contexto de dibujo
    // para saber dónde cae cada texto; lo que se hornea en la caché usa un Graphics propio sobre otra imagen
    // y la sonda no lo vería — y ahí viven los rótulos de los ejes. En el plugin no se usa nunca.
    static void setDirectPaintForTest (bool on) noexcept { directPaintForTest = on; }

    void paint (juce::Graphics& g) override
    {
        if (directPaintForTest)
        {
            renderStatic (g, getWidth(), getHeight());
            paintLive (g);
            return;
        }

        g.saveState();
        const auto d = pixelAlignment (g.getInternalContext().getPhysicalPixelScaleFactor());
        if (! d.isOrigin()) g.addTransform (juce::AffineTransform::translation (d.x, d.y));
        // Con el origen entero, pedir bilineal a una copia 1:1 sólo agrega trabajo: el filtro bajo es la
        // copia. La calidad vive en el estado del Graphics (sin getter): va entre save y restore.
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        ovni::ui::VisualizerBase::paint (g);
        g.restoreState();
    }

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
    // F5b (D-126): con look::lower y no toLowerCase, que depende del locale del host: con el «C», «FENÊTRE» salía
    // «fenÊtre» y «BALANÇO», «balanÇo».
    juce::String trLower (strings::Key k) const { return look::lower (tr (k)); }

private:
    juce::Component::SafePointer<juce::Component> pixelAnchor;
    static inline bool directPaintForTest = false;
};
}
