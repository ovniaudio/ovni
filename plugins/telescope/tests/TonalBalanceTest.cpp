// [telescope][tonal] + [uisnap] — la lente TONAL BALANCE (lente 12, spec §5.6, prompt 54).
//
// TONAL[snap]    el editor real con una referencia cargada de un ARCHIVO y un programa con mucho más aire
//                que ella: las dos curvas separadas arriba de 6.3 kHz y las barras de delta saliéndose de
//                la banda de ±3 dB por los dos lados. Y una segunda foto SIN referencia, que es el estado
//                en el que la lente se abre.
// TONAL[estado]  los textos: "sin referencia · arrastrá un archivo", "analizando <nombre> · N %",
//                "referencia no encontrada: <nombre>".
// TONAL[rt]      round-trip del path por el estado: se guarda, se restaura y el archivo se RE-ANALIZA; si
//                el archivo ya no está, la lente lo dice con el nombre y no dibuja ninguna curva.
// TONAL[drop]    soltar un .txt encima: mensaje de error, sin crash y sin referencia cargada.
// TONAL[demanda] la lente pide kSpectrum | kReference, y el editor efectivamente los prende.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestShelf.h"
#include "TestSignals.h"
#include "TestWav.h"
#include "lenses/LensIds.h"
#include "lenses/TonalBalanceLens.h"

using telescope::ReferenceFrame;
using telescope::TonalBalanceLens;
using telescope::test::HighShelf;
using telescope::test::Pink;

namespace
{
constexpr double kSr = 48000.0;
// El programa tiene MUCHO más aire que la referencia: +12 dB arriba de 6.3 kHz. Se elige una región
// ANGOSTA y arriba a propósito — un shelf ancho sube tanto la loudness que el delta se reparte casi
// entero para el lado negativo (es la propiedad de la normalización, ver ReferenceFrame.h) y la foto no
// mostraría barras grandes para arriba. Con 6.3 kHz el delta sale ~+7 arriba y ~-5 abajo: las dos cosas
// que la lente tiene que saber dibujar, en la misma imagen.
constexpr double kShelfHz = 6300.0, kShelfDb = 12.0;

void pushPink (telescope::TelescopeProcessor& proc, double seconds, float peak,
               HighShelf* shelfL = nullptr, HighShelf* shelfR = nullptr)
{
    constexpr int kBlock = 512;
    Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    juce::AudioBuffer<float> buf (2, kBlock);
    juce::MidiBuffer midi;
    long long n = 0;

    for (int blk = 0; blk < (int) std::ceil (seconds * kSr / kBlock); ++blk)
    {
        for (int i = 0; i < kBlock; ++i, ++n)
        {
            float l = a.next(), r = b.next();
            if (shelfL != nullptr) l = shelfL->process (l);
            if (shelfR != nullptr) r = shelfR->process (r);
            buf.setSample (0, i, peak * l);
            buf.setSample (1, i, peak * r);
        }
        proc.processBlock (buf, midi);

        // El freno para que el bus no descarte. 30 s de tope y no 8: el timeout es un TECHO de seguridad,
        // no el tiempo que se espera (TestHelpers.h) — con tres obreras compilando al lado el worker puede
        // tardar de verdad, y subirlo nunca hace el test más frágil, sólo más paciente.
        const double pushed = (double) n / kSr;
        if (pushed - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, 30000));
    }
}

// Escribe (una vez) el WAV de referencia: ruido rosa PLANO, la misma realización que el programa usa
// antes del shelf. Así lo único que separa las dos curvas es el filtro.
juce::File referenceWav()
{
    auto a = std::make_shared<Pink> (telescope::test::kPinkSeedA);
    auto b = std::make_shared<Pink> (telescope::test::kPinkSeedB);
    return telescope::test::writeWav ("tonal_reference_pink.wav", kSr, 2, (juce::int64) (10.0 * kSr),
                                      [a, b] (juce::int64)
                                      {
                                          return std::pair<float, float> { 0.3f * a->next(), 0.3f * b->next() };
                                      });
}

// La captura a PNG vive en TestHelpers.h, en UNA sola copia (LOW de los tres revisores).
using telescope::test::writePng;

// Abre el editor con TONAL BALANCE a la vista y devuelve el editor.
std::unique_ptr<juce::AudioProcessorEditor> openTonalEditor (telescope::TelescopeProcessor& proc)
{
    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::tonalBalance));

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    return ed;
}
}

