#pragma once
// Gate PURO del permiso de captura del audio del sistema (D-33). Sin Cocoa, sin TCC, sin JUCE audio I/O:
// decide CUÁNDO se intenta capturar y QUÉ dice la barra SOURCE. AppAudioEngine/AppTopBar lo obedecen.
//
// Por qué existe: hasta 0.3.0 la app arrancaba capturando, así que macOS levantaba su cartel de permiso en
// el primer segundo — antes de que el usuario viera siquiera para qué. Ahora el cartel lo dispara el CLICK
// en "Allow", con el aviso a la vista explicando qué se está pidiendo.
//
// Estados:
//   needsPermission — nunca se pidió. NO se captura (⇒ macOS no muestra nada) y se ve el aviso + ALLOW.
//   requesting      — hay un intento de captura en vuelo. Es el ÚNICO camino al cartel del sistema, y sólo
//                     lo levanta la primera vez: si TCC ya tiene respuesta guardada, el intento sale o
//                     falla en silencio (por eso el arranque con askedBefore=true reintenta sin avisos).
//   granted         — capturando; sin aviso.
//   denied          — el usuario dijo que no. La app sigue usable (MIC/MIDI); el aviso pasa a decir dónde
//                     prenderlo. NO se vuelve a intentar solo: sólo si TCC cambia (authorizationSeen).
//   unsupported     — esta Mac NO PUEDE capturar el audio del sistema: macOS 11/12, donde no existen ni
//                     los process taps (14.2+) ni ScreenCaptureKit (13+). No falta un permiso, falta
//                     versión: no hay cartel que macOS pueda mostrar ni botón que arregle nada, así que
//                     el aviso lo dice y NO ofrece acción (D-35). Terminal: no se reintenta solo — la
//                     versión de macOS no cambia con la app abierta.
#include <juce_core/juce_core.h>
#include "audio/SystemCapture.h"

namespace supernova {

class SystemAudioPermissionGate
{
public:
    enum class State { needsPermission, requesting, granted, denied, unsupported };

    // askedBefore = persistido por la app (¿ya salió el cartel alguna vez?).
    // authorized  = estado TCC EN VIVO si el backend lo sabe (SCK sí, por CGPreflight; taps no tiene
    //               preflight público y reporta false hasta que un intento demuestre lo contrario).
    void begin (bool askedBefore, bool authorized) noexcept
    {
        asked_ = askedBefore;
        if (authorized)          state_ = State::granted;
        else if (askedBefore)    state_ = State::requesting;   // reintento SILENCIOSO (TCC ya respondió)
        else                     state_ = State::needsPermission;
    }

    // El click de ALLOW. Devuelve true si el caller debe arrancar la captura (= dejar salir el cartel).
    bool allowClicked() noexcept
    {
        if (state_ != State::needsPermission) return false;   // denegado / sin soporte ⇒ no se insiste
        asked_ = true;
        state_ = State::requesting;
        return true;
    }

    void captureStarted() noexcept { state_ = State::granted; }

    void captureDenied() noexcept  { asked_ = true; state_ = State::denied; }

    // El intento se cayó por algo que NO es el permiso (HAL sin salida default, aggregate caído, API que
    // no existe en esta versión). Volvemos al aviso con ALLOW —reintentable— en vez de mandar al usuario
    // a un panel de Ajustes que no arregla nada. `asked_` no se toca: el cartel ya se pidió o no, aparte.
    void captureFailed() noexcept  { state_ = State::needsPermission; }

    // Esta versión de macOS no tiene NINGUNA de las dos APIs. Distinto de captureFailed(): ahí reintentar
    // tiene sentido (fue un tropiezo del HAL), acá no lo tiene nunca — hasta 0.3.1 caía igual en
    // `needsPermission` y la barra ofrecía un ALLOW que en macOS 11/12 no puede resolver nada.
    void captureUnsupported() noexcept { state_ = State::unsupported; }

    // El poll en vivo vio el permiso encendido (típico: el usuario lo prendió en Ajustes). Reintentar.
    void authorizationSeen() noexcept
    {
        if (state_ == State::granted || state_ == State::unsupported) return;
        state_ = State::requesting;
    }

    State state() const noexcept        { return state_; }
    bool  askedBefore() const noexcept  { return asked_; }

