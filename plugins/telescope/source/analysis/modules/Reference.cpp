#include "analysis/modules/Reference.h"
#include <algorithm>

namespace telescope
{
void Reference::resetLive() noexcept
{
    live.reset();
    liveIntegrated      = kSilenceDb;
    liveIntegratedValid = false;
}

void Reference::setLiveLoudness (float integratedLufs, bool valid) noexcept
{
    liveIntegrated      = integratedLufs;
    liveIntegratedValid = valid;
}

// La referencia se COPIA (ver la nota de hilos del header). Un análisis cancelado o con error NO se carga:
// sus números no son de todo el archivo, y usarlos como referencia sería comparar contra un pedazo.
void Reference::setReference (const FileAnalysis& a)
{
    Loaded l;
    if (a.ok && a.valid)
    {
        l.valid = true;
        std::copy (a.bandsDb, a.bandsDb + kNumBands, l.bands);
        l.integrated      = a.integratedLufs;
        l.integratedValid = a.integratedLufs > kSilenceDb;
        l.seconds         = (float) a.seconds;
        l.name            = a.name;
    }

    const juce::ScopedLock sl (lock);
    loaded = l;
}

void Reference::clearReference()
{
    const juce::ScopedLock sl (lock);
    loaded = Loaded{};
}

bool Reference::hasReference() const
{
    const juce::ScopedLock sl (lock);
    return loaded.valid;
}

juce::String Reference::referenceName() const
{
    const juce::ScopedLock sl (lock);
    return loaded.name;
}

// ========================================================================================================
// EL FRAME. Las dos curvas crudas, las dos normalizadas y el delta — con `bandValid` diciendo dónde el
// delta significa algo. Una banda que sólo mide uno de los dos lados NO da delta 0: da "no comparable".
//
// ===================== 57b: QUÉ CUENTA COMO "MEDIDA", Y POR QUÉ CAMBIÓ =====================
//
// Hasta el 57 el criterio era `> SpectrumFrame::kFloorDb`, y kFloorDb vale −200 dB. Eso no es un nivel
// medible: es un piso de GUARDA, el valor que se le pone a un acumulador vacío para que nada divida por
// cero. Con ese criterio, una banda que mide −123 dBFS —cien decibeles por debajo de cualquier piso de
// ruido real, o sea el redondeo del propio análisis— contaba como medición. De ahí salía el readout que
// Joaquín fotografió el 9-sep: «referencia −123.1 · delta +106.2 dB · 197 bins». El número existía; lo
// que no existía era la medición.
//
// El criterio nuevo es un nivel de verdad: −90 dBFS sobre la banda CRUDA (no la normalizada: normalizar
// mueve el cero según el loudness integrado, y lo que se quiere saber es si hubo señal). −90 dBFS está
// muy por debajo del piso de cualquier grabación —un archivo a 24 bits tiene su ruido de cuantización
// cerca de −144, pero una banda de ⅓ de octava de MÚSICA real no baja de −80 ni en silencio— y muy por
// encima del redondeo del análisis. Entre los dos hay 30 dB de margen para los dos lados.
// ========================================================================================================
namespace
{
// El piso de lo MEDIBLE. Ver el bloque de arriba: no confundir con SpectrumFrame::kFloorDb (−200), que
// es el valor de "acá no se escribió nada".
constexpr float kMeasurableDb = -90.0f;
}

void Reference::fill (ReferenceFrame& out) const
{
    out = ReferenceFrame{};

    live.bandsDb (out.liveBands);
    out.liveSeconds    = (float) live.seconds();
    out.liveIntegrated = liveIntegrated;
    out.liveValid      = liveIntegratedValid && out.liveSeconds >= ReferenceFrame::kMinLiveSeconds;

    Loaded l;
    {
        const juce::ScopedLock sl (lock);
        l = loaded;
    }

    out.refValid      = l.valid && l.integratedValid;
    out.refIntegrated = l.integrated;
    out.refSeconds    = l.seconds;
    l.name.copyToUTF8 (out.refName, (size_t) ReferenceFrame::kNameChars);

    for (int b = 0; b < kNumBands; ++b)
    {
        const bool liveHas = out.liveBands[b] >= kMeasurableDb && liveIntegratedValid;
        if (liveHas) out.liveNorm[b] = out.liveBands[b] - liveIntegrated;

        if (! l.valid) continue;
        out.refBands[b] = l.bands[b];

        const bool refHas = l.bands[b] >= kMeasurableDb && l.integratedValid;
        if (refHas) out.refNorm[b] = l.bands[b] - l.integrated;

        if (liveHas && refHas)
        {
            out.bandValid[b] = true;
            out.deltaDb[b]   = out.liveNorm[b] - out.refNorm[b];
        }
    }
}
}
