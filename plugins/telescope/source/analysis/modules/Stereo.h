#pragma once
#include <array>
#include <vector>
#include "analysis/ScopeFrame.h"

// ========================================================================================================
// Stereo — imagen estéreo de BANDA ANCHA, en el dominio del tiempo (spec §5.3, prompts 49/51).
//
// Por HOP (100 ms) acumula en double las cinco sumas del hop y las guarda en un ring:
//     ΣLL   ΣRR   ΣLR   ΣMM   ΣSS       con  M = (L+R)/2 ,  S = (L-R)/2
// Los cuatro números publicados se arman sumando los últimos W/100 ms hops (ventana W = 100/300/1000 ms):
//
//     CORR       = ΣLR / √(ΣLL·ΣRR)                         +1 mono · 0 decorrelacionado · -1 fuera de fase
//     WIDTH      = √(ΣSS / ΣMM)                             0 mono · 1 independientes · ↑ el side manda
//     BAL_dB     = 10·log10(ΣRR / ΣLL)                      + = R más fuerte · - = L más fuerte
//     MONOLOSS   = 10·log10(ΣMM) - 10·log10((ΣLL+ΣRR)/2)    0 si L=R · -3.01 si independientes · -∞ si L=-R
//
// Es EXACTAMENTE la matemática de ~/PLUGINS/orbita/tests/StereoMeasure.cpp:74-80 (que a su vez viene de
// PULSAR), para que los números sean comparables entre plugins del sello. La única diferencia declarada:
// el balance va R sobre L (como el spec §5.3), mientras el de ÓRBITA imprime L sobre R — mismo número con
// el signo dado vuelta.
//
// CASOS BORDE (nunca NaN, nunca inf — la UI dibuja esto):
//   · sin señal (ΣLL+ΣRR ≈ 0) → todo en 0 y hasSignal=false ("sin señal", no "mono perfecto")
//   · ΣLL·ΣRR ≈ 0 con señal (un canal mudo) → corr = 0: no hay correlación DEFINIDA, no es que valga 0
//   · ΣMM ≈ 0 con señal (L = -R) → width al tope (10) y monoLoss al piso (-60 dB)
//   · balance clampeado a ±60 dB · monoLoss clampeado a -60 dB
//
// DETERMINISTA: `process` acepta cualquier n y arma los hops adentro, así los números no dependen del
// tamaño de bloque del host (lo verifica [stereo], mismo esquema que casa-4 del medidor de loudness).
// ========================================================================================================
namespace telescope
{
class Stereo
{
public:
    struct Result
    {
        float corr          = 0.0f;
        float width         = 0.0f;
        float balanceDb     = 0.0f;
        float monoLossDb    = 0.0f;
        float windowSeconds = 0.0f;   // la ventana EFECTIVA (los hops que realmente se sumaron)
        bool  hasSignal     = false;
    };

    // Las tres ventanas que ofrece la lente. 1 000 ms fija el tamaño del ring de hops.
    static constexpr int kWindowMsOptions[3] = { 100, 300, 1000 };
    static constexpr int kDefaultWindowMs    = 300;
    static constexpr int kMaxWindowHops      = 10;     // 1 000 ms / 100 ms

    static constexpr double kTinyEnergy = 1.0e-20;     // por debajo de esto no hay señal que medir
    static constexpr float  kWidthMax   = 10.0f;       // tope de width (L = -R daría infinito)
    static constexpr float  kDbFloor    = -60.0f;      // piso de balance y monoLoss
    static constexpr float  kDbCeil     =  60.0f;      // techo de balance

    void prepare (double sampleRate);
    void reset();

    // 100 / 300 / 1 000 ms. Cualquier otro valor se acota al rango y se redondea al hop.
    void setWindowMs (int ms) noexcept;
    int  windowMs() const noexcept { return windowHops * 100; }

    // Acepta CUALQUIER n: los hops se arman adentro (ver la nota de determinismo del encabezado).
    void process (const float* L, const float* R, int n);

    Result            result() const noexcept { return current; }
    const ScopeFrame& scope()  const noexcept { return currentScope; }

    int       hopSamples() const noexcept { return hop; }
    long long hopsDone()   const noexcept { return hopsCompleted; }

private:
    struct HopSums { double ll = 0.0, rr = 0.0, lr = 0.0, mm = 0.0, ss = 0.0; };

    void finishHop();
    void buildScope();

    // ===== 56 ===== la envolvente por dirección del HEMISFERIO. Se llena desde buildScope() con las
    // muestras CRUDAS del hop (hopL/hopR), no con los 2 048 decimados del goniómetro: la envolvente tiene
    // que ver TODAS las muestras o un transitorio angosto —justo el que interesa— se pierde en la
    // decimación. Ver la convención de ángulos en ScopeFrame.h.
    void buildHemisphere (ScopeFrame& f) const;

    double sr  = 48000.0;
    int    hop = 4800;                 // 100 ms
    int    oscWindow = 1920;           // 40 ms, acotado a ScopeFrame::kMaxOsc
    int    triggerWindow = 960;        // 20 ms, donde se busca el cruce ascendente

    HopSums acc;                       // el hop en curso
    int     fill = 0;

    std::vector<float> hopL, hopR;     // el hop crudo (lo necesita el ScopeFrame)

    std::array<HopSums, kMaxWindowHops> ring {};
    // Monotónico (el índice real es ringWrite % kMaxWindowHops). `long long` como hopsCompleted, que
    // crece exactamente al mismo ritmo: dos contadores del mismo evento con dos tipos distintos es una
    // invitación a que alguien los compare algún día (nit del revisor del 49).
    long long ringWrite = 0;
    int       windowHops = kDefaultWindowMs / 100;
    long long hopsCompleted = 0;

    Result     current;
    ScopeFrame currentScope;
};
}