// ============================================================================================ 1 · la foto
TEST_CASE ("telescope: snapshot del editor con la lente TONAL BALANCE en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);

    auto ed = openTonalEditor (proc);
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    REQUIRE ((proc.enabledModules() & telescope::kReference) != 0u);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum)  != 0u);

    // ---- la foto SIN referencia: el estado en el que la lente se abre ----
    pushPink (proc, 4.0, 0.3f);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.reference().read().liveValid; }, 8000));
    tel->pumpLensFrames (30);
    tel->applyZoom (ovni::PluginEditorBase::Zoom::medium);
    tel->pumpLensFrames (5);
    writePng (*tel, "/tmp/ovni_telescope_tonal_noref_M.png");

    // ---- ahora con referencia: rosa plano en el archivo, +12 dB arriba de 6.3 kHz en el programa ----
    const auto wav = referenceWav();
    REQUIRE (wav.existsAsFile());
    proc.loadReference (wav);
    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.referenceBusy(); }, 30000));

    proc.resetAnalysis();   // el promedio del programa arranca limpio con el shelf ya puesto
    HighShelf shelfL { kShelfHz, kShelfDb, kSr }, shelfR { kShelfHz, kShelfDb, kSr };
    pushPink (proc, 10.0, 0.3f, &shelfL, &shelfR);

    REQUIRE (telescope::test::waitUntil ([&] { const auto& f = proc.reference().read();
                                               return f.refValid && f.liveValid; }, 10000));

    // La foto tiene que tener el CONTRASTE adentro, no sólo el tamaño correcto.
    {
        const auto& f = proc.reference().read();
        float lowSum = 0.0f, highSum = 0.0f;
        int   lowN = 0, highN = 0, outsideRef = 0;
        for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
        {
            if (! f.bandValid[b]) continue;
            const double hz = telescope::kThirdOctaveHz[b];
            if (std::abs (f.deltaDb[b]) > TonalBalanceLens::kDeltaRefDb) ++outsideRef;
            if (hz <= 500.0)  { lowSum  += f.deltaDb[b]; ++lowN; }
            if (hz >= 8000.0) { highSum += f.deltaDb[b]; ++highN; }
        }
        const float low = lowN > 0 ? lowSum / lowN : 0.0f, high = highN > 0 ? highSum / highN : 0.0f;
        std::printf ("UISNAP tonal: ref=\"%s\" (%.1f s, %+.1f LUFS)  ·  programa %+.1f LUFS  ·  "
                     "delta ≤500 Hz = %+.2f dB  ·  delta ≥8 kHz = %+.2f dB  ·  separacion %.2f dB  ·  "
                     "%d bandas fuera de la banda de ±3 dB\n",
                     f.refName, f.refSeconds, f.refIntegrated, f.liveIntegrated, low, high, high - low,
                     outsideRef);
        REQUIRE (lowN  >= 4);
        REQUIRE (highN >= 2);
        REQUIRE (high - low >= 8.0f);   // las dos curvas SE SEPARAN, que es lo que la foto tiene que mostrar
        REQUIRE (high >  TonalBalanceLens::kDeltaRefDb);
        REQUIRE (low  < -TonalBalanceLens::kDeltaRefDb);
    }

    tel->pumpLensFrames (40);

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_tonal_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_tonal_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_tonal_L.png" },
    };
    for (const auto& s : shots)
    {
        tel->applyZoom (s.zoom);
        tel->pumpLensFrames (10);
        writePng (*tel, s.path);
    }

    proc.releaseResources();
}

