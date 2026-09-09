#pragma once

// ExportOnsets — los golpes del clip exportado tienen que ser los de la música, ni uno más.
//
// Dos maneras en que el export inventaba golpes:
//   · El anillo de análisis corre a kAnalysisRingHz (30) y el clip a `fps` (60), así que cada frame del
//     anillo alimenta VARIOS cuadros del clip. El bool `onset` viajaba en todos: el motor recibía el mismo
//     kick dos cuadros seguidos y el pulso arrancaba clavado en 1,0 el doble de tiempo.
//   · Al terminar la ventana el clip vuelve al principio del anillo, y ahí el contador monotónico de onsets
//     BAJA. El detector de flanco del renderer lo leía como "pasó algo" → una explosión en cada vuelta.
//     Eso se arregló EN EL RENDERER (el flanco sólo cuenta cuando el contador sube), no acá.
//
// Puro, sin JUCE ni GPU: la regla se prueba sola (tests [exportonsets]).
namespace supernova
{
struct ExportOnsetState
{
    int lastIndex = -1;   // el frame de análisis que consumió el cuadro anterior del clip
};

struct ExportOnsetStep
{
    bool keepOnset = true;     // ¿este cuadro del clip es el PRIMERO que consume ese frame de análisis?
};

// La VUELTA del anillo no se avisa por acá: el renderer ya la cubre solo, porque su flanco sólo cuenta
// cuando el contador SUBE y al volver al principio de la ventana el contador baja. Hubo un campo
// `ringWrapped` que nadie en producción leía (sólo los tests) — señal calculada y nunca usada: fuera.
inline ExportOnsetStep exportOnsetStep (ExportOnsetState& st, int analysisIndex) noexcept
{
    const ExportOnsetStep out { analysisIndex != st.lastIndex };
    st.lastIndex = analysisIndex;
    return out;
}
}
