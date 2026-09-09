#pragma once

// ViewPhases — las fases ACUMULADAS del mundo: ROTATE (giro de vista), ORBIT (yaw auto-acumulado) y HUE CYC
// (deriva de tono). No son knobs: son estado que el renderer viene sumando desde que se abrió el editor.
//
// El export creaba un renderer FRESCO, que nace con las tres en cero: el MP4 salía FRONTAL y con el tono
// base mientras la ventana estaba inclinada por ORBIT y corrida por HUE CYC. Se leen en el message thread y
// viajan al hilo del export POR VALOR (tres floats, sin estado compartido).
namespace supernova
{
struct ViewPhases
{
    float rotate = 0.0f;   // ROTATE   — giro de vista acumulado (rad, envuelto)
    float orbit  = 0.0f;   // ORBIT    — yaw auto-acumulado (rad, envuelto); se suma al ROT Y del knob
    float hue    = 0.0f;   // HUE CYC  — deriva de tono acumulada (rad, envuelta)
};
}
