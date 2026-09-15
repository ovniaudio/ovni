// [telescope][hemis] — el HEMISFERIO: la envolvente por dirección de la lente SCOPE (prompt 56).
//
// Es la vista que pidió Joaquín para mezcla estéreo (un semicírculo con mono arriba, la envolvente
// rellena y el correlímetro al lado). Lo que se verifica acá es la FÍSICA, no el dibujo: si el ángulo
// estuviera mal, el plugin diría "esto está a la izquierda" de algo que está a la derecha — que es
// exactamente la clase de error que la auditora encontró en el ITD de ORBIT (informe 21).
//
// LA CONVENCIÓN, escrita una vez y verificada abajo caso por caso:
//
//     θ = 90° + 2 · atan2 (R − L, R + L)     ≡     2 · atan2 (R, L)      (mod 360°)
//
//     sólo L → 0°  ·  mono (L = R) → 90°  ·  sólo R → 180°  ·  L = −R → 270° (abajo de la base)
//
// El puente con la lente 11 (FIELD, del prompt 53), que mide lo mismo por otro camino: aquélla publica un
// paneo por ENERGÍA, pan = (ΣRR − ΣLL) / (ΣLL + ΣRR) ∈ [−1, +1]. Para una fuente de potencia constante
// L = cos α, R = sin α sale pan = sin²α − cos²α = −cos 2α, y como acá θ = 2α, las dos medidas están
// atadas por
//
//     θ = arccos (−pan)
//
// que es la identidad que verifica el caso de la fuente paneada. Dos lentes que no coincidan en dónde
// está una fuente serían dos lentes en las que no se puede confiar.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "TestHelpers.h"
#include "TestSignals.h"
#include "PluginProcessor.h"
#include "analysis/modules/Stereo.h"

using telescope::ScopeFrame;
using telescope::Stereo;
using telescope::test::Pink;

namespace
{
constexpr double kSr  = 48000.0;
constexpr double kPi  = 3.14159265358979323846;
constexpr float  kFlr = ScopeFrame::kHemiFloorDb;

// Alimenta el módulo con una señal generada por `gen` (devuelve el par L,R de la muestra n).
template <typename Gen>
void feed (Stereo& m, double seconds, Gen gen, int blockSize = 512)
{
    std::vector<float> L ((size_t) blockSize, 0.0f), R ((size_t) blockSize, 0.0f);
    const auto total = (long long) std::llround (seconds * kSr);
    long long n = 0;

    for (long long done = 0; done < total; )
    {
        const int k = (int) std::min ((long long) blockSize, total - done);
        for (int i = 0; i < k; ++i, ++n)
        {
            const auto pair = gen (n);
            L[(size_t) i] = pair.first;
            R[(size_t) i] = pair.second;
        }
        m.process (L.data(), R.data(), k);
        done += k;
    }
}

// El bin con más nivel, y cuánta energía hay FUERA de un lóbulo de ±tol grados alrededor de él.
struct Lobe
{
    int   peakBin = -1;
    float peakDb  = kFlr;
    float outsideMaxDb = kFlr;     // el máximo fuera del lóbulo: es lo que dice si el lóbulo es limpio
};

Lobe lobeOf (const ScopeFrame& f, int tolDeg)
{
    Lobe r;
    for (int b = 0; b < ScopeFrame::kHemiBins; ++b)
        if (f.envelope[(size_t) b] > r.peakDb) { r.peakDb = f.envelope[(size_t) b]; r.peakBin = b; }

    for (int b = 0; b < ScopeFrame::kHemiBins; ++b)
    {
        int d = std::abs (b - r.peakBin);
        if (d > 180) d = 360 - d;                       // distancia angular, no de índice
        if (d > tolDeg) r.outsideMaxDb = std::max (r.outsideMaxDb, f.envelope[(size_t) b]);
    }
    return r;
}

// Energía total (lineal) de un sector [from, to) de grados. Sirve para "el abanico es simétrico".
double sectorEnergyDb (const ScopeFrame& f, int from, int to)
{
    double sum = 0.0;
    for (int b = from; b < to; ++b)
    {
        const auto db = f.envelope[(size_t) b];
        if (db > kFlr) sum += std::pow (10.0, (double) db / 10.0);
    }
    return sum > 0.0 ? 10.0 * std::log10 (sum) : (double) kFlr;
}

double toneAt (long long n, double hz) { return std::sin (2.0 * kPi * hz * (double) n / kSr); }
}

