#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "analysis/LockFreeAudioFifo.h"
#include "analysis/TripleBuffer.h"
#include "analysis/AudioAnalyzer.h"
#include "analysis/AnalysisFrame.h"

// AnalysisThread — worker que drena el FIFO (samples del audio thread), corre el AudioAnalyzer y PUBLICA el
// último AnalysisFrame al TripleBuffer (leído por el render). Desacoplado del audio thread (RNF1) y del
// message thread. Es el "AnalysisThread" del §5 del spec.
namespace supernova
{
class AnalysisThread : public juce::Thread
{
public:
    AnalysisThread (LockFreeAudioFifo& f, TripleBuffer<AnalysisFrame>& o)
        : juce::Thread ("SupernovaAnalysis"), fifo (f), out (o) {}

    ~AnalysisThread() override { stopThread (1000); }

    void prepare (double sampleRate, int fftOrder = 11)
    {
        stopThread (1000);
        analyzer.prepare (sampleRate, fftOrder);
        chunk.assign ((size_t) juce::jmax (256, analyzer.hopSize() * 4), 0.0f);
        startThread();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            const int got = fifo.pop (chunk.data(), (int) chunk.size());
            if (got > 0)
            {
                analyzer.push (chunk.data(), got);
                AnalysisFrame f; bool any = false;
                while (analyzer.popFrame (f)) any = true;   // drena; publica el último (latest-wins)
                if (any) { out.writeSlot() = f; out.publish(); }
            }
            else
            {
                wait (2);   // ms — nada que analizar todavía
            }
        }
    }

private:
    LockFreeAudioFifo&            fifo;
    TripleBuffer<AnalysisFrame>&  out;
    AudioAnalyzer                 analyzer;
    std::vector<float>            chunk;
};
}
