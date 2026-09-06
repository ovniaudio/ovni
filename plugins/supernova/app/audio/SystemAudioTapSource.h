#pragma once
// SystemAudioTapSource — captura del audio del SISTEMA por Core Audio PROCESS TAPS (macOS 14.2+), sin
// drivers y SIN el permiso de pantalla: macOS pide el permiso chico, "System Audio Recording Only"
// (kTCCServiceAudioCapture). Es el backend por default desde 0.3.1 (D-33); ScreenCaptureKit queda de
// fallback para macOS 13 … 14.1, donde esta API todavía no existe.
//
// Receta: CATapDescription global estéreo → AudioHardwareCreateProcessTap → AudioHardwareCreateAggregateDevice
// con el tap en kAudioAggregateDeviceTapListKey → AudioDeviceIOProc sobre el aggregate → push al callback.
// Patrón pimpl (como SystemAudioSource): ningún tipo de plataforma asoma en el header.
#include "SystemCapture.h"

namespace supernova {

class SystemAudioTapSource final : public SystemCapture
{
public:
    SystemAudioTapSource();
    ~SystemAudioTapSource() override;

    bool start (int sampleRate, int channels, SampleCallback onSamples) noexcept override;
    void stop() noexcept override;
    SystemCaptureStatus status() const noexcept override;
    bool isAuthorized() const noexcept override;
    SystemAudioBackend backend() const noexcept override { return SystemAudioBackend::processTap; }

    static bool isSupported() noexcept;   // macOS 14.2+

private:
    struct Impl;
    // shared, no unique: si el usuario cierra la app con el cartel de TCC abierto, el setup sigue
    // bloqueado dentro de CoreAudio y necesita que su estado le sobreviva (ver TapLifecycle.h).
    std::shared_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SystemAudioTapSource)
};

} // namespace supernova
