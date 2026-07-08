// =====================================================================================
// Generador one-off del HRIR de PRODUCCION del sello OVNI.
// Portado de orbita/tests/GenHrir.cpp ([gen] + [genring]) a un ejecutable standalone que
// escribe DIRECTO a las 4 ubicaciones de OVNI con los namespaces correctos:
//   shared/data/HrirData.h           (ovni::data)
//   shared/data/HrirRing.h           (ovni::data)
//   shared/engines/binaural/HrirData.h   (ovni::engines)
//   shared/engines/binaural/HrirRing.h   (ovni::engines)
//
// El pipeline DSP (simetrizacion L/R, DFE parcial band-limitada + fase minima, norma L2 por
// direccion) es IDENTICO al de orbita: este archivo solo cambia el origen del SOFA, el destino
// y los namespaces. No editar a mano los headers: regenerar con este tool.
//
// Build (build dir AISLADO; NO toca el build/ principal):
//   cmake -G Ninja -B build-genhrir -S tools/genhrir \
//       -DOVNI_JUCE_DIR=/path/to/orbita/JUCE \
//       -DFETCHCONTENT_SOURCE_DIR_LIBMYSOFA=/path/to/orbita/build/_deps/libmysofa-src
//   cmake --build build-genhrir
//   ./build-genhrir/gen_hrir  <ruta-al-SOFA>  <ruta-repo-ovni>
// =====================================================================================
#include <vector>
#include <fstream>
#include <iomanip>
#include <string>
#include <cstdio>
#include <cmath>
#include <complex>
#include <juce_dsp/juce_dsp.h>

extern "C" {
#include <mysofa.h>
}

namespace
{
constexpr int   kNumDirs = 72;                  // 5° de paso
constexpr float kStepDeg = 360.0f / kNumDirs;
constexpr float kSr      = 48000.0f;

// La cabecera describe el dataset de PRODUCCION horneado.
const char* kProvenance =
    "// Dataset: SADIE II — KU100 (maniqui Neumann KU100, Univ. de York, Apache-2.0).\n"
    "// Archivo SOFA: D1_48K_24bit_256tap_FIR_SOFA.sofa (48 kHz). Cita: DOI 10.3390/app8112029.\n"
    "// Copyright 2018, University of York. All measurements are Copyright University of York.\n";

// Escribe un HrirData.h (HRIR fija, 90 izquierda) en `path` con el namespace `ns`.
void writeHrirData (const std::string& path, const std::string& ns,
                    int len, float dL, float dR,
                    const std::vector<float>& irL, const std::vector<float>& irR)
{
    std::ofstream out (path);
    out << std::showpoint << std::setprecision (9);
    out << "// GENERADO por tools/gen_hrir.cpp -- HRIR fija (90 izquierda) para PULSAR (M1).\n"
        << "// No editar a mano: regenerar con  ./build-genhrir/gen_hrir <SOFA> <repo>\n"
        << kProvenance
        << "#pragma once\n#include <array>\n\nnamespace " << ns << " {\n\n"
        << "inline constexpr int   kHrirLength     = " << len << ";\n"
        << "inline constexpr float kHrirSampleRate = " << kSr << "f;\n"
        << "inline constexpr float kHrirDelayL     = " << dL << "f;\n"
        << "inline constexpr float kHrirDelayR     = " << dR << "f;\n\n";

    auto dump = [&] (const char* name, const std::vector<float>& v) {
        out << "inline constexpr std::array<float, " << v.size() << "> " << name << " = {{\n    ";
        out << std::setprecision (9);
        for (size_t i = 0; i < v.size(); ++i) {
            out << v[i] << "f, ";
            if ((i + 1) % 6 == 0) out << "\n    ";
        }
        out << "\n}};\n\n";
    };
    dump ("kHrirL", irL);
    dump ("kHrirR", irR);
    out << "} // namespace " << ns << "\n";
}

// Escribe un HrirRing.h (anillo de movimiento) en `path` con el namespace `ns`.
void writeHrirRing (const std::string& path, const std::string& ns, int taps,
                    const std::vector<float>& ringL, const std::vector<float>& ringR,
                    const std::vector<float>& delayL, const std::vector<float>& delayR)
{
    std::ofstream out (path);
    out << std::showpoint << std::setprecision (9);
    out << "// GENERADO por tools/gen_hrir.cpp -- anillo HRIR (movimiento, PULSAR).\n"
        << "// DFE (ecualizacion de campo difuso) + fase minima aplicados; ITD en el delay field.\n"
        << "// No editar a mano: regenerar con  ./build-genhrir/gen_hrir <SOFA> <repo>\n"
        << kProvenance
        << "#pragma once\n#include <array>\n\nnamespace " << ns << " {\n\n"
        << "inline constexpr int   kNumDirs       = " << kNumDirs << ";\n"
        << "inline constexpr int   kRingTaps      = " << taps << ";\n"
        << "inline constexpr float kRingSampleRate= " << kSr << "f;\n"
        << "inline constexpr float kRingStepDeg   = " << kStepDeg << "f;\n\n"
        << "// Planos: dir d, tap t -> indice d*kRingTaps + t\n";

    auto dumpRing = [&] (const char* name, const std::vector<float>& v) {
        out << "inline constexpr std::array<float, " << v.size() << "> " << name << " = {{\n    ";
        out << std::setprecision (9);
        for (size_t i = 0; i < v.size(); ++i) {
            out << v[i] << "f, ";
            if ((i + 1) % 6 == 0) out << "\n    ";
        }
        out << "\n}};\n\n";
    };
    auto dumpDelays = [&] (const char* name, const std::vector<float>& v) {
        out << "inline constexpr std::array<float, " << v.size() << "> " << name << " = {{\n    ";
        out << std::setprecision (9);
        for (size_t i = 0; i < v.size(); ++i) {
            out << v[i] << "f, ";
            if ((i + 1) % 8 == 0) out << "\n    ";
        }
        out << "\n}};\n\n";
    };
    dumpRing   ("kRingL",      ringL);
    dumpRing   ("kRingR",      ringR);
    dumpDelays ("kRingDelayL", delayL);
    dumpDelays ("kRingDelayR", delayR);
    out << "} // namespace " << ns << "\n";
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf (stderr, "uso: gen_hrir <ruta-SOFA> <ruta-repo-ovni>\n");
        return 2;
    }
    const std::string sofaPath = argv[1];
    const std::string repo     = argv[2];

