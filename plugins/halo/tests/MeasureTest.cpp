// Test [measure][halo] — imagen estéreo / envolvimiento del wet orbital (house-standard §2, base técnica §3).
// Corre ruido rosa por el HaloProcessor REAL e imprime los números de un vectorscope pro + el IACC
// (correlación interaural normalizada): el cue de LISTENER ENVELOPMENT. Un wash mono daría IACC ~1 ("dentro
// de la cabeza"); el wash estéreo decorrelado del FDN, paneado por ITD/ILD sin sumarse a mono, lo baja.
//   IACC < 0.5 = envolvente real (target de la base técnica §9). Es DIAGNÓSTICO: imprime MEASURE[...] +
//   IACC=; REQUIRE finitud + el target de envolvimiento. Modelado en StereoMeasure.cpp de PULSAR.
//
// FIX ORBIT (envolvimiento, no angostamiento): el camino viejo sumaba el wet a mono → más ORBIT = más
// ANGOSTO (bug que Joaquín cazó con Insight). Ahora ORBIT panea el wash ANCHO COMPLETO alrededor de la cabeza
// con cues binaurales reales (ITD = delay interaural + ILD = sombra de cabeza dependiente de frecuencia), sin
// sumarlo a mono. El SWEEP de abajo lo MIDE: orbit↑ debe bajar el IACC/CORR y subir el WIDTH, y orbit=100% NO
// debe quedar cerca de mono (CORR no ~+1).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "engines/movement/Trajectory.h"   // gate del azimut liso (chaos=0)
#include <cstdio>
#include <algorithm>

// Escribe un WAV float32 estéreo (little-endian; x86/ARM nativo). Para los renders de prueba [.render].
static void ovniWriteWavF32 (const char* path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    const std::uint32_t n = (std::uint32_t) std::min (L.size(), R.size());
    const std::uint16_t ch = 2, bits = 32;
    const std::uint32_t dataBytes = n * ch * (bits / 8);
    std::FILE* f = std::fopen (path, "wb"); if (! f) return;
    auto w32 = [&] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (3); w16 (ch);                 // 3 = IEEE float
    w32 ((std::uint32_t) sr); w32 ((std::uint32_t) (sr * ch * (bits / 8)));
    w16 ((std::uint16_t) (ch * (bits / 8))); w16 (bits);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (std::uint32_t i = 0; i < n; ++i) { std::fwrite (&L[i], 4, 1, f); std::fwrite (&R[i], 4, 1, f); }
    std::fclose (f);
}

// Render de HALO a WAV para que Joaquín escuche SIN abrir Ableton (mismo código que el plugin). Oculto ([.])
// → no corre en el ctest normal; se dispara a mano con el tag [render].
TEST_CASE ("RENDER HALO con TRANSIENTES (impulso + beat) -> WAV", "[.][render]")
{
    namespace pid = halo::params::id;
    // Joaquín: con tono sostenido suena bien, con SONIDO REAL (beat) salta lo "rápido". Reproduzco con
    // TRANSIENTES (lo que revela el flutter): la respuesta al IMPULSO (el flutter se ve crudo en la cola) y
    // un BEAT sintético a 90 bpm. Settings EXACTOS de la captura (SIZE100 DECAY20 SHIMMER44 TONE100 ORBIT0).
    enum InKind { IMPULSE, BEAT };
    auto render = [] (const char* path, InKind kind, int seconds, float shimmer, float decay, float sizeP) {
        halo::HaloProcessor proc;
        const double SR = 48000.0; const int N = 512;
        auto setN = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
        setN (pid::MIX, 1.0f); setN (pid::SIZE, sizeP); setN (pid::DECAY, decay);
        setN (pid::SHIMMER, shimmer); setN (pid::TONE, 1.0f); setN (pid::ORBIT, 0.0f);
        setN (pid::ORBITSYNC, 0.0f); setN (pid::ORBITRATE, 0.0f);
        proc.prepareToPlay (SR, N);
        const long total = (long) ((double) seconds * SR);
        const long stepBeat = (long) (SR * 60.0 / 90.0 / 2.0);   // corchea @90 bpm (~0.333 s)
        std::uint32_t rng = 0xCAFE1234u; auto wht = [&] { rng = rng*1664525u+1013904223u; return ((float)(rng>>9)*(1.0f/4194304.0f))-1.0f; };
        std::vector<float> oL, oR; oL.reserve ((size_t) total); oR.reserve ((size_t) total);
        long smp = 0;
        for (long done = 0; done < total; done += N) {
            const int n = (int) std::min ((long) N, total - done);
            juce::AudioBuffer<float> buf (2, n); juce::MidiBuffer m;
            for (int i = 0; i < n; ++i) {
                float x = 0.0f;
                if (kind == IMPULSE) { x = (smp == 2400) ? 0.9f : 0.0f; }   // 1 impulso a los 50 ms
                else { // BEAT: kick (80 Hz) cada negra + hat (ruido) cada corchea, decay exp
                    const long pos = smp % stepBeat; const long beatIdx = smp / stepBeat;
                    const float env = std::exp (-(float) pos / (0.05f * (float) SR));
                    if (beatIdx % 2 == 0) x += 0.7f * std::sin (2.0f*3.14159f*80.0f*(float)pos/(float)SR) * env;  // kick en la negra
                    const float hEnv = std::exp (-(float) pos / (0.012f * (float) SR));
                    x += 0.25f * wht() * hEnv;                                                                    // hat cada corchea
                }
                buf.setSample (0, i, x); if (buf.getNumChannels()>1) buf.setSample (1, i, x); smp++;
            }
            proc.processBlock (buf, m);
            const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (buf.getNumChannels()>1?1:0);
            for (int i = 0; i < n; ++i) { oL.push_back (L[i]); oR.push_back (R[i]); }
        }
        ovniWriteWavF32 (path, oL, oR, (int) SR);
        std::printf ("RENDER -> %s\n", path);
    };
    //                              kind   seg  shim  decay size   → qué aísla
    render ("/tmp/beat_1_FULL.wav",    BEAT, 8, 0.44f, 0.20f, 1.0f); // sus settings EXACTOS (el problema)
    render ("/tmp/beat_2_NOSHIM.wav",  BEAT, 8, 0.00f, 0.20f, 1.0f); // sin shimmer → si se calma = era el shimmer
    render ("/tmp/beat_3_DECAYUP.wav", BEAT, 8, 0.44f, 0.80f, 1.0f); // DECAY largo → si se calma = era el flutter (cola corta lo exponía)
    render ("/tmp/beat_4_SIZEDN.wav",  BEAT, 8, 0.44f, 0.20f, 0.4f); // SIZE menor → si se calma = era el tamaño grande (ecos separados)
    REQUIRE (true);
}

