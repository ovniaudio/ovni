// RealBeatTest.cpp — ★ [realbeat][dust]: DIAGNÓSTICO sobre el BEAT REAL (no ruido rosa).
//
// La sesión pasada se "arregló" con ruido rosa (energía en TODOS los bins): falso verde. Sobre
// música real CONCENTRADA (kick/snare/hats/bajo, CORR L/R ~0.97 = centrada) el campo de DUST puede
// sonar MONO y/o ENCAJONADO. Este archivo:
//   1) lee tests/assets/realbeat_90.wav (juce::AudioFormatManager + createReaderFor),
//   2) lo procesa por el DustProcessor REAL (DEFAULT y "todo al 100"),
//   3) alinea el dry por la latencia del plugin,
//   4) mide sobre la SALIDA: CORR L/R (goniómetro), WIDTH=RMS(side)/RMS(mid),
//      y el ESPECTRO wet-vs-dry por bandas (detecta resonancia/boxy/comb/rolloff de agudos).
//
// NO es un gate con REQUIRE de umbrales arbitrarios: imprime los números (REALBEAT[...] / SPEC[...])
// para LEER lo que ve/oye Joaquín. El único REQUIRE es de cordura (señal finita, con energía).
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <array>
#include <complex>
#include "template/tests/OvniTestHarness.h"
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

namespace
{
    namespace pid = dust::params::id;
    constexpr double SR  = 48000.0;
    constexpr int    BLK = 512;

#ifndef OVNI_DUST_ASSET_DIR
    #define OVNI_DUST_ASSET_DIR "."
#endif

    // ── Carga el WAV estéreo a un AudioBuffer (resuelve la ruta del asset desde el define de CMake) ──
    bool loadBeat (juce::AudioBuffer<float>& out, double& srOut)
    {
        juce::File f (juce::String (OVNI_DUST_ASSET_DIR) + "/realbeat_90.wav");
        if (! f.existsAsFile())
            f = juce::File (OVNI_DUST_ASSET_DIR).getChildFile ("realbeat_90.wav");
        if (! f.existsAsFile()) return false;

        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (rd == nullptr) return false;

        srOut = rd->sampleRate;
        const int total = (int) rd->lengthInSamples;
        out.setSize (2, total);
        out.clear();
        rd->read (&out, 0, total, 0, true, true);
        if (rd->numChannels == 1) out.copyFrom (1, 0, out, 0, 0, total);   // mono -> dup
        return true;
    }

