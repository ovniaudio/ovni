// [telescope][verdictlens] — la lente 13 en pantalla: capturas, presupuesto, estado y round-trip.
//
//   VERDICTLENS[uisnap]   /tmp/ovni_telescope_verdict_{S,M,L}.png con la señal defectuosa (que se lean las
//                         TRES secciones con hallazgos) y /tmp/ovni_telescope_verdict_sano_M.png con
//                         material sano — para ver qué dice cuando NO hay nada: "sin hallazgos con estas
//                         reglas", que no es "está listo".
//   (BUDGET_VERDICT vive en LensBudgetTest.cpp, con el resto del harness calibrado.)
//   VERDICTLENS[estado]   idioma y modo hacen round-trip por el estado del plugin.
//   VERDICTLENS[demanda]  con VERDICT visible corren las cuatro banderas; al cerrar la ventana, sólo las
//                         que no se apagan nunca.
//   VERDICTLENS[drop]     soltar un .txt no carga nada y lo dice.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestDefectSignal.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "TestWav.h"
#include "lenses/LensIds.h"
#include "lenses/Look.h"
#include "lenses/NoteName.h"
#include "lenses/Strings.h"
#include "lenses/VerdictLens.h"

using telescope::VerdictLens;
using telescope::rules::Severity;

namespace
{
constexpr double kSr = telescope::test::kDefectSr;
constexpr int kLensW = 1025, kLensH = 702;   // el área de la lente en tamaño L

// Rosa SANO: L = R + un poco de ruido independiente, o sea correlación ~0.9 — la señal que el spec §5.8
// pide para el test de cero falsos positivos. Rosa INDEPENDIENTE no sirve: dos canales sin relación
// pierden 3 dB al monoficar y oscilan alrededor de corr 0, que es material ancho legítimo, no un defecto.
juce::AudioBuffer<float> makeHealthySignal (double seconds)
{
    const auto total = (int) std::llround (seconds * kSr);
    juce::AudioBuffer<float> buf (2, total);
    const float peak = std::pow (10.0f, -2.4f / 20.0f);

    telescope::test::Pink mid { telescope::test::kPinkSeedA }, sideL { telescope::test::kPinkSeedB },
                          sideR { telescope::test::kPinkSeedB ^ 0x5bd1e995u };
    for (int i = 0; i < total; ++i)
    {
        const float m = mid.next();
        buf.setSample (0, i, peak * (m + 0.33f * sideL.next()) * 0.95f);
        buf.setSample (1, i, peak * (m + 0.33f * sideR.next()) * 0.95f);
    }
    return buf;
}

juce::File writeSignal (const juce::String& name, const juce::AudioBuffer<float>& b)
{
    return telescope::test::writeWav (name, kSr, 2, (juce::int64) b.getNumSamples(),
                                      [&] (juce::int64 i)
                                      {
                                          return std::pair<float, float> { b.getSample (0, (int) i),
                                                                           b.getSample (1, (int) i) };
                                      });
}

// Deja el processor con un archivo analizado y VERDICT en modo ARCHIVO. Es el camino barato para llenar
// la lente: analizar 60 s offline tarda un par de segundos, empujarlos por processBlock tarda mucho más.
void loadIntoVerdict (telescope::TelescopeProcessor& proc, const juce::File& wav)
{
    proc.loadVerdictFile (wav);
    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.verdictFileBusy(); }, 180000));
    REQUIRE (proc.verdictAnalysis().valid);
    REQUIRE (proc.verdictMode() == telescope::TelescopeProcessor::verdictFile);
}
}