// ── La órbita de HALO debe ser LISA a RATE lento: el "bamboleo" del chaos modula el azimut a un timescale
//    FIJO (~0.2–1 s, suavizado a block-rate), INDEPENDIENTE del freeHz → a órbita glacial ese jitter rápido
//    DOMINA y se oye "rápido" aunque la rotación sea lenta (lo cazó Joaquín: "pongo despacio, sigue rápido").
//    HALO usa chaos=0 → azimut liso. Gate sobre el azimut REAL de la trayectoria (no a través del wash, que
//    enmascara): a freeHz glacial el jitter rápido (>0.5 Hz) de sin(azimut) debe ser ~0 con chaos=0, y
//    NOTORIO con chaos=0.12 (prueba de que el gate detecta el problema). Espeja kTrajChaos01 de HaloEngine.
TEST_CASE ("HALO orbita: a RATE lento el azimut es LISO (sin bamboleo rapido del chaos)", "[measure][halo]")
{
    auto fastJitter = [] (float chaos) {
        ovni::engines::Trajectory traj;
        const double SR = 48000.0; const int N = 512;
        traj.prepare (SR);
        ovni::engines::TrajectoryParams tp;
        tp.shape = ovni::engines::Ellipse; tp.rate = ovni::engines::Free;
        tp.freeHz = 0.004f;                                   // glacial (≈250 s/vuelta)
        tp.spread01 = 0.35f; tp.chaos01 = chaos; tp.dir = ovni::engines::CCW; tp.radius01 = 0.70f;
        traj.setParams (tp);
        ovni::engines::TransportInfo tr; tr.isPlaying = true; tr.bpm = 120.0;
        std::vector<double> pan; pan.reserve (3000);
        for (int blk = 0; blk < 3000; ++blk) { auto t = traj.advance (N, tr); pan.push_back (std::sin (t.azimuth)); }
        // residuo > 0.5 Hz (jitter rápido) del pan = sin(azimut), separado de la órbita lenta (LP 0.5 Hz)
        const double fb = SR / (double) N, c = 1.0 - std::exp (-2.0 * 3.14159265 * 0.5 / fb);
        double lp = 0; bool init = false, varSet = false; double var = 0; int cnt = 0;
        for (double v : pan) { if (! init) { lp = v; init = true; } else lp += c * (v - lp);
                               const double f = v - lp; var += f * f; ++cnt; (void) varSet; }
        return std::sqrt (var / (double) juce::jmax (1, cnt));
    };
    const double noChaos   = fastJitter (0.0f);
    const double withChaos = fastJitter (0.12f);
    std::printf ("MEASURE[HALO azimut] jitter rapido (>0.5Hz): chaos=0 -> %.5f   chaos=0.12 -> %.5f\n", noChaos, withChaos);
    INFO ("noChaos=" << noChaos << "  withChaos=" << withChaos);
    REQUIRE (noChaos < 0.01);                 // chaos=0 → azimut LISO a órbita lenta (sin jitter rápido)
    REQUIRE (withChaos > noChaos * 2.0);      // el chaos DUPLICA el jitter rápido → el gate detecta el problema
}

namespace {

struct Pink
{
    std::uint32_t s = 0x13572468u;
    float b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0;
    float white() { s = s * 1664525u + 1013904223u; return ((float) (s >> 9) * (1.0f / 4194304.0f)) - 1.0f; }
    float next()
    {
        const float w = white();
        b0 = 0.99886f*b0 + w*0.0555179f; b1 = 0.99332f*b1 + w*0.0750759f;
        b2 = 0.96900f*b2 + w*0.1538520f; b3 = 0.86650f*b3 + w*0.3104856f;
        b4 = 0.55000f*b4 + w*0.5329522f; b5 = -0.7616f*b5 - w*0.0168980f;
        const float p = b0+b1+b2+b3+b4+b5+b6 + w*0.5362f; b6 = w*0.115926f;
        return p * 0.11f;
    }
};

// Resultado de medir la imagen del wet a un ORBIT dado.
struct Img { double iacc, width, corr, balDB, monoDB; };

// ── IN PHASE (monoSafe) — CORR L/R de banda completa del wet con el toggle OFF vs ON ───────────────────
// Espeja el gate del sello (house-standard): IN PHASE / mono → CORR ≥ 0.95 (mono-compatible). Corre ruido
// rosa MONO por el HaloProcessor REAL con MIX pleno + ORBIT al máximo (peor caso de decorrelación) y mide
// la correlación L/R de banda completa de la cola. monoSafe del chasis (param inyectado "monoSafe"). Con
// IN PHASE ON el motor apaga el pan orbital y suma el wet a mono → CORR debe SUBIR hacia ~1.
double measureCorrMonoSafe (bool inPhase)
{
    namespace pid = halo::params::id;
    halo::HaloProcessor proc;
    const double SR = 48000.0; const int N = 512;

    auto set     = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
    auto setRaw  = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
    set (pid::MIX, 1.0f);
    set (pid::ORBIT, 1.0f);          // peor caso: pan orbital pleno (lo que IN PHASE debe apagar)
    set (pid::DECAY, 0.7f);
    set (pid::SHIMMER, 0.5f);
    setRaw ("monoSafe", inPhase ? 1.0f : 0.0f);   // IN PHASE del chasis (param inyectado)
    proc.prepareToPlay (SR, N);

    Pink pink;
    double sLL=0,sRR=0,sLR=0; long cnt=0;
    for (int blk = 0; blk < 700; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int n = 0; n < N; ++n) { const float x = pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < 250) continue;   // warmup (la cola del FDN + el binaural se asientan)
        const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (1);
        for (int n = 0; n < N; ++n) { const double l = L[n], r = R[n]; sLL += l*l; sRR += r*r; sLR += l*r; ++cnt; }
    }
    return sLR / (std::sqrt (sLL * sRR) + 1e-12);
}