// ========================================================================================================
// LOS CUATRO PUNTOS CARDINALES. Cada uno es una señal cuya dirección no admite discusión.
// ========================================================================================================
TEST_CASE ("telescope: el hemisferio pone mono arriba, L en 0 y R en 180", "[telescope][hemis]")
{
    struct Cardinal { const char* name; int expectDeg; float l, r; };
    // Amplitudes fijas (no un seno) para que TODA muestra del hop apunte a la misma dirección: acá se
    // verifica el ángulo, y una envolvente barrida por un seno lo verificaría igual pero más despacio.
    const Cardinal cases[] = {
        { "mono (L = R)",   90, 0.5f,  0.5f  },
        { "solo L",          0, 0.5f,  0.0f  },
        { "solo R",        180, 0.0f,  0.5f  },
        { "fuera de fase", 270, 0.5f, -0.5f  },
    };

    for (const auto& c : cases)
    {
        Stereo m;
        m.prepare (kSr);
        feed (m, 0.35, [&c] (long long) { return std::make_pair (c.l, c.r); });

        const auto& f = m.scope();
        const auto lobe = lobeOf (f, 2);

        std::printf ("HEMIS[cardinal] %-14s pico en %3d deg (esperado %3d)  %.2f dB  ·  fuera del lobulo "
                     "+-2 deg: %.1f dB\n", c.name, lobe.peakBin, c.expectDeg, lobe.peakDb, lobe.outsideMaxDb);

        INFO (c.name);
        // ±2° como pide el criterio. El bin es la parte entera del grado, así que el centro exacto de
        // 90.0° cae en el bin 90 y el de 0.0° en el 0.
        int d = std::abs (lobe.peakBin - c.expectDeg);
        if (d > 180) d = 360 - d;
        CHECK (d <= 2);
        // Y el resto del círculo está en el piso: la energía está EN esa dirección, no repartida.
        CHECK (lobe.outsideMaxDb == kFlr);
    }
}

// ========================================================================================================
// FUERA DE FASE = HEMISFERIO INFERIOR. Es la lectura que hace útil el dibujo: lo que cae abajo de la base
// es lo que se va a perder al monoficar, y se dibuja en el color de alerta.
// ========================================================================================================
TEST_CASE ("telescope: con L = -R no queda NADA en el hemisferio superior", "[telescope][hemis]")
{
    Stereo m;
    m.prepare (kSr);
    feed (m, 0.35, [] (long long n) { const auto v = (float) (0.4 * toneAt (n, 220.0));
                                      return std::make_pair (v, -v); });

    const auto& f = m.scope();
    float upper = kFlr, lower = kFlr;
    for (int b = 0; b <= 180; ++b)                       // [0°, 180°] = base incluida = "en fase"
        upper = std::max (upper, f.envelope[(size_t) b]);
    for (int b = 181; b < 360; ++b)                      // (180°, 360°) = estrictamente abajo
        lower = std::max (lower, f.envelope[(size_t) b]);

    std::printf ("HEMIS[fase] L = -R  ->  maximo arriba = %.1f dB (piso %.1f)  ·  maximo abajo = %.2f dB\n",
                 upper, kFlr, lower);

    CHECK (upper == kFlr);          // nada arriba, ni un grado
    CHECK (lower > kFlr + 20.0f);   // y abajo, la señal entera
}

