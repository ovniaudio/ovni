#pragma once
#include <juce_core/juce_core.h>

namespace ovni::dsp {

// Cargador de HRTF en formato SOFA (libmysofa). RAII: abre en open(), libera en el destructor.
//
// FORWARD-LOOKING: el motor de MOVIMIENTO base NO usa HRTF. Los módulos binaurales (ÓRBITA/POLVO)
// cargan sus HRIR por acá. La API "easy" de libmysofa ya resamplea al sample rate pedido, normaliza
// y arma un KD-tree de direcciones; getFilter() separa el ITD (delays) de la IR (base de fase mínima).
// La IR entra directo en un juce::dsp::FIR (forma directa = latencia 0).
//
// El header NO incluye <mysofa.h> (se mantiene opaco) -> los consumidores no necesitan los headers de
// libmysofa para compilar contra esta API.
class SofaLoader
{
public:
    SofaLoader() = default;
    ~SofaLoader();

    SofaLoader (const SofaLoader&)            = delete;
    SofaLoader& operator= (const SofaLoader&) = delete;
    SofaLoader (SofaLoader&&) noexcept;
    SofaLoader& operator= (SofaLoader&&) noexcept;

    // Abre y prepara un .sofa al sample rate dado. true si OK. Reabrir cierra el anterior.
    bool open (const juce::File& sofaFile, double sampleRate);

    bool isLoaded() const noexcept     { return handle != nullptr; }
    int  filterLength() const noexcept { return filterLen; }

    // HRIR (L,R) + ITD (delays en samples) para una dirección cartesiana NORMALIZADA
    // (x = frente, y = izquierda, z = arriba). irL/irR deben tener filterLength() floats.
    // false si no hay nada cargado o los punteros son nulos.
    bool getFilter (float x, float y, float z,
                    float* irL, float* irR, float& delayL, float& delayR) const noexcept;

    void close() noexcept;

private:
    void* handle    = nullptr;   // MYSOFA_EASY* (opaco)
    int   filterLen = 0;
};

} // namespace ovni::dsp