// Corre ruido rosa por el HaloProcessor REAL con MIX pleno y el ORBIT pedido; mide la imagen del wet.
// MIX=100 → la salida es prácticamente el wet (medimos la cola binaural, no el dry). Shimmer/decay medios.
Img measureOrbit (float orbit01)
{
    namespace pid = halo::params::id;
    halo::HaloProcessor proc;
    const double SR = 48000.0; const int N = 512;

    auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
    set (pid::MIX, 1.0f);
    set (pid::ORBIT, orbit01);
    set (pid::DECAY, 0.7f);
    set (pid::SHIMMER, 0.5f);
    proc.prepareToPlay (SR, N);

    Pink pink;
    double sLL=0,sRR=0,sLR=0,sMid=0,sSide=0,sMono=0; long cnt=0;
    for (int blk = 0; blk < 700; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int n = 0; n < N; ++n) { const float x = pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < 250) continue;   // warmup (la cola del FDN + el binaural se asientan)
        const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (1);
        for (int n = 0; n < N; ++n)
        {
            const double l = L[n], r = R[n];
            sLL += l*l; sRR += r*r; sLR += l*r;
            const double mid = 0.5*(l+r), sd = 0.5*(l-r);
            sMid += mid*mid; sSide += sd*sd; sMono += (l+r)*(l+r); ++cnt;
        }
    }
    const double iacc   = sLR / (std::sqrt (sLL * sRR) + 1e-12);   // correlación interaural (envolvimiento)
    const double corr   = iacc;                                    // misma definición que el correlímetro de PULSAR
    const double width  = std::sqrt (sSide / (sMid + 1e-12));
    const double balDB  = 10.0 * std::log10 ((sLL + 1e-12) / (sRR + 1e-12));
    const double rmsL   = std::sqrt (sLL / (double) juce::jmax (1L, cnt));
    const double rmsMono= std::sqrt (sMono / (double) juce::jmax (1L, cnt));
    const double monoDB = 20.0 * std::log10 ((rmsMono + 1e-12) / (2.0 * rmsL + 1e-12));
    return { iacc, width, corr, balDB, monoDB };
}

} // namespace

TEST_CASE ("HALO imagen estéreo / IACC del wet orbital (diagnóstico)", "[measure][halo]")
{
    const Img m = measureOrbit (1.0f);   // ORBIT pleno (medimos la cola binaural envolvente)

    std::printf ("MEASURE[HALO wet ORBIT=100] IACC=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f\n",
                 m.iacc, m.width, m.balDB, m.monoDB);
    std::printf ("IACC=%.3f\n", m.iacc);   // <- la línea que grepea measure-check.sh

    REQUIRE (std::isfinite (m.iacc));
    // Envolvimiento real (base técnica §9): el wet orbital decorrelado baja el IACC. Target < 0.5; cota
    // holgada (≤ 0.7) como gate de cordura (el target fino lo juzga el oído + el número publicado).
    REQUIRE (m.iacc < 0.7);
}

// ── IN PHASE (mono-safe): el wet colapsa a mono-compatible (CORR sube hacia 1) ─────────────────────────
// Gate del sello (house-standard): IN PHASE / mono → CORR ≥ 0.95. Con IN PHASE OFF el wet orbital está MUY
// decorrelado (CORR≈0.06, el envolvimiento del wash). Con IN PHASE ON el motor apaga el pan orbital y suma
// el wet a mono → la salida queda EN FASE (mono-compatible): CORR ≥ 0.95. Prueba que el toggle hace lo que
// dice su nombre y que el plugin es seguro al sumar a mono (club/vinilo). Espeja el test de NEBULA.
TEST_CASE ("HALO IN PHASE: el wet colapsa a mono-compatible (CORR sube hacia 1)", "[measure][halo]")
{
    const double corrOff = measureCorrMonoSafe (/*inPhase*/ false);
    const double corrOn  = measureCorrMonoSafe (/*inPhase*/ true );

    std::printf ("MEASURE[HALO inphase] CORR monoSafe=OFF %+.3f  ->  monoSafe=ON %+.3f\n", corrOff, corrOn);

    REQUIRE (std::isfinite (corrOff));
    REQUIRE (std::isfinite (corrOn));
    REQUIRE (corrOff < 0.5);          // OFF: wet orbital decorrelado (envolvente, ancho)
    REQUIRE (corrOn  >= 0.95);        // ON: mono-compatible (EN FASE) — gate del sello
}

