#pragma once
// Fachada CROSS-PLATFORM de captura del audio del SISTEMA, sin drivers.
//   mac (macOS 13+): ScreenCaptureKit (SCStream capturesAudio) — pide permiso de Screen Recording.
//   Windows        : WASAPI loopback — NO IMPLEMENTADO (M5). Hasta que exista, la regla es que la falta se
//                    declare ANTES de intentar: `isSupported()` y `hasPermission()` tienen que devolver
//                    FALSE en cualquier plataforma sin backend, no true. Prometer `hasPermission() == true`
//                    y que después `start()` falle es ofrecerle al usuario algo que no hay (la barra
//                    mostraría ALLOW y el intento moriría en silencio).
// Si no se puede (permiso negado / no soportado) start() devuelve false y el caller cae al device.
// Patrón pimpl como VideoSource: ningún tipo de plataforma aparece en el header.
//
// Threading: la captura corre en la cola propia de SCStream; el callback entrega TODOS los frames
// (NO latest-wins — dropear bloques de audio = cortes). El FIFO del processor absorbe el ritmo.
#include <functional>
#include <memory>
#include <juce_core/juce_core.h>

namespace supernova {

class SystemAudioSource
{
public:
    enum class Status { idle, capturing, permissionDenied, unsupported, error };

    // chans = punteros por-canal Float32 (planar); numFrames por canal; sr = sample rate real.
    using SampleCallback = std::function<void (const float* const* chans, int numCh, int numFrames, double sr)>;

    SystemAudioSource();
    ~SystemAudioSource();

    bool start (int sampleRate, int channels, SampleCallback onSamples) noexcept; // false si no se pudo
    void stop() noexcept;
    Status status() const noexcept;

    static bool isSupported() noexcept;    // macOS 13+ true; sin backend (macOS 11/12, Windows hoy) false
    static bool hasPermission() noexcept;  // estado TCC de Screen Recording EN VIVO; sin backend, false

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SystemAudioSource)
};

} // namespace supernova
