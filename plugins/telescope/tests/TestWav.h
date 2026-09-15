#pragma once
#include <cmath>
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

// ========================================================================================================
// TestWav — escribe los WAV que necesitan los tests del análisis de archivo, y los vuelve a leer.
//
// DÓNDE VAN LOS ARCHIVOS. En `build/tests-tmp/` del worktree, NUNCA en /tmp ni en ~/Downloads: /tmp lo
// comparten todas las obreras que corran a la vez, y dos tests escribiendo "pink.wav" al mismo tiempo se
// pisan sin que ninguno se entere. La carpeta se busca subiendo desde el ejecutable hasta encontrar
// `build/`; si no aparece (alguien movió el exe), se cae al directorio temporal del sistema con un nombre
// propio. La ruta se imprime, así que nunca hay que adivinar dónde quedó.
//
// 24 BITS, no float. Es el formato en el que llega un archivo de verdad, y —lo que importa acá— NO cambia
// nada de la prueba de identidad: el test empuja por processBlock las MISMAS muestras decodificadas que
// lee el analizador, así que los dos lados ven exactamente los mismos float. Cuantizar es del archivo,
// no de la comparación.
// ========================================================================================================
namespace telescope::test
{
// build/tests-tmp/ del worktree (ver el encabezado).
inline juce::File tempDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (int i = 0; i < 10 && d.getFullPathName().isNotEmpty(); ++i)
    {
        d = d.getParentDirectory();
        if (d.getFileName() == "build")
        {
            auto t = d.getChildFile ("tests-tmp");
            t.createDirectory();
            return t;
        }
    }

    auto t = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ovni-telescope-tests");
    t.createDirectory();
    return t;
}

// Escribe `frames` muestras generadas por `gen(i) -> {L, R}`. Con `channels == 1` se escribe SÓLO el
// canal izquierdo (es el caso "archivo mono" del test). Devuelve el archivo escrito.
inline juce::File writeWav (const juce::String& name, double sr, int channels, juce::int64 frames,
                            const std::function<std::pair<float, float> (juce::int64)>& gen,
                            int bitsPerSample = 24)
{
    auto f = tempDir().getChildFile (name);
    f.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    jassert (os != nullptr);
    std::unique_ptr<juce::AudioFormatWriter> w (
        wav.createWriterFor (os.release(), sr, (unsigned int) channels, bitsPerSample, {}, 0));
    jassert (w != nullptr);
    if (w == nullptr) return f;

    constexpr int kBlock = 4096;
    juce::AudioBuffer<float> buf (channels, kBlock);
    for (juce::int64 done = 0; done < frames; )
    {
        const int n = (int) juce::jmin ((juce::int64) kBlock, frames - done);
        // Con más de dos canales `gen` sólo llena L y R: el resto tiene que salir en SILENCIO y no con la
        // basura que el AudioBuffer traiga de fábrica (hace falta para el caso "archivo de 6 canales").
        buf.clear();
        for (int i = 0; i < n; ++i)
        {
            const auto v = gen (done + i);
            buf.setSample (0, i, v.first);
            if (channels > 1) buf.setSample (1, i, v.second);
        }
        w->writeFromAudioSampleBuffer (buf, 0, n);
        done += n;
    }
    w.reset();   // cierra y actualiza las cabeceras
    return f;
}

// Lee el archivo entero a memoria SIEMPRE COMO ESTÉREO (mono → L = R, igual que processAudio). Es lo que
// se empuja por processBlock en la prueba de identidad: los dos lados tienen que ver los mismos float.
inline juce::AudioBuffer<float> readWavStereo (const juce::File& f, double& srOut)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    jassert (r != nullptr);
    if (r == nullptr) { srOut = 0.0; return {}; }

    srOut = r->sampleRate;
    const int n = (int) r->lengthInSamples;
    juce::AudioBuffer<float> src ((int) juce::jmax (1u, r->numChannels), n);
    src.clear();
    r->read (&src, 0, n, 0, true, r->numChannels > 1);

    juce::AudioBuffer<float> out (2, n);
    out.copyFrom (0, 0, src, 0, 0, n);
    out.copyFrom (1, 0, src, src.getNumChannels() > 1 ? 1 : 0, 0, n);
    return out;
}
}