// SWEEP de ORBIT (la medición que el coordinador le muestra a Joaquín): 0% / 50% / 100%. El wash del FDN ya
// nace MÁXIMAMENTE envolvente (orbit=0: WIDTH≈0.9, IACC≈0.07 → piso de decorrelación). El bug NO era falta de
// ancho sino que ORBIT lo ANGOSTABA a mono (sumaba a mono y reconstruía un punto). El FIX panea el wash ANCHO
// COMPLETO por ITD/ILD → orbita SIN angostar. Por eso el gate honesto es: a ORBIT=100% el wash sigue ANCHO y
// decorrelado (NO cerca de mono), y NO se angosta respecto a orbit=0. El "se mueve" lo prueba el test de SYNC.
TEST_CASE ("HALO ORBIT sweep: orbita SIN angostar (ancho + envolvente a 100%, nunca mono)", "[measure][halo]")
{
    const Img m0  = measureOrbit (0.0f);
    const Img m50 = measureOrbit (0.5f);
    const Img m100= measureOrbit (1.0f);

    std::printf ("MEASURE[HALO ORBIT=0]   CORR=%+.3f  WIDTH=%.3f  IACC=%+.3f\n", m0.corr,   m0.width,   m0.iacc);
    std::printf ("MEASURE[HALO ORBIT=50]  CORR=%+.3f  WIDTH=%.3f  IACC=%+.3f\n", m50.corr,  m50.width,  m50.iacc);
    std::printf ("MEASURE[HALO ORBIT=100] CORR=%+.3f  WIDTH=%.3f  IACC=%+.3f\n", m100.corr, m100.width, m100.iacc);
    INFO ("ORBIT0 IACC="  << m0.iacc   << " WIDTH=" << m0.width);
    INFO ("ORBIT100 IACC="<< m100.iacc << " WIDTH=" << m100.width);

    REQUIRE (std::isfinite (m0.iacc));
    REQUIRE (std::isfinite (m100.iacc));

    // El FIX (vs el bug original CORR≈+1, WIDTH≈0): a ORBIT=100% NO se cierra a mono.
    REQUIRE (m100.corr  < 0.5);     // lejos de +1 (el bug era casi mono al centro)
    REQUIRE (m100.iacc  < 0.5);     // sigue ENVOLVENTE (IACC bajo) al máximo
    REQUIRE (m100.width > 0.6);     // sigue ANCHO al máximo

    // NO angosta: el wash a orbit=100% conserva ≥80% del ancho de orbit=0 (el pan no cierra la imagen).
    REQUIRE (m100.width > 0.80 * m0.width);
}

