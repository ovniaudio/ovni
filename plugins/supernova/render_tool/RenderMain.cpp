// OvniSupernovaRender — render offscreen determinista → PNG. NO abre ventana ni necesita permisos de Screen
// Recording: rinde a una MTLTexture y lee de vuelta a RGBA8. Dos modos:
//   · Interactivo (capturas QA): --width/--height/--frames/--out [--image f] [--preset n] [--kick-at F]
//                                [--ray-at F --ray-angle deg]
//   · Golden frames (§9.3/§9.4):  --scenario <name> --out-dir <dir> [--emit 0,10,20,40]
//     escenarios deterministas: idle · kick · sustained-bass · treble-shimmer · rms-breathe · explode-param
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <cstdio>
#include <cmath>
#include "render/metal/MetalRenderer.h"
#include "render/IRenderer.h"
#include "render/ParticleParams.h"
#include "analysis/AnalysisFrame.h"
#include "image/FactoryImage.h"
#include "image/ImageLoader.h"
#include "image/VisionField.h"
#include "image/ImageField.h"
#include "params/ParamMapping.h"
#include "presets/PresetTypes.h"
#include "render/RenderScenarios.h"

namespace
{
bool writePng (const std::vector<uint8_t>& rgba, int w, int h, const juce::File& of)
{
    juce::Image image (juce::Image::ARGB, w, h, false);
    {
        juce::Image::BitmapData bd (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const uint8_t* p = &rgba[((size_t) y * w + x) * 4];
                bd.setPixelColour (x, y, juce::Colour::fromRGBA (p[0], p[1], p[2], p[3]));
            }
    }
    of.deleteFile();
    auto os = std::unique_ptr<juce::FileOutputStream> (new juce::FileOutputStream (of));
    if (! os->openedOk()) return false;
    juce::PNGImageFormat png;
    return png.writeImageToStream (image, *os);
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    int w = 1280, h = 1280, frames = 90, kickAt = -1, rayAt = -1, motionMode = -1;
    float rayAngleDeg = 0.0f, sizePct = -1.0f, trailsPct = -1.0f;
    float intensityPct = -1.0f, chaosPct = -1.0f, glowPct = -1.0f;      // knobs héroe (medición de impacto)
    float blastPct = -1.0f, breathePct = -1.0f, gravityPct = NAN;       // gravity BIPOLAR → sentinela NaN
    float satPct = -1.0f, hueDeg = NAN, linksPct = -1.0f, variationPct = -1.0f;   // defaults "no tocar"
    // (hue/rotate/hue-cycle usan NaN de sentinela: son BIPOLARES, 0 es un valor legítimo que debe poder
    // ANULAR lo que diga un preset)
    float densityPct = -1.0f, scatterPct = -1.0f, speedPct = -1.0f, pumpPct = -1.0f;
    float rotateDps = NAN, hueCycDps = NAN;
    int kaleidoSeg = -1;
    // 3D + FIGURA: rot-x/rot-y/orbit son BIPOLARES → sentinela NaN (0 debe poder anular un preset).
    float depthPct = -1.0f, formPct = -1.0f, rotXDeg = NAN, rotYDeg = NAN, orbitDps = NAN;
    int figureMode = -1;
    int paletteIdx = -1;
    float colorAmtPct = -1.0f, bgPct = -1.0f;
    int shapeMode = -1;   // TODOS los flags que espejan params defaultean "no tocar" (o pisan a los presets)
    bool syphonSmoke = false, cutout = false;
    // QA de la INVARIANCIA (prompt 45): --sim-hz cambia el paso de la simulación (default 60 = el paso del
    // export y de los goldens) para comparar el mismo tramo de SEGUNDOS a 60 y a 120. Con el ancla de la
    // física en 120 Hz (D-43), el camino SIN pow() es --sim-hz 120, no el default. --luma imprime la
    // luminancia media del último cuadro (la medición de la invariancia al TAMAÑO: 1024² vs 1080p vs 4K).
    float simHz = 60.0f;
    bool  wantLuma = false, sizeInvariance = false;
    juce::String out = "/tmp/supernova_frame.png";
    juce::String imagePath, presetName, scenario, outDir, emitCsv;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a { argv[i] };
        auto next = [&] () -> juce::String { return (i + 1 < argc) ? juce::String { argv[++i] } : juce::String {}; };
        if      (a == "--width")     w = next().getIntValue();
        else if (a == "--height")    h = next().getIntValue();
        else if (a == "--frames")    frames = next().getIntValue();
        else if (a == "--kick-at")   kickAt = next().getIntValue();
        else if (a == "--ray-at")    rayAt = next().getIntValue();
        else if (a == "--ray-angle") rayAngleDeg = next().getFloatValue();
        else if (a == "--image")     imagePath = next();
        else if (a == "--preset")    presetName = next();
        else if (a == "--scenario")  scenario = next();
        else if (a == "--out-dir")   outDir = next();
        else if (a == "--emit")      emitCsv = next();
        else if (a == "--syphon-smoke") syphonSmoke = true;   // smoke headless del server (regresión del crash)
        else if (a == "--motion-mode") motionMode = next().getIntValue();   // prototipos de diseño (mockups)
        else if (a == "--cutout")    cutout = true;   // borrar el fondo (máscara de sujeto, modo escultura)
        else if (a == "--size")      sizePct = next().getFloatValue();   // knob SIZE 0..100 (verificación exposición)
        else if (a == "--intensity") intensityPct = next().getFloatValue();   // knob INTENSITY 0..100
        else if (a == "--chaos")     chaosPct = next().getFloatValue();       // knob CHAOS 0..100
        else if (a == "--glow")      glowPct = next().getFloatValue();        // knob GLOW 0..100
        else if (a == "--blast")     blastPct = next().getFloatValue();       // knob BLAST (radialGain) 0..100
        else if (a == "--breathe")   breathePct = next().getFloatValue();     // knob BREATHE 0..100
        else if (a == "--gravity")   gravityPct = next().getFloatValue();     // knob GRAVITY −100..100 (bipolar)
        else if (a == "--shape")     shapeMode = next().getIntValue();    // glifo 0..6 (vocabulario visual)
        else if (a == "--trails")    trailsPct = next().getFloatValue();  // estela 0..100 (caminos)
        else if (a == "--sat")       satPct = next().getFloatValue();     // saturación 0..100 (50 = neutro)
        else if (a == "--hue")       hueDeg = next().getFloatValue();     // rotación de tono en grados
        else if (a == "--links")     linksPct = next().getFloatValue();   // plexus 0..100 (conexiones)
        else if (a == "--variation") variationPct = next().getFloatValue();   // randomización curada 0..100
        else if (a == "--density")   densityPct = next().getFloatValue();     // fracción dibujada 0..100
        else if (a == "--scatter")   scatterPct = next().getFloatValue();     // imagen↔nube 0..100
        else if (a == "--speed")     speedPct = next().getFloatValue();       // timewarp 0..100 (50 = ×1)
        else if (a == "--rotate")    rotateDps = next().getFloatValue();      // giro de vista en °/s
        else if (a == "--pump")      pumpPct = next().getFloatValue();        // sidechain visual 0..100 (30 = clásico)
        else if (a == "--hue-cycle") hueCycDps = next().getFloatValue();      // deriva de tono en °/s
        else if (a == "--kaleido")   kaleidoSeg = next().getIntValue();       // espejos 0/2/4/6/8
        else if (a == "--depth")     depthPct = next().getFloatValue();       // volumen 3D 0..100
        else if (a == "--rot-x")     rotXDeg = next().getFloatValue();        // pitch de cámara en grados
        else if (a == "--rot-y")     rotYDeg = next().getFloatValue();        // yaw de cámara en grados
        else if (a == "--orbit")     orbitDps = next().getFloatValue();       // auto-órbita en °/s
        else if (a == "--figure")    figureMode = next().getIntValue();       // 0=Imagen..5=Hélice
        else if (a == "--form")      formPct = next().getFloatValue();        // fader imagen↔figura 0..100
        else if (a == "--palette")   paletteIdx = next().getIntValue();       // look del COLOR LAB (0=Original)
        else if (a == "--color-amt") colorAmtPct = next().getFloatValue();    // mix del gradient map 0..100
        else if (a == "--bg")        bgPct = next().getFloatValue();          // papel del look 0..100
        else if (a == "--sim-hz")    simHz = next().getFloatValue();        // pasos de simulación por segundo
        else if (a == "--luma")      wantLuma = true;                       // luminancia media del último cuadro
        else if (a == "--size-invariance") sizeInvariance = true;           // el glifo escala con min(w,h)/1024
        else if (a == "--out")       out = next();
    }

