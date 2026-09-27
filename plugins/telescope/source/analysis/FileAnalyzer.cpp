#include "analysis/FileAnalyzer.h"
#include <algorithm>
#include <cmath>

namespace telescope
{
namespace
{
// Plazo de gracia de cancel(). El worker chequea la salida una vez por bloque de 4 096 muestras, o sea
// ~85 µs de trabajo real: sale enseguida. El plazo existe para que un I/O trabado no cuelgue la UI.
constexpr int kCancelGraceMs = 2000;
}

FileAnalyzer::FileAnalyzer() : juce::Thread ("TelescopeFileAnalyzer")
{
}

FileAnalyzer::~FileAnalyzer()
{
    cancel();
}

void FileAnalyzer::start (const juce::File& f, std::optional<double> fromS, std::optional<double> toS)
{
    cancel();   // uno a la vez (ver el header)

    {
        const juce::ScopedLock sl (lock);
        pendingName = f.getFileName();
    }

    pending     = f;
    pendingFrom = fromS;
    pendingTo   = toS;
    progressValue.store (0.0f, std::memory_order_release);
    running.store (true, std::memory_order_release);
    startThread();
}

void FileAnalyzer::cancel()
{
    if (! isThreadRunning() && ! running.load (std::memory_order_acquire)) return;

    if (! stopThread (kCancelGraceMs))
    {
        juce::Logger::writeToLog ("TELESCOPE: el analizador de archivo no salio en "
                                  + juce::String (kCancelGraceMs) + " ms; esperando sin plazo");
        stopThread (-1);
    }
    running.store (false, std::memory_order_release);
}

juce::String FileAnalyzer::currentName() const
{
    const juce::ScopedLock sl (lock);
    return pendingName;
}

FileAnalysis FileAnalyzer::result() const
{
    const juce::ScopedLock sl (lock);
    return latest;
}

FileAnalyzer::Span FileAnalyzer::resultSpan() const
{
    const juce::ScopedLock sl (lock);
    return latestSpan;
}

void FileAnalyzer::publish (const RangeMeasurement& m)
{
    Span span;
    span.whole   = ! m.requestedFromS.has_value() && ! m.requestedToS.has_value();
    span.clipped = m.clipped;
    if (m.analysis.sr > 0.0)
    {
        span.fromS = (double) m.range.start / m.analysis.sr;
        span.toS   = (double) m.range.end   / m.analysis.sr;
    }
    {
        const juce::ScopedLock sl (lock);
        latest     = m.analysis;
        latestSpan = span;
    }
    rev.fetch_add (1, std::memory_order_acq_rel);
}

void FileAnalyzer::run()
{
    const auto m = analyse (pending);
    publish (m);
    progressValue.store (1.0f, std::memory_order_release);
    running.store (false, std::memory_order_release);
    if (onFinished) onFinished();
}

// ========================================================================================================
// EL ANÁLISIS: el archivo entero, por la misma función que mide un tramo (RangeAnalysis.h). Hasta el prompt 101
// el bucle vivía acá; se mudó sin cambiar una operación, y [golden] lo sostiene al bit.
// ========================================================================================================
//
// F4 (T6): con un tramo pedido, la MISMA función mide ese tramo. Sin tramo, la llamada es la de siempre.
RangeMeasurement FileAnalyzer::analyse (const juce::File& f)
{
    return engine.measure (f, pendingFrom, pendingTo,
                           [this] { return threadShouldExit(); },
                           [this] (float p) { progressValue.store (p, std::memory_order_release); });
}
}