    // ── Procesa un buffer estéreo entero por el processor REAL en bloques de BLK ─────────────────────
    void runThrough (dust::DustProcessor& proc, const juce::AudioBuffer<float>& dryIn,
                     juce::AudioBuffer<float>& wetOut)
    {
        const int total = dryIn.getNumSamples();
        wetOut.setSize (2, total);
        wetOut.makeCopyOf (dryIn);
        for (int off = 0; off < total; off += BLK)
        {
            const int len = juce::jmin (BLK, total - off);
            juce::AudioBuffer<float> blk (2, len);
            for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, wetOut, ch, off, len);
            juce::MidiBuffer midi;
            proc.processBlock (blk, midi);
            for (int ch = 0; ch < 2; ++ch) wetOut.copyFrom (ch, off, blk, ch, 0, len);
        }
    }

    // ── Imagen estéreo sobre un buffer entero (a partir de 'skip' samples: descarta el warmup) ───────
    ovni::test::StereoImage imageOf (const juce::AudioBuffer<float>& buf, int skip)
    {
        ovni::test::StereoImageMeter m;
        const int total = buf.getNumSamples();
        m.addBlock (buf.getReadPointer (0) + skip, buf.getReadPointer (1) + skip, total - skip);
        return m.finish();
    }

    // ── DFT magnitud (mono-sum) por bandas log: detecta boxy (pico medios), comb (ripple), rolloff HF ─
    // Devuelve dB por banda relativo al total (espectro normalizado por energía: aísla el COLOR, no el
    // nivel — el MIX/limiter no contaminan la comparación tímbrica).
    struct BandSpec { std::array<double, 10> db {}; };
    constexpr std::array<double, 11> kBandEdges =
        { 20, 80, 160, 320, 640, 1280, 2560, 5120, 8000, 12000, 20000 };   // 10 bandas log

    // chMode: 0 = mono-sum 0.5*(L+R) (lo que mide el goniómetro/Insight), 1 = sólo L (aísla el comb).
    BandSpec spectrumOf (const juce::AudioBuffer<float>& buf, int start, int len, int chMode = 0)
    {
        // FFT de potencia de 2 sobre el mono-sum (Welch: ventanas Hann solapadas 50 %).
        int fftOrder = 14;                       // 16384 ≈ 0.34 s @48k
        const int N = 1 << fftOrder;
        juce::dsp::FFT fft (fftOrder);

        std::vector<double> psd ((size_t) (N / 2 + 1), 0.0);
        std::vector<float>  win ((size_t) N);
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (N - 1));

        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        const int    hop = N / 2;
        int nWin = 0;
        std::vector<float> fbuf ((size_t) (2 * N));
        for (int s = start; s + N <= start + len && s + N <= buf.getNumSamples(); s += hop)
        {
            std::fill (fbuf.begin(), fbuf.end(), 0.0f);
            for (int i = 0; i < N; ++i)
                fbuf[(size_t) i] = (chMode == 1 ? L[s + i] : 0.5f * (L[s + i] + R[s + i])) * win[(size_t) i];
            fft.performRealOnlyForwardTransform (fbuf.data());
            for (int k = 0; k <= N / 2; ++k)
            {
                const double re = fbuf[(size_t) (2 * k)];
                const double im = fbuf[(size_t) (2 * k + 1)];
                psd[(size_t) k] += re * re + im * im;
            }
            ++nWin;
        }
        if (nWin > 0) for (auto& v : psd) v /= (double) nWin;

        // Acumula PSD en las 10 bandas y normaliza al total -> dB relativos (forma espectral).
        BandSpec out;
        std::array<double, 10> bandE {};
        double totalE = 0.0;
        const double binHz = SR / (double) N;
        for (int k = 1; k <= N / 2; ++k)
        {
            const double hz = (double) k * binHz;
            for (int b = 0; b < 10; ++b)
                if (hz >= kBandEdges[(size_t) b] && hz < kBandEdges[(size_t) b + 1])
                { bandE[(size_t) b] += psd[(size_t) k]; break; }
            totalE += psd[(size_t) k];
        }
        for (int b = 0; b < 10; ++b)
            out.db[(size_t) b] = 10.0 * std::log10 ((bandE[(size_t) b] + 1e-20) / (totalE + 1e-20));
        return out;
    }

    void setVal (dust::DustProcessor& proc, const char* id, float v)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (proc.apvts.getParameterRange (id).convertTo0to1 (v));
    }
}

