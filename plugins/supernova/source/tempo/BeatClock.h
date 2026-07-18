#pragma once
// BeatClock — el RELOJ musical de SUPERNOVA (Phase B · tempo sync). C++ PURO (sin JUCE) → testeable con
// tiempos sintéticos. Mantiene BPM + fase de beat para eventos CUANTIZADOS (strobe/freeze/escena/LFO al compás).
//
// Fuentes de tempo, por prioridad (la más alta que esté presente MANDA):
//   1. HOST playhead (VST3/AU dentro del DAW): BPM + ppqPosition → la fase se ENGANCHA al host (sample-accurate).
//   2. Ableton LINK (app standalone) — enchufe listo (setLinkTempo/lock), la integración del SDK es aparte.
//   3. TAP TEMPO (usuario golpea al compás).
//   4. AUTO-BPM por onsets (estima de los intervalos entre kicks).
//   5. Default (120).
//
// Uso RT: advance(dt, host) por bloque de audio (audio thread). tap()/onOnset() desde cualquier hilo con su
// marca de tiempo. Los getters (bpm/beatPhase/…) son const y baratos → el render/editor los leen a su ritmo.
#include <cmath>
#include <cstdint>

namespace supernova
{
class BeatClock
{
public:
    struct HostInfo
    {
        bool   valid       = false;   // el host entregó posición
        bool   isPlaying   = false;   // transport corriendo (si no, la fase la lleva el free-run)
        double bpm         = 0.0;     // tempo del host
        double ppqPosition = 0.0;     // posición en quarter-notes (beats) del transport
    };

    static constexpr double kMinBpm = 40.0, kMaxBpm = 300.0, kDefaultBpm = 120.0;

    void reset() noexcept { bpm_ = kDefaultBpm; phaseBeats_ = 0.0; crossedBeat_ = crossedBar_ = false;
                            tapCount_ = 0; lastTap_ = -1.0; lastOnset_ = -1.0; onsetBpm_ = 0.0;
                            linkValid_ = false; }

    void setDefaultBpm (double bpm) noexcept { if (tapCount_ == 0 && onsetBpm_ <= 0.0) bpm_ = clampBpm (bpm); }

    // Ableton Link (app): el SDK empuja tempo + fase; acá solo se guarda para que advance() lo use si el host
    // no está. La integración real del LinkKit vive en la app (fachada, como SystemAudioSource).
    void setLinkTempo (double bpm, bool active) noexcept { onsetBpm_ = 0.0; linkValid_ = active;
                                                           if (active) bpm_ = clampBpm (bpm); }

    // Avanza el reloj un bloque. Si el host está presente y tocando, la fase se ENGANCHA a su ppqPosition
    // (sample-accurate, sin deriva). Si no, corre libre al bpm_ actual. Marca cruces de beat/bar para el
    // cuantizador de eventos. dtSeconds debe ser > 0.
    void advance (double dtSeconds, const HostInfo& host) noexcept
    {
        crossedBeat_ = crossedBar_ = false;
        const double prevPhase = phaseBeats_;

        if (host.valid && host.bpm >= kMinBpm && host.bpm <= kMaxBpm)
        {
            bpm_ = host.bpm;                 // el host manda el tempo
            if (host.isPlaying)
                phaseBeats_ = host.ppqPosition;     // engancha la fase al transport (sin deriva)
            else
                phaseBeats_ += (bpm_ / 60.0) * dtSeconds;   // parado: free-run suave para que el LFO no se congele
        }
        else
        {
            phaseBeats_ += (bpm_ / 60.0) * dtSeconds;       // standalone/Link/tap/auto: free-run al bpm_
        }

        if (phaseBeats_ < prevPhase) { /* rebobinó (loop del host) */ }
        else
        {
            if (std::floor (phaseBeats_) > std::floor (prevPhase)) crossedBeat_ = true;
            if (std::floor (phaseBeats_ / 4.0) > std::floor (prevPhase / 4.0)) crossedBar_ = true;
        }
    }