// FIX SYNC (la órbita es el elemento rítmico natural de HALO): el RATE de la órbita debe cablearse a la
// división. Verificación a NIVEL de señal: con SYNC on y dos divisiones distintas (1 bar vs 8 bars) la
// VELOCIDAD de la órbita cambia → el balance L/R del wet OSCILA más rápido con 1 bar que con 8 bars (el pan
// ITD/ILD sigue al azimut θ). Lo medimos por la TASA DE CRUCES POR CERO del balance por-bloque demediado
// (LL−RR): una órbita más rápida cruza el centro más seguido. Aísla la órbita del ruido del wash (que con
// |Δside| crudo lo tapaba). Es la prueba de que "cambiar la división cambia AUDIBLEMENTE la rotación"
// (honestidad-dsp.md: el control produce el cambio que matchea su nombre).
TEST_CASE ("HALO SYNC: la division cambia la VELOCIDAD audible de la orbita", "[measure][halo]")
{
    namespace pid = halo::params::id;
    namespace sd  = halo::params::sync;

    // Tasa de cruces por cero del balance L/R por-bloque (∝ velocidad de la órbita), con SYNC on + división.
    auto orbitCrossingsPerSec = [&] (int divIdx) -> double
    {
        halo::HaloProcessor proc;
        const double SR = 48000.0; const int N = 512;
        auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
        set (pid::MIX, 1.0f);
        set (pid::ORBIT, 1.0f);
        set (pid::DECAY, 0.7f);
        set (pid::SHIMMER, 0.5f);
        set (pid::ORBITSYNC, 1.0f);                                   // SYNC on
        const float denom = (float) (sd::kCount - 1);
        set (pid::ORBITDIV, (float) divIdx / denom);                  // división elegida
        proc.prepareToPlay (SR, N);

        // PlayHead con BPM fijo + isPlaying: el SYNC deriva freeHz = BPM·división (rate Free engancha al tempo).
        struct PH : juce::AudioPlayHead {
            double ppq = 0.0;
            juce::Optional<PositionInfo> getPosition() const override {
                PositionInfo p; p.setBpm (120.0); p.setIsPlaying (true); p.setPpqPosition (ppq); return p;
            }
        } ph;
        proc.setPlayHead (&ph);

        // Recolectar el balance por-bloque (LL−RR) tras warmup. El wash decorrelado del FDN mete ruido
        // estocástico de BANDA ANCHA en ese balance que TAPA la órbita sub-Hz (con el balance crudo, los
        // cruces los pone el ruido, no la rotación). Por eso: (1) ventana larga (≈48 s) para que hasta la
        // órbita de "8 bars" (período 16 s) dé varios ciclos, y (2) LOW-PASS del balance (1-polo @ ~1.5 Hz
        // sobre la señal muestreada a fb=SR/N) para dejar SÓLO la modulación orbital (0.0625–0.5 Hz pasan;
        // el ruido del wash por encima se va) — recién ahí contamos cruces. Así el número mide la órbita,
        // no el wash (honestidad-dsp.md: medimos el efecto real, no su sombra).
        Pink pink;
        std::vector<double> bal; bal.reserve (4500);
        const int totalBlocks = 4800, warm = 300;
        for (int blk = 0; blk < totalBlocks; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int n = 0; n < N; ++n) { const float x = pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
            proc.processBlock (buf, midi);
            ph.ppq += (120.0 / 60.0) * ((double) N / SR);            // avanzar el transporte
            if (blk < warm) continue;
            const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (1);
            double bLL = 0.0, bRR = 0.0;
            for (int n = 0; n < N; ++n) { bLL += (double) L[n]*L[n]; bRR += (double) R[n]*R[n]; }
            bal.push_back (bLL - bRR);
        }
        // LP 1-polo del balance a nivel de bloque (fb = SR/N): aísla la órbita del ruido del wash antes de contar.
        const double fb     = SR / (double) N;
        const double lpCoef = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 1.5 / fb);
        std::vector<double> smooth; smooth.reserve (bal.size());
        double lp = 0.0; bool lpInit = false;
        for (double v : bal) { if (! lpInit) { lp = v; lpInit = true; } else lp += lpCoef * (v - lp); smooth.push_back (lp); }
        // Demediar + medir la energía del balance A LA FRECUENCIA ORBITAL de "1 bar" (Goertzel a 0.5 Hz
        // @120 BPM). FIX del métrico (2026-07-02): contar CRUCES medía el ruido del wash, no la órbita
        // (a "8 bars" la teoría da 0.125 cruces/s y el conteo daba ~1.2-1.8/s = puro ruido; el gate 1.5×
        // pasaba de suerte y cualquier cambio legítimo del wash — p.ej. densificar la excitación — lo
        // volteaba). La energía a la MISMA frecuencia de análisis en ambas corridas discrimina de verdad:
        // la corrida "1 bar" DEBE tener mucha más energía de balance a 0.5 Hz que la corrida "8 bars".
        double mean = 0.0; for (double v : smooth) mean += v; mean /= (double) (smooth.empty() ? size_t (1) : smooth.size());
        const double aHz = (double) sd::orbitRateHz (120.0, 0);        // frecuencia de análisis fija (0.5 Hz)
        const double w   = juce::MathConstants<double>::twoPi * aHz / fb;
        const double cw  = 2.0 * std::cos (w);
        double s0 = 0.0, s1 = 0.0, s2 = 0.0;
        for (double v : smooth) { s0 = (v - mean) + cw * s1 - s2; s2 = s1; s1 = s0; }
        const double e = (s1 * s1 + s2 * s2 - cw * s1 * s2) / juce::jmax (1.0, (double) smooth.size());
        return e;                                                       // energía del balance @ 0.5 Hz
    };

    const double fast = orbitCrossingsPerSec (0);   // "1 bar"  (órbita a 0.5 Hz → energía ALTA a 0.5 Hz)
    const double slow = orbitCrossingsPerSec (3);   // "8 bars" (órbita a 0.0625 Hz → energía BAJA a 0.5 Hz)
    const float  hzFast = sd::orbitRateHz (120.0, 0);
    const float  hzSlow = sd::orbitRateHz (120.0, 3);

    std::printf ("MEASURE[HALO SYNC] 1bar: orbitHz=%.4f  balE@0.5Hz=%.4g   8bars: orbitHz=%.4f  balE@0.5Hz=%.4g\n",
                 hzFast, fast, hzSlow, slow);
    INFO ("fast(1bar)=" << fast << " slow(8bars)=" << slow << " hzFast=" << hzFast << " hzSlow=" << hzSlow);

    // El rate orbital de "1 bar" es 8× el de "8 bars" (la tabla sync::divs lo fija) — confirma el cableado.
    REQUIRE (hzFast == Catch::Approx (hzSlow * 8.0f).margin (1e-4));
    REQUIRE (std::isfinite (fast));
    REQUIRE (std::isfinite (slow));
    // La corrida "1 bar" tiene MUCHA más energía de balance a 0.5 Hz que la de "8 bars" → la división
    // elegida realmente cambia la velocidad AUDIBLE de la órbita (SYNC ≠ inerte).
    REQUIRE (fast > slow * 3.0);
}