    supernova::MetalRenderer r;

    // --syphon-smoke: el camino EXACTO que abortaba Ableton (init del server sin metallib de bundle). Con el
    // patch, o el server queda válido (shader embebido compilado en runtime) o degrada limpio — nunca abort.
    if (syphonSmoke)
    {
        if (! r.isAvailable()) { std::fprintf (stderr, "[syphon-smoke] sin GPU\n"); return 2; }
        r.setSyphonEnabled (true);
        const bool active = r.isSyphonActive();
        std::fprintf (stderr, "[syphon-smoke] server %s\n", active ? "ACTIVO (init OK, sin abort)" : "degradó limpio (sin abort)");
        r.setSyphonEnabled (false);
        return active ? 0 : 1;
    }

    if (! r.isAvailable())
    {
        std::fprintf (stderr, "[supernova-render] sin GPU Metal — no se puede rendir offscreen\n");
        return 2;
    }

    r.prepare (512, 512);
    if (simHz > 0.0f) r.setOffscreenDt (1.0 / (double) simHz);
    r.setOffscreenSizeInvariance (sizeInvariance);   // apagado = camino legacy de los goldens
    if (imagePath.isNotEmpty())
    {
        auto loaded = supernova::ImageLoader::fromFile (juce::File (imagePath));
        if (! loaded.valid())
        {
            std::fprintf (stderr, "[supernova-render] no pude decodificar la imagen: %s\n", imagePath.toRawUTF8());
            return 4;
        }
        // Saliencia + máscara de sujeto (Vision) — igual que el hilo de decode del editor.
        loaded.saliency = supernova::visionSaliency (loaded.rgba.data(), loaded.width, loaded.height, 512, 512);
        loaded.subjectMask = supernova::ImageField::maskFromFlatBackground (loaded.rgba.data(),
                                                                            loaded.width, loaded.height, 512, 512);
        if (loaded.subjectMask.empty())
            loaded.subjectMask = supernova::visionSubjectMask (loaded.rgba.data(), loaded.width, loaded.height, 512, 512);
        if (loaded.subjectMask.empty() && ! loaded.saliency.empty())
            loaded.subjectMask = supernova::ImageField::maskFromSaliency (loaded.saliency, 512, 512);
        std::fprintf (stderr, "[supernova-render] imagen %dx%d (RF1)%s%s\n", loaded.width, loaded.height,
                      loaded.saliency.empty() ? "" : " + saliencia Vision",
                      cutout ? (loaded.subjectMask.empty() ? " (cutout PEDIDO pero sin máscara)" : " + CUTOUT") : "");
        r.uploadImage (loaded.source (true));   // la máscara siempre viaja; el knob decide
    }
    else
    {
        auto factory = supernova::makeFactoryImage (512, 512);
        r.uploadImage ({ factory.data(), 512, 512 });
    }

