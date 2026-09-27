// telescope-measure-fixtures — escribe EL JUEGO FIJO de archivos con que se compara telescope-measure entre la
// Mac y Windows (prompt 101, hito 5).
//
//   telescope-measure-fixtures <carpeta>
//
// POR QUÉ UN GENERADOR PROPIO Y SÓLO CON ENTEROS. La comparación de mediciones entre dos máquinas vale sólo si
// las dos miden LOS MISMOS BYTES. Las distribuciones de <random> cambian entre libc++ y la STL de MSVC, sin() no
// da el mismo último bit en las dos libm, y clang en arm64 puede fusionar a·b + c en una FMA donde MSVC no. Por
// eso acá no hay un solo float en el camino de la señal:
//   · el ruido es un LCG de 32 bits de semilla fija (el de TestSignals.h: s·1664525 + 1013904223);
//   · el tono es un oscilador de "círculo mágico" en enteros de 64 bits, con su constante e = 2·sin(π·f/sr)·2³⁰
//     calculada una vez y escrita acá como número;
//   · las ganancias son multiplicaciones enteras y corrimientos (C++20 define el corrimiento de negativos);
//   · el WAV en float (el que pasa de 0 dBFS) divide un entero por una potencia de dos: exacto en IEEE.
// El log del CI imprime el sha256 de cada archivo: si difieren, la comparación de mediciones no vale y se dice.
//
// No usa JUCE: escribe el RIFF a mano, en little-endian, sin chunks opcionales.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct Lcg
{
    explicit Lcg (std::uint32_t seed) : s (seed) {}
    // Una muestra de ±2²³ (24 bits con signo), de los 24 bits altos del estado.
    std::int32_t next()
    {
        s = s * 1664525u + 1013904223u;
        return (std::int32_t) (s >> 8) - (1 << 23);
    }
    std::uint32_t s;
};

// Oscilador de círculo mágico: x' = x − (e·y >> 30); y' = y + (e·x' >> 30). Estable, entero, sin libm.
struct Osc
{
    Osc (std::int64_t eIn, std::int64_t amplitude) : e (eIn), x (0), y (amplitude) {}
    std::int32_t next()
    {
        x -= (e * y) >> 30;
        y += (e * x) >> 30;
        return (std::int32_t) x;
    }
    std::int64_t e, x, y;
};

// Una fuente llena `frame` canales con enteros de 24 bits (±2²³).
using Source = std::function<void (std::int64_t n, std::int32_t* frame)>;

struct Spec
{
    std::string name;
    int sr, channels, bits;   // bits: 16, 24 o 32 (32 = float)
    double seconds;
    std::function<Source()> make;
};

void put16 (std::vector<std::uint8_t>& b, std::uint32_t v) { b.push_back ((std::uint8_t) v); b.push_back ((std::uint8_t) (v >> 8)); }
void put32 (std::vector<std::uint8_t>& b, std::uint32_t v) { put16 (b, v & 0xffffu); put16 (b, v >> 16); }