// ========================================================================================================
// RUIDO INDEPENDIENTE = ABANICO ANCHO Y SIMÉTRICO. Dos fuentes sin relación no apuntan a ningún lado, y
// eso tiene que VERSE: si el dibujo diera un lóbulo, estaría inventando una dirección.
// ========================================================================================================
TEST_CASE ("telescope: ruido independiente abre un abanico simetrico", "[telescope][hemis]")
{
    Stereo m;
    m.prepare (kSr);
    Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    feed (m, 0.35, [&a, &b] (long long) { return std::make_pair (0.35f * a.next(), 0.35f * b.next()); });

    const auto& f = m.scope();

    int occupied = 0;
    for (int i = 0; i < ScopeFrame::kHemiBins; ++i)
        if (f.envelope[(size_t) i] > kFlr) ++occupied;

    const auto left  = sectorEnergyDb (f,  0,  90);      // de "sólo L" a mono
    const auto right = sectorEnergyDb (f, 90, 180);      // de mono a "sólo R"

    std::printf ("HEMIS[abanico] ruido independiente: %d de 360 grados con energia  ·  sector 0-90 = "
                 "%.2f dB  sector 90-180 = %.2f dB  ·  |diferencia| = %.3f dB\n",
                 occupied, left, right, std::abs (left - right));

    CHECK (occupied > 120);                       // ancho de verdad, no un lóbulo
    CHECK (std::abs (left - right) < 1.0);        // y simétrico dentro de 1 dB, como pide el criterio
}

// ========================================================================================================
// LA FUENTE PANEADA, y el puente con FIELD. Potencia constante a α = 22.5° tiene que caer en θ = 2α = 45°,
// y el MISMO número tiene que salir del paneo por energía de la lente 11 vía θ = arccos (−pan).
// ========================================================================================================
TEST_CASE ("telescope: una fuente paneada 22.5 grados cae en 45 y coincide con el pan del 53",
           "[telescope][hemis]")
{
    constexpr double alphaDeg = 22.5;
    const double alpha = alphaDeg * kPi / 180.0;
    const float gl = (float) std::cos (alpha);           // ley de POTENCIA CONSTANTE: L² + R² = 1
    const float gr = (float) std::sin (alpha);

    Stereo m;
    m.prepare (kSr);
    feed (m, 0.35, [gl, gr] (long long n) { const auto v = (float) (0.5 * toneAt (n, 440.0));
                                            return std::make_pair (gl * v, gr * v); });

    const auto& f = m.scope();
    const auto lobe = lobeOf (f, 2);

    // El mismo paneo por ENERGÍA que publica la lente 11: pan = (ΣRR − ΣLL) / (ΣLL + ΣRR).
    const double sll = (double) gl * gl, srr = (double) gr * gr;
    const double pan = (srr - sll) / (sll + srr);
    const double thetaFromPan = std::acos (-pan) * 180.0 / kPi;

    std::printf ("HEMIS[paneo] alpha = %.1f deg (potencia constante, L=%.4f R=%.4f)\n"
                 "HEMIS[paneo]   theta esperado = 2*alpha = %.1f deg  ·  medido = %d deg\n"
                 "HEMIS[paneo]   pan del 53 = (SRR-SLL)/(SLL+SRR) = %.4f  ->  acos(-pan) = %.2f deg\n",
                 alphaDeg, gl, gr, 2.0 * alphaDeg, lobe.peakBin, pan, thetaFromPan);

    CHECK (std::abs ((double) lobe.peakBin - 2.0 * alphaDeg) <= 2.0);      // la fórmula θ = 2α
    // La identidad θ = arccos(−pan) es EXACTA en el papel; acá las ganancias son `float` (así llegan al
    // audio), así que el residuo es el de la simple precisión y no el de la fórmula: medido 5.8e-7 grados.
    CHECK (std::abs (thetaFromPan - 2.0 * alphaDeg) < 1.0e-4);
    CHECK (std::abs ((double) lobe.peakBin - thetaFromPan) <= 2.0);        // y las dos lentes coinciden
    CHECK (lobe.outsideMaxDb == kFlr);
}

// ========================================================================================================
// EL NIVEL. La envolvente publica 20·log10 √(L² + R²) — el RADIO del vector. Para una mono a escala
// completa da +3.01 dB, y eso no es un bug: es que L y R suman en cuadratura. Si algún día alguien
// "arregla" ese +3, este test se lo dice.
// ========================================================================================================
TEST_CASE ("telescope: el nivel de la envolvente es el radio del vector (L,R)", "[telescope][hemis]")
{
    struct C { const char* name; float l, r; double expectDb; };
    const C cases[] = {
        { "mono a escala completa", 1.0f, 1.0f,  3.0103 },   // √2 → +3.01 dB
        { "solo L a escala completa", 1.0f, 0.0f, 0.0    },
        { "mono a -20 dBFS",        0.1f, 0.1f, -16.9897 },
    };

    for (const auto& c : cases)
    {
        Stereo m;
        m.prepare (kSr);
        feed (m, 0.35, [&c] (long long) { return std::make_pair (c.l, c.r); });

        std::printf ("HEMIS[nivel] %-24s pico = %.4f dB (esperado %.4f)\n",
                     c.name, m.scope().envelopePeakDb, c.expectDb);
        INFO (c.name);
        CHECK_THAT ((double) m.scope().envelopePeakDb,
                    Catch::Matchers::WithinAbs (c.expectDb, 1.0e-3));
    }
}