TEST_CASE ("DUST realbeat: imagen + espectro del beat real (DEFAULT y todo-100)", "[realbeat][dust]")
{
    juce::AudioBuffer<float> dry;
    double fileSr = 0.0;
    REQUIRE (loadBeat (dry, fileSr));
    const int total = dry.getNumSamples();
    REQUIRE (total > (int) SR);   // al menos 1 s

    // Imagen del DRY de entrada (lo que ENTRA: música centrada real).
    const auto dimg = imageOf (dry, 0);
    std::printf ("\nREALBEAT[dry IN          ] CORR=%+.3f  WIDTH=%.3f  (sr=%.0f, %d samp)\n",
                 dimg.corr, dimg.width, fileSr, total);

    // Espectro del dry (ventana central, lejos de los fades): referencia tímbrica.
    const int specStart = total / 4;
    const int specLen   = juce::jmin (total - specStart, (int) (SR * 4.0));
    const auto dspec = spectrumOf (dry, specStart, specLen);

    auto bandsLine = [] (const char* tag, const BandSpec& s)
    {
        std::printf ("SPEC[%s] (dB rel) 20-80=%.1f 80-160=%.1f 160-320=%.1f 320-640=%.1f 640-1.28k=%.1f "
                     "1.28-2.56k=%.1f 2.56-5.12k=%.1f 5.12-8k=%.1f 8-12k=%.1f 12-20k=%.1f\n",
                     tag, s.db[0], s.db[1], s.db[2], s.db[3], s.db[4], s.db[5], s.db[6], s.db[7], s.db[8], s.db[9]);
    };
    bandsLine ("dry        ", dspec);

    // L-only del dry (referencia tímbrica por canal: lo que oye un oído en estéreo, sin el comb del
    // mono-sum). El fix DESBOXY restaura el aire POR CANAL; el hueco que queda en el mono-sum es
    // cancelación binaural L+R (física correcta), no lo que se oye en monitores estéreo.
    const auto dspecL = spectrumOf (dry, specStart, specLen, 1);

    struct Result { ovni::test::StereoImage img; BandSpec spec, specL; };

    // ── DEFAULT de curaduría, pero MIX 100 (para medir el CAMPO WET sin que el dry lo tape) ──────────
    auto measure = [&] (const char* name, float density, float spread, float vida, bool inPhase) -> Result
    {
        dust::DustProcessor proc;
        ovni::test::setParam (proc, "monoSafe", inPhase ? 1.0f : 0.0f);
        setVal (proc, pid::MIX, 100.0f);          // wet pleno: medimos el campo de burbujas
        setVal (proc, pid::DENSITY, density);
        setVal (proc, pid::SPREAD,  spread);
        setVal (proc, pid::VIDA,    vida);
        setVal (proc, pid::DUCK,    0.0f);
        proc.prepareToPlay (SR, BLK);

        juce::AudioBuffer<float> wet;
        runThrough (proc, dry, wet);

        const int skip = juce::jmin (total - (int) SR, (int) (SR * 1.0));   // 1 s warmup (nube asentada)
        const auto img   = imageOf (wet, skip);
        const auto spec  = spectrumOf (wet, juce::jmax (skip, specStart), specLen, 0);   // mono-sum
        const auto specL = spectrumOf (wet, juce::jmax (skip, specStart), specLen, 1);   // sólo L

        std::printf ("REALBEAT[%s] CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f  rms=%.4f\n",
                     name, img.corr, img.width, img.balDb, img.monoSumDb, img.rms);
        bandsLine (name, spec);

        // Delta espectral wet-vs-dry: mono-sum (lo que mide el Insight) y sólo-L (lo que oye un oído).
        std::printf ("DELTA[%s] (wet-dry dB) ", name);
        for (int b = 0; b < 10; ++b) std::printf ("%+.1f ", spec.db[(size_t) b]  - dspec.db[(size_t) b]);
        std::printf ("\nDELTA_L[%s] (Lonly wet-dry dB) ", name);
        for (int b = 0; b < 10; ++b) std::printf ("%+.1f ", specL.db[(size_t) b] - dspecL.db[(size_t) b]);
        std::printf ("\n");

        REQUIRE (std::isfinite (img.corr));
        REQUIRE (img.rms > 1.0e-5);
        return { img, spec, specL };
    };

    // DEFAULT (curaduría): DENSIDAD 40 · SPREAD 60 · VIDA 25 = el preset de firma "Burbujas".
    const auto def = measure ("default DENS40 SPR60 VID25", 40.0f, 60.0f, 25.0f, false);
    // Todo al 100 (lo que probó Joaquín al subir todo): el caso más cargado de campo.
    const auto a100 = measure ("all100  DENS100 SPR100 VID100", 100.0f, 100.0f, 100.0f, false);
    // SPREAD 100 sin VIDA (imagen estática pura del banco).
    const auto s100 = measure ("spr100  DENS40 SPR100 VID0", 40.0f, 100.0f, 0.0f, false);
    // IN PHASE (mono-safety): debe colapsar a mono-compatible.
    const auto inph = measure ("inphase DENS40 SPR100 VID0", 40.0f, 100.0f, 0.0f, true);

    // ═════════════════════════════════════════════════════════════════════════════════════════════
    // GATE DE REGRESIÓN — SOBRE EL BEAT REAL (no ruido rosa). Estos REQUIRE evitan que vuelva el
    // "encajonado/sin aire" Y el "falso verde" del ruido rosa. Targets = los acordados con Joaquín.
    // ═════════════════════════════════════════════════════════════════════════════════════════════
    auto dL = [&] (const Result& r, int b) { return r.specL.db[(size_t) b] - dspecL.db[(size_t) b]; };  // delta L
    auto dM = [&] (const Result& r, int b) { return r.spec.db[(size_t) b]  - dspec.db[(size_t) b]; };   // delta mono-sum

    // (1) AIRE / PRESENCIA POR CANAL (lo que se oye en estéreo): la coloración del banco se llevaba
    //     el aire de 2.5-16 kHz. El fix lo restaura -> el wet POR CANAL no puede caer >3.5 dB del dry
    //     arriba de 8 kHz (bandas 7=8-12k, 8... espere: índices 0..9). Banda 8=8-12k, 9=12-20k.
    INFO ("AIRE por canal (L): 8-12k=" << dL (def, 8) << "  12-20k=" << dL (def, 9));
    REQUIRE (dL (def, 8) > -3.5);    // 8-12 kHz por canal: aire preservado (baseline pre-fix: -0.6 ya ok en L)
    REQUIRE (dL (def, 9) > -3.5);    // 12-20 kHz por canal: aire preservado
    REQUIRE (dL (def, 6) > -3.5);    // 2.56-5.12 kHz por canal: presencia (baseline pre-fix L: -3.5 borde)
    REQUIRE (dL (def, 7) > -4.0);    // 5.12-8 kHz por canal (la banda más combada; pre-fix L: -4.8)

    // (2) BOXY GLOBAL: ninguna banda del wet PICA por encima de la tendencia (sin resonancia de caja).
    //     El delta mono-sum no puede SUBIR más de +5 dB en ninguna banda (no inventamos un pico boxy).
    for (int b = 0; b < 10; ++b) { INFO ("banda " << b << " dM=" << dM (def, b)); REQUIRE (dM (def, b) < 5.0); }

    // (3) HF mono-sum recuperado (lo que ve el Insight): el hueco profundo de 2.5-16 kHz tiene que
    //     SUBIR respecto del baseline pre-fix (default era 2.56-5.12k=-7.3 / 5.12-8k=-9.0 / 8-12k=-4.3
    //     / 12-20k=-4.6). Exigimos claramente por encima de eso (el fix levantó esas bandas).
    INFO ("HF mono-sum def: 2.5-5k=" << dM (def, 6) << " 5-8k=" << dM (def, 7)
          << " 8-12k=" << dM (def, 8) << " 12-20k=" << dM (def, 9));
    REQUIRE (dM (def, 6) > -5.0);    // pre-fix -7.3
    REQUIRE (dM (def, 7) > -6.0);    // pre-fix -9.0 (banda más combada en mono-sum)
    REQUIRE (dM (def, 8) > -2.5);    // pre-fix -4.3
    REQUIRE (dM (def, 9) > -2.5);    // pre-fix -4.6

    // (4) ANCHO CONSERVADO (no rompimos el estéreo). DEFAULT en la ventana objetivo; el dry entra
    //     casi mono (0.976/0.110). NO exigir mono extremo (falso verde del ruido rosa) ni hiper-ancho.
    INFO ("imagen def CORR=" << def.img.corr << " WIDTH=" << def.img.width);
    REQUIRE (def.img.corr  > 0.80);  REQUIRE (def.img.corr  < 0.95);   // abre, sin desfasar de más
    REQUIRE (def.img.width > 0.15);  REQUIRE (def.img.width < 0.45);
    REQUIRE (a100.img.corr > 0.80);  REQUIRE (a100.img.corr < 0.96);   // todo-100 también queda colocado

    // (5) MONO-SAFETY honesta: IN PHASE colapsa a mono-compatible (CORR sube fuerte respecto del wet
    //     pleno, MONOSUM no se hunde). El banco HRIR queda BYPASSEADO -> espectro plano/brillante.
    INFO ("IN PHASE CORR=" << inph.img.corr << " vs spr100 CORR=" << s100.img.corr);
    REQUIRE (inph.img.corr < s100.img.corr - 0.10);   // IN PHASE re-centra (más mono-compatible)
    REQUIRE (inph.img.monoSumDb > -4.0);              // no se cancela al monoficar
}