// ========================================================================================= 2 · los textos
TEST_CASE ("telescope: los estados de TONAL BALANCE se dicen con palabras", "[telescope][tonal]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kReference | telescope::kLoudness);

    TonalBalanceLens lens (proc);
    lens.setSize (900, 600);
    lens.pumpFrames (2);

    // 56b (D-50): el texto ya NO está en castellano a mano — sale de Strings.h en el idioma vigente.
    // Así que el test deja de comparar contra palabras castellanas fijas (que ahora sólo se verían con
    // `language = es`) y compara contra LA CLAVE, en los dos idiomas revisados. Es más fuerte que
    // antes: verifica el texto Y que el selector de idioma llegue efectivamente hasta acá.
    using telescope::strings::Key;
    const auto say = [] (Key k, const char* lang)
        { return telescope::strings::get (k, lang); };

    for (const char* lang : { "en", "es" })
    {
        telescope::strings::setLanguage (proc.apvts.state, lang);
        lens.pumpFrames (1);
        const auto sinRef = lens.stateText();
        std::printf ("TONAL[estado] sin referencia (%s): \"%s\"\n", lang, sinRef.toRawUTF8());
        REQUIRE (sinRef == say (Key::noReferenceDrag, lang));
    }
    telescope::strings::setLanguage (proc.apvts.state, "es");

    // --- analizando: se mira mientras corre (un archivo largo, para llegar a verlo) ---
    auto a = std::make_shared<Pink> (telescope::test::kPinkSeedA);
    const auto big = telescope::test::writeWav ("tonal_long.wav", kSr, 2, (juce::int64) (120.0 * kSr),
                                                [a] (juce::int64)
                                                { const float v = 0.3f * a->next(); return std::pair<float, float> { v, v }; });
    proc.loadReference (big);

    juce::String analizando;
    REQUIRE (telescope::test::waitUntil ([&]
                                         {
                                             lens.pumpFrames (1);
                                             analizando = lens.stateText();
                                             return analizando.startsWith (say (Key::analysing, "es"));
                                         }, 5000));
    std::printf ("TONAL[estado] analizando: \"%s\"\n", analizando.toRawUTF8());
    REQUIRE (analizando.contains (big.getFileName()));
    REQUIRE (analizando.contains ("%"));

    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.referenceBusy(); }, 60000));

    // --- referencia no encontrada: el archivo desaparece y se restaura el estado ---
    const auto state = [&proc]
    {
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        return mb;
    }();
    big.deleteFile();
    proc.setStateInformation (state.getData(), (int) state.getSize());
    lens.pumpFrames (2);

    const auto faltante = lens.stateText();
    std::printf ("TONAL[estado] no encontrada: \"%s\"\n", faltante.toRawUTF8());
    REQUIRE (faltante.startsWith (say (Key::referenceMissing, "es")));
    REQUIRE (faltante.contains ("tonal_long.wav"));
    REQUIRE (proc.referenceMissingName() == "tonal_long.wav");
    REQUIRE_FALSE (proc.reference().read().refValid);

    proc.releaseResources();
}

// ==================================================================================== 3 · round-trip real
TEST_CASE ("telescope: el path de la referencia sobrevive al estado y el archivo se re-analiza",
           "[telescope][tonal]")
{
    const auto wav = referenceWav();
    juce::MemoryBlock state;

    {
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (kSr, 512);
        proc.loadReference (wav);
        REQUIRE (telescope::test::waitUntil ([&] { return ! proc.referenceBusy(); }, 30000));
        REQUIRE (proc.referencePath() == wav.getFullPathName());
        proc.getStateInformation (state);
        proc.releaseResources();
    }

    // Otro processor: se le mete el estado y tiene que RE-ANALIZAR el archivo (no restaurar 30 números).
    telescope::TelescopeProcessor proc2;
    proc2.prepareToPlay (kSr, 512);
    proc2.setEnabledModules (telescope::kSpectrum | telescope::kReference | telescope::kLoudness);
    proc2.setStateInformation (state.getData(), (int) state.getSize());

    REQUIRE (proc2.referencePath() == wav.getFullPathName());
    REQUIRE (telescope::test::waitUntil ([&] { return ! proc2.referenceBusy(); }, 30000));

    const auto restored = proc2.referenceAnalysis();
    REQUIRE (restored.ok);
    REQUIRE (restored.valid);

    // Y el motor la tiene: hay que empujar audio para que se publique un frame nuevo.
    pushPink (proc2, 4.0, 0.3f);
    REQUIRE (telescope::test::waitUntil ([&] { return proc2.reference().read().refValid; }, 8000));

    const auto& f = proc2.reference().read();
    std::printf ("TONAL[rt] restaurada \"%s\"  %.1f s  I=%+.3f LUFS  ·  refValid=%d\n",
                 f.refName, f.refSeconds, f.refIntegrated, (int) f.refValid);
    REQUIRE (juce::String (f.refName) == wav.getFileName());
    REQUIRE (f.refIntegrated == restored.integratedLufs);

    proc2.releaseResources();
}

