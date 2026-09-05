#pragma once
#include <juce_data_structures/juce_data_structures.h>   // ValueTree (trae juce_core)
#include <cmath>
#include <cstdint>
#include <vector>

// PHOTO SEQUENCE (spec 2026-07-12 §D + MEDIA SESSION PRO 2026-09-02) — rotación de fotos por tiempo, compás o
// kick. PURO: lista + índice + relojes; nada de decode ni GPU (eso lo hace el editor, que pregunta
// shouldAdvance() cada tick y confirma con advanced() cuando la siguiente foto YA está decodificada — si no
// llegó, se espera, no se saltea). La persistencia va por ValueTree (child "sequence" del state del APVTS);
// la lista vuelve ENTERA aunque algún archivo ya no esté — el editor marca los faltantes (setMissing) y el
// reloj los saltea, en vez de podarlos en silencio. Vive en el processor; el editor lo maneja en el msg thread.
//
// MEDIA SESSION PRO: la tira de miniaturas necesita CUE (jumpTo), REORDENAR (moveItem), SACAR (removeAt) y el
// PROGRESO del intervalo; el VJ necesita cortar AL COMPÁS (BEATS, host tempo / tap) o CON EL KICK (onsets del
// análisis) además de por SEGUNDOS; SHUFFLE y la transición BURST (explosión al cambiar) son flags que el
// editor lee. Todo determinista y testeable sin JUCE gráfico.
namespace supernova
{
enum class SeqClock : int { Seconds = 0, Beats = 1, Kick = 2 };
enum class SeqOrder : int { Loop = 0, Shuffle = 1 };

// Lo que el editor sabe en cada tick: reloj de pared, posición en beats del BeatClock y el contador MONOTÓNICO
// de onsets del análisis (el bool `onset` de un frame puede pisarse; el contador no).
struct SeqTick
{
    double   nowMs      = 0.0;
    double   beatPos    = 0.0;
    unsigned onsetCount = 0;
};

class PhotoSequence
{
public:
    static constexpr double kMinIntervalSec = 2.0,  kMaxIntervalSec = 60.0;
    static constexpr double kMinBeats       = 1.0,  kMaxBeats       = 64.0;
    static constexpr double kMinKickGapSec  = 0.25, kMaxKickGapSec  = 8.0;

    void setFiles (const juce::StringArray& paths)
    {
        files = paths;
        cancelCue();                              // la sesión cambió: el cue armado ya no apunta a nada
        rots.clearQuick();
        rots.resize (files.size());              // todas en 0 (sin rotar)
        miss.clearQuick();
        miss.insertMultiple (0, false, files.size());   // nadie falta hasta que el editor mire el disco
        index = 0;
        rearmClocks();                            // start() o el primer tick re-arma el reloj
        refreshNext();
    }

    // Agrega fotos al FINAL sin perder la rotación ni la posición actual ("meter más fotos").
    void appendFiles (const juce::StringArray& paths)
    {
        for (const auto& p : paths) { files.add (p); rots.add (0); miss.add (false); }
        refreshNext();
    }

    // INSERTAR en una posición (soltar archivos ENTRE dos tiles, ronda 3): el orden es parte del pase, y
    // hasta acá todo drop caía al final. La foto EN PANTALLA sigue siendo la misma aunque cambie de número;
    // `at` se clampea a [0, size].
    void insertFiles (int at, const juce::StringArray& paths)
    {
        if (paths.isEmpty()) return;
        const int had = files.size();             // ¿había una foto EN PANTALLA que correr de número?
        const int pos = juce::jlimit (0, had, at);
        cancelCue();                              // los índices se corren: el cue armado deja de ser válido
        for (int i = 0; i < paths.size(); ++i)
        {
            files.insert (pos + i, paths[i]);
            rots.insert  (pos + i, 0);
            miss.insert  (pos + i, false);
        }
        // Insertar ANTES corre el número, no la foto. Sobre una lista VACÍA no hay foto que correr: el
        // índice se quedaba uno pasado el final y currentPath() clampeaba a la última en vez de la primera.
        if (had > 0 && pos <= index) index += paths.size();
        index = juce::jlimit (0, juce::jmax (0, files.size() - 1), index);   // cinturón: nunca fuera de rango
        refreshNext();
    }

