#pragma once
#include <memory>
#include <vector>
#include "analysis/modules/Verdict.h"
#include "lenses/Lens.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// VERDICT — la lente 13. La única que no muestra: DICE.
//
// Las otras doce ponen un número en pantalla y el que mira concluye. Ésta concluye, y la regla de hierro
// (D-47) es que **cada frase lleva el número que la sostiene y el id de la regla que la produjo**, los dos
// visibles al mismo tiempo. Sin IA, sin red, determinista.
//
//   cabecera    el resumen en tabular: I · LRA · PLR · correlación · tonalidad, más cuántos segundos se
//               analizaron. Es el contexto sin el cual las frases de abajo no significan nada.
//   titular     (57d) la CUENTA de lo medido, fija bajo la cabecera: "n chequeos dentro de rango · m para
//               revisar, el primero en t0". Sin adjetivos.
//   dentro      (57d) DENTRO DE RANGO — una fila por regla que se evaluó y no se disparó, con su número y su
//               límite. Primero, compacta, ✓ en gris; si no entra, una sola fila.
//   sección 1   CÓMO SE VA A SENTIR — vocabulario de mezcla mapeado desde números.
//   sección 2   DÓNDE TRADUCE — las seis cajas, con ✓ / ⚠ / ✗ y su número. Pronóstico con curvas
//               genéricas, y lo dice.
//   sección 3   QUÉ REVISAR Y DÓNDE — hallazgos con mm:ss–mm:ss y banda, y al final dónde mirar.
//   pie         "Medición, no gusto. Curvas de dispositivos genéricas. Rehacé el análisis tras cada
//               cambio." Fijo, en el idioma elegido, siempre.
//
// DOS MODOS. EN VIVO acumula desde el último RESET; ARCHIVO analiza un tema entero offline y da los
// tiempos EXACTOS. Los dos usan el mismo motor y las mismas filas por segundo, así que producen el mismo
// informe sobre el mismo audio (verificado frase por frase en VERDICT[archivo]).
//
// LO QUE NO HACE, y está escrito en pantalla: "sin hallazgos" NO es "está terminado". Es "nada fuera de rango
// en estas reglas; miden, y lo que no escuchan es tuyo". La diferencia es todo el producto.
//
// REDUCED MOTION: acá no hay nada que animar. El informe se rehace cuando cambian los datos, no todos los
// frames — la lente pide repintado sólo cuando el informe cambió de verdad (o mientras un archivo se
// analiza y la barra de progreso se mueve).
// ========================================================================================================
class VerdictLens : public Lens,
                    public juce::FileDragAndDropTarget
{
public:
    explicit VerdictLens (TelescopeProcessor& p);
    ~VerdictLens() override;

    juce::String name() const override { return kLensNames[(int) LensId::verdict]; }
    LensId       id() const override   { return LensId::verdict; }
    // Las CUATRO banderas: el espectro y el estéreo por banda para las lentes que comparten el motor,
    // kReference para la FFT fija que alimenta la historia por segundo, y kCqt para la tonalidad.
    juce::uint32 requiredModules() const override { return kSpectrum | kStereoBands | kReference | kCqt; }
    const juce::ValueTree& stateTree() const override;

    // Los rótulos de VERDICT salen de rules:: (la MISMA tabla que las frases del informe, y la misma
    // propiedad `language`): si el informe habla en italiano, los botones también. Las trece lentes
    // comparten el setting; VERDICT es la única que además comparte el vocabulario con el motor.
    juce::String ph (const char* key) const;

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;

    // Los botones, en el orden en que se dibujan. Públicos para que el test los apriete sin clicks.
    // 56b: el botón LANGUAGE se fue a la TIRA (una propiedad, un control). Ver ui/LensStrip.h: el
    // idioma es del plugin, no de esta lente, y tenerlo acá adentro obligaba a entrar a VERDICT para
    // poder leer SPECTRUM en castellano.
    enum Control { ctrlReset = 0, ctrlMode, ctrlLoad, kNumControls };
    void pressControl (int control);

    // El estado como DATO (la lente lo dibuja; el test lo lee).
    juce::String stateText() const;
    // La TONALIDAD tal como sale en la cabecera. Pública para que el test la compare, para las seis
    // lenguas y las veinticuatro tonalidades, contra la que dibuja CQT en su pie.
    static juce::String keyText (int tonic, int mode, const juce::String& language);
    // El informe vigente, para que el test lea las frases sin mirar píxeles.
    const VerdictReport& report() const noexcept { return rep; }
    int  scrollOffset() const noexcept { return scroll; }

    // La geometría del texto del panel, pública porque el TEST del wrap tiene que usar exactamente la
    // misma cuenta que el dibujo: si el test copiara los números, mediría su propia copia.
    static constexpr int kTextIndent = 22;   // sangría del texto (deja lugar a la insignia)
    static constexpr int kTextRight  = 12;   // margen derecho: la barra de scroll vive ahí
    // 57d — cuánto del alto de la lista puede ocupar "Dentro de rango" desplegada, en M y en L, antes de
    // colapsarse a una fila (en S se colapsa siempre, ver buildLines). Por encima de esto, lo que hay que
    // revisar queda debajo del pliegue sin que nadie lo vea. Público para que el test imprima el aire.
    static constexpr float kWithinMaxShare = 0.6f;
    static int textWidthFor (int listWidth) noexcept
        { return juce::jmax (60, listWidth - kTextIndent - kTextRight); }
    // El texto envuelto al ancho dado, con la tipografía del panel. Lo comparten medir, dibujar y el test.
    static juce::TextLayout layoutFor (const juce::String& text, float fontHeight, int width,
                                       juce::Colour colour);
    // Lo que el panel MIDE de cada fila, para el test del wrap: cuánto alto reservó y en cuántas
    // líneas quedó el texto. Son los dos números que tienen que coincidir con lo que se pinta.
    int  numLines() const noexcept          { return (int) lines.size(); }
    int  textHeightOf (int i) const noexcept;
    int  wrappedLinesOf (int i) const noexcept;
    float layoutHeightOf (int i) const noexcept;   // lo que MIDE el texto envuelto, antes de redondear
    // El ancho con el que se envuelve el texto y el borde derecho del panel: el test los necesita para
    // saber DÓNDE mirar, y tienen que ser los mismos que usa el dibujo (no una copia).
    int  textWidth() const noexcept         { return textWidthFor (listArea().getWidth()); }
    juce::Rectangle<int> listArea() const noexcept { return zones.list; }
    int  contentHeight() const noexcept { return contentH; }

    // ===== 57d · para el test del orden y del colapso =====
    // El texto de la fila i tal como se dibuja (el título de sección sin pasar a mayúsculas), si la sección
    // "Dentro de rango" quedó colapsada a una línea, y el titular que se dibuja entre la cabecera y la lista.
    juce::String lineTextOf (int i) const;
    bool         withinCollapsed() const noexcept { return collapsedWithin; }
    juce::String headlineText() const;

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    static constexpr int kHeadH   = 46;
    static constexpr int kRowGap  = 4;
    static constexpr int kSecGap  = 10;

    struct Zones
    {
        juce::Rectangle<int> head, headline, list, footer;
        juce::Rectangle<int> button[kNumControls];
    };
    Zones zonesFor (int w, int h) const;

    void rebuildReport();
    void paintHead (juce::Graphics&) const;
    void paintHeadline (juce::Graphics&) const;   // 57d
    void paintList (juce::Graphics&) const;
    void paintButton (juce::Graphics&, juce::Rectangle<int>, const juce::String& label,
                      const juce::String& value, bool hovered) const;

    // Una línea del panel: o un título de sección, o un hallazgo, o una fila de dispositivo.
    //
    // 56b — EL WRAP ES REAL Y SE CALCULA UNA SOLA VEZ. Hasta el 56 el alto se ESTIMABA por ancho de
    // glifo y el dibujo era `drawFittedText (…, 3)`, que no envuelve: APRIETA horizontalmente hasta
    // meter todo en una línea. Se veía en "Phone: …" del verdict_M — el texto se comía el margen — y en
    // alemán o francés, más largos, iba a cortar. Ahora cada línea guarda su `juce::TextLayout` armado
    // con el MISMO ancho con el que se dibuja, así que el alto reservado y el alto pintado son el mismo
    // número por construcción, no por estimación. Y se arma en buildLines() —que corre cuando cambia el
    // informe— en vez de rearmar un GlyphArrangement por hallazgo en cada frame: es más barato que lo
    // que había.
    struct Line
    {
        enum Kind { section = 0, finding, deviceRow, note, strength } kind = finding;   // 57d: strength
        juce::String text, evidence, badge;
        juce::Colour colour;
        int height = 18;
        int textHeight = 14;          // lo que ocupa `layout` (sin la línea de evidencia)
        juce::TextLayout layout;      // el texto ya envuelto al ancho del panel
    };
    // El ancho útil del texto de una fila: el panel menos la sangría de la insignia y el margen
    // derecho. UNA sola cuenta, usada por el que mide y por el que dibuja (era el origen del desborde).
    void buildLines();
    // El texto envuelto al ancho dado, con la tipografía del panel. Lo comparten medir y dibujar.


    TelescopeProcessor& processor;

    VerdictReport rep;
    std::vector<Line> lines;
    std::vector<SecondRow> rows;
    int  contentH = 0;
    int  scroll   = 0;
    bool collapsedWithin = false;   // 57d — ver buildLines()

    juce::uint32 lastSeconds = 0xffffffffu;
    juce::uint32 lastFileRev = 0xffffffffu;
    int          lastMode = -1;
    juce::String lastLanguage;
    bool         lastRefValid = false;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String dropMessage;   // "eso no es un archivo de audio", y por qué

    Zones zones {};
    int   hovered = -1;
    bool  dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VerdictLens)
};
}