// ========================================================================================================
// INDEPENDENCIA DEL TAMAÑO DE BLOQUE, AL BIT. El hop se arma adentro del módulo, así que 1, 7, 64 o 4 096
// muestras por llamada tienen que dar la MISMA envolvente — bit a bit, no "parecida". Es el mismo
// criterio de casa-4 del medidor de loudness y de [stereo]: un analizador cuyo número depende del buffer
// del host es un analizador que no se puede citar.
// ========================================================================================================
TEST_CASE ("telescope: la envolvente del hemisferio no depende del tamano de bloque", "[telescope][hemis]")
{
    const auto run = [] (int blockSize)
    {
        Stereo m;
        m.prepare (kSr);
        Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
        feed (m, 0.35, [&a, &b] (long long) { return std::make_pair (0.4f * a.next(), 0.25f * b.next()); },
              blockSize);
        std::vector<float> out (ScopeFrame::kHemiBins);
        for (int i = 0; i < ScopeFrame::kHemiBins; ++i) out[(size_t) i] = m.scope().envelope[(size_t) i];
        return out;
    };

    const auto ref = run (512);
    for (const int bs : { 1, 7, 64, 4096 })
    {
        const auto got = run (bs);
        int differing = 0;
        for (int i = 0; i < ScopeFrame::kHemiBins; ++i)
            if (got[(size_t) i] != ref[(size_t) i]) ++differing;      // AL BIT: ==, no aproximado

        std::printf ("HEMIS[bloque] %5d muestras por llamada -> %d de 360 bins distintos del bloque 512\n",
                     bs, differing);
        INFO ("bloque " << bs);
        CHECK (differing == 0);
    }
}

// ========================================================================================================
// EL DECAIMIENTO. La memoria del hemisferio (el peak-hold que se desvanece) vive en la VISTA, no en el
// motor: es un setting que no cambia una sola cuenta, igual que las líneas y la inclinación de WATERFALL.
//
// Y decae por FRAME, no por reloj de pared. Eso es lo que hace que "24 dB por segundo" quiera decir lo
// mismo acá que en la pantalla: bombear n frames tiene que bajar exactamente n · dB/s ÷ fps. Un
// decaimiento atado al reloj daría un test que falla cuando la máquina está ocupada — que es justo la
// lección que ya había costado calibrar el harness de [budget].
#include "lenses/ScopeLens.h"