    // ¿El engine debe tener la captura andando?
    bool shouldStartCapture() const noexcept
    {
        return state_ == State::requesting || state_ == State::granted;
    }

    // El aviso se ve cuando hay algo que hacer (pedirlo, ir a Ajustes) o algo que explicar (macOS viejo).
    bool noticeVisible() const noexcept
    {
        return state_ == State::needsPermission || state_ == State::denied
            || state_ == State::unsupported;
    }

private:
    State state_ = State::needsPermission;
    bool  asked_ = false;
};

// --------------------------------------------------------------------------------- qué muestra la barra
// El estado del gate no alcanza para decidir el aviso, porque `requesting` son DOS situaciones distintas:
//   · el reintento silencioso normal (TCC ya tiene respuesta guardada) → resuelve en milisegundos y no
//     tiene que hacer parpadear nada;
//   · el cartel del sistema ABIERTO esperando al usuario → puede durar minutos, y hasta 0.3.0 la barra no
//     mostraba nada mientras tanto (visto en el smoke del 5-sep con el permiso reseteado): salía un cartel
//     de macOS de la nada y la app parecía indiferente.
// Los separa el TIEMPO, así que el aviso necesita un modelo con reloj. Sigue siendo puro: la barra le pasa
// su reloj de 30Hz y él decide.
enum class NoticeMode { hidden, ask, waiting, denied, unsupported };

// 2s: no alcanza para que el reintento silencioso parpadee, y es poco para el que está mirando el cartel.
inline constexpr double kWaitNoticeMs = 2000.0;

class SourceNoticeModel
{
public:
    NoticeMode update (SystemAudioPermissionGate::State state, double nowMs) noexcept
    {
        using State = SystemAudioPermissionGate::State;

        if (state != State::requesting)   waiting_ = false;                        // cada intento, de cero
        else if (! waiting_)            { waiting_ = true; sinceMs_ = nowMs; }

        switch (state)
        {
            case State::needsPermission: return NoticeMode::ask;
            case State::denied:          return NoticeMode::denied;
            case State::unsupported:     return NoticeMode::unsupported;   // sin reloj: no se espera a nadie
            case State::requesting:      return (nowMs - sinceMs_) >= kWaitNoticeMs ? NoticeMode::waiting
                                                                                    : NoticeMode::hidden;
            case State::granted:         break;
        }
        return NoticeMode::hidden;
    }

private:
    double sinceMs_ = 0.0;
    bool   waiting_ = false;
};

// --------------------------------------------------------------------------------- textos del aviso
// El nombre del panel de Ajustes cambia con el backend: en 14.2+ el permiso es "System Audio Recording";
// en 13 … 14.1, donde la captura va por ScreenCaptureKit, sigue siendo el de "Screen Recording".
inline juce::String noticeText (NoticeMode mode, SystemAudioBackend backend)
{
    if (mode == NoticeMode::denied)
        return juce::String (juce::CharPointer_UTF8 ("Enable in System Settings \xe2\x80\xba Privacy \xe2\x80\xba "))
             + (backend == SystemAudioBackend::processTap ? "System Audio Recording" : "Screen Recording");

    if (mode == NoticeMode::ask)
        return "System Audio needs permission";

    if (mode == NoticeMode::waiting)
        return juce::String (juce::CharPointer_UTF8 ("Waiting for macOS permission\xe2\x80\xa6"));

    // macOS 11/12: no depende del backend (no hay ninguno disponible) ni de Ajustes. Se nombra la salida
    // que SÍ existe y está a dos centímetros, en la misma barra.
    if (mode == NoticeMode::unsupported)
        return juce::String (juce::CharPointer_UTF8 (
            "System Audio needs macOS 13 or later on this Mac \xe2\x80\x94 use an input device or MIDI"));

    return {};
}

// Vacío = sin botón. Esperando al sistema no hay NADA que el usuario pueda hacer desde acá: un botón que
// no hace nada es peor que ninguno.
inline juce::String noticeAction (NoticeMode mode)
{
    switch (mode)
    {
        case NoticeMode::ask:    return "ALLOW";
        case NoticeMode::denied: return "OPEN";
        case NoticeMode::waiting:       // esperando al sistema: no hay nada que el usuario pueda hacer
        case NoticeMode::unsupported:   // macOS viejo: tampoco, y no va a cambiar
        case NoticeMode::hidden: break;
    }
    return {};
}

} // namespace supernova