TEST_CASE ("telescope: snapshot del editor con la lente VERDICT en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);

    const auto wav = writeSignal ("verdict_lens_defect.wav", telescope::test::makeDefectSignal());
    loadIntoVerdict (proc, wav);

    // La lente 13 seleccionada por el parámetro, como la elegiría el usuario o el host.
    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::verdict));

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    tel->pumpLensFrames (6);

    {
        // La foto tiene que tener las TRES secciones con contenido, no sólo el tamaño correcto.
        telescope::VerdictLens probe (proc);
        probe.setSize (kLensW, kLensH);
        probe.pumpFrames (4);
        const auto& rep = probe.report();
        int feel = rep.countInSection (telescope::rules::Section::feel);
        int tr   = rep.countInSection (telescope::rules::Section::translate);
        int miss = rep.countInSection (telescope::rules::Section::missing);
        std::printf ("UISNAP verdict: seccion 1 = %d  ·  seccion 2 = %d  ·  seccion 3 = %d  ·  "
                     "%d s analizados  ·  pie: \"%s\"\n", feel, tr, miss, rep.summary.seconds,
                     rep.footer.c_str());
        std::printf ("UISNAP verdict: titular \"%s\"  ·  %d lineas dentro de rango\n",
                     rep.summary.headline.c_str(), (int) rep.strengths.size());
        REQUIRE (feel >= 1);
        REQUIRE (tr   == telescope::devices::kNumDevices);
        // 57d: eran ≥ 3 porque el hueco de 0:20-0:35 salía una vez por banda (cinco frases). Fusionado es UNO,
        // así que la sección 3 de esta señal tiene exactamente el hueco y el tramo bajo: 2 (VERDICT[hueco]).
        REQUIRE (miss >= 2);
        REQUIRE (rep.strengths.size() >= 8);
        REQUIRE_FALSE (rep.summary.headline.empty());
        REQUIRE_FALSE (rep.footer.empty());
    }

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_verdict_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_verdict_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_verdict_L.png" },
    };
    for (const auto& s : shots)
    {
        tel->applyZoom (s.zoom);
        tel->pumpLensFrames (4);
        telescope::test::writePng (*tel, s.path);
    }

    proc.releaseResources();
    wav.deleteFile();
}

TEST_CASE ("telescope: snapshot de VERDICT sin hallazgos", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);

    const auto wav = writeSignal ("verdict_lens_healthy.wav", makeHealthySignal (30.0));
    loadIntoVerdict (proc, wav);

    auto* lensParam = proc.apvts.getParameter ("lens");
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::verdict));

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    tel->pumpLensFrames (6);

    telescope::VerdictLens probe (proc);
    probe.setSize (kLensW, kLensH);
    probe.pumpFrames (4);
    const auto& rep = probe.report();

    int findings = 0;
    for (const auto& f : rep.findings)
        if (f.severity != Severity::info)
        {
            ++findings;
            std::printf ("UISNAP verdict sano: HALLAZGO -> %s   [%s]\n", f.text.c_str(), f.evidence.c_str());
        }
    std::printf ("UISNAP verdict sano: %d hallazgos (criterio 0)  ·  %d s  ·  I %.2f LUFS  ·  corr %.2f\n",
                 findings, rep.summary.seconds, rep.summary.integrated, rep.summary.corr);
    REQUIRE (findings == 0);

    tel->applyZoom (ovni::PluginEditorBase::Zoom::medium);
    tel->pumpLensFrames (4);
    telescope::test::writePng (*tel, "/tmp/ovni_telescope_verdict_sano_M.png");

    proc.releaseResources();
    wav.deleteFile();
}