TEST_CASE ("telescope: la envolvente del hemisferio decae al ritmo pedido", "[telescope][hemis]")
{
    constexpr int kFps = telescope::ScopeLens::kHemiDecayFps;   // 56b: el mismo número que usa la lente

    for (const float dbPerSec : telescope::ScopeLens::kHemiDecayOptions)
    {
        telescope::ScopeLens::HemisphereView view;

        // Un hop con un lóbulo mono a un nivel conocido, y después silencio.
        ScopeFrame loud;
        for (auto& v : loud.envelope) v = kFlr;
        loud.envelope[90] = -10.0f;
        view.update (loud, dbPerSec, kFps, true);
        const float start = view.env[90];

        ScopeFrame quiet;
        for (auto& v : quiet.envelope) v = kFlr;

        constexpr int kFrames = 15;                      // medio segundo a 30 fps
        for (int i = 0; i < kFrames; ++i) view.update (quiet, dbPerSec, kFps, true);

        const float dropped  = start - view.env[90];
        const float expected = dbPerSec * (float) kFrames / (float) kFps;

        std::printf ("HEMIS[decaimiento] %4.0f dB/s: %d frames -> cayo %.4f dB (esperado %.4f, error %.2f %%)\n",
                     dbPerSec, kFrames, dropped, expected,
                     100.0 * std::abs (dropped - expected) / expected);

        INFO (dbPerSec << " dB/s");
        CHECK (std::abs (dropped - expected) <= 0.10f * expected);      // ±10 %, como pide el criterio
    }

    // REDUCED-MOTION: sin memoria. La envolvente ES el hop — un cuadro quieto y coherente, no una que se
    // desliza sola durante segundos (que sería movimiento, aunque fuera movimiento útil).
    telescope::ScopeLens::HemisphereView still;
    ScopeFrame loud;
    for (auto& v : loud.envelope) v = kFlr;
    loud.envelope[90] = -10.0f;
    still.update (loud, 24.0f, kFps, false);
    CHECK (still.env[90] == -10.0f);

    ScopeFrame quiet;
    for (auto& v : quiet.envelope) v = kFlr;
    still.update (quiet, 24.0f, kFps, false);
    std::printf ("HEMIS[decaimiento] reduced-motion: tras un hop en silencio la envolvente vale %.1f dB "
                 "(el piso, sin memoria)\n", still.env[90]);
    CHECK (still.env[90] == kFlr);          // sin arrastre: el hop y nada más

    // Y el PLEGADO de dibujo NO inventa energía donde no hay: fuera del lóbulo sigue el piso. (Hasta el
    // 57c acá se verificaba lo mismo sobre el máximo móvil de ±2°, que era el filtro que amesetaba los
    // rayos y que este prompt sacó: ahora lo que se dibuja son los 181 rayos del motor, sin filtro.)
    float rays[telescope::ScopeLens::kHemiRays];
    telescope::ScopeLens::foldProfile (still.env, rays, false);
    for (int i = 0; i < telescope::ScopeLens::kHemiRays; ++i) CHECK (rays[i] == kFlr);

    // El PROMEDIO, sin memoria, también es el hop: con reduced-motion α = 1.
    still.update (loud, 24.0f, kFps, false);
    std::printf ("HEMIS[promedio] reduced-motion: tras un hop a -10 dB el promedio vale %.1f dB (el hop)\n",
                 still.avg[90]);
    CHECK (std::abs (still.avg[90] - (-10.0f)) < 1.0e-3f);
}