    int  size() const noexcept          { return files.size(); }
    bool active() const noexcept        { return files.size() >= 2; }
    int  currentIndex() const noexcept  { return index; }

    juce::String currentPath() const
    {
        if (files.isEmpty()) return {};
        return files[juce::jlimit (0, files.size() - 1, index)];
    }
    // El próximo item: LOOP = el siguiente; SHUFFLE = el elegido al entrar al actual (así el prefetch sabe qué
    // decodificar ANTES del cambio). Sin secuencia (<2) = el actual.
    int nextIndex() const noexcept
    {
        if (! active()) return index;
        if (order == SeqOrder::Shuffle && juce::isPositiveAndBelow (pendingNext, files.size())
            && pendingNext != index && ! isMissing (pendingNext))
            return pendingNext;
        return nextAliveIndex();   // LOOP (y el fallback del SHUFFLE cuando el sorteado falta)
    }
    // El SIGUIENTE que exista EN EL ORDEN DE LA TIRA — el espejo exacto de prevIndex(). Con esta pareja
    // camina el paso a mano (← / → y las notas 88 / 89): en vivo, → seguido de ← tiene que volver a donde
    // estabas, y bajo SHUFFLE no volvía (→ iba al sorteado, ← al vecino). El sorteo es del reloj
    // automático, que sigue yendo por nextIndex(). Si no queda ningún otro vivo, el actual — congelar es
    // más honesto que cortar a un archivo que no está.
    int nextAliveIndex() const noexcept
    {
        if (! active()) return index;
        const int n = files.size();
        for (int k = 1; k < n; ++k)
        {
            const int c = (index + k) % n;
            if (! isMissing (c)) return c;
        }
        return index;
    }
    // El ANTERIOR que exista: el espejo de nextAliveIndex() para ← y la nota 89 (el SHUFFLE no tiene
    // "previo sorteado", así que acá el orden es siempre el de la tira). Si no queda otro vivo, el actual.
    int prevIndex() const noexcept
    {
        if (! active()) return index;
        const int n = files.size();
        for (int k = 1; k < n; ++k)
        {
            const int c = ((index - k) % n + n) % n;
            if (! isMissing (c)) return c;
        }
        return index;
    }
    juce::String nextPath() const       { return active() ? files[nextIndex()] : juce::String(); }
    juce::String pathAt (int i) const   { return juce::isPositiveAndBelow (i, files.size()) ? files[i] : juce::String(); }

    // Rotación (cuartos de vuelta CW) de la foto actual / la siguiente / cualquier índice — el "problema de
    // la foto vertical": el usuario la endereza a mano y la rotación PERSISTE por foto.
    int  rotationAt (int i) const       { return juce::isPositiveAndBelow (i, rots.size()) ? rots[i] : 0; }
    int  currentRotation() const        { return rotationAt (index); }
    int  nextRotation() const           { return active() ? rotationAt (nextIndex()) : 0; }
    void rotateCurrent()                { if (juce::isPositiveAndBelow (index, rots.size())) rots.set (index, (rots[index] + 1) % 4); }
    void setRotationAt (int i, int rot) { if (juce::isPositiveAndBelow (i, rots.size())) rots.set (i, ((rot % 4) + 4) % 4); }

    // MEDIA FALTANTE (ronda 3): mover una carpeta ya no hace desaparecer las fotos. El item se queda en la
    // sesión, MARCADO, y el reloj lo saltea hasta que se relinkea — como un clip offline de Premiere o
    // Resolume. Quién existe y quién no lo mira el EDITOR (el filesystem no es asunto de este modelo puro).
    void setMissing (int i, bool m) noexcept   { if (juce::isPositiveAndBelow (i, miss.size())) miss.set (i, m); }
    bool isMissing (int i) const noexcept      { return juce::isPositiveAndBelow (i, miss.size()) && miss[i]; }
    // El primer item que SÍ está (-1 = faltan todos): el lienzo AUTO y el restore lo usan para no colgarse
    // del aspecto/decode de un archivo que no está.
    int  firstAliveIndex() const noexcept
    {
        for (int i = 0; i < files.size(); ++i) if (! isMissing (i)) return i;
        return -1;
    }
    // RELINK: el usuario encontró el archivo (o la carpeta) → el item apunta ahí y deja de faltar. La
    // rotación que le había dado se conserva: es la misma foto, mudada.
    bool setPathAt (int i, const juce::String& p)
    {
        if (! juce::isPositiveAndBelow (i, files.size()) || p.isEmpty()) return false;
        files.set (i, p);
        setMissing (i, false);
        refreshNext();
        return true;
    }