bool writeWav (const std::string& path, const Spec& s)
{
    const auto frames = (std::int64_t) (s.seconds * s.sr + 0.5);
    const int bytesPer = s.bits / 8;
    const std::uint32_t dataBytes = (std::uint32_t) (frames * s.channels * bytesPer);

    std::vector<std::uint8_t> b;
    b.reserve (44 + dataBytes);
    b.insert (b.end(), { 'R', 'I', 'F', 'F' });
    put32 (b, 36 + dataBytes);
    b.insert (b.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    put32 (b, 16);
    put16 (b, s.bits == 32 ? 3u : 1u);   // 3 = IEEE float, 1 = PCM entero
    put16 (b, (std::uint32_t) s.channels);
    put32 (b, (std::uint32_t) s.sr);
    put32 (b, (std::uint32_t) (s.sr * s.channels * bytesPer));
    put16 (b, (std::uint32_t) (s.channels * bytesPer));
    put16 (b, (std::uint32_t) s.bits);
    b.insert (b.end(), { 'd', 'a', 't', 'a' });
    put32 (b, dataBytes);

    auto src = s.make();
    std::vector<std::int32_t> frame ((size_t) s.channels);
    for (std::int64_t n = 0; n < frames; ++n)
    {
        std::fill (frame.begin(), frame.end(), 0);
        src (n, frame.data());
        for (int c = 0; c < s.channels; ++c)
        {
            std::int32_t v = frame[(size_t) c];
            if (s.bits == 32)
            {
                // El entero de 24 bits sobre 2²² (una potencia de dos: exacto). Llega a ±2.0, o sea +6 dBFS.
                const float f = (float) v / 4194304.0f;
                std::uint32_t u;
                std::memcpy (&u, &f, 4);
                put32 (b, u);
                continue;
            }
            if (v > (1 << 23) - 1) v = (1 << 23) - 1;
            if (v < -(1 << 23))    v = -(1 << 23);
            if (s.bits == 16)
                put16 (b, (std::uint32_t) (std::uint16_t) (std::int16_t) (v >> 8));
            else
            {
                const auto u = (std::uint32_t) v;
                b.push_back ((std::uint8_t) u);
                b.push_back ((std::uint8_t) (u >> 8));
                b.push_back ((std::uint8_t) (u >> 16));
            }
        }
    }

    FILE* fp = std::fopen (path.c_str(), "wb");
    if (fp == nullptr) return false;
    const bool ok = std::fwrite (b.data(), 1, b.size(), fp) == b.size();
    return std::fclose (fp) == 0 && ok;
}

// ---- las fuentes ----
// Ganancia entera: v·g >> 15 (g = 32768 es 0 dB).
std::int32_t gain (std::int32_t v, std::int32_t g) { return (std::int32_t) (((std::int64_t) v * g) >> 15); }

Source noiseStereo (std::int32_t g)
{
    auto a = std::make_shared<Lcg> (0x13572468u);
    auto b = std::make_shared<Lcg> (0x2468ACE0u);
    return [=] (std::int64_t, std::int32_t* f) { f[0] = gain (a->next(), g); f[1] = gain (b->next(), g); };
}

Source toneMono (std::int64_t e, std::int64_t amp)
{
    auto o = std::make_shared<Osc> (e, amp);
    return [=] (std::int64_t, std::int32_t* f) { f[0] = o->next(); };
}

const std::vector<Spec>& specs()
{
    static const std::vector<Spec> s {
        { "f01-noise-48k-stereo.wav", 48000, 2, 24, 20.0, [] { return noiseStereo (8192); } },          // −12 dB
        { "f02-tone1k-44k-mono16.wav", 44100, 1, 16, 12.0,
          [] { return toneMono (152852926, 1 << 21); } },                                                 // −12 dBFS
        { "f03-tone100-noise-96k-stereo.wav", 96000, 2, 24, 8.0, []
          {
              auto o = std::make_shared<Osc> (7027611, 1 << 22);
              auto n = std::make_shared<Lcg> (0x0badf00du);
              return Source ([=] (std::int64_t, std::int32_t* f)
              {
                  const auto t = o->next();
                  f[0] = t + gain (n->next(), 1024);
                  f[1] = t - gain (n->next(), 1024);
              });
          } },
        { "f04-silence-48k-stereo.wav", 48000, 2, 24, 5.0,
          [] { return Source ([] (std::int64_t, std::int32_t* f) { f[0] = f[1] = 0; }); } },
        { "f05-short300ms-48k-stereo.wav", 48000, 2, 24, 0.3, [] { return noiseStereo (16384); } },
        { "f06-over0-float-48k-stereo.wav", 48000, 2, 32, 6.0, []
          {
              auto o = std::make_shared<Osc> (140031393, (1 << 23) - 1);   // ±2²³ / 2²² → pico ≈ +6 dBFS
              return Source ([=] (std::int64_t, std::int32_t* f) { f[0] = f[1] = o->next(); });
          } },
        { "f07-antiphase-44k-stereo.wav", 44100, 2, 24, 10.0, []
          {
              auto a = std::make_shared<Lcg> (0x13572468u);
              return Source ([=] (std::int64_t, std::int32_t* f) { f[0] = gain (a->next(), 8192); f[1] = -f[0]; });
          } },
        { "f08-quiet-48k-stereo.wav", 48000, 2, 24, 5.0, [] { return noiseStereo (4); } },               // ≈ −78 dB
        { "f09-six-channels-48k.wav", 48000, 6, 24, 6.0, []
          {
              auto a = std::make_shared<Lcg> (0x13572468u);
              auto b = std::make_shared<Lcg> (0x2468ACE0u);
              return Source ([=] (std::int64_t, std::int32_t* f)
              {
                  f[0] = gain (a->next(), 8192);
                  f[1] = gain (b->next(), 8192);
                  f[2] = f[0] / 2;   // los otros cuatro con algo, para que "se miden los dos primeros" importe
                  f[4] = f[1] / 2;
              });
          } },
        { "f10-dynamic-48k-stereo.wav", 48000, 2, 24, 60.0, []
          {
              // Ruido que cambia de nivel cada 5 s: el LRA tiene que dar distinto de cero.
              static const std::int32_t steps[12] = { 16384, 4096, 8192, 2048, 16384, 1024,
                                                      8192, 4096, 16384, 2048, 8192, 16384 };
              auto a = std::make_shared<Lcg> (0x13572468u);
              auto b = std::make_shared<Lcg> (0x2468ACE0u);
              return Source ([=] (std::int64_t n, std::int32_t* f)
              {
                  const auto g = steps[(n / (5 * 48000)) % 12];
                  f[0] = gain (a->next(), g);
                  f[1] = gain (b->next(), g);
              });
          } },
    };
    return s;
}
}

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "uso: telescope-measure-fixtures <carpeta>\n");
        return 2;
    }
    const std::string dir = argv[1];
    for (const auto& s : specs())
    {
        const auto path = dir + "/" + s.name;
        if (! writeWav (path, s))
        {
            std::fprintf (stderr, "no se pudo escribir %s\n", path.c_str());
            return 1;
        }
        std::printf ("%s\n", s.name.c_str());
    }
    return 0;
}