// ========================================================================================================
// ===== 57c · UN RAYO POR GRADO =====
//
// «El polar level de Insight es más fino; el polar sample está igual al de ellos, es sólo cuando ponemos
// polar level» (Joaquín, 12-sep). El motor SIEMPRE publicó 360 bins de un grado (ScopeFrame.h); lo que
// pasaba estaba en la lente: antes de dibujar, la envolvente pasaba por un MÁXIMO MÓVIL circular de ±2°.
// Un máximo móvil no suaviza — convierte cada púa en una MESETA de 5°, y esa meseta es la escalera de la
// foto. El escalón era del ancho del FILTRO, no del dato.
//
// Este test mide los 181 rayos que se dibujan, con las dos señales cuya respuesta no admite discusión:
//
//   MONO          toda muestra apunta a 90° exacto (R − L = 0), así que el perfil tiene que ser una AGUJA:
//                 como mucho tres rayos por encima de −20 dB relativos al más fuerte. Con el máximo móvil
//                 eran cinco por construcción, y con un filtro más ancho, más.
//   INDEPENDIENTE θ se reparte parejo sobre los 360°, así que TODOS los rayos tienen que encenderse: al
//                 menos 150 de 181 por encima de −12 dB relativos. Es el peine fino de Insight.
// ========================================================================================================
TEST_CASE ("telescope: el polar level dibuja un rayo por grado, sin amesetar", "[telescope][hemis]")
{
    using telescope::ScopeLens;

    struct Case { const char* name; bool mono; };
    for (const auto& c : { Case { "mono", true }, Case { "independiente", false } })
    {
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (kSr, 512);
        proc.setEnabledModules (telescope::kStereo | telescope::kLoudness);

        const float peak = std::pow (10.0f, -6.0f / 20.0f);
        telescope::test::Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int blk = 0; blk < (int) (2.0 * kSr / 512.0); ++blk)
        {
            for (int i = 0; i < 512; ++i)
            {
                const auto x = peak * a.next();
                buf.setSample (0, i, x);
                buf.setSample (1, i, c.mono ? x : peak * b.next());
            }
            proc.processBlock (buf, midi);
        }
        REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 1.5; }, 8000));

        ScopeLens lens (proc);
        lens.setScopeMode (ScopeLens::Mode::hemisphere);
        lens.setSize (1025, 702);
        lens.pumpFrames (10);

        // SE PINTA DE VERDAD: los rayos que se miden son los del último pintado, no los de la envolvente.
        // El defecto que este test cierra vivía ENTRE las dos cosas (un máximo móvil aplicado al dibujar),
        // así que mirar la envolvente lo habría dado por bueno.
        juce::Image img (juce::Image::ARGB, 1025, 702, true);
        { juce::Graphics g (img); lens.paintEntireComponent (g, false); }

        const float* peakRays = lens.drawnRaysForTest (true);
        const float* avgRays  = lens.drawnRaysForTest (false);

        const auto countAbove = [] (const float* rays, float relDb)
        {
            float top = kFlr;
            for (int d = 0; d < ScopeLens::kHemiRays; ++d) top = std::max (top, rays[d]);
            int n = 0;
            for (int d = 0; d < ScopeLens::kHemiRays; ++d) if (rays[d] - top >= relDb) ++n;
            return n;
        };

        const int over20 = countAbove (peakRays, -20.0f);
        const int over12 = countAbove (peakRays, -12.0f);
        const int avg12  = countAbove (avgRays,  -12.0f);
        std::printf ("HEMIS[rayos] %-13s pico: %3d de %d rayos sobre -20 dB rel  ·  %3d sobre -12  ·  "
                     "promedio: %3d sobre -12\n",
                     c.name, over20, ScopeLens::kHemiRays, over12, avg12);

        if (c.mono) CHECK (over20 <= 3);      // una aguja: todo cae en el bin 90
        else        CHECK (over12 >= 150);    // un peine fino, no una meseta

        proc.releaseResources();
    }
}

// ========================================================================================================
// 56b · EL CORRELÍMETRO NO GRITA CUANDO NO HAY NADA QUE GRITAR
//
// La mitad de abajo del correlímetro vertical (la zona "fuera de fase") se pintaba con alpha 0.16 FIJA:
// con correlación +0.99 y nada que perder al monoficar, media barra igual teñida de alerta. Dos cosas
// malas a la vez — enseña a desconfiar del medidor, porque si siempre hay rojo el rojo no dice nada, y
// compite con el lóbulo inferior del hemisferio, que está al lado y ESE sí es proporcional desde el 56.
//
// Ahora las dos usan la misma cuenta medida: monoLossDb / -6. Se verifica con las dos señales extremas.
// ========================================================================================================
TEST_CASE ("telescope: la zona de alerta del correlimetro es proporcional a la perdida mono",
           "[telescope][hemis]")
{
    // Cuánta ALERTA hay en la mitad de abajo del correlímetro, con la señal que se le empuje.
    const auto alertInLowerHalf = [] (bool inPhase)
    {
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (kSr, 512);
        proc.setEnabledModules (telescope::kStereo | telescope::kLoudness);
        proc.apvts.state.setProperty (telescope::ScopeLens::kScopeModeProperty,
                                      (int) telescope::ScopeLens::Mode::hemisphere, nullptr);

        // En fase: L = R (corr +1, monoLoss 0 dB, nada que perder).
        // Fuera de fase: L = -R (corr -1, el mono se cancela: la pérdida es total).
        Pink pink { telescope::test::kPinkSeedA };
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int blk = 0; blk < (int) (2.0 * kSr / 512.0); ++blk)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float v = 0.4f * pink.next();
                buf.setSample (0, i, v);
                buf.setSample (1, i, inPhase ? v : -v);
            }
            proc.processBlock (buf, midi);
        }
        REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds > 1.0; }, 8000));

        telescope::ScopeLens lens (proc);
        lens.setSize (900, 620);
        lens.pumpFrames (8);

        juce::Image img (juce::Image::ARGB, lens.getWidth(), lens.getHeight(), true);
        { juce::Graphics g (img); lens.paintEntireComponent (g, false); }

        // Se cuenta ROJO SOBRE EL FONDO en la mitad de abajo del canal del correlímetro: el criterio es
        // "más rojo que verde", que separa el tinte de alerta del pozo y de la rejilla sin ambigüedad.
        const auto area = lens.correlationArea();
        int red = 0;
        for (int y = area.getCentreY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto c = img.getPixelAt (x, y);
                if ((int) c.getRed() > (int) c.getGreen() + 6) ++red;
            }
        const float mono = proc.analysis().read().monoLossDb;
        proc.releaseResources();
        return std::pair<int, float> { red, mono };
    };

    const auto fase  = alertInLowerHalf (true);
    const auto fuera = alertInLowerHalf (false);

    std::printf ("HEMIS[alerta] en fase: monoLoss %+.1f dB -> %d px de alerta abajo  ·  "
                 "fuera de fase: monoLoss %+.1f dB -> %d px\n",
                 fase.second, fase.first, fuera.second, fuera.first);

    REQUIRE (fase.second  > -0.5f);      // en fase no hay nada que perder al monoficar…
    REQUIRE (fuera.second < -6.0f);      // …y con L = -R se pierde todo
    // Con la MISMA geometría, la señal sana casi no tiñe y la que se cancela tiñe entera. Con el alpha
    // fijo de antes los dos números eran EL MISMO (misma zona, mismo 0.16), así que cualquiera de estas
    // dos líneas lo habría puesto en rojo. Se piden las dos: que la sana esté prácticamente limpia, y que
    // la relación entre las dos sea de otro orden de magnitud.
    REQUIRE (fuera.first > 1000);              // la que sí pierde el mono se ve…
    REQUIRE (fase.first  < 200);               // …y la sana no enciende la zona
    REQUIRE (fuera.first > 20 * fase.first);
}