    // ---------------------------------------------------------------------------------
    // [gen] HRIR FIJA: par L/R a 90° izquierda. (0,1,0) cartesiano = izquierda.
    // ---------------------------------------------------------------------------------
    int len = 0, err = 0;
    MYSOFA_EASY* sofa = mysofa_open (sofaPath.c_str(), kSr, &len, &err);
    if (sofa == nullptr || err != MYSOFA_OK || len <= 0) {
        std::fprintf (stderr, "[gen_hrir] mysofa_open FALLO: err=%d len=%d sofa=%p\n",
                      err, len, (void*) sofa);
        return 1;
    }
    std::printf ("[gen_hrir] SOFA cargado OK: len(taps)=%d sr=%.0f\n", len, kSr);

    {
        std::vector<float> irL ((size_t) len), irR ((size_t) len);
        float dL = 0.0f, dR = 0.0f;
        mysofa_getfilter_float (sofa, 0.0f, 1.0f, 0.0f, irL.data(), irR.data(), &dL, &dR);
        std::printf ("[gen] HrirData: %d taps delayL=%.4f delayR=%.4f\n", len, dL, dR);
        writeHrirData (repo + "/shared/data/HrirData.h",             "ovni::data",    len, dL, dR, irL, irR);
        writeHrirData (repo + "/shared/engines/binaural/HrirData.h", "ovni::engines", len, dL, dR, irL, irR);
    }

    // ---------------------------------------------------------------------------------
    // [genring] ANILLO de movimiento: 72 azimuts a elevacion 0.
    // ---------------------------------------------------------------------------------
    const int taps = len;
    std::vector<float> ringL ((size_t) kNumDirs * taps, 0.0f);
    std::vector<float> ringR ((size_t) kNumDirs * taps, 0.0f);
    std::vector<float> delayL ((size_t) kNumDirs, 0.0f);
    std::vector<float> delayR ((size_t) kNumDirs, 0.0f);

    {
        std::vector<float> irL ((size_t) taps), irR ((size_t) taps);
        for (int d = 0; d < kNumDirs; ++d)
        {
            const float theta = (float) d * kStepDeg * (3.14159265358979324f / 180.0f);
            const float x = std::cos (theta);   // frente
            const float y = std::sin (theta);   // izquierda
            float dl = 0.0f, dr = 0.0f;
            mysofa_getfilter_float (sofa, x, y, 0.0f, irL.data(), irR.data(), &dl, &dr);
            for (int t = 0; t < taps; ++t) {
                ringL[(size_t) d * taps + t] = irL[(size_t) t];
                ringR[(size_t) d * taps + t] = irR[(size_t) t];
            }
            delayL[(size_t) d] = dl;
            delayR[(size_t) d] = dr;
        }
    }
    mysofa_close (sofa);