    supernova::ParticleParams pp;
    int presetIndex = 0;   // semilla del VARIATION (mismo criterio que el editor)
    if (presetName.isNotEmpty())
    {
        const auto& presets = ovni::presets::factoryPresets();
        const ovni::presets::FactoryPreset* chosen = nullptr;
        if (presetName.containsOnly ("0123456789"))
        {
            const int idx = presetName.getIntValue();
            if (idx >= 0 && idx < (int) presets.size()) chosen = &presets[(size_t) idx];
        }
        else
            for (const auto& p : presets)
                if (juce::String (p.name).containsIgnoreCase (presetName)) { chosen = &p; break; }

        if (chosen == nullptr)
        {
            std::fprintf (stderr, "[supernova-render] preset no encontrado: %s\n", presetName.toRawUTF8());
            return 7;
        }
        // Fallback = default REAL del APVTS (paramDefault, fuente única) — un id omitido por el preset debe
        // renderizar acá EXACTAMENTE como en el plugin (applyFactory resetea a defaults y aplica el overlay).
        pp = supernova::mapParticleParams ([chosen] (const char* id) -> float
        {
            for (const auto& pv : chosen->params)
                if (juce::String (pv.id) == id) return pv.value;
            return supernova::params::id::paramDefault (id);
        });
        presetIndex = (int) (chosen - presets.data());
        std::fprintf (stderr, "[supernova-render] preset '%s' (#%d)\n", chosen->name, presetIndex);
    }
    else if (scenario.isEmpty())
    {
        pp.intensity = 0.6f; pp.chaos = 0.35f; pp.particleSize = 1.5f;
    }
    if (motionMode >= 0) pp.motionMode = motionMode;   // sólo si se pidió explícito (default = el del motor)
    if (sizePct >= 0.0f) pp.particleSize = 0.5f + sizePct / 100.0f * 3.5f;   // mismo mapeo del editor
    if (intensityPct >= 0.0f) pp.intensity = intensityPct / 100.0f;
    if (chaosPct >= 0.0f)     pp.chaos = chaosPct / 100.0f;
    if (glowPct >= 0.0f)      pp.glow = glowPct / 100.0f;
    if (blastPct >= 0.0f)     pp.radialGain  = 0.20f + blastPct / 100.0f * 2.00f;    // mismo mapeo del editor
    if (breathePct >= 0.0f)   pp.breatheGain = breathePct / 100.0f * 0.56f;
    if (! std::isnan (gravityPct)) pp.gravity = gravityPct / 100.0f * 0.60f;
    // Overrides EXPLÍCITOS solamente — la trampa (b): un flag con default "activo" PISA lo que el preset
    // definió (los 30 mundos se renderizaban con shape=Dot/trails=0/cutout=0; el bug de --motion-mode, repetido).
    if (shapeMode >= 0)   pp.shapeMode = shapeMode;
    if (trailsPct >= 0.0f) pp.trailAmt = trailsPct / 100.0f;
    if (cutout)           pp.cutoutAmt = 1.0f;   // el flag solo FUERZA el recorte; sin flag manda el preset
    if (satPct >= 0.0f) pp.satAmt = satPct / 50.0f;               // 0..100 → 0..2 (50 = neutro, mismo mapeo del editor)
    if (! std::isnan (hueDeg)) pp.hueShift = hueDeg * 3.14159265f / 180.0f;
    if (linksPct >= 0.0f) pp.linksAmt = linksPct / 100.0f;
    if (densityPct >= 0.0f) pp.densityAmt = juce::jlimit (0.01f, 1.0f, densityPct / 100.0f);
    if (scatterPct >= 0.0f) pp.scatterAmt = scatterPct / 100.0f;
    if (speedPct >= 0.0f)   pp.speedMul = 0.25f * std::pow (16.0f, speedPct / 100.0f);   // mismo mapeo log del editor
    if (! std::isnan (rotateDps)) pp.rotateRate = rotateDps * 0.01745329252f;
    if (pumpPct >= 0.0f)    pp.pumpAmt = pumpPct / 100.0f * 2.0f;
    if (! std::isnan (hueCycDps)) pp.hueCycleRate = hueCycDps * 0.01745329252f;
    if (kaleidoSeg >= 0)    pp.kaleidoSeg = kaleidoSeg;
    if (depthPct >= 0.0f)   pp.depthAmt = depthPct / 100.0f;
    if (! std::isnan (rotXDeg))  pp.rotXRad = rotXDeg * 0.01745329252f;
    if (! std::isnan (rotYDeg))  pp.rotYRad = rotYDeg * 0.01745329252f;
    if (! std::isnan (orbitDps)) pp.orbitRate = orbitDps * 0.01745329252f;
    if (figureMode >= 0)    pp.formMode = juce::jlimit (0, 5, figureMode);
    if (formPct >= 0.0f)    pp.formAmt = formPct / 100.0f;
    if (paletteIdx >= 0)    pp.paletteIdx = paletteIdx;
    if (colorAmtPct >= 0.0f) pp.rampAmt = colorAmtPct / 100.0f;
    if (bgPct >= 0.0f)      pp.bgAmt = bgPct / 100.0f;
    if (variationPct >= 0.0f)
        supernova::applyVariation (pp, presetIndex, variationPct / 100.0f);   // mismo motor que el editor