// ── LOW CUT [lowcut][halo] — el high-pass del WET que entra a la cola (bloque compartido ovni::dsp::LowCut),
//    SOLO sobre el wet (la familia FDN acumula graves en el tail → el HP los limpia antes de entrar al lazo;
//    el DRY se suma intacto al final por MIX). Verificación a NIVEL de señal por el HaloProcessor REAL:
//      (1) con LOW CUT al máximo (~500 Hz), un seno de ~60 Hz por el wet (MIX=100) sale CLARAMENTE atenuado
//          frente al MISMO seno con LOW CUT=0 (off) → el filtro corta los graves de verdad;
//      (2) un seno de ~2 kHz (arriba del corte) queda ~transparente entre LOW CUT max y off → el filtro NO toca
//          los medios/agudos (es un HP, no un trim global);
//      (3) con LOW CUT=0 (default) el wet de 60 Hz queda ~transparente → el default NO cambia el sonido actual.
//    SHIMMER=0 (reverb a secas, sin capa pitched) para que la cola refleje fielmente el espectro de entrada y
//    el número mida el filtro, no el pitch-shift (honestidad-dsp.md: medimos el efecto real). Sin NaN, estable.
TEST_CASE ("HALO LOW CUT: atenua los graves del wet, deja pasar los medios, off=transparente", "[lowcut][halo]")
{
    namespace pid = halo::params::id;

    // RMS de salida en estado estable (wet, MIX=100) para un seno de freqHz a un LOW CUT dado (0..1 normalizado).
    auto wetRms = [] (float freqHz, float lowCutNorm) -> double
    {
        halo::HaloProcessor proc;
        const double SR = 48000.0; const int N = 512;
        auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
        set (pid::MIX, 1.0f);        // salida ≈ wet (medimos la cola, no el dry)
        set (pid::SHIMMER, 0.0f);    // reverb a secas: la cola refleja el espectro de entrada (sin capa pitched)
        set (pid::DECAY, 0.5f);      // cola media (se asienta rápido, energía estable)
        set (pid::SIZE, 0.5f);
        set (pid::TONE, 0.0f);       // LP del lazo abierto → no enmascara el efecto del HP en graves/medios
        set (pid::ORBIT, 0.0f);
        set (pid::LOWCUT, lowCutNorm);
        proc.prepareToPlay (SR, N);

        double acc = 0.0; long cnt = 0; long smp = 0;
        const int totalBlocks = 900, warm = 500;   // warmup generoso: pre-delay (≤120 ms) + cola se asientan
        for (int blk = 0; blk < totalBlocks; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int n = 0; n < N; ++n)
            {
                const float x = 0.5f * std::sin (juce::MathConstants<float>::twoPi * freqHz * (float) smp / (float) SR);
                buf.setSample (0, n, x); buf.setSample (1, n, x); ++smp;
            }
            proc.processBlock (buf, midi);
            if (blk < warm) continue;
            const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (1);
            for (int n = 0; n < N; ++n) { acc += (double) L[n]*L[n] + (double) R[n]*R[n]; ++cnt; }
        }
        return std::sqrt (acc / (double) juce::jmax (1L, cnt));
    };

    const double bassOff  = wetRms (60.0f,   0.0f);   // 60 Hz, LOW CUT apagado (default)
    const double bassMax  = wetRms (60.0f,   1.0f);   // 60 Hz, LOW CUT máximo (~500 Hz)
    const double midOff   = wetRms (2000.0f, 0.0f);   // 2 kHz, LOW CUT apagado
    const double midMax   = wetRms (2000.0f, 1.0f);   // 2 kHz, LOW CUT máximo

    const double bassAttenDB = 20.0 * std::log10 ((bassMax + 1e-12) / (bassOff + 1e-12));
    const double midAttenDB  = 20.0 * std::log10 ((midMax  + 1e-12) / (midOff  + 1e-12));
    std::printf ("MEASURE[HALO LOWCUT] 60Hz off=%.5f max=%.5f (%.2f dB)   2kHz off=%.5f max=%.5f (%.2f dB)\n",
                 bassOff, bassMax, bassAttenDB, midOff, midMax, midAttenDB);
    INFO ("bassOff=" << bassOff << " bassMax=" << bassMax << " bassAttenDB=" << bassAttenDB
          << " midOff=" << midOff << " midMax=" << midMax << " midAttenDB=" << midAttenDB);

    // Estable / sin NaN.
    REQUIRE (std::isfinite (bassOff)); REQUIRE (std::isfinite (bassMax));
    REQUIRE (std::isfinite (midOff));  REQUIRE (std::isfinite (midMax));

    // (1) LOW CUT al máximo CORTA los graves del wet: el 60 Hz cae CLARAMENTE (≥ 6 dB) vs LOW CUT off.
    REQUIRE (bassMax < bassOff * 0.5);          // ≥ −6 dB en 60 Hz
    REQUIRE (bassAttenDB < -6.0);

    // (2) NO es un trim global: el 2 kHz (arriba del corte de 500 Hz) queda ~transparente entre max y off
    //     (margen holgado por la cola estocástica; lo que importa es que NO se atenúa como los graves).
    REQUIRE (midMax > midOff * 0.7);            // 2 kHz casi intacto (≤ ~3 dB)
    // El HP discrimina por frecuencia: corta los graves MUCHO más que los medios.
    REQUIRE (bassAttenDB < midAttenDB - 4.0);

    // (3) DEFAULT (LOW CUT=0) = transparente: el wet de 60 Hz off es el mismo camino que el HALO de hoy (el
    //     20 Hz piso del LowCut no toca 60 Hz) → no cambia el sonido actual. Sanidad: el bass off tiene energía.
    REQUIRE (bassOff > 1.0e-4);
}

