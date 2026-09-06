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
    SystemAudioBackend backend() const noexcept override { return SystemAudioBackend::screenCapture; }

private:
    SystemAudioSource src;
};

} // namespace

std::unique_ptr<SystemCapture> makeSystemCapture()
{
    const auto v = NSProcessInfo.processInfo.operatingSystemVersion;
    const auto choice = pickBackend ((int) v.majorVersion, (int) v.minorVersion);

    if (choice == SystemAudioBackend::processTap && SystemAudioTapSource::isSupported())
        return std::make_unique<SystemAudioTapSource>();

    return std::make_unique<ScreenCaptureAdapter>();
}

} // namespace supernova

#endif // JUCE_MAC
