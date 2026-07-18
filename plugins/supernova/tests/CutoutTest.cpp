// [supernova][cutout][.gpu] — CUTOUT "nivel ultra" (pedido de campo: "cutout al palo = SOLO los puntos de
// la figura; ningún fondo que me quite la forma 3D"). Las máscaras REALES no son binarias: la cascada cae a
// saliencia (fondo ≈ 0.25..0.4) y Vision deja bordes suaves — con el mix lineal viejo, a cutout=1 el fondo
// retenía alpha=mask → NIEBLA residual sobre la escultura. El contrato nuevo (cutoutMask, curva de contraste):
//   · cutout=1 → fondo (mask < 0.5) MUERTO EXACTO (0), sujeto (mask > 0.72) a brillo PLENO, borde AA;
//   · cutout=0 → identidad byte-exacta (goldens intactos), con o sin máscara;
//   · gradualidad: a cutout medio el fondo se apaga progresivamente (ni binario ni inerte).
// Tag [.gpu]: se auto-saltea sin GPU Metal (CI sin Metal sigue verde).
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <cstring>
#include <cstdlib>
#include "render/metal/MetalRenderer.h"
#include "render/IRenderer.h"
#include "render/ParticleParams.h"
#include "analysis/AnalysisFrame.h"

namespace
{
constexpr int kGrid = 256;
constexpr int kPx   = 160;

// Imagen sintética partida al medio: IZQUIERDA = "fondo" BRILLANTE con máscara configurable (0.35 = el
// caso saliencia SUAVE; 0.0 = máscara dura de referencia), DERECHA = "sujeto" (máscara 0.92). Si el cutout
// deja pasar el fondo suave, la izquierda LUCE por el apilado aditivo.
struct SplitImage
{
    std::vector<uint8_t> rgba;
    std::vector<float>   mask;
    explicit SplitImage (float bgMask = 0.35f)
    {
        rgba.resize ((size_t) kGrid * kGrid * 4);
        mask.resize ((size_t) kGrid * kGrid);
        for (int y = 0; y < kGrid; ++y)
            for (int x = 0; x < kGrid; ++x)
            {
                const size_t i = (size_t) y * kGrid + x;
                const bool subject = x >= kGrid / 2;
                rgba[i * 4 + 0] = subject ? 255 : 205;
                rgba[i * 4 + 1] = subject ? 150 : 205;
                rgba[i * 4 + 2] = subject ?  70 : 225;
                rgba[i * 4 + 3] = 255;
                mask[i] = subject ? 0.92f : bgMask;
            }
    }
    supernova::SourceImage source (bool withMask) const
    {
        supernova::SourceImage s;
        s.rgba = rgba.data(); s.width = kGrid; s.height = kGrid;
        if (withMask) s.subjectMask = mask.data();
        return s;
    }
};

std::vector<uint8_t> renderFrames (supernova::MetalRenderer& r, const supernova::ParticleParams& pp, int frames)
{
    std::vector<uint8_t> rgba ((size_t) kPx * kPx * 4, 0);
    supernova::AnalysisFrame af;   // silencio
    for (int f = 0; f < frames; ++f)
        REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));
    return rgba;
}

// SNV_CUTOUT_DUMP=<dir> → vuelca los frames del test a PNG (mismo espíritu que SNV_THUMB_DUMP: el ojo
// confirma lo que el assert mide).
void maybeDump (const std::vector<uint8_t>& rgba, const char* name)
{
    const char* dir = std::getenv ("SNV_CUTOUT_DUMP");
    if (dir == nullptr) return;
    juce::Image img (juce::Image::ARGB, kPx, kPx, true);
    for (int y = 0; y < kPx; ++y)
        for (int x = 0; x < kPx; ++x)
        {
            const size_t i = ((size_t) y * kPx + x) * 4;
            img.setPixelAt (x, y, juce::Colour (rgba[i], rgba[i + 1], rgba[i + 2]));
        }
    juce::File f = juce::File (juce::String (dir)).getChildFile (juce::String (name) + ".png");
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream (img, os); }
}

// Luma máxima y media de una banda vertical [x0..x1) en píxeles.
struct BandStats { double maxL = 0.0, meanL = 0.0; };
BandStats band (const std::vector<uint8_t>& rgba, int x0, int x1)
{
    BandStats b; double sum = 0.0; int n = 0;
    for (int y = 0; y < kPx; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const size_t i = ((size_t) y * kPx + x) * 4;
            const double l = 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
            b.maxL = l > b.maxL ? l : b.maxL;
            sum += l; ++n;
        }
    b.meanL = n ? sum / n : 0.0;
    return b;
}