    // Modo golden: emitir N PNGs numerados a --out-dir (subconjunto --emit, o todos).
    const bool goldenMode = outDir.isNotEmpty();
    juce::File dir (outDir);
    if (goldenMode) dir.createDirectory();
    juce::StringArray emitList;
    if (emitCsv.isNotEmpty()) emitList.addTokens (emitCsv, ",", "");

    std::vector<uint8_t> rgba ((size_t) w * h * 4);
    bool ok = false;
    for (int f = 0; f < frames; ++f)                 // cada llamada avanza la sim (dt fijo) → determinista
    {
        supernova::AnalysisFrame af;
        if (scenario.isNotEmpty())
        {
            af = supernova::scenarioFrame (scenario.toRawUTF8(), f, frames);
            pp.explode = supernova::scenarioExplode (scenario.toRawUTF8(), f, frames) ? 1.0f : 0.0f;
        }
        else                                          // modo interactivo
        {
            af.onset = (kickAt >= 0 && f == kickAt);
            // La cola de graves del kick dura los mismos SEGUNDOS a cualquier --sim-hz (4 cuadros a 60 Hz,
            // que es el valor histórico exacto): si no, comparar 60 con 120 compararía dos audios distintos.
            const int bassFrames = juce::jmax (1, juce::roundToInt (4.0f * simHz / 60.0f));
            af.bass  = (kickAt >= 0 && f >= kickAt && f < kickAt + bassFrames) ? 0.85f : 0.08f;
            af.rms   = 0.30f; af.energy = 0.30f; af.treble = 0.12f;
            pp.rayTrigger = (rayAt >= 0 && f == rayAt);
            pp.rayAngle   = rayAngleDeg * 3.14159265f / 180.0f;
        }
        ok = r.renderOffscreen (af, pp, w, h, rgba.data());
        if (! ok) break;

        if (goldenMode && (emitList.isEmpty() || emitList.contains (juce::String (f))))
        {
            const juce::File frameFile = dir.getChildFile (juce::String::formatted ("frame_%04d.png", f));
            if (! writePng (rgba, w, h, frameFile))
            {
                std::fprintf (stderr, "[supernova-render] no pude escribir %s\n", frameFile.getFullPathName().toRawUTF8());
                return 5;
            }
        }
    }

