#pragma once
#include <juce_core/juce_core.h>
#include "analysis/FileAnalysis.h"
#include "analysis/ReferenceFrame.h"
#include "analysis/modules/Spectrum.h"

// ========================================================================================================
// Reference — el módulo del bit kReference (spec §5.6, prompt 54): el lado VIVO de TONAL BALANCE.
//
// Dos mitades:
//
//   VIVO       un acumulador INFINITO de potencia por ⅓ de octava desde el RESET (ThirdOctaveAverage, el
//              mismo que usa el análisis de archivo — ver analysis/FileAnalysis.h), alimentado por el
//              FrameSink del módulo Spectrum: consume TODOS los frames calculados, no sólo los emitidos.
//              Que sean todos importa: con 87.5 % de solape se perderían 6 de cada 7, y el promedio
//              dependería de en qué pedazos entró el audio en vez de del audio. Más el LUFS integrado,
//              que se lo empuja el AnalysisThread desde el medidor (no hay un segundo medidor acá).
//
//              ES APARTE DEL PROMEDIADO DE DISPLAY DE SPECTRUM. Ese es exponencial y de 0.5 s por default,
//              y sirve para que la curva se mueva; éste es infinito desde el reset y sirve para que la
//              comparación signifique algo. Dos cosas distintas, dos acumuladores distintos.
//
//   REFERENCIA una FileAnalysis cargada de un archivo. Se copia acá adentro (30 floats, el integrado, el
//              nombre y la duración): el módulo no se queda con un puntero a algo que el message thread
//              puede reemplazar mientras el worker lo lee.
//
// HILOS. `spectrumFrameComputed` y `fill` corren en el worker; `setReference`/`clearReference` en el
// message thread (el usuario soltó un archivo). La parte compartida es un POD chico protegido por una
// CriticalSection que el worker toma UNA VEZ POR CHUNK (~10 veces por segundo), nunca en el audio thread.
// No es un camino caliente: es un dato que cambia cuando alguien arrastra un archivo.
// ========================================================================================================
namespace telescope
{
class Reference : public Spectrum::FrameSink
{
public:
    static constexpr int kNumBands = ReferenceFrame::kNumBands;

    // Explícito porque el macro de no-copiable declara el constructor de copia y eso suprime el default.
    Reference() = default;

    // ---- lado vivo (worker) ----
    void resetLive() noexcept;
    void spectrumFrameComputed (const Spectrum::FrameInfo& info) override { live.spectrumFrameComputed (info); }

    // El integrado del medidor de loudness, tal cual. `valid` es su `integratedValid`: sin integrado no
    // hay normalización posible, y una curva sin normalizar comparada contra una normalizada sería basura.
    void setLiveLoudness (float integratedLufs, bool valid) noexcept;

    // ---- lado referencia (message thread) ----
    void setReference (const FileAnalysis& a);
    void clearReference();
    bool hasReference() const;
    // Nombre y duración de la referencia cargada (para los textos de la lente sin copiar el análisis).
    juce::String referenceName() const;

    // ---- salida ----
    void fill (ReferenceFrame& out) const;

    // Lo que el acumulador vivo lleva medido. Público porque es la definición de "todavía no alcanza".
    double liveSeconds() const noexcept { return live.seconds(); }
    long long liveFrames() const noexcept { return live.frames(); }
    int  binsInBand (int band) const noexcept { return live.binsInBand (band); }

private:
    // La parte de la referencia que el worker necesita, copiada y chica.
    struct Loaded
    {
        bool  valid = false;
        float bands[kNumBands];
        float integrated = kSilenceDb;
        bool  integratedValid = false;
        float seconds = 0.0f;
        juce::String name;

        Loaded() { for (auto& b : bands) b = SpectrumFrame::kFloorDb; }
    };

    ThirdOctaveAverage live;
    float              liveIntegrated = kSilenceDb;
    bool               liveIntegratedValid = false;

    mutable juce::CriticalSection lock;
    Loaded                        loaded;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Reference)
};

// ========================================================================================================
// FrameSinkFanout — un FrameSink que reenvía a N (55/1e: N = 4).
//
// El módulo Spectrum dispara UN solo sink (Spectrum::setFrameSink), y desde el prompt 51 ese lugar lo
// ocupa StereoBands. TONAL BALANCE necesitó el mismo caño y VERDICT necesita otros dos (la referencia y
// la historia por segundo). En la práctica las lentes que los piden casi nunca están visibles a la vez
// —la máscara la fija LA lente visible— pero apoyar el motor en esa casualidad de la UI sería apoyarlo en
// nada. Con el fan-out, quien quiera engancharse se engancha y el módulo sigue disparando un sink.
//
// UN ARRAY CHICO Y FIJO, no dos punteros con nombre ni un vector: cuatro slots alcanzan para todo lo que
// el spec pide (§4: Spectrum lo comen StereoBands, Reference, la historia por segundo y —si algún día
// hace falta— uno más), el orden de reenvío es SIEMPRE el mismo (0 → 3, así el determinismo no depende
// de en qué orden se engancharon) y no hay una asignación de memoria en el camino del worker.
//
// El slot vacío no cuesta nada: es una comparación contra nullptr por frame.
// ========================================================================================================
struct FrameSinkFanout : Spectrum::FrameSink
{
    static constexpr int kSlots = 4;

    Spectrum::FrameSink* slot[kSlots] {};

    // Fuera de rango se ignora en vez de escribir al lado: esto lo llama el worker en cada vuelta.
    void set (int i, Spectrum::FrameSink* s) noexcept
    {
        if (i >= 0 && i < kSlots) slot[i] = s;
    }

    Spectrum::FrameSink* get (int i) const noexcept
    {
        return (i >= 0 && i < kSlots) ? slot[i] : nullptr;
    }

    void clear() noexcept { for (auto*& p : slot) p = nullptr; }

    bool empty() const noexcept
    {
        for (auto* p : slot) if (p != nullptr) return false;
        return true;
    }

    int count() const noexcept
    {
        int n = 0;
        for (auto* p : slot) if (p != nullptr) ++n;
        return n;
    }

    void spectrumFrameComputed (const Spectrum::FrameInfo& info) override
    {
        for (auto* p : slot) if (p != nullptr) p->spectrumFrameComputed (info);
    }
};
}
