// FactoryPresets.cpp — tabla de presets de fábrica de PULSAR (modos con nombre que
// enseñan el rango del rediseño "campo de caos"). La INTERFAZ (FactoryPreset/PresetParam/
// Category + factoryPresets()) la define shared/presets/PresetTypes.h. Valores en unidades
// del parámetro. El PresetManager los aplica con convertTo0to1 sobre el APVTS.
//
// SYNC es bool (0/1). En modo sync, RATE indexa la tabla de divisiones (params/ParameterIDs.h):
// idx -> rate = idx/(kCount-1)*10. Helper `syncRate()` para no hardcodear el mapeo.
#include "presets/PresetTypes.h"
#include "../params/ParameterIDs.h"

namespace ovni::presets
{

namespace pid = pulsar::params::id;
namespace psy = pulsar::params::sync;

// Índice de división -> valor del param RATE (rango 0..10). idx 6 = "1/8", idx 5 = "1/4", etc.
static constexpr float syncRate (int idx)
{
    return (float) idx / (float) (psy::kCount - 1) * 10.0f;
}

const std::vector<FactoryPreset>& factoryPresets()
{
    using C = Category;
    static const std::vector<FactoryPreset> presets =
    {
        // DERIVA — default amistoso: campo suave, morph entre Orbit/Pendulum, estela corta.
        { "Drift", C::Production, {
            { pid::MOTION, 30.0f }, { pid::RATE, 0.4f },  { pid::SYNC, 0.0f },
            { pid::SHAPE, 40.0f },  { pid::SMEAR, 20.0f }, { pid::WIDTH, 65.0f } } },

        // PULSO 1/8 — enganchado al tempo (1/8), rítmico: el carácter "pulsar" sincronizado.
        { "Pulse 1/8", C::Production, {
            { pid::MOTION, 55.0f }, { pid::RATE, syncRate (6) }, { pid::SYNC, 1.0f },
            { pid::SHAPE, 30.0f },  { pid::SMEAR, 30.0f },       { pid::WIDTH, 80.0f } } },

        // MARIPOSA — Lorenz vivo (salta entre lóbulos), estela media, ancho amplio.
        { "Butterfly", C::SoundDesign, {
            { pid::MOTION, 75.0f }, { pid::RATE, 0.6f },  { pid::SYNC, 0.0f },
            { pid::SHAPE, 66.0f },  { pid::SMEAR, 45.0f }, { pid::WIDTH, 90.0f } } },

        // COMETA — Rössler salvaje + SMEAR alto: la cola de cometa que ÓRBITA no tiene.
        { "Comet", C::SoundDesign, {
            { pid::MOTION, 85.0f }, { pid::RATE, 1.2f },  { pid::SYNC, 0.0f },
            { pid::SHAPE, 100.0f }, { pid::SMEAR, 80.0f }, { pid::WIDTH, 95.0f } } },

        // ÓRBITA CALMA — Orbit puro, lento, casi sin estela: el extremo manso/didáctico.
        { "Calm Orbit", C::Production, {
            { pid::MOTION, 20.0f }, { pid::RATE, 0.25f }, { pid::SYNC, 0.0f },
            { pid::SHAPE, 0.0f },   { pid::SMEAR, 10.0f }, { pid::WIDTH, 55.0f } } },


        // ===== presets ADICIONALES (rediseño: 33 modos cubriendo todo el rango sonoro) =====
        // Heartbeat — Pulso rítmico apretado al 1/4 — vaivén tipo péndulo sincronizado, perfecto…
        { "Heartbeat", C::Production, { { pid::MOTION, 45.0f }, { pid::RATE, 2.0f }, { pid::SYNC, 1.0f }, { pid::DIVISION, 0.0f }, { pid::SHAPE, 33.0f }, { pid::SMEAR, 25.0f }, { pid::WIDTH, 50.0f }, { pid::MIX, 70.0f }, { pid::LOWCUT, 15.0f } } },

        // Strobe Orbit — Órbita elíptica limpia y rápida al 1/4 con mucho ancho — gira la fuente…
        { "Strobe Orbit", C::Production, { { pid::MOTION, 55.0f }, { pid::RATE, 3.0f }, { pid::SYNC, 1.0f }, { pid::DIVISION, 0.0f }, { pid::SHAPE, 8.0f }, { pid::SMEAR, 15.0f }, { pid::WIDTH, 75.0f }, { pid::MIX, 80.0f }, { pid::LOWCUT, 20.0f } } },

        // Tidal Sway — Balanceo de medio compás (1/2), péndulo amplio y envolvente con cola media…
        { "Tidal Sway", C::Production, { { pid::MOTION, 50.0f }, { pid::RATE, 1.5f }, { pid::SYNC, 1.0f }, { pid::DIVISION, 1.0f }, { pid::SHAPE, 40.0f }, { pid::SMEAR, 45.0f }, { pid::WIDTH, 80.0f }, { pid::MIX, 65.0f }, { pid::LOWCUT, 10.0f }, { pid::HICUT, 20.0f } } },

        // Meridian — Trazo de un compás (1 bar) entre órbita y péndulo, movimiento musical…
        { "Meridian", C::Production, { { pid::MOTION, 40.0f }, { pid::RATE, 1.0f }, { pid::SYNC, 1.0f }, { pid::DIVISION, 2.0f }, { pid::SHAPE, 22.0f }, { pid::SMEAR, 35.0f }, { pid::WIDTH, 60.0f }, { pid::MIX, 60.0f }, { pid::LOWCUT, 12.0f }, { pid::HICUT, 10.0f } } },

        // Solar Tide — Barrido de un compás (1 bar) con energía alta y carácter Lorenz suave — más…
        { "Solar Tide", C::Production, { { pid::MOTION, 68.0f }, { pid::RATE, 1.5f }, { pid::SYNC, 1.0f }, { pid::DIVISION, 2.0f }, { pid::SHAPE, 55.0f }, { pid::SMEAR, 60.0f }, { pid::WIDTH, 90.0f }, { pid::MIX, 75.0f }, { pid::LOWCUT, 25.0f } } },

        // Slow Eclipse — Ciclo lento de dos compases (2 bar), órbita-péndulo amplísima y cinemática…
        { "Slow Eclipse", C::Production, { { pid::MOTION, 35.0f }, { pid::RATE, 1.0f }, { pid::SYNC, 1.0f }, { pid::DIVISION, 3.0f }, { pid::SHAPE, 18.0f }, { pid::SMEAR, 70.0f }, { pid::WIDTH, 95.0f }, { pid::MIX, 55.0f }, { pid::LOWCUT, 8.0f }, { pid::HICUT, 30.0f } } },

        // Glassveil — El más transparente: Orbit puro, rotación apenas perceptible y angosta.…
        { "Glassveil", C::Production, { { pid::MOTION, 12.0f }, { pid::RATE, 0.08f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 0.0f }, { pid::SMEAR, 8.0f }, { pid::WIDTH, 30.0f }, { pid::MIX, 28.0f }, { pid::LOWCUT, 12.0f } } },

        // Wander — Vaivén Pendulum lento y orgánico, ancho medio-angosto. Deriva natural sobre…
        { "Wander", C::Production, { { pid::MOTION, 30.0f }, { pid::RATE, 0.18f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 28.0f }, { pid::SMEAR, 18.0f }, { pid::WIDTH, 45.0f }, { pid::MIX, 42.0f }, { pid::LOWCUT, 18.0f } } },

        // Parallax — Mezcla Orbit→Pendulum con apertura amplia y mix moderado. Suma profundidad…
        { "Parallax", C::Production, { { pid::MOTION, 38.0f }, { pid::RATE, 0.25f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 18.0f }, { pid::SMEAR, 14.0f }, { pid::WIDTH, 62.0f }, { pid::MIX, 50.0f }, { pid::LOWCUT, 22.0f } } },

        // Slow Tide — La sutil rítmica: Pendulum sincronizado a 1 compás, respiración espacial al…
        { "Slow Tide", C::Production, { { pid::MOTION, 34.0f }, { pid::RATE, 0.2f }, { pid::SYNC, 1.0f }, { pid::DIVISION, 2.0f }, { pid::SHAPE, 32.0f }, { pid::SMEAR, 22.0f }, { pid::WIDTH, 48.0f }, { pid::MIX, 46.0f }, { pid::LOWCUT, 16.0f }, { pid::HICUT, 10.0f } } },

        // Lull — El más íntimo: Pendulum suavísimo, rate lentísimo y hiCut cálido. Cuna…
        { "Lull", C::Production, { { pid::MOTION, 18.0f }, { pid::RATE, 0.06f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 22.0f }, { pid::SMEAR, 10.0f }, { pid::WIDTH, 36.0f }, { pid::MIX, 34.0f }, { pid::LOWCUT, 8.0f }, { pid::HICUT, 28.0f } } },

        // Cathedral — Órbita amplia y serena que externaliza la fuente fuera de la cabeza:…
        { "Cathedral", C::Production, { { pid::MOTION, 28.0f }, { pid::RATE, 0.18f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 8.0f }, { pid::SMEAR, 55.0f }, { pid::WIDTH, 96.0f }, { pid::MIX, 78.0f }, { pid::LOWCUT, 18.0f }, { pid::HICUT, 12.0f } } },

        // Aurora Veil — Vaivén lento tipo péndulo con estela larga y velo difuso: cortina estéreo…
        { "Aurora Veil", C::Production, { { pid::MOTION, 42.0f }, { pid::RATE, 0.1f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 33.0f }, { pid::SMEAR, 72.0f }, { pid::WIDTH, 100.0f }, { pid::MIX, 84.0f }, { pid::LOWCUT, 24.0f }, { pid::HICUT, 8.0f } } },

        // Tidal Wash — Sincronizado a 1 compás, morph orbital-péndulo: barridos amplios y…
        { "Tidal Wash", C::Production, { { pid::MOTION, 50.0f }, { pid::RATE, 0.5f }, { pid::SYNC, 1.0f }, { pid::DIVISION, 2.0f }, { pid::SHAPE, 22.0f }, { pid::SMEAR, 60.0f }, { pid::WIDTH, 88.0f }, { pid::MIX, 72.0f }, { pid::LOWCUT, 30.0f }, { pid::HICUT, 6.0f } } },

        // Stratosphere — Caos suave del atractor de Lorenz a media energía pero muy ancho: deriva…
        { "Stratosphere", C::Production, { { pid::MOTION, 58.0f }, { pid::RATE, 0.32f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 60.0f }, { pid::SMEAR, 50.0f }, { pid::WIDTH, 92.0f }, { pid::MIX, 80.0f }, { pid::LOWCUT, 14.0f }, { pid::HICUT, 18.0f } } },

        // Celestial Bloom — Espiral cometa de Rössler a baja velocidad con estela exuberante y mix casi…
        { "Celestial Bloom", C::Production, { { pid::MOTION, 64.0f }, { pid::RATE, 0.22f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 90.0f }, { pid::SMEAR, 78.0f }, { pid::WIDTH, 100.0f }, { pid::MIX, 90.0f }, { pid::LOWCUT, 22.0f }, { pid::HICUT, 4.0f } } },

        // Event Horizon — Caos Lorenz pesado y lento que se traga la fuente: deriva gravitacional…
        { "Event Horizon", C::SoundDesign, { { pid::MOTION, 92.0f }, { pid::RATE, 0.18f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 68.0f }, { pid::SMEAR, 72.0f }, { pid::WIDTH, 85.0f }, { pid::MIX, 100.0f }, { pid::LOWCUT, 18.0f }, { pid::HICUT, 48.0f } } },

        // Static Storm — Espiral Rössler agitada a alta velocidad con estela larga: textura de…
        { "Static Storm", C::SoundDesign, { { pid::MOTION, 88.0f }, { pid::RATE, 6.5f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 94.0f }, { pid::SMEAR, 88.0f }, { pid::WIDTH, 52.0f }, { pid::MIX, 95.0f }, { pid::LOWCUT, 35.0f } } },

        // Quasar — Cometa Rössler puro pulsando al tempo (1/2): caos rítmico ultra-ancho y…
        { "Quasar", C::SoundDesign, { { pid::MOTION, 90.0f }, { pid::RATE, 1.0f }, { pid::SYNC, 1.0f }, { pid::DIVISION, 1.0f }, { pid::SHAPE, 100.0f }, { pid::SMEAR, 78.0f }, { pid::WIDTH, 100.0f }, { pid::MIX, 100.0f }, { pid::LOWCUT, 12.0f }, { pid::HICUT, 22.0f } } },

        // Tesseract — Mezcla Lorenz/Rössler sincronizada a 1 compás: paneo geométrico que…
        { "Tesseract", C::SoundDesign, { { pid::MOTION, 78.0f }, { pid::RATE, 1.0f }, { pid::SYNC, 1.0f }, { pid::DIVISION, 2.0f }, { pid::SHAPE, 82.0f }, { pid::SMEAR, 58.0f }, { pid::WIDTH, 70.0f }, { pid::MIX, 85.0f }, { pid::LOWCUT, 42.0f }, { pid::HICUT, 30.0f } } },

        // Plasma Field — Lorenz profundo con movimiento y estela al máximo: muro envolvente que…
        { "Plasma Field", C::SoundDesign, { { pid::MOTION, 96.0f }, { pid::RATE, 0.35f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 70.0f }, { pid::SMEAR, 95.0f }, { pid::WIDTH, 95.0f }, { pid::MIX, 100.0f }, { pid::LOWCUT, 28.0f }, { pid::HICUT, 58.0f } } },

        // Singularity — Rössler a tope, todo al extremo y muy rápido: el patch más violento y…
        { "Singularity", C::SoundDesign, { { pid::MOTION, 100.0f }, { pid::RATE, 8.8f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 100.0f }, { pid::SMEAR, 92.0f }, { pid::WIDTH, 62.0f }, { pid::MIX, 100.0f }, { pid::LOWCUT, 55.0f }, { pid::HICUT, 12.0f } } },

        // Orbital — Órbita amplia y lenta, círculo limpísimo y envolvente. Para pads y texturas…
        { "Orbital", C::Production, { { pid::MOTION, 25.0f }, { pid::RATE, 0.08f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 0.0f }, { pid::SMEAR, 22.0f }, { pid::WIDTH, 78.0f }, { pid::MIX, 70.0f }, { pid::LOWCUT, 12.0f } } },

        // Slow Pulse — Péndulo suave de vaivén, lento e íntimo, estéreo contenido. Ideal para…
        { "Slow Pulse", C::Production, { { pid::MOTION, 35.0f }, { pid::RATE, 0.18f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 30.0f }, { pid::SMEAR, 30.0f }, { pid::WIDTH, 45.0f }, { pid::MIX, 60.0f }, { pid::LOWCUT, 20.0f }, { pid::HICUT, 8.0f } } },

        // Halo — Órbita rápida y apretada, brillante y nítida. Aporta vida y giro definido a…
        { "Halo", C::Production, { { pid::MOTION, 50.0f }, { pid::RATE, 2.4f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 8.0f }, { pid::SMEAR, 15.0f }, { pid::WIDTH, 60.0f }, { pid::MIX, 75.0f }, { pid::LOWCUT, 28.0f } } },

        // Tidal — Mezcla péndulo-órbita media, ancha y respirando de lado a lado. Buena para…
        { "Tidal", C::Production, { { pid::MOTION, 42.0f }, { pid::RATE, 0.55f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 40.0f }, { pid::SMEAR, 38.0f }, { pid::WIDTH, 85.0f }, { pid::MIX, 68.0f }, { pid::LOWCUT, 15.0f }, { pid::HICUT, 6.0f } } },

        // Equinox — Rotación de órbita media, equilibrada y de referencia, limpia con un poco…
        { "Equinox", C::Production, { { pid::MOTION, 38.0f }, { pid::RATE, 0.85f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 15.0f }, { pid::SMEAR, 25.0f }, { pid::WIDTH, 55.0f }, { pid::MIX, 80.0f }, { pid::LOWCUT, 30.0f } } },

        // Vintage Radio — Voz/sinte como dial de radio vieja: banda angosta y nasal que se balancea…
        { "Vintage Radio", C::SoundDesign, { { pid::MOTION, 38.0f }, { pid::RATE, 0.35f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 33.0f }, { pid::SMEAR, 25.0f }, { pid::WIDTH, 35.0f }, { pid::MIX, 100.0f }, { pid::LOWCUT, 62.0f }, { pid::HICUT, 58.0f } } },

        // Dust Channel — Lo-fi sutil con deriva orbital floja: recorta graves de barro y algo de…
        { "Dust Channel", C::Production, { { pid::MOTION, 28.0f }, { pid::RATE, 0.18f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 15.0f }, { pid::SMEAR, 40.0f }, { pid::WIDTH, 50.0f }, { pid::MIX, 55.0f }, { pid::LOWCUT, 48.0f }, { pid::HICUT, 40.0f } } },

        // Ionosphere — Aéreo y sin peso: saca todo el grave, espiral Rössler ancha con estela…
        { "Ionosphere", C::SoundDesign, { { pid::MOTION, 72.0f }, { pid::RATE, 0.9f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 92.0f }, { pid::SMEAR, 78.0f }, { pid::WIDTH, 92.0f }, { pid::MIX, 100.0f }, { pid::LOWCUT, 80.0f } } },

        // Cellar — Oscuro y subterráneo: hiCut fuerte apaga todo, mariposa Lorenz lenta…
        { "Cellar", C::SoundDesign, { { pid::MOTION, 58.0f }, { pid::RATE, 0.12f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 66.0f }, { pid::SMEAR, 55.0f }, { pid::WIDTH, 58.0f }, { pid::MIX, 100.0f }, { pid::HICUT, 72.0f } } },

        // Shortwave — Onda corta agitada al tempo: banda estrecha y telefónica con vaivén…
        { "Shortwave", C::Production, { { pid::MOTION, 62.0f }, { pid::RATE, 1.0f }, { pid::SYNC, 1.0f }, { pid::DIVISION, 0.0f }, { pid::SHAPE, 45.0f }, { pid::SMEAR, 20.0f }, { pid::WIDTH, 44.0f }, { pid::MIX, 85.0f }, { pid::LOWCUT, 55.0f }, { pid::HICUT, 50.0f } } },

        // Veil — Velado y discreto: órbita limpia y oscura suave que empuja la fuente atrás.…
        { "Veil", C::Production, { { pid::MOTION, 22.0f }, { pid::RATE, 0.25f }, { pid::SYNC, 0.0f }, { pid::SHAPE, 5.0f }, { pid::SMEAR, 30.0f }, { pid::WIDTH, 62.0f }, { pid::MIX, 45.0f }, { pid::LOWCUT, 18.0f }, { pid::HICUT, 55.0f } } },
    };
    return presets;
}

} // namespace ovni::presets