    // ---- reloj SECONDS (el de siempre) ----
    void setIntervalSeconds (double s) noexcept { intervalSec = juce::jlimit (kMinIntervalSec, kMaxIntervalSec, s); }
    double intervalSeconds() const noexcept     { return intervalSec; }

    // ---- reloj: SECONDS / BEATS / KICK ----
    void     setClock (SeqClock c) noexcept     { clockMode = c; rearmClocks(); }
    SeqClock clock() const noexcept             { return clockMode; }
    // BEATS: cambia cada N beats del BeatClock (4 = un compás de 4/4). − / + recorren 1·2·4·8·16·32·64.
    void   setIntervalBeats (double b) noexcept { intervalBeats_ = juce::jlimit (kMinBeats, kMaxBeats, b); }
    double intervalBeats() const noexcept       { return intervalBeats_; }
    void   stepIntervalBeats (int dir) noexcept
    {
        static constexpr double steps[] = { 1, 2, 4, 8, 16, 32, 64 };
        int k = 0;
        for (int i = 0; i < 7; ++i) if (std::abs (steps[i] - intervalBeats_) < 1e-9) { k = i; break; }
        k = juce::jlimit (0, 6, k + (dir > 0 ? 1 : -1));
        intervalBeats_ = steps[k];
    }
    // KICK: cambia con cada onset, pero nunca más seguido que el GAP mínimo (− / + de a 0.25 s).
    void   setKickGapSeconds (double s) noexcept { kickGapSec = juce::jlimit (kMinKickGapSec, kMaxKickGapSec, s); }
    double kickGapSeconds() const noexcept       { return kickGapSec; }

    // ---- orden y transición ----
    void     setOrder (SeqOrder o) noexcept     { order = o; refreshNext(); }
    SeqOrder orderMode() const noexcept         { return order; }
    void     setBurst (bool b) noexcept         { burstOnSwitch = b; }
    bool     burst() const noexcept             { return burstOnSwitch; }
    void     setSeed (uint32_t s) noexcept      { rng = (s == 0 ? 0x9E3779B9u : s); refreshNext(); }

    // Una foto AL AZAR que NO sea la actual (la nota 90 del cue por MIDI: "otra cualquiera"). Con 0 o 1
    // items devuelve el actual. Mismo RNG propio que el SHUFFLE → determinista con la misma semilla.
    // Sortea SOLO entre los que EXISTEN: caer en un faltante no mostraba nada (la pantalla se quedaba con
    // la foto anterior y la tira marcaba como actual un tile con el glifo `!`). Si no queda ningún otro
    // vivo, el actual — congelar es más honesto que cortar a un archivo que no está.
    int randomOtherIndex() noexcept
    {
        const int n = files.size();
        if (n < 2) return index;
        int alive = 0;
        for (int i = 0; i < n; ++i) if (i != index && ! isMissing (i)) ++alive;
        if (alive <= 0) return index;
        int r = (int) (nextRandom() % (uint32_t) alive);   // el r-ésimo vivo que no es el actual
        for (int i = 0; i < n; ++i)
        {
            if (i == index || isMissing (i)) continue;
            if (r-- == 0) return i;
        }
        return index;
    }

    void setPlaying (bool p) noexcept   { isPlaying = p; }
    bool playing() const noexcept       { return isPlaying; }

    void start (double nowMs) noexcept  { rearmClocks(); lastSwitchMs = nowMs; }