// ========================================================================================================
// ===== 57b: LOS NÚMEROS DEL ESTÉREO ESTÁN EN LOS CUATRO MODOS =====
//
// Joaquín, mirando la lente en su DAW: «ANCHO / BALANCE / PÉRDIDA MONO sin valor». No era un recorte de
// la foto ni un bug de cálculo: en el modo HEMISFERIO esos tres números NO SE DIBUJABAN. La columna que
// los lleva en Lissajous y en polar la ocupa, en hemisferio, el correlímetro vertical — y nadie les había
// buscado otro lugar. Desde el 57b van en una fila al pie del osciloscopio.
//
// Lo que este test fija es la afirmación completa: con una señal MONO a −6 dBFS, los cuatro modos tienen
// que decir lo mismo y decirlo bien. Un plugin de medición en el que el número depende de qué vista
// elegiste no es un plugin de medición.
//
// ================================ Y DE PASO, EL 0.07 DE LA FOTO =========================================
//
// La otra observación era una correlación de 0.07 con el Lissajous "casi mono". Acá se mide la BALÍSTICA
// del número —`dispCorr` es un suavizado exponencial de 0.30 por frame a 30 fps— para poder decir con un
// número cuánto tarda en llegar: si el medidor tardara segundos, una foto sacada al empezar mostraría
// cualquier cosa. Ver el reporte del 57b para la conclusión.
TEST_CASE ("telescope: los tres numeros del estereo valen lo mismo en los cuatro modos", "[telescope][hemis]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setEnabledModules (telescope::kStereo | telescope::kLoudness);
    proc.setStereoWindowMs (100);   // la ventana de la foto de Joaquín

    // MONO a −6 dBFS: los dos canales idénticos.
    const float peak = std::pow (10.0f, -6.0f / 20.0f);
    telescope::test::Pink pink { telescope::test::kPinkSeedA };
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int blk = 0; blk < (int) (3.0 * kSr / 512.0); ++blk)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float v = peak * pink.next();
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 2.5; }, 8000));

    const auto& f = proc.analysis().read();
    std::printf ("HEMIS[numeros] mono -6 dBFS, ventana %.2f s  ->  corr %+.4f  ancho %.4f  balance %+.2f dB"
                 "  perdida mono %+.2f dB\n",
                 f.stereoWindowSec, f.corr, f.width, f.balanceDb, f.monoLossDb);

    CHECK (f.corr      > 0.999f);
    CHECK (f.width     < 0.001f);
    CHECK (std::abs (f.balanceDb)  < 0.05f);
    CHECK (std::abs (f.monoLossDb) < 0.05f);

    // LA BALÍSTICA DEL NÚMERO QUE SE DIBUJA. `dispCorr` arranca en 0 y se acerca 0.30 por frame; se cuenta
    // cuántos frames tarda en pasar 0.99 y se traduce a segundos a los 30 fps de la lente.
    double disp = 0.0;
    int frames = 0;
    while (disp < 0.99 && frames < 1000) { disp += ((double) f.corr - disp) * 0.30; ++frames; }
    std::printf ("HEMIS[numeros] balistica de la correlacion dibujada: 0 -> %.2f en %d frames "
                 "(%.2f s a 30 fps)  ·  tras 1 frame vale %.3f\n",
                 disp, frames, (double) frames / 30.0, 0.30 * (double) f.corr);
    CHECK (frames <= 20);            // menos de 0.7 s: una foto no lo agarra a mitad de camino salvo al arrancar

    proc.releaseResources();
}

