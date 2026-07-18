// FactoryPresets.cpp — tabla de presets de fábrica de SUPERNOVA (36 MUNDOS: M3+vocabulario+3D/color).
// Cada preset es un MUNDO completo (pedido "nivel TouchDesigner"): MOVIMIENTO (motion) + FÍSICA (7 coefs) +
// GEOMETRÍA (shape/trails/links/size) + COLOR (sat/hue/glow) + CUTOUT donde aplica. Valores en UNIDADES de
// parámetro (0-100; gravity −100..100; hue −180..180; choices como índice). applyFactory resetea TODO a
// default y luego setea los ids listados → un id omitido vuelve a su default (motion 0=Contornos, shape 0=Dot,
// trails/links/hue/cutout 0, sat 50).
//   motion: 0=Contornos · 1=Materia · 2=Onda · 3=Vórtices · 4=Radial      (índices del choice, ParamMapping)
//   shape:  0=Dot · 1=Disc · 2=Ring · 3=Dash · 4=Tri · 5=Quad · 6=Spark
// Regla de estabilidad: home alto ↔ momentum bajo (snap seco), momentum alto ↔ home bajo (orbital sin ringing).
// Los índices 0..19 y su física NO cambian (tests + MIDI 48-71 los referencian); el vocabulario se les SUMA.
// Nombres del universo OVNI. Gate de craft §9.7 (kick fuerte / ambient / voz) pendiente con Joaquín.
#include "presets/PresetTypes.h"