    // ¿Toca pasar a la siguiente? (el caller decide CUÁNDO confirmar: espera el decode si hace falta). La
    // respuesta es PEGAJOSA hasta advanced(): si la foto siguiente todavía no decodificó, el próximo tick
    // vuelve a decir "sí" (un kick no se pierde por esperar un decode).
    bool shouldAdvance (const SeqTick& t) noexcept
    {
        if (! active() || ! isPlaying) return false;
        switch (clockMode)
        {
            case SeqClock::Seconds:
                if (lastSwitchMs < 0.0) { lastSwitchMs = t.nowMs; return false; }   // primer tick = armar el reloj
                return (t.nowMs - lastSwitchMs) >= intervalSec * 1000.0;

            case SeqClock::Beats:
            {
                if (lastSwitchMs < 0.0) lastSwitchMs = t.nowMs;
                if (lastBeatPos < 0.0 || t.beatPos < lastBeatPos - 1e-6)   // primer tick, o el transport rebobinó
                    { lastBeatPos = t.beatPos; return false; }
                const double ib = intervalBeats_;
                return std::floor (t.beatPos / ib) > std::floor (lastBeatPos / ib);   // cruzó un límite de ventana
            }

            case SeqClock::Kick:
            {
                if (lastSwitchMs < 0.0) lastSwitchMs = t.nowMs;
                if (! kickArmed) { kickArmed = true; lastOnsetCount = t.onsetCount; return false; }
                if (t.onsetCount != lastOnsetCount)
                {
                    lastOnsetCount = t.onsetCount;
                    if ((t.nowMs - lastSwitchMs) >= kickGapSec * 1000.0) kickPending = true;   // fuera del gap: vale
                }
                return kickPending;
            }
        }
        return false;
    }
    bool shouldAdvance (double nowMs) noexcept { return shouldAdvance (SeqTick { nowMs, 0.0, 0u }); }

    void advanced (const SeqTick& t) noexcept
    {
        if (! active()) return;
        index = nextIndex();
        lastSwitchMs    = t.nowMs;
        lastBeatPos     = t.beatPos;
        lastOnsetCount  = t.onsetCount;
        kickPending     = false;
        refreshNext();
    }
    void advanced (double nowMs) noexcept { advanced (SeqTick { nowMs, lastBeatPos < 0.0 ? 0.0 : lastBeatPos, lastOnsetCount }); }

    // Progreso [0..1] hacia el próximo cambio (la barrita del tile actual): SECONDS = tiempo transcurrido;
    // BEATS = posición dentro de la ventana de N beats; KICK = cuánto del gap mínimo ya pasó (1 = armado).
    float progress (const SeqTick& t) const noexcept
    {
        if (! active()) return 0.0f;
        switch (clockMode)
        {
            case SeqClock::Seconds:
                if (lastSwitchMs < 0.0) return 0.0f;
                return (float) juce::jlimit (0.0, 1.0, (t.nowMs - lastSwitchMs) / (intervalSec * 1000.0));
            case SeqClock::Beats:
            {
                const double ib = intervalBeats_;
                const double f  = std::fmod (t.beatPos, ib);
                return (float) juce::jlimit (0.0, 1.0, (f < 0.0 ? f + ib : f) / ib);
            }
            case SeqClock::Kick:
                if (lastSwitchMs < 0.0) return 0.0f;
                return (float) juce::jlimit (0.0, 1.0, (t.nowMs - lastSwitchMs) / (kickGapSec * 1000.0));
        }
        return 0.0f;
    }

    // CUE desde la tira: salta YA a un item (re-arma los relojes; el editor muestra + resetea el prefetch).
    bool jumpTo (int i) noexcept
    {
        if (! juce::isPositiveAndBelow (i, files.size())) return false;
        index = i;
        cancelCue();                              // el salto consume cualquier cue armado
        rearmClocks();
        refreshNext();
        return true;
    }