// ============================================================================================ 4 · un .txt
TEST_CASE ("telescope: soltar un archivo que no es audio da un mensaje y no rompe nada", "[telescope][tonal]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kReference | telescope::kLoudness);

    TonalBalanceLens lens (proc);
    lens.setSize (900, 600);

    auto txt = telescope::test::tempDir().getChildFile ("tonal_no_soy_audio.txt");
    txt.deleteFile();
    txt.replaceWithText ("esto no es audio\n");

    juce::StringArray files;
    files.add (txt.getFullPathName());
    REQUIRE (lens.isInterestedInFileDrag (files));
    lens.fileDragEnter (files, 10, 10);
    lens.filesDropped (files, 10, 10);

    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.referenceBusy(); }, 10000));
    lens.pumpFrames (2);

    const auto text = lens.stateText();
    std::printf ("TONAL[drop] .txt → \"%s\"  ·  refValid=%d\n", text.toRawUTF8(),
                 (int) proc.reference().read().refValid);
    REQUIRE (text.containsIgnoreCase ("formato"));
    REQUIRE_FALSE (proc.reference().read().refValid);

    // Y la lente sigue pintando (que "no rompe nada" quiera decir algo).
    juce::Image img (juce::Image::ARGB, 900, 600, true);
    { juce::Graphics g (img); lens.paintEntireComponent (g, false); }

    proc.releaseResources();
}

// ========================================================================================== 5 · a demanda
TEST_CASE ("telescope: TONAL BALANCE enciende su modulo y lo apaga al cerrarse", "[telescope][tonal]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);

    {
        TonalBalanceLens lens (proc);
        REQUIRE (lens.requiredModules() == (telescope::kSpectrum | telescope::kReference));
        REQUIRE (lens.id() == telescope::LensId::tonalBalance);
    }

    {
        auto ed = openTonalEditor (proc);
        std::printf ("TONAL[demanda] con la lente abierta la mascara es 0x%x\n", proc.enabledModules());
        REQUIRE ((proc.enabledModules() & telescope::kReference) != 0u);
        REQUIRE ((proc.enabledModules() & telescope::kSpectrum)  != 0u);
    }

    // Al cerrar la última ventana el motor vuelve a los módulos siempre-activos.
    std::printf ("TONAL[demanda] cerrada la ventana, la mascara es 0x%x (esperado 0x%x)\n",
                 proc.enabledModules(), telescope::kAlwaysOnModules);
    REQUIRE (proc.enabledModules() == telescope::kAlwaysOnModules);

    proc.releaseResources();
}