bool gpuOk (supernova::MetalRenderer& r)
{
    if (r.isAvailable()) return true;
    SUCCEED ("sin GPU Metal — test [cutout] saltado (CI)");
    return false;
}
} // namespace

TEST_CASE ("cutout=1: el fondo de máscara suave muere tan muerto como el lienzo vacío; el sujeto vive",
           "[supernova][cutout][.gpu]")
{
    supernova::ParticleParams pp;   // defaults (2D, sin figura, sin trails)
    pp.cutoutAmt = 1.0f;

    // Render con la máscara SUAVE del caso saliencia (fondo 0.35 — el que dejaba niebla con el mix lineal).
    supernova::MetalRenderer rSoft;
    if (! gpuOk (rSoft)) return;
    rSoft.prepare (kGrid, kGrid);
    SplitImage soft (0.35f);
    rSoft.uploadImage (soft.source (true));
    const auto frame = renderFrames (rSoft, pp, 8);
    maybeDump (frame, "cutout-100");

    // Referencia: la MISMA escena con máscara DURA (fondo 0.0) — el fondo matemáticamente imposible.
    // OJO: el lienzo del motor NO es negro puro (clear HDR 0.006/0.007/0.011, el casi-negro del sello) —
    // el contrato no es "cero absoluto" sino "NADA de luz de partículas sobre el lienzo".
    supernova::MetalRenderer rHard;
    rHard.prepare (kGrid, kGrid);
    SplitImage hard (0.0f);
    rHard.uploadImage (hard.source (true));
    const auto ref = renderFrames (rHard, pp, 8);

    // Banda IZQUIERDA con margen (el hervor idle apenas mueve; el bloom del sujeto no cruza medio frame):
    // BYTE-EXACTA contra la referencia — la máscara suave no deja ni un pixel de niebla residual.
    const int x1 = (int) (kPx * 0.35);
    bool bandEqual = true;
    for (int y = 0; y < kPx && bandEqual; ++y)
        for (int x = 0; x < x1 && bandEqual; ++x)
        {
            const size_t i = ((size_t) y * kPx + x) * 4;
            bandEqual = frame[i] == ref[i] && frame[i + 1] == ref[i + 1] && frame[i + 2] == ref[i + 2];
        }
    const auto bg = band (frame, 0, x1);
    INFO ("fondo suave: maxLuma=" << bg.maxL << " (lienzo vacío de referencia: " << band (ref, 0, x1).maxL << ")");
    REQUIRE (bandEqual);

    // El sujeto SIGUE VIVO (la escultura no se apaga con el fondo).
    const auto subj = band (frame, (int) (kPx * 0.55), kPx);
    INFO ("sujeto: meanLuma=" << subj.meanL);
    REQUIRE (subj.meanL > 2.0);
}

TEST_CASE ("cutout=0 con máscara ≡ byte-exacto a sin máscara (identidad, la promesa de los goldens)",
           "[supernova][cutout][.gpu]")
{
    supernova::ParticleParams pp;   // cutoutAmt = 0 default
    SplitImage img;

    supernova::MetalRenderer rA;
    if (! gpuOk (rA)) return;
    rA.prepare (kGrid, kGrid);
    rA.uploadImage (img.source (false));
    const auto a = renderFrames (rA, pp, 6);

    supernova::MetalRenderer rB;
    rB.prepare (kGrid, kGrid);
    rB.uploadImage (img.source (true));
    const auto b = renderFrames (rB, pp, 6);

    REQUIRE (std::memcmp (a.data(), b.data(), a.size()) == 0);
}

TEST_CASE ("cutout gradual: a medio knob el fondo se apaga progresivo (ni binario ni inerte)",
           "[supernova][cutout][.gpu]")
{
    SplitImage img;
    auto renderAt = [&img] (float amt)
    {
        supernova::MetalRenderer r;
        r.prepare (kGrid, kGrid);
        r.uploadImage (img.source (true));
        supernova::ParticleParams pp;
        pp.cutoutAmt = amt;
        return renderFrames (r, pp, 6);
    };

    supernova::MetalRenderer probe;
    if (! gpuOk (probe)) return;

    const auto at0  = band (renderAt (0.0f), 0, (int) (kPx * 0.35));
    const auto at05 = band (renderAt (0.5f), 0, (int) (kPx * 0.35));
    INFO ("fondo meanLuma: knob 0 = " << at0.meanL << " · knob 0.5 = " << at05.meanL);
    REQUIRE (at05.meanL > 0.1);              // no binario: a medio knob el fondo aún respira
    REQUIRE (at05.meanL < at0.meanL * 0.8);  // pero claramente más apagado que sin cutout
}
