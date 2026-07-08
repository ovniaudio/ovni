// Test [clipscan][nebula] — HERRAMIENTA de diagnóstico: ¿DÓNDE nace el clip de NÉBULA? El oído de Joaquín
// SIGUE oyendo clip/crackle con transientes agresivas pese al ceiling 0.80. En vez de adivinar, MEDIMOS:
// corremos material agresivo por el nebula::NebulaProcessor REAL e imprimimos un MAPA de la cadena etapa
// por etapa, con sample-peak y true-peak (oversample 4× Catmull-Rom) en cada punto, + cuánto "trabaja" el
// limiter (reducción máx en dB + % de samples reducidos = proxy de aspereza, ver
// references/anti-click-clip-truepeak.md §4/§8).
//
// La cadena (mapeada): inGain (chasis) → fdn.process [mezcla dry+wet; el limiter actúa SÓLO sobre el WET]
//                      → bass-mono (chasis, si monoSafe) → outGain (chasis) → meter.
// Etapas del mapa:
//   (1) dry post-inGain        — la señal DIRECTA del usuario que entra al mix (variante mix=0 → out=dry).
//   (2) wet del FDN PRE-limiter — la cola difusa cruda (variante mix=1 + limiter OFF → out=wet crudo).
//   (3) total dry+wet PRE-limiter (variante mix real + limiter OFF → out=dry+wet sin limitar).
//   (4) salida POST-limiter     (mix real + limiter ON → out=dry + wet LIMITADO = salida del motor).
//   (5) salida final POST-chasis (la corrida real completa: + bass-mono + outGain).
//
// Cada etapa se AÍSLA corriendo el processor en una configuración distinta y capturando la salida final
// (lo que el doc llama "medir variantes": con/sin limiter, dry-only via mix=0, wet-only). El MeterProbe del
// motor (picos internos + trabajo del limiter) corrobora desde adentro. Es DIAGNÓSTICO + GATE: imprime el
// mapa y REQUIRE lo crítico del NUEVO diseño (dry transparente: el límite es sobre el WET, no sobre el dry).
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include "PluginProcessor.h"
#include "engines/fdn/FdnReverb.h"