// ========================================================================================================
// ===== 57b: UNA REFERENCIA QUE NO LLEGA A TODAS LAS BANDAS =====
//
// «Tonal Balance: se rompen las líneas» (Joaquín, 9-sep) y, en la misma foto, el readout decía
// «referencia −123.1 · delta +106.2 dB · 197 bins».
//
// Las dos cosas eran el MISMO defecto y era de criterio, no de dibujo: una banda contaba como medida con
// `> SpectrumFrame::kFloorDb`, y kFloorDb vale −200 dB (un piso de guarda, no un nivel). Una banda a
// −123 dBFS —el redondeo del propio análisis— pasaba por medición, así que la curva la dibujaba… cien
// decibeles por debajo del plot, o sea una raya vertical hasta el borde. Y el delta contra ella daba
// +106 dB, que es un número que no puede existir entre dos programas.
//
// Este test arma justo ese caso a propósito: una referencia PAD —energía sólo por debajo de 2 kHz— contra
// un programa de banda ancha. Arriba de 2 kHz la referencia no tiene nada que decir, y lo que se exige es
// que la lente lo diga así: la curva se CORTA, el delta no existe, y el readout de esas bandas dice "sin
// referencia en esta banda" en vez de inventar una cifra.
TEST_CASE ("telescope: una referencia sin agudos corta la curva y no inventa deltas", "[telescope][tonal]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kLoudness | telescope::kReference);

    // EL PAD: suma de senos de 1/12 de octava entre 25 Hz y 1.6 kHz, y NADA por encima. Una suma de senos
    // está limitada en banda por construcción — no hay filtro que pueda dejar una cola, que es lo que
    // haría dudar de si el hueco es del dibujo o de la señal.
    std::vector<double> tones;
    for (double f = 25.0; f <= 1600.0; f *= std::pow (2.0, 1.0 / 12.0)) tones.push_back (f);
    REQUIRE (tones.size() > 40);

    const auto pad = telescope::test::writeWav (
        "tonal_pad_lowonly.wav", kSr, 2, (juce::int64) (5.0 * kSr),
        [&tones] (juce::int64 i)
        {
            double v = 0.0;
            for (size_t k = 0; k < tones.size(); ++k)
                v += std::sin (2.0 * juce::MathConstants<double>::pi * tones[k] * (double) i / kSr
                               + 0.7 * (double) k);
            v *= 0.25 / std::sqrt ((double) tones.size());
            return std::pair<float, float> { (float) v, (float) v };
        });

    proc.loadReference (pad);
    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.referenceBusy(); }, 60000));
    REQUIRE (proc.referenceError().isEmpty());

    // El programa: ruido rosa de banda ancha, bien por encima del piso en TODAS las bandas.
    pushPink (proc, 8.0, 0.3f);
    REQUIRE (telescope::test::waitUntil ([&] { const auto& f = proc.reference().read();
                                               return f.liveValid && f.refValid; }, 30000));

    const auto& f = proc.reference().read();

    // ---- 1 · los HUECOS: bandas que el programa mide y la referencia no ----
    int gaps = 0, refMissing = 0, liveOk = 0, worstDelta = 0;
    float maxAbsDelta = 0.0f;
    bool prevHadRef = true;
    for (int b = 0; b < telescope::ReferenceFrame::kNumBands; ++b)
    {
        const bool liveHas = f.liveNorm[b] > telescope::SpectrumFrame::kFloorDb;
        const bool refHas  = f.refNorm[b]  > telescope::SpectrumFrame::kFloorDb;
        if (liveHas) ++liveOk;
        if (! refHas) ++refMissing;
        if (prevHadRef && ! refHas) ++gaps;      // un hueco = un tramo nuevo que el path deja abierto
        prevHadRef = refHas;

        if (f.bandValid[b] && std::abs (f.deltaDb[b]) > maxAbsDelta)
        {
            maxAbsDelta = std::abs (f.deltaDb[b]);
            worstDelta = b;
        }
    }

    std::printf ("REF[pad] referencia sólo < 2 kHz  ·  %d de %d bandas del programa con medición  ·  "
                 "%d bandas sin referencia (%d hueco[s] en el path)  ·  |delta| máximo = %.2f dB "
                 "(banda %d, %.0f Hz)\n",
                 liveOk, telescope::ReferenceFrame::kNumBands, refMissing, gaps, maxAbsDelta,
                 worstDelta, telescope::kThirdOctaveHz[worstDelta]);

    CHECK (refMissing > 0);        // la referencia efectivamente no llega arriba…
    CHECK (gaps >= 1);             // …y eso deja al menos un tramo abierto en el path
    CHECK (liveOk > refMissing);   // el programa SÍ mide ahí: el hueco es de la referencia, no del dibujo
    CHECK (maxAbsDelta <= 60.0f);  // ningún delta imposible: era +106.2 dB en la foto

    // ---- 2 · el READOUT de una banda sin referencia ----
    telescope::TonalBalanceLens lens (proc);
    lens.setSize (1025, 702);
    lens.pumpFrames (4);

    int checked = 0;
    for (int b = 0; b < telescope::ReferenceFrame::kNumBands; ++b)
    {
        if (f.refNorm[b] > telescope::SpectrumFrame::kFloorDb) continue;
        const auto r = lens.readoutForBand (b);
        REQUIRE (r.valid);
        CHECK (! r.comparable);                  // no comparable: el readout dice "sin referencia"
        CHECK (std::abs (r.deltaDb) < 1.0e-6f);  // y el delta no existe, no es "cero" disfrazado
        ++checked;
    }
    std::printf ("REF[pad] readout verificado en %d bandas sin referencia: comparable=false y delta=0\n",
                 checked);
    REQUIRE (checked > 0);

    proc.releaseResources();
}