namespace ovni::presets
{
const std::vector<FactoryPreset>& factoryPresets()
{
    using C = Category;
    static const std::vector<FactoryPreset> presets {
        // --- Production: usables, sostienen material real sin saturar ---
        { "Genesis", C::Production, {
            { "intensity", 50.0f }, { "chaos", 30.0f }, { "particleSize", 42.0f }, { "glow", 52.0f },
            { "curlScale", 45.0f }, { "homeStrength", 55.0f }, { "momentum", 48.0f },
            { "radialGain", 50.0f }, { "jitterGain", 45.0f }, { "breatheGain", 45.0f } } },   // la base viva: contornos puros, re-arma sólido
        { "Nebula", C::Production, {
            { "intensity", 30.0f }, { "chaos", 20.0f }, { "particleSize", 62.0f }, { "glow", 70.0f },
            { "curlScale", 25.0f }, { "homeStrength", 22.0f }, { "gravity", -8.0f }, { "momentum", 82.0f },
            { "radialGain", 30.0f }, { "jitterGain", 20.0f }, { "breatheGain", 75.0f },
            { "shape", 1.0f }, { "trails", 22.0f }, { "sat", 62.0f } } },                     // gas: discos blandos que dejan bruma
        { "Orbit", C::Production, {
            { "intensity", 45.0f }, { "chaos", 38.0f }, { "particleSize", 45.0f }, { "glow", 55.0f },
            { "curlScale", 40.0f }, { "homeStrength", 30.0f }, { "momentum", 90.0f },
            { "radialGain", 45.0f }, { "jitterGain", 40.0f }, { "breatheGain", 40.0f },
            { "trails", 16.0f } } },                                                          // orbital con overshoot; caminos cortos
        { "Pulsar", C::Production, {
            { "intensity", 55.0f }, { "chaos", 25.0f }, { "particleSize", 38.0f }, { "glow", 60.0f },
            { "curlScale", 55.0f }, { "homeStrength", 78.0f }, { "momentum", 35.0f },
            { "radialGain", 70.0f }, { "jitterGain", 35.0f }, { "breatheGain", 55.0f },
            { "motion", 4.0f } , { "pump", 68.0f } } },                                                           // el latido RADIAL clásico, como preset
        { "Aurora", C::Production, {
            { "intensity", 35.0f }, { "chaos", 22.0f }, { "particleSize", 55.0f }, { "glow", 68.0f },
            { "curlScale", 30.0f }, { "homeStrength", 35.0f }, { "gravity", -15.0f }, { "momentum", 80.0f },
            { "radialGain", 25.0f }, { "jitterGain", 75.0f }, { "breatheGain", 50.0f },
            { "motion", 2.0f }, { "shape", 3.0f }, { "trails", 30.0f }, { "sat", 58.0f } } }, // cortinas: la ONDA barre trazos que ondulan
        { "Heartbeat", C::Production, {
            { "intensity", 32.0f }, { "chaos", 12.0f }, { "particleSize", 50.0f }, { "glow", 58.0f },
            { "curlScale", 35.0f }, { "homeStrength", 60.0f }, { "momentum", 55.0f },
            { "radialGain", 40.0f }, { "jitterGain", 15.0f }, { "breatheGain", 90.0f },
            { "motion", 1.0f } } },                                                           // MATERIA que respira con el RMS, sin epicentro
        { "Crystal", C::Production, {
            { "intensity", 40.0f }, { "chaos", 8.0f }, { "particleSize", 30.0f }, { "glow", 45.0f },
            { "curlScale", 60.0f }, { "homeStrength", 92.0f }, { "momentum", 25.0f },
            { "radialGain", 35.0f }, { "jitterGain", 20.0f }, { "breatheGain", 20.0f },
            { "shape", 5.0f }, { "links", 14.0f } } },                                        // celosía: teselas + enlaces = red cristalina
        { "Aether", C::Production, {
            { "intensity", 25.0f }, { "chaos", 18.0f }, { "particleSize", 48.0f }, { "glow", 62.0f },
            { "curlScale", 20.0f }, { "homeStrength", 12.0f }, { "gravity", -5.0f }, { "momentum", 95.0f },
            { "radialGain", 25.0f }, { "jitterGain", 25.0f }, { "breatheGain", 55.0f },
            { "trails", 45.0f }, { "sat", 40.0f } , { "speed", 35.0f }, { "scatter", 8.0f } } },                                        // flota eterno: bruma desaturada con memoria
        { "Constellation", C::Production, {
            { "intensity", 40.0f }, { "chaos", 6.0f }, { "particleSize", 80.0f }, { "glow", 65.0f },
            { "curlScale", 50.0f }, { "homeStrength", 85.0f }, { "momentum", 30.0f },
            { "radialGain", 30.0f }, { "jitterGain", 30.0f }, { "breatheGain", 30.0f },
            { "shape", 2.0f }, { "links", 55.0f }, { "sat", 55.0f } , { "density", 30.0f } } },                      // RING+LINKS: anillos GRANDES legibles (4px se fundían)
        { "Drift", C::Production, {
            { "intensity", 30.0f }, { "chaos", 15.0f }, { "particleSize", 52.0f }, { "glow", 56.0f },
            { "curlScale", 28.0f }, { "homeStrength", 28.0f }, { "gravity", -25.0f }, { "momentum", 85.0f },
            { "radialGain", 30.0f }, { "jitterGain", 25.0f }, { "breatheGain", 45.0f },
            { "shape", 3.0f }, { "trails", 35.0f } , { "speed", 42.0f } } },                                       // humo/incienso: jirones que ascienden con rastro

        // --- Sound Design: extremos, energía alta, gestos dramáticos ---
        { "Collapse", C::SoundDesign, {
            { "intensity", 80.0f }, { "chaos", 65.0f }, { "particleSize", 28.0f }, { "glow", 40.0f },
            { "curlScale", 70.0f }, { "homeStrength", 40.0f }, { "gravity", 20.0f }, { "momentum", 60.0f },
            { "radialGain", 55.0f }, { "jitterGain", 60.0f }, { "breatheGain", 40.0f },
            { "motion", 1.0f }, { "shape", 4.0f }, { "trails", 15.0f } } },                   // fragmentos direccionales: se desmorona y re-arma
        { "Supernova", C::SoundDesign, {
            { "intensity", 78.0f }, { "chaos", 35.0f }, { "particleSize", 45.0f }, { "glow", 80.0f },
            { "curlScale", 45.0f }, { "homeStrength", 62.0f }, { "momentum", 62.0f },
            { "radialGain", 95.0f }, { "jitterGain", 45.0f }, { "breatheGain", 55.0f },
            { "motion", 4.0f }, { "shape", 6.0f }, { "trails", 25.0f } , { "pump", 58.0f } } },                   // firma: expansión RADIAL masiva en destellos
        { "Outburst", C::SoundDesign, {
            { "intensity", 70.0f }, { "chaos", 30.0f }, { "particleSize", 35.0f }, { "glow", 70.0f },
            { "curlScale", 50.0f }, { "homeStrength", 90.0f }, { "momentum", 22.0f },
            { "radialGain", 90.0f }, { "jitterGain", 40.0f }, { "breatheGain", 45.0f },
            { "motion", 4.0f }, { "shape", 1.0f } } },                                        // estrobo seco por kick: estalla y vuelve
        { "Quasar", C::SoundDesign, {
            { "intensity", 92.0f }, { "chaos", 40.0f }, { "particleSize", 40.0f }, { "glow", 95.0f },
            { "curlScale", 55.0f }, { "homeStrength", 50.0f }, { "gravity", -20.0f }, { "momentum", 70.0f },
            { "radialGain", 75.0f }, { "jitterGain", 55.0f }, { "breatheGain", 50.0f },
            { "shape", 3.0f }, { "trails", 55.0f } , { "pump", 45.0f } } },                                       // ríos de luz: trazos por los contornos al máximo
        { "Storm", C::SoundDesign, {
            { "intensity", 68.0f }, { "chaos", 72.0f }, { "particleSize", 30.0f }, { "glow", 48.0f },
            { "curlScale", 88.0f }, { "homeStrength", 45.0f }, { "gravity", 10.0f }, { "momentum", 55.0f },
            { "radialGain", 45.0f }, { "jitterGain", 85.0f }, { "breatheGain", 40.0f },
            { "motion", 3.0f }, { "shape", 6.0f }, { "links", 20.0f } , { "speed", 62.0f }, { "pump", 55.0f } } },                    // VÓRTICES eléctricos: chispas + relámpagos entre cercanos
        { "Vortex", C::SoundDesign, {
            { "intensity", 62.0f }, { "chaos", 78.0f }, { "particleSize", 42.0f }, { "glow", 55.0f },
            { "curlScale", 18.0f }, { "homeStrength", 28.0f }, { "momentum", 92.0f },
            { "radialGain", 50.0f }, { "jitterGain", 30.0f }, { "breatheGain", 45.0f },
            { "motion", 3.0f }, { "trails", 40.0f } , { "rotate", 12.0f } } },                                      // galaxia: los remolinos PINTAN brazos espirales
        { "Meteor", C::SoundDesign, {
            { "intensity", 65.0f }, { "chaos", 35.0f }, { "particleSize", 38.0f }, { "glow", 60.0f },
            { "curlScale", 50.0f }, { "homeStrength", 40.0f }, { "gravity", 50.0f }, { "momentum", 88.0f },
            { "radialGain", 55.0f }, { "jitterGain", 40.0f }, { "breatheGain", 35.0f },
            { "shape", 4.0f }, { "trails", 60.0f } } },                                       // puntas que CAEN con estela larga, luego re-arman
        { "Swarm", C::SoundDesign, {
            { "intensity", 55.0f }, { "chaos", 48.0f }, { "particleSize", 26.0f }, { "glow", 50.0f },
            { "curlScale", 65.0f }, { "homeStrength", 20.0f }, { "momentum", 45.0f },
            { "radialGain", 45.0f }, { "jitterGain", 80.0f }, { "breatheGain", 35.0f },
            { "motion", 1.0f }, { "shape", 4.0f }, { "links", 28.0f } } },                    // insectos conectándose al pasar (red que zumba)
        { "Singularity", C::SoundDesign, {
            { "intensity", 45.0f }, { "chaos", 15.0f }, { "particleSize", 24.0f }, { "glow", 42.0f },
            { "curlScale", 40.0f }, { "homeStrength", 96.0f }, { "momentum", 18.0f },
            { "radialGain", 30.0f }, { "jitterGain", 15.0f }, { "breatheGain", 15.0f },
            { "motion", 4.0f }, { "sat", 35.0f } , { "density", 55.0f } } },                                         // todo comprimido a la retícula, austero
        { "Plasma", C::SoundDesign, {
            { "intensity", 85.0f }, { "chaos", 60.0f }, { "particleSize", 34.0f }, { "glow", 78.0f },
            { "curlScale", 60.0f }, { "homeStrength", 42.0f }, { "momentum", 72.0f },
            { "radialGain", 60.0f }, { "jitterGain", 65.0f }, { "breatheGain", 60.0f },
            { "motion", 1.0f }, { "shape", 1.0f }, { "trails", 30.0f },
            { "sat", 70.0f }, { "hue", 25.0f } , { "pump", 55.0f }, { "hueCycle", 6.0f } } },                                           // fluido incandescente, paleta corrida al fuego

        // --- Los 10 NUEVOS: familias del vocabulario (índices 20..29) ---
        { "Calligraphy", C::Production, {   // literal partido: \xAD + 'a' se comería la 'a' (trampa hex)
            { "intensity", 45.0f }, { "chaos", 28.0f }, { "particleSize", 45.0f }, { "glow", 55.0f },
            { "curlScale", 40.0f }, { "homeStrength", 45.0f }, { "momentum", 62.0f },
            { "radialGain", 45.0f }, { "jitterGain", 25.0f }, { "breatheGain", 40.0f },
            { "shape", 3.0f }, { "trails", 70.0f }, { "sat", 55.0f } } },                     // ★ la imagen SE PINTA a sí misma (mockup estrella)
        { "Eclipse", C::Production, {
            { "intensity", 38.0f }, { "chaos", 22.0f }, { "particleSize", 42.0f }, { "glow", 42.0f },
            { "curlScale", 38.0f }, { "homeStrength", 50.0f }, { "momentum", 58.0f },
            { "radialGain", 40.0f }, { "jitterGain", 20.0f }, { "breatheGain", 45.0f },
            { "shape", 3.0f }, { "trails", 55.0f }, { "sat", 0.0f } , { "density", 70.0f } } },                      // MONOCROMO EDITORIAL: tinta B/N que caligrafía
        { "Cluster", C::Production, {
            { "intensity", 35.0f }, { "chaos", 25.0f }, { "particleSize", 30.0f }, { "glow", 62.0f },
            { "curlScale", 35.0f }, { "homeStrength", 40.0f }, { "momentum", 75.0f },
            { "radialGain", 35.0f }, { "jitterGain", 35.0f }, { "breatheGain", 50.0f },
            { "motion", 1.0f }, { "links", 70.0f }, { "trails", 20.0f }, { "sat", 60.0f } , { "density", 45.0f }, { "scatter", 10.0f } } },// red viva: DOT+LINKS+memoria (neuronas del cosmos)
        { "Monolith", C::Production, {
            { "intensity", 42.0f }, { "chaos", 8.0f }, { "particleSize", 30.0f }, { "glow", 48.0f },
            { "curlScale", 20.0f }, { "homeStrength", 88.0f }, { "momentum", 28.0f },
            { "radialGain", 45.0f }, { "jitterGain", 8.0f }, { "breatheGain", 35.0f },
            { "motion", 2.0f }, { "shape", 5.0f }, { "sat", 65.0f } , { "density", 7.0f } } },                      // MOSAICO: teselas CHICAS nítidas (las grandes se funden en lo denso), grilla dura barrida por la onda
        { "Perseids", C::SoundDesign, {
            { "intensity", 60.0f }, { "chaos", 30.0f }, { "particleSize", 34.0f }, { "glow", 70.0f },
            { "curlScale", 45.0f }, { "homeStrength", 42.0f }, { "gravity", 35.0f }, { "momentum", 80.0f },
            { "radialGain", 85.0f }, { "jitterGain", 30.0f }, { "breatheGain", 35.0f },
            { "motion", 4.0f }, { "shape", 6.0f }, { "trails", 65.0f } , { "pump", 60.0f } } },                   // PIROTECNIA: cada kick = fuegos artificiales que llueven
        { "Tide", C::Production, {
            { "intensity", 33.0f }, { "chaos", 25.0f }, { "particleSize", 55.0f }, { "glow", 58.0f },
            { "curlScale", 22.0f }, { "homeStrength", 30.0f }, { "momentum", 88.0f },
            { "radialGain", 30.0f }, { "jitterGain", 20.0f }, { "breatheGain", 85.0f },
            { "motion", 1.0f }, { "trails", 28.0f }, { "sat", 58.0f } , { "speed", 35.0f } } },                    // FLUIDO: la imagen sube y baja como agua lenta
        { "Ultraviolet", C::SoundDesign, {
            { "intensity", 55.0f }, { "chaos", 30.0f }, { "particleSize", 40.0f }, { "glow", 88.0f },
            { "curlScale", 45.0f }, { "homeStrength", 55.0f }, { "momentum", 60.0f },
            { "radialGain", 55.0f }, { "jitterGain", 40.0f }, { "breatheGain", 50.0f },
            { "shape", 2.0f }, { "trails", 30.0f }, { "sat", 80.0f }, { "hue", -60.0f } } },  // NEÓN: anillos-tubo, paleta corrida al violeta
        { "Totem", C::Production, {
            { "intensity", 45.0f }, { "chaos", 20.0f }, { "particleSize", 44.0f }, { "glow", 60.0f },
            { "curlScale", 38.0f }, { "homeStrength", 65.0f }, { "momentum", 50.0f },
            { "radialGain", 40.0f }, { "jitterGain", 25.0f }, { "breatheGain", 45.0f },
            { "cutout", 100.0f }, { "links", 35.0f }, { "trails", 12.0f }, { "sat", 60.0f } } }, // ESCULTURA: sin fondo, la forma conectada flota
        { "Mirage", C::Production, {
            { "intensity", 36.0f }, { "chaos", 24.0f }, { "particleSize", 52.0f }, { "glow", 64.0f },
            { "curlScale", 30.0f }, { "homeStrength", 38.0f }, { "momentum", 78.0f },
            { "radialGain", 30.0f }, { "jitterGain", 60.0f }, { "breatheGain", 65.0f },
            { "motion", 2.0f }, { "shape", 1.0f }, { "trails", 40.0f },
            { "sat", 30.0f }, { "hue", 140.0f } , { "hueCycle", 12.0f }, { "speed", 42.0f } } },                                          // paleta alien lavada: onda que sueña
        { "Big Bang", C::SoundDesign, {
            { "intensity", 75.0f }, { "chaos", 45.0f }, { "particleSize", 42.0f }, { "glow", 85.0f },
            { "curlScale", 50.0f }, { "homeStrength", 55.0f }, { "momentum", 75.0f },
            { "radialGain", 100.0f }, { "jitterGain", 45.0f }, { "breatheGain", 50.0f },
            { "motion", 4.0f }, { "shape", 6.0f }, { "trails", 45.0f }, { "links", 15.0f } , { "pump", 80.0f } } },  // el show-stopper: TODO explota y se re-teje

        // --- Tier PRO (idx 30-31): mundos caleidoscópicos — el multiplicador de look nuevo ---
        { "Kaleidoscope", C::SoundDesign, {
            { "intensity", 48.0f }, { "chaos", 30.0f }, { "particleSize", 45.0f }, { "glow", 62.0f },
            { "curlScale", 45.0f }, { "homeStrength", 50.0f }, { "momentum", 65.0f },
            { "radialGain", 50.0f }, { "jitterGain", 30.0f }, { "breatheGain", 45.0f },
            { "kaleido", 3.0f }, { "trails", 30.0f }, { "rotate", 8.0f }, { "sat", 62.0f } } },  // 6 espejos girando: mandala vivo de TU imagen
        { "Hypnosis", C::SoundDesign, {
            { "intensity", 42.0f }, { "chaos", 24.0f }, { "particleSize", 52.0f }, { "glow", 68.0f },
            { "curlScale", 30.0f }, { "homeStrength", 40.0f }, { "momentum", 78.0f },
            { "radialGain", 35.0f }, { "jitterGain", 45.0f }, { "breatheGain", 60.0f },
            { "motion", 2.0f }, { "shape", 1.0f }, { "kaleido", 4.0f }, { "rotate", -14.0f },
            { "hueCycle", 18.0f }, { "trails", 40.0f }, { "speed", 44.0f }, { "sat", 60.0f } } },  // 8 espejos en contra-giro + tono en deriva: el túnel

        // --- 3D + COLOR LAB (idx 32-35): la escultura se da vuelta, la figura y la paleta como identidad ---
        { "Satellite", C::Production, {
            { "intensity", 45.0f }, { "chaos", 18.0f }, { "particleSize", 44.0f }, { "glow", 62.0f },
            { "curlScale", 36.0f }, { "homeStrength", 62.0f }, { "momentum", 52.0f },
            { "radialGain", 45.0f }, { "jitterGain", 25.0f }, { "breatheGain", 45.0f },
            { "cutout", 100.0f }, { "depth", 70.0f }, { "orbit", 30.0f }, { "rotX", -10.0f },
            { "links", 18.0f }, { "sat", 58.0f } } },                                     // la ESCULTURA en órbita perpetua (el pedido "darlo vuelta")
        { "Globe", C::SoundDesign, {
            { "intensity", 50.0f }, { "chaos", 22.0f }, { "particleSize", 46.0f }, { "glow", 68.0f },
            { "curlScale", 40.0f }, { "homeStrength", 70.0f }, { "momentum", 45.0f },
            { "radialGain", 60.0f }, { "jitterGain", 30.0f }, { "breatheGain", 50.0f },
            { "figure", 1.0f }, { "depth", 75.0f }, { "orbit", 40.0f }, { "rotX", -12.0f },
            { "cutout", 100.0f }, { "pump", 50.0f } } },                                  // tus píxeles envueltos en una ESFERA que gira y explota
        { "Silkscreen", C::Production, {   // literal partido: \xAD + 'a' hex = trampa (a)
            { "intensity", 40.0f }, { "chaos", 22.0f }, { "particleSize", 48.0f }, { "glow", 52.0f },
            { "curlScale", 40.0f }, { "homeStrength", 55.0f }, { "momentum", 55.0f },
            { "radialGain", 45.0f }, { "jitterGain", 30.0f }, { "breatheGain", 45.0f },
            { "palette", 3.0f }, { "trails", 16.0f } } },                                 // DUOTONO editorial: tinta navy sobre papel crema (el lienzo claro)
        { "Radar", C::SoundDesign, {
            { "intensity", 55.0f }, { "chaos", 28.0f }, { "particleSize", 40.0f }, { "glow", 72.0f },
            { "curlScale", 50.0f }, { "homeStrength", 55.0f }, { "momentum", 60.0f },
            { "radialGain", 65.0f }, { "jitterGain", 40.0f }, { "breatheGain", 50.0f },
            { "palette", 1.0f }, { "depth", 45.0f }, { "rotX", -16.0f }, { "orbit", 16.0f },
            { "pump", 55.0f }, { "trails", 22.0f } } },                                   // TÉRMICA FLIR en relieve: la imagen como firma de calor girando

        // --- EXPANSION (idx 36-43): 8 mundos nuevos — huecos del vocabulario, paletas LookRamps como
        // identidad, regla de estabilidad respetada (home alto ↔ momentum bajo y viceversa). SOLO append:
        // los índices previos no se tocan (tests + MIDI 48-71 + estados guardados renormalizan igual que
        // en expansiones anteriores 20→30→36).
        { "Deep Field", C::Production, {
            { "intensity", 34.0f }, { "chaos", 14.0f }, { "particleSize", 16.0f }, { "glow", 46.0f },
            { "curlScale", 30.0f }, { "homeStrength", 34.0f }, { "momentum", 72.0f },
            { "radialGain", 25.0f }, { "jitterGain", 22.0f }, { "breatheGain", 40.0f },
            { "motion", 1.0f }, { "density", 85.0f }, { "sat", 66.0f },
            { "scatter", 12.0f }, { "speed", 26.0f }, { "trails", 8.0f } } },             // campo profundo Hubble: miles de galaxias diminutas a la deriva
        { "Blueprint", C::Production, {
            { "intensity", 40.0f }, { "chaos", 5.0f }, { "particleSize", 26.0f }, { "glow", 40.0f },
            { "curlScale", 25.0f }, { "homeStrength", 90.0f }, { "momentum", 20.0f },
            { "radialGain", 30.0f }, { "jitterGain", 10.0f }, { "breatheGain", 22.0f },
            { "palette", 4.0f }, { "shape", 5.0f }, { "links", 45.0f }, { "density", 45.0f } } }, // plano técnico: retícula CYANOTYPE, teselas + cotas (snap seco)
        { "Solar Flare", C::SoundDesign, {
            { "intensity", 74.0f }, { "chaos", 45.0f }, { "particleSize", 34.0f }, { "glow", 85.0f },
            { "curlScale", 72.0f }, { "homeStrength", 45.0f }, { "momentum", 65.0f },
            { "radialGain", 80.0f }, { "jitterGain", 50.0f }, { "breatheGain", 45.0f },
            { "motion", 3.0f }, { "shape", 3.0f }, { "trails", 60.0f },
            { "sat", 75.0f }, { "hue", 15.0f }, { "pump", 65.0f }, { "speed", 55.0f } } }, // arcos magnéticos: lazos de fuego que latigan con el kick
        { "Frostbite", C::Production, {
            { "intensity", 38.0f }, { "chaos", 6.0f }, { "particleSize", 30.0f }, { "glow", 55.0f },
            { "curlScale", 55.0f }, { "homeStrength", 88.0f }, { "momentum", 24.0f },
            { "radialGain", 35.0f }, { "jitterGain", 14.0f }, { "breatheGain", 25.0f },
            { "palette", 9.0f }, { "shape", 4.0f }, { "links", 22.0f },
            { "trails", 10.0f }, { "density", 55.0f } } },                                // escarcha COLD FIRE: agujas que cristalizan en red (gélido, quieto)
        { "Redshift", C::SoundDesign, {
            { "intensity", 52.0f }, { "chaos", 24.0f }, { "particleSize", 42.0f }, { "glow", 70.0f },
            { "curlScale", 40.0f }, { "homeStrength", 40.0f }, { "momentum", 75.0f },
            { "radialGain", 60.0f }, { "jitterGain", 30.0f }, { "breatheGain", 50.0f },
            { "motion", 4.0f }, { "shape", 2.0f }, { "trails", 35.0f }, { "sat", 60.0f },
            { "hue", -25.0f }, { "hueCycle", 10.0f }, { "depth", 60.0f }, { "orbit", 22.0f },
            { "rotX", -8.0f }, { "pump", 45.0f } } },                                     // universo en fuga: anillos 3D que se corren al rojo mientras orbitan
        { "Magnetar", C::SoundDesign, {
            { "intensity", 66.0f }, { "chaos", 70.0f }, { "particleSize", 24.0f }, { "glow", 66.0f },
            { "curlScale", 85.0f }, { "homeStrength", 42.0f }, { "momentum", 52.0f },
            { "radialGain", 50.0f }, { "jitterGain", 90.0f }, { "breatheGain", 40.0f },
            { "motion", 3.0f }, { "shape", 6.0f }, { "links", 55.0f }, { "trails", 25.0f },
            { "palette", 7.0f }, { "pump", 60.0f }, { "speed", 58.0f } } },               // tormenta NEON: chispas + arcos eléctricos entre vecinos (el tesla)
        { "Ember Rain", C::Production, {
            { "intensity", 48.0f }, { "chaos", 20.0f }, { "particleSize", 30.0f }, { "glow", 62.0f },
            { "curlScale", 40.0f }, { "homeStrength", 30.0f }, { "momentum", 82.0f },
            { "radialGain", 40.0f }, { "jitterGain", 28.0f }, { "breatheGain", 42.0f },
            { "gravity", 55.0f }, { "shape", 6.0f }, { "trails", 50.0f },
            { "palette", 8.0f }, { "speed", 40.0f } } },                                  // brasas MAGMA cayendo con estela: fogata invertida, hipnótico
        { "Cathedral", C::Production, {
            { "intensity", 44.0f }, { "chaos", 14.0f }, { "particleSize", 40.0f }, { "glow", 66.0f },
            { "curlScale", 35.0f }, { "homeStrength", 60.0f }, { "momentum", 45.0f },
            { "radialGain", 45.0f }, { "jitterGain", 18.0f }, { "breatheGain", 50.0f },
            { "kaleido", 2.0f }, { "rotate", 5.0f }, { "shape", 2.0f },
            { "trails", 20.0f }, { "palette", 15.0f }, { "sat", 60.0f } } },              // rosetón BLACK GOLD: mandala solemne de 4 espejos girando lento

        // --- EXPANSION 2 (idx 44-49): las figuras y paletas que faltaban estrenar ---
        { "Wormhole", C::SoundDesign, {
            { "intensity", 58.0f }, { "chaos", 30.0f }, { "particleSize", 38.0f }, { "glow", 72.0f },
            { "curlScale", 45.0f }, { "homeStrength", 45.0f }, { "momentum", 70.0f },
            { "radialGain", 55.0f }, { "jitterGain", 30.0f }, { "breatheGain", 45.0f },
            { "figure", 2.0f }, { "depth", 70.0f }, { "orbit", 25.0f }, { "rotX", -10.0f },
            { "motion", 3.0f }, { "shape", 2.0f }, { "trails", 45.0f },
            { "hueCycle", 8.0f }, { "pump", 50.0f }, { "sat", 60.0f } } },                // túnel ESPIRAL 3D: anillos cayendo al centro con deriva de tono
        { "Mercury", C::Production, {
            { "intensity", 40.0f }, { "chaos", 15.0f }, { "particleSize", 55.0f }, { "glow", 60.0f },
            { "curlScale", 30.0f }, { "homeStrength", 35.0f }, { "momentum", 85.0f },
            { "radialGain", 30.0f }, { "jitterGain", 20.0f }, { "breatheGain", 60.0f },
            { "palette", 5.0f }, { "motion", 2.0f }, { "shape", 1.0f },
            { "trails", 20.0f }, { "speed", 38.0f } } },                                  // metal líquido CHROME: gotas de mercurio que ondulan lentas
        { "Hologram", C::SoundDesign, {
            { "intensity", 55.0f }, { "chaos", 25.0f }, { "particleSize", 42.0f }, { "glow", 80.0f },
            { "curlScale", 40.0f }, { "homeStrength", 55.0f }, { "momentum", 55.0f },
            { "radialGain", 50.0f }, { "jitterGain", 35.0f }, { "breatheGain", 50.0f },
            { "palette", 6.0f }, { "kaleido", 1.0f }, { "shape", 2.0f }, { "links", 25.0f },
            { "trails", 30.0f }, { "hueCycle", 6.0f }, { "pump", 45.0f } } },             // VAPORWAVE espejado: proyección rosa/cian que parpadea simétrica
        { "Helix", C::Production, {
            { "intensity", 45.0f }, { "chaos", 12.0f }, { "particleSize", 34.0f }, { "glow", 58.0f },
            { "curlScale", 35.0f }, { "homeStrength", 75.0f }, { "momentum", 35.0f },
            { "radialGain", 40.0f }, { "jitterGain", 18.0f }, { "breatheGain", 45.0f },
            { "figure", 5.0f }, { "shape", 3.0f }, { "links", 40.0f },
            { "trails", 15.0f }, { "rotate", 6.0f }, { "density", 55.0f } } },            // la HÉLICE: doble hebra con puentes (el ADN del cosmos)
        { "Canopy", C::Production, {
            { "intensity", 36.0f }, { "chaos", 18.0f }, { "particleSize", 26.0f }, { "glow", 62.0f },
            { "curlScale", 35.0f }, { "homeStrength", 30.0f }, { "momentum", 78.0f },
            { "radialGain", 28.0f }, { "jitterGain", 45.0f }, { "breatheGain", 55.0f },
            { "palette", 13.0f }, { "motion", 1.0f }, { "links", 12.0f },
            { "density", 65.0f }, { "scatter", 10.0f }, { "speed", 30.0f } } },           // luciérnagas FOREST: enjambre verde que respira en la espesura
        { "Relic", C::Production, {
            { "intensity", 38.0f }, { "chaos", 10.0f }, { "particleSize", 44.0f }, { "glow", 48.0f },
            { "curlScale", 30.0f }, { "homeStrength", 82.0f }, { "momentum", 28.0f },
            { "radialGain", 35.0f }, { "jitterGain", 12.0f }, { "breatheGain", 35.0f },
            { "palette", 11.0f }, { "shape", 5.0f }, { "trails", 25.0f },
            { "density", 40.0f } } },                                                     // mosaico SEPIA: teselas de un fresco antiguo que se re-arma
    };
    return presets;
}
}