TEST_CASE ("telescope: el idioma y el modo de VERDICT hacen round-trip por el estado",
           "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);

    // Los defaults de D-50: ingles y modo EN VIVO.
    REQUIRE (proc.verdictLanguage() == "en");
    REQUIRE (proc.verdictMode() == telescope::TelescopeProcessor::verdictLive);

    proc.setVerdictLanguage ("es");
    proc.setVerdictMode (telescope::TelescopeProcessor::verdictFile);

    juce::MemoryBlock state;
    proc.getStateInformation (state);

    telescope::TelescopeProcessor other;
    other.prepareToPlay (kSr, 512);
    other.setStateInformation (state.getData(), (int) state.getSize());

    std::printf ("VERDICTLENS[estado] idioma \"%s\" -> \"%s\"  ·  modo %d -> %d\n",
                 proc.verdictLanguage().toRawUTF8(), other.verdictLanguage().toRawUTF8(),
                 proc.verdictMode(), other.verdictMode());
    REQUIRE (other.verdictLanguage() == "es");
    REQUIRE (other.verdictMode() == telescope::TelescopeProcessor::verdictFile);

    // Un idioma que esta version NO tiene no deja la lente muda: cae al default.
    other.apvts.state.setProperty (telescope::TelescopeProcessor::kVerdictLanguage, "zz", nullptr);
    std::printf ("VERDICTLENS[estado] idioma inexistente \"zz\" -> \"%s\"\n",
                 other.verdictLanguage().toRawUTF8());
    REQUIRE (other.verdictLanguage() == "en");

    // 56b: el selector de idioma se fue de VERDICT a la TIRA (una propiedad, un control). Lo que se
    // verifica acá es lo mismo de antes —recorre los idiomas que HAY y vuelve al principio— pero sobre el
    // control que de verdad existe, y que ademas escribe en la misma propiedad `language`.
    telescope::strings::setLanguage (proc.apvts.state, "en");
    telescope::LensStrip strip;
    strip.setSize (200, 620);
    strip.onLanguage = [&proc] (const juce::String& code)
        { telescope::strings::setLanguage (proc.apvts.state, code); };

    const auto codes = telescope::strings::availableLanguages();
    const juce::String first = proc.verdictLanguage();
    juce::StringArray seen;
    for (int i = 0; i < codes.size(); ++i)
    {
        strip.setLanguage (proc.verdictLanguage());
        strip.cycleLanguage();
        seen.add (proc.verdictLanguage());
    }
    std::printf ("VERDICTLENS[idioma] %d pulsaciones del chip: \"%s\" -> %s -> \"%s\"\n",
                 codes.size(), first.toRawUTF8(), seen.joinIntoString (", ").toRawUTF8(),
                 proc.verdictLanguage().toRawUTF8());
    REQUIRE (proc.verdictLanguage() == first);            // da la vuelta completa
    REQUIRE (seen.size() == codes.size());
    for (const auto& c : codes) REQUIRE (seen.contains (c));   // y pasa por TODOS, ninguno se saltea

    proc.releaseResources();
    other.releaseResources();
}

TEST_CASE ("telescope: VERDICT enciende sus cuatro modulos y los apaga al cerrar", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);

    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
        REQUIRE (tel != nullptr);
        auto* lensParam = proc.apvts.getParameter ("lens");
        lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::verdict));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

        const auto mask = proc.enabledModules();
        const auto want = telescope::kSpectrum | telescope::kStereoBands | telescope::kReference
                        | telescope::kCqt | telescope::kAlwaysOnModules;
        std::printf ("VERDICTLENS[demanda] con VERDICT visible la mascara es 0x%02X (esperado 0x%02X)\n",
                     (unsigned) mask, (unsigned) want);
        REQUIRE (mask == want);
    }

    std::printf ("VERDICTLENS[demanda] cerrada la ventana, la mascara es 0x%02X (esperado 0x%02X)\n",
                 (unsigned) proc.enabledModules(), (unsigned) telescope::kAlwaysOnModules);
    REQUIRE (proc.enabledModules() == telescope::kAlwaysOnModules);

    proc.releaseResources();
}