// ── HI CUT [hicut][halo] — el low-pass del WET de la cola (bloque compartido ovni::dsp::HiCut), el PAR del
//    LowCut. Va ENCADENADO DESPUÉS del LowCut sobre el MISMO wet (graves fuera, ahora agudos fuera) → la cola
//    sale más oscura/suave. SOLO sobre el wet; el DRY se suma intacto al final por MIX. Verificación a NIVEL de
//    señal por el HaloProcessor REAL:
//      (1) con HI CUT al máximo (~1.5 kHz), un seno de ~8 kHz por el wet (MIX=100) sale CLARAMENTE atenuado
//          frente al MISMO seno con HI CUT=0 (off) → el filtro corta los agudos de verdad;
//      (2) un seno de ~300 Hz (abajo del corte) queda ~transparente entre HI CUT max y off → el filtro NO toca
//          los graves/medios (es un LP, no un trim global);
//      (3) con HI CUT=0 (default) el wet de 8 kHz queda ~transparente → el default NO cambia el sonido actual.
//    SHIMMER=0 (reverb a secas, sin capa pitched) para que la cola refleje fielmente el espectro de entrada y
//    el número mida el filtro, no el pitch-shift (honestidad-dsp.md: medimos el efecto real). Sin NaN, estable.
TEST_CASE ("HALO HI CUT: atenua los agudos del wet, deja pasar los graves, off=transparente", "[hicut][halo]")
{
    namespace pid = halo::params::id;

    // RMS de salida en estado estable (wet, MIX=100) para un seno de freqHz a un HI CUT dado (0..1 normalizado).
    auto wetRms = [] (float freqHz, float hiCutNorm) -> double
    {
        halo::HaloProcessor proc;
        const double SR = 48000.0; const int N = 512;
        auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
        set (pid::MIX, 1.0f);        // salida ≈ wet (medimos la cola, no el dry)
        set (pid::SHIMMER, 0.0f);    // reverb a secas: la cola refleja el espectro de entrada (sin capa pitched)
        set (pid::DECAY, 0.5f);      // cola media (se asienta rápido, energía estable)
        set (pid::SIZE, 0.5f);
        set (pid::TONE, 0.0f);       // LP del lazo abierto (≈9 kHz) → no enmascara el efecto del HI CUT en agudos
        set (pid::ORBIT, 0.0f);
        set (pid::LOWCUT, 0.0f);     // LOW CUT off → el HI CUT actúa solo (el par no interfiere en este test)
        set (pid::HICUT, hiCutNorm);
        proc.prepareToPlay (SR, N);

        double acc = 0.0; long cnt = 0; long smp = 0;
        const int totalBlocks = 900, warm = 500;   // warmup generoso: pre-delay (≤120 ms) + cola se asientan
        for (int blk = 0; blk < totalBlocks; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int n = 0; n < N; ++n)
            {
                const float x = 0.5f * std::sin (juce::MathConstants<float>::twoPi * freqHz * (float) smp / (float) SR);
                buf.setSample (0, n, x); buf.setSample (1, n, x); ++smp;
            }
            proc.processBlock (buf, midi);
            if (blk < warm) continue;
            const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (1);
            for (int n = 0; n < N; ++n) { acc += (double) L[n]*L[n] + (double) R[n]*R[n]; ++cnt; }
        }
        return std::sqrt (acc / (double) juce::jmax (1L, cnt));
    };

    const double trebOff  = wetRms (8000.0f, 0.0f);   // 8 kHz, HI CUT apagado (default)
    const double trebMax  = wetRms (8000.0f, 1.0f);   // 8 kHz, HI CUT máximo (~1.5 kHz)
    const double lowOff   = wetRms (300.0f,  0.0f);   // 300 Hz, HI CUT apagado
    const double lowMax   = wetRms (300.0f,  1.0f);   // 300 Hz, HI CUT máximo

    const double trebAttenDB = 20.0 * std::log10 ((trebMax + 1e-12) / (trebOff + 1e-12));
    const double lowAttenDB  = 20.0 * std::log10 ((lowMax  + 1e-12) / (lowOff  + 1e-12));
    std::printf ("MEASURE[HALO HICUT] 8kHz off=%.5f max=%.5f (%.2f dB)   300Hz off=%.5f max=%.5f (%.2f dB)\n",
                 trebOff, trebMax, trebAttenDB, lowOff, lowMax, lowAttenDB);
    INFO ("trebOff=" << trebOff << " trebMax=" << trebMax << " trebAttenDB=" << trebAttenDB
          << " lowOff=" << lowOff << " lowMax=" << lowMax << " lowAttenDB=" << lowAttenDB);

    // Estable / sin NaN.
    REQUIRE (std::isfinite (trebOff)); REQUIRE (std::isfinite (trebMax));
    REQUIRE (std::isfinite (lowOff));  REQUIRE (std::isfinite (lowMax));

    // (1) HI CUT al máximo CORTA los agudos del wet: el 8 kHz cae CLARAMENTE (≥ 6 dB) vs HI CUT off.
    REQUIRE (trebMax < trebOff * 0.5);          // ≥ −6 dB en 8 kHz
    REQUIRE (trebAttenDB < -6.0);

    // (2) NO es un trim global: el 300 Hz (abajo del corte de 1.5 kHz) queda ~transparente entre max y off
    //     (margen holgado por la cola estocástica; lo que importa es que NO se atenúa como los agudos).
    REQUIRE (lowMax > lowOff * 0.7);            // 300 Hz casi intacto (≤ ~3 dB)
    // El LP discrimina por frecuencia: corta los agudos MUCHO más que los graves.
    REQUIRE (trebAttenDB < lowAttenDB - 4.0);

    // (3) DEFAULT (HI CUT=0) = transparente: el wet de 8 kHz off es el mismo camino que el HALO de hoy (el motor
    //     bypassea el HiCut cuando hiCut01==0) → no cambia el sonido actual. Sanidad: el treble off tiene energía.
    REQUIRE (trebOff > 1.0e-4);
}