// ========================================================================================================
// ===== 57c · LAS DOS FORMAS EN QUE SE CORTABA LA LÍNEA =====
//
// «Tonal balance: se corta la línea» (Joaquín, 12-sep, con la foto de `pad-frente-mix50-v03.wav`). En la
// misma imagen había DOS defectos distintos, y ninguno era la música:
//
//   1 · un HUECO en 30–50 Hz en las DOS curvas — la rejilla, no la señal: con FFT de 4 096 a 48 kHz la
//       banda de 40 Hz mide 9.26 Hz y el bin 11.72, y los dos `ceil` del conteo entero caían en el mismo
//       número, así que la banda recibía CERO bins teniendo energía de sobra;
//   2 · un DESPLOME vertical hasta −42 en 90 y en 350 Hz, con barras de delta clavadas en +12 — la
//       referencia ahí medía entre −90 y −63 dBFS (o sea "medible" por el criterio del 57b) pero su
//       normalizada caía veinte decibeles por DEBAJO del piso del plot, y `yForNorm` la clampeaba al borde.
// ========================================================================================================

// ---------------------------------------------------------------------------------------- 1 · la rejilla
TEST_CASE ("telescope: ninguna banda de tercio de octava queda vacia por la rejilla de la FFT",
           "[telescope][tonal][bands]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kLoudness | telescope::kReference);

    pushPink (proc, 10.0, 0.3f);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.reference().read().liveValid; }, 30000));

    const auto& f = proc.reference().read();

    telescope::TonalBalanceLens lens (proc);
    lens.setSize (1025, 702);
    lens.pumpFrames (4);

    int measured = 0;
    float band40 = 0.0f, band31 = 0.0f, band50 = 0.0f;
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
    {
        const auto r = lens.readoutForBand (b);
        const bool has = f.liveBands[b] > telescope::SpectrumFrame::kFloorDb;
        if (has) ++measured;
        const double hz = telescope::kThirdOctaveHz[b];
        if (hz > 19.0 && hz < 51.0)
            std::printf ("TONAL[bins] %6.1f Hz  %.3f bins  ·  %+8.3f dBFS%s\n",
                         hz, r.bins, f.liveBands[b], has ? "" : "   SIN MEDICION");
        if (std::abs (hz - 40.0)   < 0.5) band40 = f.liveBands[b];
        if (std::abs (hz - 31.5)   < 0.5) band31 = f.liveBands[b];
        if (std::abs (hz - 50.0)   < 0.5) band50 = f.liveBands[b];
    }

    // La de 40 Hz contra la MEDIA de sus vecinas: en ruido rosa las tres están sobre la misma recta, así
    // que un hueco de rejilla se ve como un pozo y una medición sana no. σ del estimador ≈ 4.34/√(BW·T) =
    // 4.34/√(9.26·10) = 0.45 dB, o sea que ±2.5 dB es más de cinco sigmas: no puede salir por azar.
    const float vecinas = 0.5f * (band31 + band50);
    std::printf ("TONAL[bins] %d de %d bandas con medicion  ·  40 Hz = %+.3f dBFS  ·  media de 31.5 y 50 = "
                 "%+.3f  ·  diferencia %+.3f dB (tol 2.5, sigma 0.45)\n",
                 measured, ReferenceFrame::kNumBands, band40, vecinas, band40 - vecinas);

    CHECK (measured == ReferenceFrame::kNumBands);
    CHECK (std::abs (band40 - vecinas) <= 2.5f);
    // Y los bins fraccionarios son los que dice la cuenta (4 096 a 48 k → bin de 11.719 Hz).
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
    {
        const double hz = telescope::kThirdOctaveHz[b];
        const auto r = lens.readoutForBand (b);
        if (std::abs (hz - 20.0) < 0.5) CHECK (std::abs (r.bins - 0.39f) < 0.02f);
        if (std::abs (hz - 40.0) < 0.5) CHECK (std::abs (r.bins - 0.79f) < 0.02f);
        if (std::abs (hz - 50.0) < 0.5) CHECK (std::abs (r.bins - 0.99f) < 0.02f);
    }

    proc.releaseResources();
}