TEST_CASE ("telescope: soltar un archivo que no es audio sobre VERDICT lo dice", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);

    VerdictLens lens (proc);
    lens.setSize (900, 600);
    lens.pumpFrames (2);

    auto txt = telescope::test::tempDir().getChildFile ("verdict_drop.txt");
    txt.replaceWithText ("esto no es un wav");

    juce::StringArray files;
    files.add (txt.getFullPathName());
    REQUIRE (lens.isInterestedInFileDrag (files));
    lens.filesDropped (files, 10, 10);

    std::printf ("VERDICTLENS[drop] .txt -> \"%s\"  ·  path cargado: \"%s\" (esperado vacio)\n",
                 lens.stateText().toRawUTF8(), proc.verdictFilePath().toRawUTF8());
    REQUIRE (lens.stateText().contains (".txt"));
    REQUIRE (proc.verdictFilePath().isEmpty());
    REQUIRE (proc.verdictMode() == telescope::TelescopeProcessor::verdictLive);

    txt.deleteFile();
    proc.releaseResources();
}

// ========================================================================================================
// 56b · EL WRAP DE VERDICT ES REAL
//
// Lo que había: `wrapHeight()` reservaba el alto de dos líneas y el dibujo era `drawFittedText (…, 3)`,
// que NO envuelve — comprime la tipografía horizontalmente hasta meter todo en una sola línea. El texto
// se apretaba, se comía el margen derecho y la segunda línea quedaba vacía. Se veía en la línea
// "Phone: …" del verdict_M, y en alemán o francés —más largos— iba a cortar.
//
// Se verifica lo que importa, con un CONTRAEJEMPLO al lado para que el test demuestre que mide algo: si
// `drawFittedText` envolviera, la mitad de abajo estaría encendida en los dos casos y este test no
// probaría nada.
// ========================================================================================================
TEST_CASE ("telescope: VERDICT envuelve el texto de verdad y no se come el margen",
           "[telescope][verdict][uisnap]")
{
    // Una frase de 300 caracteres: más larga que cualquiera de la tabla, para que el criterio no dependa
    // de qué hallazgo salió.
    juce::String phrase;
    while (phrase.length() < 300)
        phrase << "Club: you lose 3.1 dB of low end when the sub goes mono, below 120 Hz. ";
    phrase = phrase.substring (0, 300);

    constexpr int kPanelW = 420;          // el ancho del panel de texto en tamaño S
    constexpr float kFont = 10.5f;

    const auto layout = telescope::VerdictLens::layoutFor (phrase, kFont, kPanelW,
                                                           telescope::look::txtPrimary);
    std::printf ("VERDICTLENS[wrap] 300 caracteres en %d px: %d lineas, %.1f px de alto, %.1f px de ancho\n",
                 kPanelW, layout.getNumLines(), layout.getHeight(), layout.getWidth());
    REQUIRE (layout.getNumLines() >= 2);                 // envuelve…
    REQUIRE (layout.getWidth() <= (float) kPanelW + 1.0f);   // …y no se pasa del ancho que le dieron

    // EL CONTRAEJEMPLO, con lo que de verdad hacía mal `drawFittedText (…, 3)`. No es que no parta —
    // parte, hasta 3 líneas. Es que cuando el texto necesita MÁS de esas 3, en vez de usar el alto que se
    // le reservó APRIETA los glifos horizontalmente para que entren igual. El resultado tiene menos
    // renglones de los que el texto pide y una tipografía más angosta que el resto del panel.
    //
    // Se cuentan RENGLONES: bandas de filas de píxeles con tinta separadas por filas vacías. Es la medida
    // directa de "cuántas líneas se dibujaron de verdad", y no depende de cuánta tinta tenga cada una.
    const int boxH = (int) std::ceil (layout.getHeight()) + 2;
    const auto rowsOfInk = [&] (bool useLayout)
    {
        juce::Image img (juce::Image::ARGB, kPanelW, boxH, true);
        {
            juce::Graphics g (img);
            if (useLayout)
                layout.draw (g, juce::Rectangle<float> (0.0f, 0.0f, (float) kPanelW, (float) boxH));
            else
            {
                g.setColour (telescope::look::txtPrimary);
                g.setFont (telescope::look::tabularFont (kFont));
                g.drawFittedText (phrase, 0, 0, kPanelW, boxH, juce::Justification::topLeft, 3);
            }
        }
        int bands = 0;
        bool inBand = false;
        for (int y = 0; y < boxH; ++y)
        {
            int ink = 0;
            for (int x = 0; x < kPanelW; ++x)
                if (img.getPixelAt (x, y).getAlpha() > 24) ++ink;
            if (ink > 0 && ! inBand) { ++bands; inBand = true; }
            else if (ink == 0)       { inBand = false; }
        }
        return bands;
    };

    const int conLayout = rowsOfInk (true), conFitted = rowsOfInk (false);
    std::printf ("VERDICTLENS[wrap] renglones dibujados en la misma caja: TextLayout %d  ·  "
                 "drawFittedText(…,3) %d (lo que habia)  ·  el texto pide %d\n",
                 conLayout, conFitted, layout.getNumLines());
    REQUIRE (conLayout == layout.getNumLines());   // se dibujan TODOS los renglones que el texto pide…
    REQUIRE (conFitted <  layout.getNumLines());   // …y con lo anterior faltaban (los apretaba)

    // ---- y ahora sobre la lente de verdad, en tamaño S, con el informe completo ----
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kStereoBands | telescope::kReference
                            | telescope::kCqt | telescope::kLoudness);
    const auto wav = writeSignal ("verdict_wrap_defect.wav", telescope::test::makeDefectSignal());
    proc.loadVerdictFile (wav);
    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.verdictFileBusy(); }, 60000));

    VerdictLens lens (proc);
    lens.setSize (784 - 190, 496 - 120);          // el área de la lente en tamaño S
    lens.pumpFrames (4);

    juce::Image img (juce::Image::ARGB, lens.getWidth(), lens.getHeight(), true);
    { juce::Graphics g (img); lens.paintEntireComponent (g, false); }

    // 1 · EL ALTO RESERVADO ES EL ALTO DIBUJADO. Antes eran dos cuentas distintas —una estimación por
    // ancho de glifo para reservar, un drawFittedText para pintar— y podían no coincidir. Ahora hay un
    // solo objeto: el mismo TextLayout que se mide es el que se dibuja. Esto verifica que no vuelvan a
    // separarse, y de paso que en tamaño S de verdad haya frases que no entran en un renglón.
    int worst = 0, multi = 0;
    for (int i = 0; i < lens.numLines(); ++i)
    {
        const int n = lens.wrappedLinesOf (i);
        if (n == 0) continue;                       // los títulos de sección no llevan texto envuelto
        if (n >= 2) ++multi;
        const int reserved = lens.textHeightOf (i);
        const int needed   = juce::jmax (14, (int) std::ceil (lens.layoutHeightOf (i)) + 2);
        worst = juce::jmax (worst, needed - reserved);
    }
    std::printf ("VERDICTLENS[wrap] %d filas, %d con 2+ lineas  ·  peor deficit de alto = %d px\n",
                 lens.numLines(), multi, worst);
    REQUIRE (multi > 0);            // en S hay frases que no entran en una línea (si no, no medimos nada)
    REQUIRE (worst == 0);           // reservado == dibujado, exacto

    // 2 · NINGÚN PÍXEL CON TINTA a la derecha del panel. Es la columna de margen que el texto comprimido
    // se comía: el ancho con el que se envuelve sale del mismo `textWidthFor` que usa el dibujo.
    // "Con tinta" = distinto del POZO. El fondo de la lente es opaco (renderStatic lo rellena con
    // look::well), así que mirar el alpha marcaría los 1820 píxeles de fondo de la franja como si fueran
    // texto. Lo que se busca es cualquier cosa dibujada ENCIMA del fondo.
    const auto bg = telescope::look::well;
    const auto isInk = [&] (juce::Colour c)
    {
        return std::abs ((int) c.getRed()   - (int) bg.getRed())   > 12
            || std::abs ((int) c.getGreen() - (int) bg.getGreen()) > 12
            || std::abs ((int) c.getBlue()  - (int) bg.getBlue())  > 12;
    };

    // Los SEPARADORES de sección sí cruzan el panel entero (son la jerarquía del 56, no un desborde): se
    // reconocen porque encienden la franja COMPLETA en esa fila. Lo que se prohíbe es el texto, que llega
    // en pedazos. Se cuentan las dos cosas por separado para que el número diga cuál es cuál.
    const auto list = lens.listArea();
    const int textRight = list.getX() + telescope::VerdictLens::kTextIndent + lens.textWidth();
    const int x0 = textRight + 1, x1 = list.getRight() - 4;   // -4: ahí vive la barra de scroll
    int overflow = 0, rules = 0;
    for (int y = list.getY(); y < list.getBottom(); ++y)
    {
        int lit = 0;
        for (int x = x0; x < x1; ++x)
            if (isInk (img.getPixelAt (x, y))) ++lit;
        // Un separador enciende la franja de punta a punta; el antialiasing puede dejar un extremo por
        // debajo del umbral, así que se lo reconoce por MAYORÍA y no por unanimidad. Un desborde de texto
        // es lo contrario: unos pocos glifos pegados al borde izquierdo de la franja.
        if (lit * 2 >= x1 - x0) ++rules;
        else                    overflow += lit;
    }

    std::printf ("VERDICTLENS[wrap] margen derecho (x %d..%d): %d px de texto desbordado  ·  "
                 "%d separadores de seccion cruzando\n", x0, x1 - 1, overflow, rules);
    REQUIRE (overflow == 0);
    REQUIRE (rules == 3);      // los tres títulos de sección; si fueran 0, el criterio de arriba no mediría

    proc.releaseResources();
}