// ── REPRODUCCIÓN del bug reportado por Joaquín ("no andan"): con SHIMMER>0 (uso real), el HI CUT debe
//    OSCURECER la cola. El pitch-shifter (octava+quinta UP) vive DENTRO del lazo y REGENERA agudos en cada
//    vuelta; si el HI CUT está en la ENTRADA del lazo (pre-pitch), esa regeneración lo enmascara → el feature
//    no se oye. Este test mide la energía ALTA (>3 kHz) de la SALIDA con ruido blanco + SHIMMER=0.6 + DECAY
//    alto, HI CUT off vs máx. FALLA si el filtro está en la entrada (regeneración) ; PASA si está en la SALIDA
//    (post-pitch). Es la condición que los tests [hicut]/[lowcut] NO cubrían (ponían SHIMMER=0 → sin regen).
TEST_CASE ("HALO HI CUT con SHIMMER (uso REAL): oscurece la cola post-pitch", "[hicutreal][halo]")
{
    namespace pid = halo::params::id;

    auto highBandRms = [] (float hiCutNorm) -> double
    {
        halo::HaloProcessor proc;
        const double SR = 48000.0; const int N = 512;
        auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
        set (pid::MIX, 1.0f);        // salida ≈ wet (la cola)
        set (pid::SHIMMER, 0.6f);    // capa pitched ON → el lazo REGENERA agudos (la condición real)
        set (pid::DECAY, 0.7f);      // cola larga → la regeneración compone (los agudos suben vuelta a vuelta)
        set (pid::SIZE, 0.5f);
        set (pid::TONE, 0.0f);       // LP del lazo abierto → los agudos del shimmer sobreviven
        set (pid::ORBIT, 0.0f);
        set (pid::LOWCUT, 0.0f);
        set (pid::HICUT, hiCutNorm);
        proc.prepareToPlay (SR, N);

        // one-pole high-pass @ 3 kHz para medir SOLO la banda ALTA de la salida (los agudos del shimmer).
        const double fc = 3000.0, a = std::exp (-2.0 * juce::MathConstants<double>::pi * fc / SR);
        double zL = 0.0, zR = 0.0, xpL = 0.0, xpR = 0.0;
        std::uint32_t s = 0xBEEF1234u;
        auto wn = [&] { s = s * 1664525u + 1013904223u; return ((float) (s >> 9) * (1.0f / 4194304.0f)) - 1.0f; };

        double acc = 0.0; long cnt = 0;
        const int total = 900, warm = 500;
        for (int blk = 0; blk < total; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int n = 0; n < N; ++n) { const float x = 0.3f * wn(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
            proc.processBlock (buf, midi);
            if (blk < warm) continue;
            const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (1);
            for (int n = 0; n < N; ++n)
            {
                const double xL = L[n], xR = R[n];
                zL = a * (zL + xL - xpL); xpL = xL;
                zR = a * (zR + xR - xpR); xpR = xR;
                acc += zL * zL + zR * zR; ++cnt;
            }
        }
        return std::sqrt (acc / (double) juce::jmax (1L, cnt));
    };

    const double hiOff = highBandRms (0.0f);
    const double hiMax = highBandRms (1.0f);
    const double attenDB = 20.0 * std::log10 ((hiMax + 1e-12) / (hiOff + 1e-12));
    std::printf ("MEASURE[HALO HICUT-REAL shimmer=0.6] highband(>3k) off=%.6f max=%.6f (%.2f dB)\n", hiOff, hiMax, attenDB);
    INFO ("highband off=" << hiOff << " max=" << hiMax << " attenDB=" << attenDB);

    REQUIRE (std::isfinite (hiOff)); REQUIRE (std::isfinite (hiMax));
    REQUIRE (hiOff > 1.0e-5);   // sanidad: con SHIMMER hay agudos regenerados de verdad
    // Con SHIMMER>0, el HI CUT al máximo (1.5 kHz) DEBE bajar la banda alta CLARAMENTE (≥ 10 dB).
    REQUIRE (attenDB < -10.0);
}

// ── DIAGNÓSTICO (oculto del suite, [.]): mide la SALIDA TOTAL del HaloProcessor real a los settings EXACTOS de
//    la captura de Joaquín (MIX/SIZE/DECAY/SHIMMER/TONE/ORBIT), banda baja (<150 Hz) y alta (>3 kHz), filtros
//    OFF vs ambos al 100%, a MIX=100% (solo wet) y MIX=40% (el suyo). Revela cuánto del efecto sobrevive al dry.
TEST_CASE ("DIAG HALO filtros a settings reales (total output)", "[.][diag][halo]")
{
    namespace pid = halo::params::id;
    auto measure = [] (float mix, float lowCutN, float hiCutN, double& lowRms, double& highRms)
    {
        halo::HaloProcessor proc;
        const double SR = 48000.0; const int N = 512;
        auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
        set (pid::MIX, mix); set (pid::SIZE, 0.65f); set (pid::DECAY, 0.75f); set (pid::SHIMMER, 0.55f);
        set (pid::TONE, 0.45f); set (pid::ORBIT, 0.40f);
        set (pid::LOWCUT, lowCutN); set (pid::HICUT, hiCutN);
        proc.prepareToPlay (SR, N);
        const double aL = std::exp (-2.0 * juce::MathConstants<double>::pi * 150.0  / SR);
        const double aH = std::exp (-2.0 * juce::MathConstants<double>::pi * 3000.0 / SR);
        double lpz = 0, hpz = 0, xp = 0;
        std::uint32_t s = 0xABCDEF01u;
        auto wn = [&] { s = s * 1664525u + 1013904223u; return ((float) (s >> 9) * (1.0f / 4194304.0f)) - 1.0f; };
        double accL = 0, accH = 0; long cnt = 0;
        const int total = 900, warm = 500;
        for (int blk = 0; blk < total; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int n = 0; n < N; ++n) { const float x = 0.3f * wn(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
            proc.processBlock (buf, midi);
            if (blk < warm) continue;
            const float* L = buf.getReadPointer (0);
            for (int n = 0; n < N; ++n)
            {
                const double x = L[n];
                lpz = aL * lpz + (1.0 - aL) * x;       // LP 150 Hz (banda baja)
                hpz = aH * (hpz + x - xp); xp = x;     // HP 3 kHz (banda alta)
                accL += lpz * lpz; accH += hpz * hpz; ++cnt;
            }
        }
        lowRms  = std::sqrt (accL / (double) juce::jmax (1L, cnt));
        highRms = std::sqrt (accH / (double) juce::jmax (1L, cnt));
    };
    for (float mix : { 1.0f, 0.4f })
    {
        double loOff, hiOff, loOn, hiOn;
        measure (mix, 0.f, 0.f, loOff, hiOff);
        measure (mix, 1.f, 1.f, loOn, hiOn);
        const double lowDB  = 20.0 * std::log10 ((loOn + 1e-12) / (loOff + 1e-12));
        const double highDB = 20.0 * std::log10 ((hiOn + 1e-12) / (hiOff + 1e-12));
        std::printf ("DIAG MIX=%3.0f%%: LOW(<150) off=%.5f on=%.5f (%+.2f dB)  |  HIGH(>3k) off=%.5f on=%.5f (%+.2f dB)\n",
                     mix * 100.0f, loOff, loOn, lowDB, hiOff, hiOn, highDB);
    }
    REQUIRE (true);
}