// ----------------------------------------------------------------------------------------- 2 · el piso
TEST_CASE ("telescope: una referencia por debajo del plot corta la curva y no se dibuja en el piso",
           "[telescope][tonal]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kLoudness | telescope::kReference);

    // LA REFERENCIA SINTÉTICA de la foto: un pad de 90 a 350 Hz a −20 dBFS más ruido blanco a −50 dBFS.
    // El ruido pone las bandas de fuera del pad alrededor de −83 dBFS crudo (pasan el −90 de "medible")
    // con normalizada ≈ −62 LU, o sea veinte decibeles por debajo del piso del plot de −42.
    std::vector<double> tones;
    for (double hz = 90.0; hz <= 350.0; hz *= std::pow (2.0, 1.0 / 12.0)) tones.push_back (hz);
    REQUIRE (tones.size() > 20);

    telescope::test::Pink hissA { telescope::test::kPinkSeedA };
    const auto pad = telescope::test::writeWav (
        "tonal_pad_bajo_el_piso.wav", kSr, 2, (juce::int64) (6.0 * kSr),
        [&tones, &hissA] (juce::int64 i)
        {
            double v = 0.0;
            for (size_t k = 0; k < tones.size(); ++k)
                v += std::sin (2.0 * juce::MathConstants<double>::pi * tones[k] * (double) i / kSr
                               + 0.9 * (double) k);
            v *= 0.1 / std::sqrt ((double) tones.size());
            const double hiss = 0.00316 * (double) hissA.next();   // ≈ −50 dBFS
            return std::pair<float, float> { (float) (v + hiss), (float) (v + hiss) };
        });

    proc.loadReference (pad);
    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.referenceBusy(); }, 60000));
    REQUIRE (proc.referenceError().isEmpty());

    pushPink (proc, 8.0, 0.3f);
    REQUIRE (telescope::test::waitUntil ([&] { const auto& f = proc.reference().read();
                                               return f.liveValid && f.refValid; }, 30000));

    telescope::TonalBalanceLens lens (proc);
    lens.setSize (1025, 702);
    lens.pumpFrames (6);
    const auto& f = proc.reference().read();

    int medibles = 0, dibujables = 0, fueraDeRango = 0, enElPiso = 0, comparables = 0, clavadas = 0;
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
    {
        const auto r = lens.readoutForBand (b);
        const bool medible = f.refValid && f.refNorm[b] > telescope::SpectrumFrame::kFloorDb;
        if (medible) ++medibles;
        if (r.refDrawable) ++dibujables;
        if (medible && ! r.refDrawable) ++fueraDeRango;
        if (r.comparable) ++comparables;

        // UN VÉRTICE EN LA FILA DEL PISO es lo que dibujaba la raya vertical: el valor con el que se arma
        // el vértice tiene que estar POR ENCIMA de kNormBottomDb siempre que la banda se dibuje.
        if (r.refDrawable  && f.refNorm[b]  <= TonalBalanceLens::kNormBottomDb) ++enElPiso;
        if (r.liveDrawable && f.liveNorm[b] <= TonalBalanceLens::kNormBottomDb) ++enElPiso;
        // Y ninguna barra de delta clavada en el tope en una banda que no es comparable.
        if (! r.comparable && std::abs (r.deltaDb) >= TonalBalanceLens::kDeltaSpanDb) ++clavadas;
    }

    std::printf ("TONAL[piso] referencia pad 90-350 Hz + ruido a -50 dBFS  ·  %d bandas medibles, %d "
                 "dibujables, %d fuera de rango (-42 LU)  ·  %d comparables  ·  vertices en la fila del "
                 "piso = %d  ·  barras clavadas en +-12 sin comparacion = %d\n",
                 medibles, dibujables, fueraDeRango, comparables, enElPiso, clavadas);

    CHECK (fueraDeRango > 0);    // el caso existe: si no, el test no está mirando lo que dice
    CHECK (enElPiso == 0);
    CHECK (clavadas == 0);
    CHECK (comparables > 0);     // y las bandas del pad SÍ se comparan

    // El readout de una banda fuera de rango lo DICE, y el de una banda del pad da los tres números.
    const auto texto = [&] (double hz)
    {
        int best = 0;
        for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
            if (std::abs (telescope::kThirdOctaveHz[b] - hz) < std::abs (telescope::kThirdOctaveHz[best] - hz))
                best = b;
        return best;
    };
    for (const double hz : { 50.0, 10000.0, 125.0 })
    {
        const auto r = lens.readoutForBand (texto (hz));
        std::printf ("TONAL[piso] %6.0f Hz  ·  bins %.2f  ·  refDibujable=%d  comparable=%d  "
                     "refNorm=%+.1f LU\n",
                     telescope::kThirdOctaveHz[texto (hz)], r.bins, (int) r.refDrawable,
                     (int) r.comparable, f.refNorm[texto (hz)]);
    }
    CHECK (! lens.readoutForBand (texto (10000.0)).comparable);
    CHECK (lens.readoutForBand (texto (125.0)).comparable);

    proc.releaseResources();
}
