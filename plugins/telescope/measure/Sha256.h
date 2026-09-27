#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// ========================================================================================================
// Sha256 — FIPS 180-4, sin dependencias. telescope-measure identifica el archivo medido por su sha256 (nunca por
// su ruta: la ruta lleva el nombre del usuario, D-100). JUCE lo tiene en juce_cryptography, pero la herramienta
// linkea sólo juce_core, juce_audio_formats y juce_dsp: son cien líneas, y las prueba MEASURE[sha] contra los
// vectores de la norma ("", "abc" y el de 448 bits).
// ========================================================================================================
namespace telescope::measure
{
class Sha256
{
public:
    Sha256() { reset(); }

    void reset()
    {
        h = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
        used = 0;
        bits = 0;
    }

    void update (const void* data, size_t n)
    {
        auto p = static_cast<const std::uint8_t*> (data);
        bits += (std::uint64_t) n * 8u;
        while (n > 0)
        {
            const size_t take = std::min (n, (size_t) 64 - used);
            std::memcpy (buf.data() + used, p, take);
            used += take;
            p += take;
            n -= take;
            if (used == 64) { block (buf.data()); used = 0; }
        }
    }

    std::string hex()
    {
        const std::uint64_t total = bits;
        const std::uint8_t one = 0x80;
        update (&one, 1);
        const std::uint8_t zero = 0;
        while (used != 56) update (&zero, 1);
        std::uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = (std::uint8_t) (total >> (56 - 8 * i));
        update (len, 8);

        static const char* digits = "0123456789abcdef";
        std::string s;
        s.reserve (64);
        for (const auto w : h)
            for (int i = 28; i >= 0; i -= 4) s += digits[(w >> i) & 0xfu];
        return s;
    }

private:
    static std::uint32_t rotr (std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block (const std::uint8_t* p)
    {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u };

        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = ((std::uint32_t) p[4 * i] << 24) | ((std::uint32_t) p[4 * i + 1] << 16)
                 | ((std::uint32_t) p[4 * i + 2] << 8) | (std::uint32_t) p[4 * i + 3];
        for (int i = 16; i < 64; ++i)
        {
            const auto s0 = rotr (w[i - 15], 7) ^ rotr (w[i - 15], 18) ^ (w[i - 15] >> 3);
            const auto s1 = rotr (w[i - 2], 17) ^ rotr (w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i)
        {
            const auto S1 = rotr (e, 6) ^ rotr (e, 11) ^ rotr (e, 25);
            const auto ch = (e & f) ^ (~e & g);
            const auto t1 = hh + S1 + ch + k[i] + w[i];
            const auto S0 = rotr (a, 2) ^ rotr (a, 13) ^ rotr (a, 22);
            const auto mj = (a & b) ^ (a & c) ^ (b & c);
            const auto t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::array<std::uint32_t, 8> h {};
    std::array<std::uint8_t, 64> buf {};
    size_t used = 0;
    std::uint64_t bits = 0;
};
}