    // TAP TEMPO (message thread): cada golpe con su marca. Con ≥2 golpes deriva el BPM del intervalo medio;
    // descarta intervalos absurdos (>2s = tap perdido → reinicia la serie).
    void tap (double atSeconds) noexcept
    {
        if (lastTap_ >= 0.0)
        {
            const double dt = atSeconds - lastTap_;
            if (dt > 0.20 && dt < 2.0)     // 30..300 BPM
            {
                const double b = 60.0 / dt;
                bpm_ = (tapCount_ == 0) ? clampBpm (b) : clampBpm (bpm_ * 0.5 + b * 0.5);   // promedio suave
                ++tapCount_;
                linkValid_ = false; onsetBpm_ = 0.0;   // el tap gana sobre el auto
            }
            else tapCount_ = 0;            // serie rota
        }
        lastTap_ = atSeconds;
    }

    // AUTO-BPM por onsets: estima del intervalo entre kicks (plegado a un rango musical 70..170). Solo influye
    // si no hay host/link/tap. Suaviza fuerte (los kicks reales tienen jitter).
    void onOnset (double atSeconds) noexcept
    {
        if (lastOnset_ >= 0.0)
        {
            double dt = atSeconds - lastOnset_;
            if (dt > 0.05 && dt < 2.0)
            {
                double b = 60.0 / dt;
                while (b < 70.0)  b *= 2.0;    // pliega a 70..170 (subdivisiones/octavas del tempo)
                while (b > 170.0) b *= 0.5;
                onsetBpm_ = (onsetBpm_ <= 0.0) ? b : onsetBpm_ * 0.8 + b * 0.2;
                if (tapCount_ == 0 && ! linkValid_) bpm_ = clampBpm (onsetBpm_);
            }
        }
        lastOnset_ = atSeconds;
    }

    double bpm() const noexcept { return bpm_; }
    // Fase dentro del beat (quarter note): 0 en el downbeat, →1 justo antes del próximo.
    double beatPhase() const noexcept { return phaseBeats_ - std::floor (phaseBeats_); }
    // Fase dentro del compás de `beatsPerBar` beats.
    double barPhase (int beatsPerBar = 4) const noexcept
    {
        const double b = phaseBeats_ / (double) beatsPerBar;
        return b - std::floor (b);
    }
    double phaseInBeats() const noexcept { return phaseBeats_; }
    bool crossedBeat() const noexcept { return crossedBeat_; }   // ¿cruzó un beat en el último advance()?
    bool crossedBar()  const noexcept { return crossedBar_; }

    // Fase de un LFO sincronizado, en ciclos [0..1), para una tasa de `beatsPerCycle` (1=beat, 4=bar, 0.25=16th).
    double syncPhase (double beatsPerCycle) const noexcept
    {
        if (beatsPerCycle <= 1e-6) return 0.0;
        const double c = phaseBeats_ / beatsPerCycle;
        return c - std::floor (c);
    }

    // Segundos hasta el próximo límite de división (para cuantizar un evento). beatsPerDiv: 1=beat, 4=bar,
    // 0.5=corchea. Devuelve 0 si estamos justo en el límite.
    double secondsToNextDivision (double beatsPerDiv) const noexcept
    {
        if (beatsPerDiv <= 1e-6 || bpm_ <= 0.0) return 0.0;
        const double pos  = phaseBeats_ / beatsPerDiv;
        const double frac = pos - std::floor (pos);
        const double beatsLeft = (frac <= 1e-9 ? 0.0 : (1.0 - frac)) * beatsPerDiv;
        return beatsLeft * 60.0 / bpm_;
    }

private:
    static double clampBpm (double b) noexcept { return b < kMinBpm ? kMinBpm : (b > kMaxBpm ? kMaxBpm : b); }

    double bpm_        = kDefaultBpm;
    double phaseBeats_ = 0.0;      // posición absoluta en beats (acumula; el host la puede reenganchar)
    bool   crossedBeat_ = false, crossedBar_ = false;
    int    tapCount_ = 0;
    double lastTap_  = -1.0;
    double lastOnset_ = -1.0, onsetBpm_ = 0.0;
    bool   linkValid_ = false;
};
}