    // Simetrizacion L/R (promedio con la direccion espejo). KU100 ya es simetrico -> casi no-op,
    // pero se conserva para mantener el pipeline IDENTICO al de orbita y garantizar simetria exacta.
    {
        const std::vector<float> oL (ringL), oR (ringR), oDL (delayL), oDR (delayR);
        for (int d = 0; d < kNumDirs; ++d)
        {
            const int dm = (kNumDirs - d) % kNumDirs;
            for (int t = 0; t < taps; ++t) {
                ringL[(size_t) d * taps + t] = 0.5f * (oL[(size_t) d * taps + t] + oR[(size_t) dm * taps + t]);
                ringR[(size_t) d * taps + t] = 0.5f * (oR[(size_t) d * taps + t] + oL[(size_t) dm * taps + t]);
            }
            delayL[(size_t) d] = 0.5f * (oDL[(size_t) d] + oDR[(size_t) dm]);
            delayR[(size_t) d] = 0.5f * (oDR[(size_t) d] + oDL[(size_t) dm]);
        }
    }

    // DFE parcial band-limitada + fase minima (cepstrum real). Identico a orbita.
    {
        const int order = 9;
        const int N = 1 << order;
        if (N < taps) { std::fprintf (stderr, "[gen_hrir] N(%d) < taps(%d)\n", N, taps); return 1; }
        juce::dsp::FFT fft (order);
        using C = std::complex<float>;

        std::vector<C> u ((size_t) N, C{}), uf ((size_t) N), ui ((size_t) N);
        u[0] = C { 1.0f, 0.0f };
        fft.perform (u.data(), uf.data(), false);
        fft.perform (uf.data(), ui.data(), true);
        const float invScale = 1.0f / ui[0].real();

        auto magOf = [&] (const float* ir, std::vector<float>& mag) {
            std::vector<C> in ((size_t) N, C{}), out ((size_t) N);
            for (int i = 0; i < taps; ++i) in[(size_t) i] = C { ir[i], 0.0f };
            fft.perform (in.data(), out.data(), false);
            mag.assign ((size_t) N, 0.0f);
            for (int k = 0; k < N; ++k) mag[(size_t) k] = std::abs (out[(size_t) k]);
        };

        auto midBandDiffuse = [&] (std::vector<float>& mgScratch, int b1, int b2) {
            std::vector<double> p ((size_t) N, 0.0);
            for (int d = 0; d < kNumDirs; ++d) {
                magOf (&ringL[(size_t) d * taps], mgScratch); for (int k=0;k<N;++k) p[(size_t)k]+=(double)mgScratch[(size_t)k]*mgScratch[(size_t)k];
                magOf (&ringR[(size_t) d * taps], mgScratch); for (int k=0;k<N;++k) p[(size_t)k]+=(double)mgScratch[(size_t)k]*mgScratch[(size_t)k];
            }
            double ls = 0.0;
            for (int k = b1; k <= b2; ++k) ls += std::log (std::max ((float) std::sqrt (p[(size_t)k] / (2.0 * kNumDirs)), 1.0e-9f));
            return std::pair<float, std::vector<double>> { (float) std::exp (ls / std::max (1, b2 - b1 + 1)), std::move (p) };
        };

        auto binHz = [&] (double hz) { return juce::jlimit (1, N/2, (int) std::round (hz / ((double) kSr / N))); };
        const int b1 = binHz (300.0), b2 = binHz (6000.0);

        std::vector<float> mg;
        auto [Dref, pacc] = midBandDiffuse (mg, b1, b2);
        std::vector<float> D ((size_t) N);
        for (int k = 0; k < N; ++k) D[(size_t)k] = (float) std::sqrt (pacc[(size_t)k] / (2.0 * kNumDirs));

        float rawMaxL1 = 0.0f;
        for (int d = 0; d < kNumDirs; ++d) {
            float sL = 0.0f, sR = 0.0f;
            for (int t = 0; t < taps; ++t) { sL += std::abs (ringL[(size_t) d * taps + t]); sR += std::abs (ringR[(size_t) d * taps + t]); }
            rawMaxL1 = std::max (rawMaxL1, std::max (sL, sR));
        }

        float rawMaxMag = 0.0f;
        for (int d = 0; d < kNumDirs; ++d) {
            magOf (&ringL[(size_t) d * taps], mg); for (int k=0;k<N;++k) rawMaxMag = std::max (rawMaxMag, mg[(size_t)k]);
            magOf (&ringR[(size_t) d * taps], mg); for (int k=0;k<N;++k) rawMaxMag = std::max (rawMaxMag, mg[(size_t)k]);
        }

        const float gMax = 2.0f, gMin = 0.25f;
        const float alpha0 = 0.6f;
        const int   bLoFull = binHz (3000.0), bHiZero = binHz (6000.0);
        std::vector<float> G ((size_t) N, 1.0f);
        for (int k = 0; k <= N/2; ++k) {
            const float raw = juce::jlimit (gMin, gMax, Dref / std::max (D[(size_t)k], 1.0e-9f));
            float a;
            if (k <= bLoFull)       a = alpha0;
            else if (k >= bHiZero)  a = 0.0f;
            else                    a = alpha0 * (1.0f - (float) (k - bLoFull) / (float) std::max (1, bHiZero - bLoFull));
            G[(size_t)k] = std::pow (raw, a);
        }
        G[0] = 1.0f;
        for (int k = 1; k < N/2; ++k) G[(size_t)(N - k)] = G[(size_t)k];

        auto minPhase = [&] (float* ir) {
            std::vector<float> mag; magOf (ir, mag);
            for (int k = 0; k < N; ++k) mag[(size_t)k] *= G[(size_t)k];
            std::vector<C> X ((size_t) N), x ((size_t) N);
            for (int k = 0; k < N; ++k) X[(size_t)k] = C { std::log (std::max (mag[(size_t)k], 1.0e-9f)), 0.0f };
            fft.perform (X.data(), x.data(), true);
            for (int k = 0; k < N; ++k) x[(size_t)k] *= invScale;
            std::vector<C> w ((size_t) N, C{});
            w[0] = C { x[0].real(), 0.0f };
            for (int n = 1; n < N/2; ++n) w[(size_t)n] = C { 2.0f * x[(size_t)n].real(), 0.0f };
            w[(size_t)(N/2)] = C { x[(size_t)(N/2)].real(), 0.0f };
            std::vector<C> W ((size_t) N); fft.perform (w.data(), W.data(), false);
            for (int k = 0; k < N; ++k) W[(size_t)k] = std::exp (W[(size_t)k]);
            std::vector<C> h ((size_t) N); fft.perform (W.data(), h.data(), true);
            for (int i = 0; i < taps; ++i) ir[i] = h[(size_t)i].real() * invScale;
        };
        for (int d = 0; d < kNumDirs; ++d) {
            minPhase (&ringL[(size_t) d * taps]);
            minPhase (&ringR[(size_t) d * taps]);
        }

        float newMaxL1 = 0.0f;
        for (int d = 0; d < kNumDirs; ++d) {
            float sL = 0.0f, sR = 0.0f;
            for (int t = 0; t < taps; ++t) { sL += std::abs (ringL[(size_t) d * taps + t]); sR += std::abs (ringR[(size_t) d * taps + t]); }
            newMaxL1 = std::max (newMaxL1, std::max (sL, sR));
        }

        const float kTargetE = 0.70f;
        float safeGain = 0.0f;
        for (int d = 0; d < kNumDirs; ++d) {
            double e = 0.0;
            for (int t = 0; t < taps; ++t) {
                const float L = ringL[(size_t) d * taps + t], R = ringR[(size_t) d * taps + t];
                e += (double) L * L + (double) R * R;
            }
            const float s = kTargetE / std::max ((float) std::sqrt (e), 1.0e-9f);
            for (int t = 0; t < taps; ++t) { ringL[(size_t) d * taps + t] *= s; ringR[(size_t) d * taps + t] *= s; }
            if (d == 0) safeGain = s;
        }

        float newMaxMag = 0.0f;
        for (int d = 0; d < kNumDirs; ++d) {
            magOf (&ringL[(size_t) d * taps], mg); for (int k=0;k<N;++k) newMaxMag = std::max (newMaxMag, mg[(size_t)k]);
            magOf (&ringR[(size_t) d * taps], mg); for (int k=0;k<N;++k) newMaxMag = std::max (newMaxMag, mg[(size_t)k]);
        }

        bool allFinite = true;
        for (float v : ringL) if (! std::isfinite (v)) allFinite = false;
        for (float v : ringR) if (! std::isfinite (v)) allFinite = false;
        if (! allFinite) { std::fprintf (stderr, "[gen_hrir] FALLO: anillo con NaN/Inf\n"); return 1; }
        if (newMaxMag > rawMaxMag * 1.05f) {
            std::fprintf (stderr, "[gen_hrir] FALLO cut-only: newMaxMag(%.4f) > rawMaxMag(%.4f)*1.05\n", newMaxMag, rawMaxMag);
            return 1;
        }
        std::printf ("[genring] DFE cut-only OK: rawMaxMag=%.4f newMaxMag=%.4f | rawMaxL1=%.4f newMaxL1=%.4f safeGain=%.4f\n",
                     rawMaxMag, newMaxMag, rawMaxL1, newMaxL1, safeGain);
    }

    writeHrirRing (repo + "/shared/data/HrirRing.h",             "ovni::data",    taps, ringL, ringR, delayL, delayR);
    writeHrirRing (repo + "/shared/engines/binaural/HrirRing.h", "ovni::engines", taps, ringL, ringR, delayL, delayR);

    std::printf ("[gen_hrir] OK: 4 headers escritos (data + engines/binaural), %d dirs x %d taps.\n", kNumDirs, taps);
    return 0;
}