    // CUE ARMADO (beat snap, MEDIA SESSION PRO ronda 2): en BEATS, cuear un tile no corta al instante —
    // espera al próximo límite de ventana, como cualquier software de VJ. Acá sólo vive el CUÁNDO; el salto
    // lo hace el editor con jumpTo. En SECONDS/KICK el editor ni arma: salta directo.
    // `beatPosNow` >= 0 fija la ventana EN EL MOMENTO del armado (lo que hace el editor: sabe el beatPos).
    // Fijarla en el primer tick costaba hasta 33 ms, y si el compás cruzaba en esa ventana el corte caía un
    // compás entero tarde — justo el gesto del VJ que anticipa el downbeat. -1 = el comportamiento viejo.
    void armCue (int i, double beatPosNow = -1.0) noexcept
    {
        if (! juce::isPositiveAndBelow (i, files.size())) return;
        cueIdx     = i;
        cueArmBeat = beatPosNow >= 0.0 ? beatPosNow : -1.0;
    }
    int  pendingCue() const noexcept { return cueIdx; }
    void cancelCue() noexcept        { cueIdx = -1; cueArmBeat = -1.0; }

    // ¿Ya toca el cue armado? Dispara cuando floor(beatPos/intervalBeats) cambió desde el armado. Si el
    // transport rebobina, se re-arma en la ventana nueva (no dispara por el salto hacia atrás).
    bool shouldFireCue (const SeqTick& t) noexcept
    {
        if (cueIdx < 0) return false;
        if (cueArmBeat < 0.0 || t.beatPos < cueArmBeat - 1e-6) { cueArmBeat = t.beatPos; return false; }
        return std::floor (t.beatPos / intervalBeats_) > std::floor (cueArmBeat / intervalBeats_);
    }

    // REORDENAR desde la tira (drag): saca `from` y lo inserta en `to` (índice en la lista resultante). El
    // item ACTUAL sigue siendo el actual aunque cambie de número.
    bool moveItem (int from, int to) noexcept
    {
        const int n = files.size();
        if (! juce::isPositiveAndBelow (from, n) || ! juce::isPositiveAndBelow (to, n) || from == to) return false;
        cancelCue();                              // los índices se corren: el cue armado deja de ser válido
        const juce::String p = files[from];
        const int  r = rotationAt (from);
        const bool m = isMissing (from);
        files.remove (from); rots.remove (from); miss.remove (from);
        files.insert (to, p); rots.insert (to, r); miss.insert (to, m);
        if      (from == index)                  index = to;
        else if (from < index && to >= index)    --index;
        else if (from > index && to <= index)    ++index;
        refreshNext();
        return true;
    }

    // SACAR un item por índice: si era el actual, el índice queda apuntando al que le seguía (wrap).
    void removeAt (int i)
    {
        if (! juce::isPositiveAndBelow (i, files.size())) return;
        cancelCue();                              // los índices se corren: el cue armado deja de ser válido
        files.remove (i);
        if (i < rots.size()) rots.remove (i);
        if (i < miss.size()) miss.remove (i);
        if (files.isEmpty()) index = 0;
        else if (i < index)  --index;
        else                 index = index % files.size();
        refreshNext();
    }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree vt ("sequence");
        vt.setProperty ("interval", intervalSec, nullptr);
        vt.setProperty ("playing",  isPlaying,   nullptr);
        vt.setProperty ("index",    index,       nullptr);
        vt.setProperty ("clock",    (int) clockMode,  nullptr);
        vt.setProperty ("beats",    intervalBeats_,   nullptr);
        vt.setProperty ("kickGap",  kickGapSec,       nullptr);
        vt.setProperty ("order",    (int) order,      nullptr);
        vt.setProperty ("burst",    burstOnSwitch,    nullptr);
        for (int i = 0; i < files.size(); ++i)
        {
            juce::ValueTree c ("photo");
            c.setProperty ("path", files[i], nullptr);
            c.setProperty ("rot",  rotationAt (i), nullptr);   // rotación manual persistida por foto
            vt.appendChild (c, nullptr);
        }
        return vt;
    }

