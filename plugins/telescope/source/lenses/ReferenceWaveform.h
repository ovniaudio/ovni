#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

namespace telescope
{
// ========================================================================================================
// ReferenceWaveform — la forma de onda de la referencia de TONAL BALANCE, para la tira de T6 (F4 de la 0.2).
//
// Es un DIBUJO del archivo, no una medición: el mínimo y el máximo de las muestras (los dos canales juntos)
// en `kBuckets` tramos iguales del archivo entero. No pasa por el motor, no toca ningún número de nadie y
// vive del lado de la lente (la medición del tramo es RangeAnalyzer::measure, en el analizador de archivo
// del processor).
//
// SE LEE EN UN HILO PROPIO. Leer un tema de cinco minutos son unos cientos de milisegundos: ni en el hilo de
// audio ni bloqueando el de la interfaz. La lente pide `load (archivo)` y lee `latest()` en cada cuadro;
// mientras se calcula, `latest()` es lo anterior (o nada). Un archivo nuevo cancela el anterior.
// ========================================================================================================
class ReferenceWaveform : private juce::Thread
{
public:
    // 2 048 tramos: la tira más ancha (L a 2×) mide unos 2 000 píxeles físicos.
    static constexpr int kBuckets = 2048;

    struct Overview
    {
        juce::String path;
        double       seconds = 0.0;
        std::vector<float> lo, hi;   // kBuckets cada uno, en [−1, 1] (se recorta a ±1 al dibujar)
        bool ok() const noexcept { return seconds > 0.0 && (int) lo.size() == kBuckets; }
    };

    ReferenceWaveform() : juce::Thread ("TelescopeRefWaveform") { formats.registerBasicFormats(); }
    ~ReferenceWaveform() override { stopThread (2000); }

    // Pide la forma de onda de `f`. Si ya es la que hay (o la que se está calculando), no hace nada.
    void load (const juce::File& f)
    {
        const auto path = f.getFullPathName();
        {
            const juce::ScopedLock sl (lock);
            if (path == requested) return;
            requested = path;
        }
        stopThread (2000);
        if (path.isEmpty())
        {
            const juce::ScopedLock sl (lock);
            current.reset();
            return;
        }
        pending = f;
        startThread (juce::Thread::Priority::low);
    }

    std::shared_ptr<const Overview> latest() const
    {
        const juce::ScopedLock sl (lock);
        return current;
    }

    bool busy() const noexcept { return isThreadRunning(); }

    // Cambia cada vez que se publica una forma de onda (la lente compara contra la suya para rehornear).
    juce::uint32 revision() const noexcept { return rev.load (std::memory_order_acquire); }

private:
    void run() override
    {
        auto o = std::make_shared<Overview>();
        o->path = pending.getFullPathName();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (pending));
        if (reader != nullptr && reader->sampleRate > 0.0 && reader->lengthInSamples > 0)
        {
            const auto total = (juce::int64) reader->lengthInSamples;
            o->seconds = (double) total / reader->sampleRate;
            o->lo.assign ((size_t) kBuckets, 0.0f);
            o->hi.assign ((size_t) kBuckets, 0.0f);
            const int ch = juce::jlimit (1, 2, (int) reader->numChannels);
            juce::Range<float> r[2];
            for (int b = 0; b < kBuckets; ++b)
            {
                if (threadShouldExit()) return;
                const auto s0 = total * b / kBuckets, s1 = total * (b + 1) / kBuckets;
                if (s1 <= s0) continue;
                reader->readMaxLevels (s0, s1 - s0, r, ch);
                float lo = r[0].getStart(), hi = r[0].getEnd();
                if (ch > 1) { lo = juce::jmin (lo, r[1].getStart()); hi = juce::jmax (hi, r[1].getEnd()); }
                o->lo[(size_t) b] = lo;
                o->hi[(size_t) b] = hi;
            }
        }

        {
            const juce::ScopedLock sl (lock);
            if (o->path != requested) return;   // lo pidieron otro mientras tanto
            current = o;
        }
        rev.fetch_add (1, std::memory_order_acq_rel);
    }

    juce::AudioFormatManager formats;
    juce::File pending;
    mutable juce::CriticalSection lock;
    juce::String requested;
    std::shared_ptr<const Overview> current;
    std::atomic<juce::uint32> rev { 0 };

    JUCE_DECLARE_NON_COPYABLE (ReferenceWaveform)
};
}
