#pragma once
// Contrato ÚNICO de captura del audio del sistema + la DECISIÓN de qué backend usar (D-33).
//
// Dos backends, el MISMO contrato (start/stop/status/isAuthorized):
//   · processTap    (macOS 14.2+) — Core Audio process taps (AudioHardwareCreateProcessTap). El permiso
//                    que pide macOS es el CHICO: "System Audio Recording Only". La app NO aparece pidiendo
//                    grabar la pantalla, porque no mira un píxel.
//   · screenCapture (macOS 13 … 14.1) — ScreenCaptureKit. Ahí la API de taps todavía NO existe, así que el
//                    único camino sin drivers vive bajo el permiso de PANTALLA (así se lanzó v0.1.0…v0.3.0).
//
// La elección es una función PURA para poder testearla sin Cocoa; la fábrica (que sí toca plataforma) vive
// en SystemCapture.mm. Entrega PUSH: el backend empuja TODOS los frames por el callback (no latest-wins:
// dropear bloques de audio = cortes). El FIFO del processor absorbe el ritmo.
#include <functional>
#include <memory>
#include <juce_core/juce_core.h>

namespace supernova {

// `none` = en esta máquina no existe NINGUNO de los dos caminos (macOS 11/12, o una plataforma sin backend
// todavía). No es lo mismo que "screenCapture y ya fallará": antes esta función devolvía screenCapture para
// macOS 11/12 y el "no se puede" sólo aparecía después, al preguntarle al adaptador.
enum class SystemAudioBackend { none, processTap, screenCapture };

enum class SystemCaptureStatus { idle, capturing, permissionDenied, unsupported, error };

// macOS 14.2 es la primera versión con AudioHardwareCreateProcessTap (API_AVAILABLE(macos(14.2))); macOS 13
// la primera con ScreenCaptureKit capturando audio. Debajo de eso no hay camino sin drivers: `none`.
constexpr SystemAudioBackend pickBackend (int osMajor, int osMinor) noexcept
{
    if (osMajor > 14 || (osMajor == 14 && osMinor >= 2)) return SystemAudioBackend::processTap;
    if (osMajor >= 13)                                   return SystemAudioBackend::screenCapture;
    return SystemAudioBackend::none;
}

// El panel de Ajustes que hay que abrirle al usuario es el del permiso que pide SU backend. Sin backend no
// hay permiso que dar: cadena vacía, y el que llama no abre nada (no existe un panel que arregle un macOS 12).
inline juce::String settingsPaneUrl (SystemAudioBackend backend)
{
    switch (backend)
    {
        case SystemAudioBackend::processTap:
            return "x-apple.systempreferences:com.apple.preference.security?Privacy_AudioCapture";
        case SystemAudioBackend::screenCapture:
            return "x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture";
        case SystemAudioBackend::none:
            break;
    }
    return {};
}

// Nombre corto para los logs y la telemetría.
inline const char* backendName (SystemAudioBackend b) noexcept
{
    switch (b)
    {
        case SystemAudioBackend::processTap:    return "process-tap";
        case SystemAudioBackend::screenCapture: return "screencapturekit";
        case SystemAudioBackend::none:          break;
    }
    return "none";
}

// Fachada común: lo único que AppAudioEngine necesita de CUALQUIER backend.
class SystemCapture
{
public:
    // chans = punteros por-canal Float32 (planar); numFrames por canal; sr = sample rate real.
    using SampleCallback = std::function<void (const float* const* chans, int numCh, int numFrames, double sr)>;

    virtual ~SystemCapture() = default;

    virtual bool start (int sampleRate, int channels, SampleCallback onSamples) noexcept = 0;
    virtual void stop() noexcept = 0;
    virtual SystemCaptureStatus status() const noexcept = 0;
    virtual bool isAuthorized() const noexcept = 0;          // estado TCC EN VIVO del permiso del backend

    // ¿La API que necesita este backend EXISTE en esta Mac? false = macOS 11/12 (ni taps 14.2+ ni SCK
    // 13+). Se pregunta ANTES de intentar: sin esto el único modo de enterarse era arrancar la captura y
    // esperar el `unsupported` — un intento condenado, y en el arranque con `sysAudioAsked` guardado, uno
    // que ni siquiera nace de un click del usuario.
    virtual bool isSupported() const noexcept = 0;
    virtual SystemAudioBackend backend() const noexcept = 0;
};

// Elige por la versión de macOS EN VIVO (pickBackend). Implementada en SystemCapture.mm.
std::unique_ptr<SystemCapture> makeSystemCapture();

} // namespace supernova