    static PhotoSequence fromValueTree (const juce::ValueTree& vt)
    {
        PhotoSequence s;
        if (! vt.hasType (juce::Identifier ("sequence"))) return s;
        // La lista vuelve ENTERA: un path que hoy no está no se poda (antes sí — "patrón sampler" — y las
        // fotos desaparecían sin explicación al mover una carpeta). Quién falta lo marca el editor, que es
        // el que mira el disco; el reloj saltea los marcados y el usuario los relinkea.
        juce::StringArray paths;
        juce::Array<int>  rotations;
        for (const auto& c : vt)
        {
            paths.add (c.getProperty ("path").toString());
            rotations.add (((int) c.getProperty ("rot", 0) % 4 + 4) % 4);
        }
        s.setFiles (paths);
        s.rots = rotations;
        s.setIntervalSeconds ((double) vt.getProperty ("interval", 8.0));
        s.setPlaying ((bool) vt.getProperty ("playing", true));
        s.clockMode = (SeqClock) juce::jlimit (0, 2, (int) vt.getProperty ("clock", 0));
        s.setIntervalBeats ((double) vt.getProperty ("beats", 4.0));
        s.setKickGapSeconds ((double) vt.getProperty ("kickGap", 1.0));
        s.order = (SeqOrder) juce::jlimit (0, 1, (int) vt.getProperty ("order", 0));
        s.burstOnSwitch = (bool) vt.getProperty ("burst", false);
        s.index = juce::jlimit (0, juce::jmax (0, paths.size() - 1), (int) vt.getProperty ("index", 0));
        s.refreshNext();
        return s;
    }

private:
    void rearmClocks() noexcept
    {
        lastSwitchMs = -1.0;
        lastBeatPos  = -1.0;
        kickArmed    = false;
        kickPending  = false;
    }

    uint32_t nextRandom() noexcept   // xorshift32: determinista, sin <random>
    {
        uint32_t x = rng;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        rng = x;
        return x;
    }

    // SHUFFLE: elige el próximo al entrar al actual (nunca el mismo dos veces seguidas). Es exactamente el
    // sorteo de randomOtherIndex (mismo RNG, mismo salteo del actual): una sola implementación.
    void refreshNext() noexcept
    {
        const int n = files.size();
        if (n < 2 || order == SeqOrder::Loop) { pendingNext = -1; return; }
        pendingNext = randomOtherIndex();
    }

    juce::StringArray files;
    juce::Array<int>  rots;      // rotación (0..3 cuartos CW) paralela a files
    juce::Array<bool> miss;      // el archivo no está en el disco (paralela a files; NO se persiste)
    int    index        = 0;
    int    pendingNext  = -1;    // SHUFFLE: el próximo ya elegido (-1 = LOOP / sin secuencia)
    double intervalSec  = 8.0;
    double lastSwitchMs = -1.0;
    bool   isPlaying    = true;

    SeqClock clockMode      = SeqClock::Seconds;
    double   intervalBeats_ = 4.0;     // un compás de 4/4
    double   kickGapSec     = 1.0;
    double   lastBeatPos    = -1.0;    // beatPos en el último cambio (-1 = sin armar)
    unsigned lastOnsetCount = 0;
    bool     kickArmed      = false;
    bool     kickPending    = false;
    int      cueIdx         = -1;      // CUE armado esperando el compás (-1 = ninguno)
    double   cueArmBeat     = -1.0;    // beatPos en el que se armó (-1 = todavía sin tick)
    SeqOrder order          = SeqOrder::Loop;
    bool     burstOnSwitch  = false;
    uint32_t rng            = 0x9E3779B9u;
};

// El ORDEN DE REPRODUCCIÓN de los próximos `count` slots, empezando por el ACTUAL: LOOP = el natural
// (3,4,5,…) y SHUFFLE = el que produce la propia secuencia (determinista con la misma semilla). Camina una
// COPIA → la secuencia viva no se toca. Lo usa el EXPORT: el MP4 sale con el mismo orden que se ve.
inline std::vector<int> playOrderFrom (PhotoSequence copy, int count)
{
    std::vector<int> order;
    if (copy.size() <= 0 || count <= 0) return order;
    order.reserve ((size_t) count);
    order.push_back (copy.currentIndex());
    for (int i = 1; i < count; ++i) { copy.advanced (SeqTick {}); order.push_back (copy.currentIndex()); }
    return order;
}
}