    if (! ok) { std::fprintf (stderr, "[supernova-render] renderOffscreen falló\n"); return 3; }

    if (wantLuma)   // Rec.709 sobre el cuadro final, en 0..255 — la métrica de "el 4K sale más oscuro"
    {
        double sum = 0.0; long lit = 0;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        {
            const double y = 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
            sum += y; if (y > 16.0) ++lit;
        }
        const double n = (double) (w * h);
        std::fprintf (stderr, "[supernova-render] luma %.4f  encendidos %.4f%%  (%dx%d, sim %.0f Hz, size-inv %d)\n",
                      sum / n, 100.0 * (double) lit / n, w, h, simHz, sizeInvariance ? 1 : 0);
    }

    if (goldenMode)
    {
        std::fprintf (stderr, "[supernova-render] escenario '%s' → %s (%dx%d, %d frames)\n",
                      scenario.toRawUTF8(), dir.getFullPathName().toRawUTF8(), w, h, frames);
        return 0;
    }

    if (! writePng (rgba, w, h, juce::File (out)))
    {
        std::fprintf (stderr, "[supernova-render] falló la escritura del PNG: %s\n", out.toRawUTF8());
        return 6;
    }
    std::fprintf (stderr, "[supernova-render] escrito %s (%dx%d, %d frames)\n", out.toRawUTF8(), w, h, frames);
    return 0;
}
