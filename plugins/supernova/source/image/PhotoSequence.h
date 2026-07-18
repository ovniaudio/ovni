#pragma once
#include <juce_data_structures/juce_data_structures.h>   // ValueTree (trae juce_core)

// PHOTO SEQUENCE (spec 2026-07-12 §D) — rotación de fotos por tiempo. PURO: lista + índice + reloj;
// nada de decode ni GPU (eso lo hace el editor, que pregunta shouldAdvance() cada tick y confirma con
// advanced() cuando la siguiente foto YA está decodificada — si no llegó, se espera, no se saltea).
// La persistencia va por ValueTree (child "sequence" del state del APVTS); los paths que ya no existen
// se omiten al restaurar (patrón sampler). Vive en el processor; el editor lo maneja en el msg thread.
namespace supernova
{
class PhotoSequence
{
public:
    void setFiles (const juce::StringArray& paths)
    {
        files = paths;
        rots.clearQuick();
        rots.resize (files.size());              // todas en 0 (sin rotar)
        index = 0;
        lastSwitchMs = -1.0;                      // start() o el primer tick re-arma el reloj
    }

    // Agrega fotos al FINAL sin perder la rotación ni la posición actual ("meter más fotos").
    void appendFiles (const juce::StringArray& paths)
    {
        for (const auto& p : paths) { files.add (p); rots.add (0); }
    }

    int  size() const noexcept          { return files.size(); }
    bool active() const noexcept        { return files.size() >= 2; }
    int  currentIndex() const noexcept  { return index; }

    juce::String currentPath() const
    {
        if (files.isEmpty()) return {};
        return files[juce::jlimit (0, files.size() - 1, index)];
    }
    juce::String nextPath() const       { return active() ? files[(index + 1) % files.size()] : juce::String(); }
    juce::String pathAt (int i) const   { return juce::isPositiveAndBelow (i, files.size()) ? files[i] : juce::String(); }

    // Rotación (cuartos de vuelta CW) de la foto actual / la siguiente / cualquier índice — el "problema de
    // la foto vertical": el usuario la endereza a mano y la rotación PERSISTE por foto.
    int  rotationAt (int i) const       { return juce::isPositiveAndBelow (i, rots.size()) ? rots[i] : 0; }
    int  currentRotation() const        { return rotationAt (index); }
    int  nextRotation() const           { return active() ? rotationAt ((index + 1) % files.size()) : 0; }
    void rotateCurrent()                { if (juce::isPositiveAndBelow (index, rots.size())) rots.set (index, (rots[index] + 1) % 4); }
    void setRotationAt (int i, int rot) { if (juce::isPositiveAndBelow (i, rots.size())) rots.set (i, ((rot % 4) + 4) % 4); }

    void setIntervalSeconds (double s) noexcept { intervalSec = juce::jlimit (2.0, 60.0, s); }
    double intervalSeconds() const noexcept     { return intervalSec; }

    void setPlaying (bool p) noexcept   { isPlaying = p; }
    bool playing() const noexcept       { return isPlaying; }

    void start (double nowMs) noexcept  { lastSwitchMs = nowMs; }

    // ¿Toca pasar a la siguiente? (el caller decide CUÁNDO confirmar: espera el decode si hace falta)
    bool shouldAdvance (double nowMs) noexcept
    {
        if (! active() || ! isPlaying) return false;
        if (lastSwitchMs < 0.0) { lastSwitchMs = nowMs; return false; }   // primer tick = armar el reloj
        return (nowMs - lastSwitchMs) >= intervalSec * 1000.0;
    }

    void advanced (double nowMs) noexcept
    {
        if (! active()) return;
        index = (index + 1) % files.size();
        lastSwitchMs = nowMs;
    }

    // La foto actual falló al decodificar → se poda y el índice queda apuntando a la que le seguía.
    void removeCurrent()
    {
        if (files.isEmpty()) return;
        files.remove (index);
        if (index < rots.size()) rots.remove (index);
        index = files.isEmpty() ? 0 : index % files.size();
    }

    // Poda por path (p.ej. la SIGUIENTE no decodificó): la foto actual no se mueve.
    void removePath (const juce::String& p)
    {
        const int i = files.indexOf (p);
        if (i < 0) return;
        files.remove (i);
        if (i < rots.size()) rots.remove (i);
        if (files.isEmpty())  index = 0;
        else if (i < index)   --index;
        else                  index = index % files.size();
    }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree vt ("sequence");
        vt.setProperty ("interval", intervalSec, nullptr);
        vt.setProperty ("playing",  isPlaying,   nullptr);
        vt.setProperty ("index",    index,       nullptr);
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
        juce::StringArray alive;
        juce::Array<int>  aliveRots;
        for (const auto& c : vt)
        {
            const juce::String p = c.getProperty ("path").toString();
            if (juce::File (p).existsAsFile())                // los muertos se omiten (patrón sampler)
            {
                alive.add (p);
                aliveRots.add (((int) c.getProperty ("rot", 0) % 4 + 4) % 4);
            }
        }
        s.setFiles (alive);
        s.rots = aliveRots;
        s.setIntervalSeconds ((double) vt.getProperty ("interval", 8.0));
        s.setPlaying ((bool) vt.getProperty ("playing", true));
        s.index = juce::jlimit (0, juce::jmax (0, alive.size() - 1), (int) vt.getProperty ("index", 0));
        return s;
    }

private:
    juce::StringArray files;
    juce::Array<int>  rots;      // rotación (0..3 cuartos CW) paralela a files
    int    index        = 0;
    double intervalSec  = 8.0;
    double lastSwitchMs = -1.0;
    bool   isPlaying    = true;
};
}