namespace
{
// ── Material agresivo (lo PEOR para un reverb): tres generadores seleccionables ──────────────────────
// Ruido blanco determinístico (LCG con semilla fija → reproducible) en [−1,1].
struct White
{
    std::uint32_t s = 0xCAFEF00Du;
    explicit White (std::uint32_t seed) : s (seed) {}
    float next() { s = s * 1664525u + 1013904223u; return ((float) (s >> 9) * (1.0f / 4194304.0f)) - 1.0f; }
};

enum class Material { TransientBursts, SynthPercHF, KickHat };

// Genera una muestra global g (mono full-scale) del material elegido. Determinístico (depende sólo de g).
float materialSample (Material m, long g, double sr)
{
    switch (m)
    {
        case Material::TransientBursts:
        {
            // Ráfagas densas de impulsos full-scale ±1 (energía de banda ancha, el clásico click de vinilo)
            // cada ~0.16 s, con un click HF de ruido decayendo. El peor caso inter-sample.
            const int   period = (int) std::lround (0.16 * sr);
            const int   hitLen = (int) std::lround (0.005 * sr);
            const int   phase  = (int) (g % period);
            if (phase >= hitLen) return 0.0f;
            const long  hitIdx = g / period;
            White hw (0x9E3779B9u ^ (std::uint32_t) (hitIdx * 2654435761u));
            for (int k = 0; k < phase; ++k) hw.next();
            float x = 0.0f;
            if (phase < 3) x = (phase % 2 == 0) ? 1.0f : -1.0f;          // tren de impulsos ±1
            const float env = std::exp (-5.0f * (float) phase / (float) hitLen);
            x += 0.9f * env * hw.next();
            return juce::jlimit (-1.0f, 1.0f, x);
        }
        case Material::SynthPercHF:
        {
            // Percusión sintética con MUCHO HF: golpes de ruido pasado por un HP (diferenciador simple) con
            // envolvente de ataque rapidísimo → transitorios agudos densos (cada ~0.12 s).
            const int   period = (int) std::lround (0.12 * sr);
            const int   hitLen = (int) std::lround (0.03 * sr);
            const int   phase  = (int) (g % period);
            if (phase >= hitLen) return 0.0f;
            const long  hitIdx = g / period;
            // Ruido HF: avanzamos el LCG hasta la muestra del golpe y tomamos (actual − anterior) =
            // diferenciador de 1-tap → realza agudos (transitorio áspero). Determinístico por golpe.
            White hw (0x1234567u ^ (std::uint32_t) (hitIdx * 40503u));
            float prev = 0.0f, cur = 0.0f;
            for (int k = 0; k <= phase; ++k) { prev = cur; cur = hw.next(); }
            const float hp  = cur - prev;                                 // diferenciador → realza agudos
            const float env = std::exp (-9.0f * (float) phase / (float) hitLen);
            return juce::jlimit (-1.0f, 1.0f, 0.98f * env * hp * 1.8f);
        }
        case Material::KickHat:
        default:
        {
            // Kick (seno grave con pitch-drop + env) + hat (ruido HF corto) sincopados. Mezcla transiente
            // grave + brillo: pone a prueba el dry de banda ancha.
            const int   beat   = (int) std::lround (0.25 * sr);          // negra a 240? no: 0.25s → 4/seg
            const int   phaseB = (int) (g % beat);
            float out = 0.0f;
            // Kick en el down-beat.
            {
                const float t   = (float) phaseB / (float) sr;
                const float env = std::exp (-22.0f * t);
                const float f   = 110.0f * std::exp (-30.0f * t) + 45.0f; // pitch-drop
                out += 0.95f * env * std::sin (juce::MathConstants<float>::twoPi * f * t);
            }
            // Hat en el off-beat (medio compás).
            const int phaseH = (int) ((g + beat / 2) % beat);
            if (phaseH < (int) std::lround (0.02 * sr))
            {
                White hh (0xBEEFu ^ (std::uint32_t) ((g / beat) * 2246822519u));
                float a = 0.0f; for (int k = 0; k < 3; ++k) a += hh.next();   // ruido
                const float env = std::exp (-60.0f * (float) phaseH / (float) sr);
                out += 0.55f * env * (a - 1.5f * hh.next());
            }
            return juce::jlimit (-1.0f, 1.0f, out);
        }
    }
}

// ── True-peak por oversample 4× (Catmull-Rom) — references/...truepeak.md §3 ─────────────────────────
float truePeakOf (const std::vector<float>& x)
{
    const int n = (int) x.size();
    if (n < 4) return 0.0f;
    constexpr int OS = 4;
    auto at = [&] (int i) -> float { return x[(size_t) juce::jlimit (0, n - 1, i)]; };
    float tp = 0.0f;
    for (int i = 1; i < n - 2; ++i)
    {
        const float P0 = at (i - 1), P1 = at (i), P2 = at (i + 1), P3 = at (i + 2);
        for (int s = 0; s < OS; ++s)
        {
            const float t = (float) s / (float) OS;
            const float v = 0.5f * ((2.0f * P1) + (-P0 + P2) * t
                          + (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * t * t
                          + (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * t * t * t);
            tp = juce::jmax (tp, std::abs (v));
        }
    }
    return tp;
}

float samplePeakOf (const std::vector<float>& x)
{
    float pk = 0.0f; for (float v : x) pk = juce::jmax (pk, std::abs (v)); return pk;
}

// Picos (sample + true) de una etapa = corre el material por el processor en cierta config y devuelve la
// salida final concatenada (de L, el de mayor energía suele dar el peor true-peak; medimos ambos canales).
struct StagePk { float sample = 0.0f; float trueP = 0.0f; };

// Config de una corrida: mix, y si el limiter del motor está activo (para aislar PRE/POST limiter).
struct RunCfg { float size, decay, tone, breath, mix; bool limiterOn; bool attachProbe; };

// Corre el material por el NebulaProcessor REAL y captura la salida final (etapa 5 efectiva de esa config).
// Si attachProbe, devuelve también el MeterProbe lleno (picos internos + trabajo del limiter).
StagePk runStage (Material mat, double sr, int block, double seconds, const RunCfg& cfg,
                  ovni::engines::FdnReverb::MeterProbe* outProbe = nullptr)
{
    nebula::NebulaProcessor proc;
    auto setN = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
    setN ("size", cfg.size); setN ("decay", cfg.decay); setN ("tone", cfg.tone);
    setN ("breath", cfg.breath); setN ("mix", cfg.mix);
    proc.prepareToPlay (sr, block);

    // Diagnóstico: togglear el limiter del motor + enganchar el probe (NO afecta el audio del usuario).
    proc.engineForTest().setLimiterEnabled (cfg.limiterOn);
    ovni::engines::FdnReverb::MeterProbe probe;
    if (cfg.attachProbe) proc.engineForTest().setProbe (&probe);

    const long total = (long) std::llround (seconds * sr);
    std::vector<float> allL, allR;
    allL.reserve ((size_t) total + (size_t) block);
    allR.reserve ((size_t) total + (size_t) block);

    long g = 0;
    while (g < total)
    {
        const int n = (int) juce::jmin ((long) block, total - g);
        juce::AudioBuffer<float> buf (2, n);
        juce::MidiBuffer midi;
        for (int i = 0; i < n; ++i)
        {
            const float x = materialSample (mat, g + i, sr);
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        proc.processBlock (buf, midi);
        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        for (int i = 0; i < n; ++i) { allL.push_back (L[i]); allR.push_back (R[i]); }
        g += n;
    }
    proc.engineForTest().setProbe (nullptr);   // soltar el probe antes de que el processor muera

    StagePk pk;
    pk.sample = juce::jmax (samplePeakOf (allL), samplePeakOf (allR));
    pk.trueP  = juce::jmax (truePeakOf (allL),  truePeakOf (allR));
    if (outProbe) *outProbe = probe;
    return pk;
}

// Imprime una fila del mapa: las 5 etapas (sample/true) + trabajo del limiter, para un caso (material+mix+decay).
struct MapRow
{
    std::string label;
    StagePk s1Dry, s2WetPre, s3TotalPre, s4Post, s5Final;
    float  limMaxRedDb = 0.0f;
    double limWorkPct  = 0.0;
};

void printMapHeader()
{
    std::printf ("\n");
    std::printf ("CLIPSCAN MAPA DE LA CADENA (sample-peak / true-peak por etapa; lim = trabajo del limiter)\n");
    std::printf ("  etapas: (1)dry post-inGain  (2)wet PRE-lim  (3)total PRE-lim  (4)POST-lim  (5)final post-chasis\n");
    std::printf ("  %-22s | %-13s | %-13s | %-13s | %-13s | %-13s | %-16s\n",
                 "caso", "(1)dry", "(2)wetPRE", "(3)totalPRE", "(4)POST", "(5)final", "limiter");
}

void printMapRow (const MapRow& r)
{
    auto cell = [] (const StagePk& s) { char b[16]; std::snprintf (b, sizeof b, "%.3f/%.3f", s.sample, s.trueP); return std::string (b); };
    char lim[24]; std::snprintf (lim, sizeof lim, "%.2fdB %.1f%%", r.limMaxRedDb, r.limWorkPct);
    std::printf ("  %-22s | %-13s | %-13s | %-13s | %-13s | %-13s | %-16s\n",
                 r.label.c_str(), cell (r.s1Dry).c_str(), cell (r.s2WetPre).c_str(),
                 cell (r.s3TotalPre).c_str(), cell (r.s4Post).c_str(), cell (r.s5Final).c_str(), lim);
}

// Arma el mapa completo de un caso (material+preset) corriendo las variantes que aíslan cada etapa.
MapRow buildMap (const std::string& label, Material mat, double sr, int block, double secs,
                 float size, float decay, float tone, float breath, float mix)
{
    MapRow r; r.label = label;
    // (1) dry post-inGain: mix=0 → la salida es SÓLO el dry (el limiter no toca nada con wet=0).
    r.s1Dry     = runStage (mat, sr, block, secs, { size, decay, tone, breath, 0.0f, true,  false });
    // (2) wet PRE-limiter: mix=1 (out = wet, dryGain=0) + limiter OFF → wet crudo.
    r.s2WetPre  = runStage (mat, sr, block, secs, { size, decay, tone, breath, 1.0f, false, false });
    // (3) total dry+wet PRE-limiter: mix real + limiter OFF.
    r.s3TotalPre= runStage (mat, sr, block, secs, { size, decay, tone, breath, mix,  false, false });
    // (4) POST-limiter (salida del motor) = mix real + limiter ON. Enganchamos el probe acá para leer el
    //     trabajo del limiter (reducción máx + % de samples reducidos).
    ovni::engines::FdnReverb::MeterProbe probe;
    r.s4Post    = runStage (mat, sr, block, secs, { size, decay, tone, breath, mix,  true,  true }, &probe);
    r.limMaxRedDb = probe.limMaxRedDb;
    r.limWorkPct  = 100.0 * probe.limWorkFrac;
    // (5) final post-chasis: la corrida real completa (idéntica a (4) con inGain/outGain a 0 dB y monoSafe
    //     off → el chasis no agrega ganancia; lo medimos igual para CERRAR el mapa de punta a punta).
    r.s5Final   = runStage (mat, sr, block, secs, { size, decay, tone, breath, mix,  true,  false });
    return r;
}
} // namespace

TEST_CASE ("NEBULA clip-scan: mapa de la cadena etapa por etapa (¿dónde nace el clip?)", "[clipscan][nebula]")
{
    constexpr double SR    = 48000.0;
    constexpr int    BLOCK = 128;
    constexpr double SECS  = 2.5;

    printMapHeader();

    // Barrido de material × mix (20/50/80%) × decay. El mix BAJO (20%) deja el dry dominante → es donde
    // el viejo diseño (limiter sobre dry+wet) DISTORSIONABA el dry; ahora debe pasar transparente.
    struct Case { const char* name; Material mat; float decay; };
    const std::array<Case, 3> mats {{
        { "transient-bursts", Material::TransientBursts, 0.90f },
        { "synth-perc-HF",    Material::SynthPercHF,     0.80f },
        { "kick+hat",         Material::KickHat,         0.70f },
    }};
    const std::array<float, 3> mixes { 0.20f, 0.50f, 0.80f };

    std::vector<MapRow> rows;
    for (const auto& c : mats)
        for (float mx : mixes)
        {
            char lbl[40]; std::snprintf (lbl, sizeof lbl, "%s mix%d", c.name, (int) std::lround (mx * 100));
            rows.push_back (buildMap (lbl, c.mat, SR, BLOCK, SECS, 0.65f, c.decay, 0.30f, 0.50f, mx));
        }
    for (const auto& r : rows) printMapRow (r);

    // ── Verificación EXTRA del nuevo diseño: el limiter SÍ contiene el WET. Corremos wet-only (mix=1) con
    //    el limiter ON y medimos el sample-peak: debe quedar ≤ ceiling (0.80) + un pelo de release. Esto
    //    prueba que la red de seguridad funciona sobre la cola (lo único que limita ahora). ───────────────
    float worstWetPost = 0.0f;
    for (const auto& c : mats)
    {
        const StagePk wetPost = runStage (c.mat, SR, BLOCK, SECS,
                                          { 0.65f, c.decay, 0.30f, 0.50f, 1.0f, /*limiterOn*/ true, false });
        worstWetPost = juce::jmax (worstWetPost, wetPost.sample);
    }

    // ── DIAGNÓSTICO IMPRESO (B2): la conclusión, leída del mapa. ──────────────────────────────────────
    // Señales clave, separadas por régimen:
    //   · dryPeakLowMix: el SAMPLE-peak del dry a mix=0. El lookahead de salida es true-peak-safe (atenúa por
    //     el pico INTER-sample, no por la muestra), así que el sample-peak del dry queda ≈ ceiling (~0.95): el
    //     dry pasa con nivel pleno (no se crusha como con el viejo attack instantáneo) y su true-peak queda safe.
    //   · sustainWork: con material TONAL sostenido (kick) el wet se acumula y el limiter de WET trabaja sobre
    //     la COLA (no sobre el dry) → transparente (§2); el lookahead final remata la suma.
    //   · worstFinalTrue: el TRUE-PEAK de la salida real (lo que mide el DAW). Es el número que importa.
    float dryPeakLowMix = 0.0f, sustainWork = 0.0f, worstFinalTrue = 0.0f;
    for (const auto& r : rows)
    {
        worstFinalTrue = juce::jmax (worstFinalTrue, r.s5Final.trueP);
        if (r.label.rfind ("kick+hat", 0) == 0)         sustainWork   = juce::jmax (sustainWork,   (float) r.limWorkPct);
        if (r.label.find ("mix20") != std::string::npos) dryPeakLowMix = juce::jmax (dryPeakLowMix, r.s1Dry.sample);
    }

    std::printf ("\nCLIPSCAN DIAGNOSTICO (lookahead true-peak-safe sobre la SUMA dry+wet):\n");
    std::printf ("  · DRY con nivel pleno: sample-peak del dry a mix0 = %.3f  (≈ ceiling; pasa sin crusharse,\n", dryPeakLowMix);
    std::printf ("     el lookahead atenua por el pico INTER-sample → su true-peak queda safe sin matar el nivel)\n");
    std::printf ("  · WET contenido (red intermedia): wet-only POST sample-peak = %.3f  (el limiter de wet lo cierra a ~0.80)\n", worstWetPost);
    std::printf ("  · limiter con TONAL sostenido (kick): el limiter de WET trabaja %.1f%% sobre la COLA (no sobre el\n", sustainWork);
    std::printf ("     dry; limitar una cola difusa es transparente, §2). El lookahead final remata la suma dry+wet.\n");
    std::printf ("  · salida FINAL peor true-peak = %.3f", worstFinalTrue);
    if (worstFinalTrue > 0.97f)
        std::printf ("  → el LOOKAHEAD limiter de salida NO contuvo la suma dry+wet (REGRESION: deberia quedar\n     <=0.95; ver 7c en FdnReverb::process).\n");
    else
        std::printf ("  → el LOOKAHEAD limiter de salida contiene la SUMA dry+wet bajo el techo (sin clip, sin crackle).\n");
    std::printf ("  >> VEREDICTO: el LOOKAHEAD de salida (7c) mira el pico que viene (~3 ms) y baja la ganancia\n");
    std::printf ("     con rampa SUAVE antes de que el pico emerja → la suma dry+wet NUNCA pasa el techo (0.95) y\n");
    std::printf ("     sin el escalón del attack instantaneo (= se va el crackle). Latencia reportada al host (PDC).\n");

    // ── GATE DEFINITIVO: el lookahead de salida contiene la SUMA dry+wet (la señal que sale al DAW). ─────
    // 1) DRY TRANSPARENTE: a mix=0 el dry full-scale entra a la suma SIN ser clampeado a un techo interno (el
    //    lookahead sólo actúa si la suma pasa 0.95; un dry full-scale a ~1.0 lo limita apenas, no lo crusha).
    REQUIRE (dryPeakLowMix > 0.90f);
    // 2) El WET sí queda contenido por su limiter intermedio (red sobre la cola difusa, §2): wet-only POST ≤
    //    ceiling del wet (0.80) + margen de release. (Ojo: este wet-only es la salida FINAL, así que también
    //    pasó por el lookahead → queda incluso más bajo; el guard 0.86 sigue válido como cota superior.)
    REQUIRE (worstWetPost <= 0.86f);
    // 3) GATE CRÍTICO (el fix definitivo del clip que Joaquín seguía oyendo): la etapa (5)final true-peak ≤
    //    0.97 en TODOS los casos del barrido — INCLUIDO kick+hat mix50 (que ANTES daba 1.363, clip duro). El
    //    lookahead sobre la suma dry+wet lo garantiza con ceiling 0.95 (margen inter-sample). Si esto falla,
    //    la suma volvió a clipear y el fix se rompió.
    REQUIRE (worstFinalTrue <= 0.97f);
    // 4) Todo finito (sin NaN/Inf en ninguna etapa de ninguna corrida).
    REQUIRE (std::isfinite (worstFinalTrue));
}