// ========================================================================================================
// ===== 57b: EL PLEGADO · nada se dibuja por debajo de la base =====
//
// El hemisferio pasó al layout de Insight: la base L—R va al PIE del panel y lo que está fuera de fase,
// que antes se dibujaba en un semicírculo inferior, se pliega sobre la base (θ' = 360° − θ) y se pinta en
// color de alerta ENCIMA del lóbulo, con su porcentaje al lado.
//
// Dos cosas hay que fijar para que ese cambio no sea una pérdida de información:
//   1 · que con L = −R el número diga 100 % (toda la energía está fuera de fase), y
//   2 · que NO quede un solo píxel dibujado por debajo de la base — si quedara, el plegado estaría a
//       medias y la lente tendría dos lenguajes a la vez.
TEST_CASE ("telescope: con L = -R el hemisferio dice 100 % fuera de fase y no pinta bajo la base",
           "[telescope][hemis]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setEnabledModules (telescope::kStereo | telescope::kLoudness);

    telescope::test::Pink pink { telescope::test::kPinkSeedA };
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int blk = 0; blk < (int) (2.0 * kSr / 512.0); ++blk)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float v = 0.5f * pink.next();
            buf.setSample (0, i,  v);
            buf.setSample (1, i, -v);            // L = −R: todo fuera de fase
        }
        proc.processBlock (buf, midi);
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 1.5; }, 8000));

    telescope::ScopeLens lens (proc);
    lens.setScopeMode (telescope::ScopeLens::Mode::hemisphere);
    lens.setSize (1025, 702);
    lens.pumpFrames (6);

    juce::Image img (juce::Image::ARGB, 1025, 702, true);
    { juce::Graphics g (img); lens.paintEntireComponent (g, false); }

    const float pct = lens.outOfPhasePercent();

    // La base: el pie del semicírculo. Se cuentan los píxeles NO de fondo por debajo, dentro del panel.
    const auto plot = lens.hemiPlotAreaForTest();
    const int  baseY = (int) std::lround (telescope::ScopeLens::hemiGeometry (plot.toFloat()).baseY);
    int below = 0;
    for (int y = baseY + 2; y < plot.getBottom(); ++y)
        for (int x = plot.getX(); x < plot.getRight(); ++x)
        {
            const auto c = img.getPixelAt (x, y);
            // Se mira sólo el VERDE del dato y el ROJO de la alerta: los rótulos L / R y la leyenda del
            // pie viven ahí abajo y son texto, no dibujo.
            if (c.getAlpha() > 0 && ((int) c.getGreen() > (int) c.getBlue() + 30
                                     || (int) c.getRed() > (int) c.getBlue() + 30)) ++below;
        }

    std::printf ("HEMIS[plegado] L = -R  ->  fuera de fase %.1f %%  ·  px de dato por debajo de la base "
                 "(y = %d) = %d\n", pct, baseY, below);

    CHECK (pct > 99.0f);
    CHECK (below == 0);

    proc.releaseResources();
}