// ========================================================================================================
// ===== 57d · EL TITULAR Y "DENTRO DE RANGO" VAN PRIMERO; EN S, "DENTRO DE RANGO" ES UNA SOLA LÍNEA =====
//
// Lo que se verifica, sobre la lente de verdad y con material sano (que es cuando salen las doce líneas):
//   · el titular que se dibuja es el del informe, y no está vacío;
//   · la PRIMERA fila de la lista es "Dentro de rango": su título y una fila por línea, en el orden del
//     informe — y recién después viene "Cómo se va a sentir";
//   · en tamaño S la sección entera no entra con el resto, así que se colapsa a UNA fila que nombra cada
//     regla dentro de rango ("Within range: transients · body · …"); en M y en L va desplegada.
// ========================================================================================================
TEST_CASE ("telescope: VERDICT abre con el titular y con lo que esta dentro de rango", "[telescope][verdict]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    const auto wav = writeSignal ("verdict_lens_rango.wav", makeHealthySignal (30.0));
    loadIntoVerdict (proc, wav);

    const auto t = [] (const std::string& key)
    { return juce::String::fromUTF8 (telescope::Verdict::translate (key.c_str(), "en").c_str()); };

    const struct { const char* name; int w, h; bool collapsed; } kSizes[] = {
        { "S", 784 - 190, 496 - 120, true  },   // el área de la lente en tamaño S (la del test del wrap)
        { "M", 820,       562,       false },
        { "L", kLensW,    kLensH,    false },
    };

    for (const auto& sz : kSizes)
    {
        VerdictLens lens (proc);
        lens.setSize (sz.w, sz.h);
        lens.pumpFrames (4);
        const auto& rep = lens.report();
        const int   n   = (int) rep.strengths.size();

        std::printf ("VERDICTLENS[rango] %s %4dx%-4d  titular \"%s\"  ·  %d lineas dentro de rango  ·  "
                     "colapsada: %s  ·  fila 0 \"%s\"  ·  lista %d px, contenido %d px\n",
                     sz.name, sz.w, sz.h, lens.headlineText().toRawUTF8(), n,
                     lens.withinCollapsed() ? "si" : "no", lens.lineTextOf (0).toRawUTF8(),
                     lens.listArea().getHeight(), lens.contentHeight());

        if (! lens.withinCollapsed())
        {
            // Cuánto del alto de la lista ocupa la sección desplegada, con la misma cuenta de la lente (título
            // de 22 px + una fila compacta por línea, cada una con su separación de 4). Se imprime para ver el
            // aire contra VerdictLens::kWithinMaxShare; lo que se exige es el colapso de arriba.
            int expandedH = 22 + 4;
            for (int i = 1; i <= n; ++i)
                expandedH += juce::jmax (12, (int) std::ceil (lens.layoutHeightOf (i))) + 4;
            std::printf ("VERDICTLENS[rango] %s  desplegada: %d px = %.0f %% de la lista (colapsa por encima de %.0f %%)\n",
                         sz.name, expandedH, 100.0 * expandedH / juce::jmax (1, lens.listArea().getHeight()),
                         100.0 * VerdictLens::kWithinMaxShare);
        }

        INFO (sz.name);
        REQUIRE (n >= 8);
        // Lo que redondea a cero se escribe como cero: "-0.0 dB on average" es ruido de coma flotante con cara
        // de dato (lo mostró la captura verdict_sano_M del primer intento, sobre este mismo material).
        for (const auto& s : rep.strengths)
        {
            INFO (s.text);
            REQUIRE (s.text.find ("-0.0 ") == std::string::npos);
            REQUIRE (s.text.find ("-0.00 ") == std::string::npos);
        }
        REQUIRE (lens.headlineText().isNotEmpty());
        REQUIRE (lens.headlineText() == juce::String::fromUTF8 (rep.summary.headline.c_str()));
        REQUIRE (lens.withinCollapsed() == sz.collapsed);

        if (sz.collapsed)
        {
            // UNA fila: arranca con el rótulo de la sección y nombra cada regla dentro de rango.
            const auto row = lens.lineTextOf (0);
            REQUIRE (row.startsWith (t ("section.within")));
            for (const auto& s : rep.strengths)
                REQUIRE (row.contains (t (std::string ("within.") + telescope::rules::rule (s.ruleId).name)));
            REQUIRE (lens.lineTextOf (1) == t ("section.feel"));
        }
        else
        {
            REQUIRE (lens.lineTextOf (0) == t ("section.within"));
            for (int i = 0; i < n; ++i)
                REQUIRE (lens.lineTextOf (i + 1) == juce::String::fromUTF8 (rep.strengths[(size_t) i].text.c_str()));
            REQUIRE (lens.lineTextOf (n + 1) == t ("section.feel"));
        }
    }

    proc.releaseResources();
    wav.deleteFile();
}

