// makeSystemCapture() — la fábrica: mira la versión de macOS EN VIVO y arma el backend que corresponde
// (D-33). Acá vive también el adaptador del backend viejo (ScreenCaptureKit) al contrato común, para que
// AppAudioEngine no sepa cuál de los dos tiene entre manos.
#include "SystemCapture.h"

#if JUCE_MAC

#import <Foundation/Foundation.h>

#include "SystemAudioTapSource.h"
#include "SystemAudioSource.h"

namespace supernova {

namespace {

// El backend viejo con el contrato nuevo. Su permiso es el de PANTALLA (CGPreflightScreenCaptureAccess),
// que SÍ tiene preflight público — por eso isAuthorized() acá es exacto y en el tap es observado.
class ScreenCaptureAdapter final : public SystemCapture
{
public:
    bool start (int sampleRate, int channels, SampleCallback onSamples) noexcept override
    {
        return src.start (sampleRate, channels, std::move (onSamples));
    }

    void stop() noexcept override { src.stop(); }

    SystemCaptureStatus status() const noexcept override
    {
        switch (src.status())
        {
            case SystemAudioSource::Status::idle:             return SystemCaptureStatus::idle;
            case SystemAudioSource::Status::capturing:        return SystemCaptureStatus::capturing;
            case SystemAudioSource::Status::permissionDenied: return SystemCaptureStatus::permissionDenied;
            case SystemAudioSource::Status::unsupported:      return SystemCaptureStatus::unsupported;
            case SystemAudioSource::Status::error:            return SystemCaptureStatus::error;
        }
        return SystemCaptureStatus::error;
    }

    bool isAuthorized() const noexcept override         { return SystemAudioSource::hasPermission(); }
    bool isSupported()  const noexcept override         { return SystemAudioSource::isSupported(); }
    SystemAudioBackend backend() const noexcept override { return SystemAudioBackend::screenCapture; }

private:
    SystemAudioSource src;
};

// macOS 11/12: ni taps ni ScreenCaptureKit. En vez de armar un adaptador que va a decir que no se puede,
// se devuelve el que lo dice de entrada — y `AppAudioEngine::useSystemAudio` corta ahí (D-35), sin intentar.
class NoCapture final : public SystemCapture
{
public:
    bool start (int, int, SampleCallback) noexcept override { return false; }
    void stop() noexcept override {}
    SystemCaptureStatus status() const noexcept override { return SystemCaptureStatus::unsupported; }
    bool isAuthorized() const noexcept override          { return false; }
    bool isSupported()  const noexcept override          { return false; }
    SystemAudioBackend backend() const noexcept override { return SystemAudioBackend::none; }
};

} // namespace

std::unique_ptr<SystemCapture> makeSystemCapture()
{
    const auto v = NSProcessInfo.processInfo.operatingSystemVersion;
    const auto choice = pickBackend ((int) v.majorVersion, (int) v.minorVersion);

    if (choice == SystemAudioBackend::none)
        return std::make_unique<NoCapture>();

    if (choice == SystemAudioBackend::processTap && SystemAudioTapSource::isAvailable())
        return std::make_unique<SystemAudioTapSource>();

    return std::make_unique<ScreenCaptureAdapter>();
}

} // namespace supernova

#endif // JUCE_MAC