// ========================================================================================================
// ===== 56c ===== LA TÓNICA DE VERDICT, EN LA CONVENCIÓN DEL IDIOMA
//
// El 56b llevó los nombres de nota a la convención de cada lengua (D-50) y lo dejó a medias: CQT y SPIRAL
// pasaron por lenses/NoteName.h, pero VERDICT se quedó con un array local de letras inglesas adentro de
// paintHead(). El resultado era que la MISMA tonalidad salía "Do menor" en CQT y "C menor" en VERDICT, en
// la misma pantalla y en cinco de los seis idiomas — exactamente lo contrario de lo que decía el reporte.
//
// Y falta una convención. `Table::solfege` era una bandera de dos estados y el alemán la tenía en false,
// o sea que compartía el array del inglés. En alemán el Si es H y el Si♭ se llama B: escribir "B" donde
// un alemán lee "H" no es un detalle de gusto, es nombrar OTRA nota (un semitono abajo). Son TRES
// convenciones vivas, no dos.
//
// El test mide las dos cosas a la vez:
//   · que la cabecera de VERDICT y el pie de CQT digan lo mismo. CQT arma su pie con `keyLabel()`
//     directamente (CqtLens.cpp, la zona keyText), así que comparar contra `keyLabel()` ES comparar
//     contra lo que dibuja CQT;
//   · y que las dos digan lo que corresponde a la convención del idioma, contra una tabla escrita a mano
//     acá. Sin esto, si mañana las dos se rompen juntas el test seguiría en verde.
// ========================================================================================================
TEST_CASE ("telescope: la tonica de VERDICT usa la convencion de notas del idioma",
           "[telescope][verdict][i18n]")
{
    // Las tres convenciones, escritas a mano. Ésta es la referencia: no sale de ninguna constante del
    // plugin, justamente para que un cambio en el plugin no pueda arrastrar al test.
    const char* letras[12]  = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const char* solfeo[12]  = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    const char* aleman[12]  = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "B", "H" };

    struct Lang { const char* code; const char* const* names; };
    const Lang langs[] = { { "en", letras }, { "es", solfeo }, { "pt", solfeo },
                           { "fr", solfeo }, { "de", aleman }, { "it", solfeo } };

    // Se acumula por idioma y se juzga UNA vez por idioma: con un CHECK por tonalidad, seis lenguas rotas
    // llenan el log de 96 líneas iguales y no se ve cuál es cuál.
    const auto tonicOf = [] (const juce::String& s) { return s.upToFirstOccurrenceOf (" ", false, false); };

    for (const auto& l : langs)
    {
        juce::String row, mismatch;
        int checked = 0;

        for (int tonic = 0; tonic < 12; ++tonic)
            for (int mode = 0; mode < 2; ++mode)      // 0 mayor · 1 menor → las 24 tonalidades
            {
                const auto verdict = telescope::VerdictLens::keyText (tonic, mode, l.code);
                const auto cqt     = telescope::keyLabel (tonic, mode, l.code);
                const auto want    = juce::String::fromUTF8 (l.names[tonic]);
                ++checked;

                // 1 · las dos lentes dicen lo mismo · 2 · y las dos lo dicen en la convención del idioma.
                //     Se compara el PRIMER token: el sufijo de modo ya lo cubren las tablas de rules::phrase.
                if (verdict != cqt)
                    mismatch << " VERDICT\"" << verdict << "\"!=CQT\"" << cqt << "\"";
                else if (tonicOf (verdict) != want)
                    mismatch << " \"" << tonicOf (verdict) << "\"!=\"" << want << "\"";

                if (mode == 0) row << (tonic > 0 ? " " : "") << tonicOf (verdict);
            }

        std::printf ("VERDICT[tonica] %s  %-44s  %s\n", l.code, row.toRawUTF8(),
                     mismatch.isEmpty() ? "OK (24 tonalidades, VERDICT == CQT == convencion)"
                                        : ("MAL:" + mismatch.substring (0, 120)).toRawUTF8());
        CHECK (mismatch.isEmpty());
        CHECK (checked == 24);
    }

    // Lo que distingue al alemán de las letras, dicho aparte para que se lea en el log: el Si es H y el
    // Si♭ es B — y en inglés esas dos posiciones son B y A#.
    std::printf ("VERDICT[tonica] de: clase 11 -> \"%s\"  clase 10 -> \"%s\"   |   "
                 "en: clase 11 -> \"%s\"  clase 10 -> \"%s\"\n",
                 telescope::VerdictLens::keyText (11, 0, "de").upToFirstOccurrenceOf (" ", false, false).toRawUTF8(),
                 telescope::VerdictLens::keyText (10, 0, "de").upToFirstOccurrenceOf (" ", false, false).toRawUTF8(),
                 telescope::VerdictLens::keyText (11, 0, "en").upToFirstOccurrenceOf (" ", false, false).toRawUTF8(),
                 telescope::VerdictLens::keyText (10, 0, "en").upToFirstOccurrenceOf (" ", false, false).toRawUTF8());

    // Sin tonalidad estimada VERDICT sigue diciendo "--" (su contrato de siempre).
    REQUIRE (telescope::VerdictLens::keyText (-1, 0, "es") == "--");
    REQUIRE (telescope::VerdictLens::keyText (0, -1, "es") == "--");
}
