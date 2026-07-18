#include "image/ImageField.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace supernova
{
namespace
{
inline int clampi (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 8SSEDT (8-point signed sequential euclidean distance transform), con offset al borde más cercano.
// 2 pasadas O(n); indistinguible del exacto para VFX (informe de investigación, algoritmo estándar).
struct Off { int16_t dx, dy; };
inline int off2 (Off o) { return (int) o.dx * o.dx + (int) o.dy * o.dy; }
}

ImageFieldResult ImageField::compute (const uint8_t* rgba, int imgW, int imgH, int gridW, int gridH)
{
    ImageFieldResult r;
    const size_t cells = (size_t) gridW * (size_t) gridH;
    r.weight.assign (cells, 1.0f);
    r.flowXY.assign (cells * 2, 0.0f);
    if (rgba == nullptr || imgW <= 0 || imgH <= 0 || gridW <= 0 || gridH <= 0)
        return r;   // neutro: fábrica/fallback siguen igual

    // 1) Luma muestreada a la grilla (mismo mapeo celda→píxel que uploadImage: centro de celda).
    std::vector<float> luma (cells);
    for (int gy = 0; gy < gridH; ++gy)
        for (int gx = 0; gx < gridW; ++gx)
        {
            const float u = (gx + 0.5f) / (float) gridW;
            const float v = (gy + 0.5f) / (float) gridH;
            const int ix = clampi ((int) (u * imgW), 0, imgW - 1);
            const int iy = clampi ((int) (v * imgH), 0, imgH - 1);
            luma[(size_t) gy * gridW + gx] = lumaOf (rgba + ((size_t) iy * imgW + ix) * 4);
        }

    // 2) Centroide de luminancia (γ=2: el brillo manda — el sujeto pesa más que un fondo gris).
    double sw = 0.0, sx = 0.0, sy = 0.0;
    for (int gy = 0; gy < gridH; ++gy)
        for (int gx = 0; gx < gridW; ++gx)
        {
            const float w = luma[(size_t) gy * gridW + gx];
            const double ww = (double) w * w;
            sw += ww;
            sx += ww * ((gx + 0.5) / gridW);
            sy += ww * ((gy + 0.5) / gridH);
        }
    if (sw > 1e-9)
    {
        r.centroidX = (float) (sx / sw);
        r.centroidY = (float) (sy / sw);
    }

    // 3) Pesos de saliencia: luma con raíz (levanta medios) y PISO 0.25 — el fondo oscuro acompaña, no muere.
    for (size_t i = 0; i < cells; ++i)
        r.weight[i] = 0.25f + 0.75f * std::sqrt (luma[i]);

    // 4) Flow: tangente a los bordes (⟂ del gradiente Sobel de luma), magnitud = fuerza del borde (saturada).
    //    Las partículas empujadas por esto RECORREN los contornos de la imagen (la firma "entiende la forma").
    std::vector<float> mags (cells, 0.0f);   // magnitud del gradiente (para el ETF de abajo)
    for (int gy = 0; gy < gridH; ++gy)
        for (int gx = 0; gx < gridW; ++gx)
        {
            auto L = [&] (int x, int y)
            { return luma[(size_t) clampi (y, 0, gridH - 1) * gridW + clampi (x, 0, gridW - 1)]; };

            const float gxv = (L (gx + 1, gy - 1) + 2.0f * L (gx + 1, gy) + L (gx + 1, gy + 1))
                            - (L (gx - 1, gy - 1) + 2.0f * L (gx - 1, gy) + L (gx - 1, gy + 1));
            const float gyv = (L (gx - 1, gy + 1) + 2.0f * L (gx, gy + 1) + L (gx + 1, gy + 1))
                            - (L (gx - 1, gy - 1) + 2.0f * L (gx, gy - 1) + L (gx + 1, gy - 1));

            const float mag = std::sqrt (gxv * gxv + gyv * gyv);
            const size_t c = (size_t) gy * gridW + gx;
            mags[c] = mag;
            if (mag > 1e-4f)
            {
                r.flowXY[c * 2 + 0] = -gyv / mag;   // tangente UNITARIA (la magnitud se re-aplica tras el ETF)
                r.flowXY[c * 2 + 1] =  gxv / mag;
            }
        }

    // 5) ETF — Edge Tangent Flow (Kang, Lee & Chui, "Coherent Line Drawing"): suavizado NO-LINEAL iterativo
    //    del campo tangente. Cada vector se promedia con sus vecinos ponderando por magnitud (wm: mandan los
    //    bordes fuertes), alineación (wd: |t·t'|, con signo φ para no cancelar opuestos) y cercanía (caja de
    //    radio R). Convierte el Sobel crudo (ruidoso, vectores que chocan) en PINCELADAS coherentes que siguen
    //    las formas — la diferencia entre "ruido con dirección" y movimiento natural.
    {
        constexpr int   kIters = 3, kR = 2;      // 3 pasadas, kernel 5×5 (radio 2 celdas de grilla)
        constexpr float kEta = 5.0f;             // pendiente del tanh de wm (Kang usa ~1; subimos el contraste)
        std::vector<float> cur = r.flowXY, next (cells * 2, 0.0f);

        for (int it = 0; it < kIters; ++it)
        {
            for (int gy = 0; gy < gridH; ++gy)
                for (int gx = 0; gx < gridW; ++gx)
                {
                    const size_t c = (size_t) gy * gridW + gx;
                    const float tx = cur[c * 2], ty = cur[c * 2 + 1];
                    float ax = 0.0f, ay = 0.0f;
                    for (int dy = -kR; dy <= kR; ++dy)
                        for (int dx = -kR; dx <= kR; ++dx)
                        {
                            const int nx = clampi (gx + dx, 0, gridW - 1);
                            const int ny = clampi (gy + dy, 0, gridH - 1);
                            const size_t n = (size_t) ny * gridW + nx;
                            const float sx = cur[n * 2], sy = cur[n * 2 + 1];
                            const float dot = tx * sx + ty * sy;
                            const float phi = (dot >= 0.0f) ? 1.0f : -1.0f;             // no cancelar opuestos
                            const float wd  = std::fabs (dot);                           // alineación
                            const float wm  = 0.5f * (1.0f + std::tanh (kEta * (mags[n] - mags[c])));  // mandan los fuertes
                            ax += phi * sx * wd * wm;
                            ay += phi * sy * wd * wm;
                        }
                    const float len = std::sqrt (ax * ax + ay * ay);
                    if (len > 1e-6f) { next[c * 2] = ax / len; next[c * 2 + 1] = ay / len; }
                    else             { next[c * 2] = tx;       next[c * 2 + 1] = ty;      }
                }
            std::swap (cur, next);
        }

        // Re-aplicar la fuerza del borde (saturada) sobre la dirección coherente. Difundimos la magnitud con
        // una caja 5×5 (max local suave) para que el flujo "abrace" los alrededores del borde, no solo la línea.
        for (int gy = 0; gy < gridH; ++gy)
            for (int gx = 0; gx < gridW; ++gx)
            {
                const size_t c = (size_t) gy * gridW + gx;
                float m = 0.0f;
                for (int dy = -kR; dy <= kR; ++dy)
                    for (int dx = -kR; dx <= kR; ++dx)
                        m = std::max (m, mags[(size_t) clampi (gy + dy, 0, gridH - 1) * gridW
                                             + clampi (gx + dx, 0, gridW - 1)]
                                          * (1.0f - 0.15f * (float) (std::abs (dx) + std::abs (dy))));
                const float k = std::min (1.0f, m * 1.5f);
                r.flowXY[c * 2 + 0] = cur[c * 2 + 0] * k;
                r.flowXY[c * 2 + 1] = cur[c * 2 + 1] * k;
            }
    }

    return r;
}

std::vector<float> ImageField::maskFromSaliency (const std::vector<float>& saliency01,
                                                 int gridW, int gridH, float maskPercentile)
{
    const size_t cells = (size_t) gridW * (size_t) gridH;
    if (saliency01.size() != cells || cells == 0)
        return {};

    std::vector<float> sorted (saliency01);
    const size_t nth = (size_t) ((float) (cells - 1)
                                 * (float) clampi ((int) (maskPercentile * 100.0f), 1, 99) / 100.0f);
    std::nth_element (sorted.begin(), sorted.begin() + (long) nth, sorted.end());
    const float thr = sorted[nth];
    if (thr < 1e-4f)
        return {};   // saliencia ~plana → sin sujeto discernible → sin cutout

    std::vector<float> mask (cells);
    const float lo = thr * 0.6f, hi = thr * 1.4f;
    for (size_t i = 0; i < cells; ++i)
    {
        const float t = (saliency01[i] - lo) / std::max (hi - lo, 1e-6f);
        const float s = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        mask[i] = s * s * (3.0f - 2.0f * s);   // smoothstep → bordes suaves
    }
    return mask;
}

std::vector<float> ImageField::maskFromFlatBackground (const uint8_t* rgba, int imgW, int imgH,
                                                       int gridW, int gridH)
{
    if (rgba == nullptr || imgW < 8 || imgH < 8 || gridW <= 0 || gridH <= 0)
        return {};

    // 1) Muestrear el MARCO exterior (grosor 2% del lado, mín 1 px) con paso fijo → color mediano del fondo.
    const int inset = std::max (1, std::min (imgW, imgH) / 50);
    std::vector<uint8_t> rs, gs, bs;
    auto sample = [&] (int x, int y)
    {
        const uint8_t* p = rgba + ((size_t) y * imgW + x) * 4;
        rs.push_back (p[0]); gs.push_back (p[1]); bs.push_back (p[2]);
    };
    const int step = std::max (1, imgW / 96);
    for (int x = 0; x < imgW; x += step) { sample (x, inset); sample (x, imgH - 1 - inset); }
    for (int y = 0; y < imgH; y += std::max (1, imgH / 96)) { sample (inset, y); sample (imgW - 1 - inset, y); }
    if (rs.size() < 16) return {};

    auto median = [] (std::vector<uint8_t>& v)
    {
        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        return (float) v[v.size() / 2];
    };
    const float bgR = median (rs), bgG = median (gs), bgB = median (bs);

    // 2) Confianza: ¿el marco ES uniforme? Fracción de muestras cerca del color mediano. <72% → no es un
    //    fondo plano (foto) → vacío, que decida Vision.
    size_t near = 0;
    for (size_t i = 0; i < rs.size(); ++i)
    {
        const float d = std::fabs (rs[i] - bgR) + std::fabs (gs[i] - bgG) + std::fabs (bs[i] - bgB);
        if (d < 90.0f) ++near;
    }
    if ((float) near / (float) rs.size() < 0.72f)
        return {};

    // 3) Máscara por celda de grilla: distancia de color al fondo, con rampa suave (sin recorte duro).
    std::vector<float> mask ((size_t) gridW * (size_t) gridH);
    size_t inCount = 0;
    for (int gy = 0; gy < gridH; ++gy)
        for (int gx = 0; gx < gridW; ++gx)
        {
            const int ix = clampi ((int) (((gx + 0.5f) / (float) gridW) * imgW), 0, imgW - 1);
            const int iy = clampi ((int) (((gy + 0.5f) / (float) gridH) * imgH), 0, imgH - 1);
            const uint8_t* p = rgba + ((size_t) iy * imgW + ix) * 4;
            const float d = std::fabs (p[0] - bgR) + std::fabs (p[1] - bgG) + std::fabs (p[2] - bgB);
            // rampa 40..140 (suma de |Δ| por canal, 0..765): tolera compresión JPEG y antialiasing del logo.
            const float m = std::min (1.0f, std::max (0.0f, (d - 40.0f) / 100.0f));
            mask[(size_t) gy * gridW + gx] = m;
            if (m > 0.5f) ++inCount;
        }

    // 4) Racionalidad: el sujeto debe ser 0.5%..85% del cuadro (si no, el "fondo plano" era la imagen entera
    //    o casi nada → vacío).
    const size_t cells = mask.size();
    if (inCount < cells / 200 || inCount > (size_t) ((float) cells * 0.85f))
        return {};
    return mask;
}

SubjectField ImageField::computeSubject (const std::vector<float>& saliency01,
                                         int gridW, int gridH, float maskPercentile)
{
    SubjectField s;
    const size_t cells = (size_t) gridW * (size_t) gridH;
    s.mask.assign (cells, 0);
    s.signedDist.assign (cells, 1.0f);      // neutro: "lejos del sujeto" en todas partes
    s.burstXY.assign (cells * 2, 0.0f);
    if (saliency01.size() != cells || gridW < 4 || gridH < 4)
        return s;

    // 1) Umbral por percentil de la saliencia (informe: 70–80). nth_element sobre una copia.
    std::vector<float> sorted (saliency01);
    const size_t nth = (size_t) ((float) (cells - 1) * clampi ((int) (maskPercentile * 100.0f), 1, 99) / 100.0f);
    std::nth_element (sorted.begin(), sorted.begin() + (long) nth, sorted.end());
    const float thr = std::max (sorted[nth], 1e-4f);

    size_t inCount = 0;
    for (size_t i = 0; i < cells; ++i)
        if (saliency01[i] > thr) { s.mask[i] = 1; ++inCount; }
    if (inCount < cells / 200 || inCount > cells - cells / 200)
        return s;   // máscara degenerada (saliencia plana) → campo neutro

    auto gid = [gridW] (int x, int y) { return (size_t) y * gridW + x; };

    // 2) Semillas = celdas de BORDE de la máscara (mask con vecino 4-conexo fuera).
    std::vector<Off> g (cells, Off { 0x3FFF, 0x3FFF });
    for (int y = 0; y < gridH; ++y)
        for (int x = 0; x < gridW; ++x)
        {
            if (! s.mask[gid (x, y)]) continue;
            const bool border = (x > 0 && ! s.mask[gid (x - 1, y)]) || (x < gridW - 1 && ! s.mask[gid (x + 1, y)])
                             || (y > 0 && ! s.mask[gid (x, y - 1)]) || (y < gridH - 1 && ! s.mask[gid (x, y + 1)]);
            if (border) g[gid (x, y)] = Off { 0, 0 };
        }

    // 3) Las 2 pasadas del 8SSEDT (relajación con offsets).
    auto relax = [&] (int x, int y, int ox, int oy)
    {
        const int xx = x + ox, yy = y + oy;
        if ((unsigned) xx >= (unsigned) gridW || (unsigned) yy >= (unsigned) gridH) return;
        Off c = g[gid (xx, yy)];
        c.dx = (int16_t) (c.dx + ox); c.dy = (int16_t) (c.dy + oy);
        if (off2 (c) < off2 (g[gid (x, y)])) g[gid (x, y)] = c;
    };
    for (int y = 0; y < gridH; ++y)
    {
        for (int x = 0; x < gridW; ++x)  { relax (x, y, -1, 0); relax (x, y, 0, -1); relax (x, y, -1, -1); relax (x, y, 1, -1); }
        for (int x = gridW - 1; x >= 0; --x) relax (x, y, 1, 0);
    }
    for (int y = gridH - 1; y >= 0; --y)
    {
        for (int x = gridW - 1; x >= 0; --x) { relax (x, y, 1, 0); relax (x, y, 0, 1); relax (x, y, 1, 1); relax (x, y, -1, 1); }
        for (int x = 0; x < gridW; ++x)      relax (x, y, -1, 0);
    }

    // 4) sd (normalizado por gridW; negativo adentro) + burstDir (= ∇sd = normal exterior en todas partes).
    for (int y = 0; y < gridH; ++y)
        for (int x = 0; x < gridW; ++x)
        {
            const size_t i = gid (x, y);
            const float dist = std::sqrt ((float) off2 (g[i]));
            const bool inside = s.mask[i] != 0;
            s.signedDist[i] = (inside ? -dist : dist) / (float) gridW;
            float dx = (float) g[i].dx, dy = (float) g[i].dy;
            const float len = std::sqrt (dx * dx + dy * dy);
            if (len > 1e-6f) { dx /= len; dy /= len; }
            else             { dx = 0.0f; dy = 0.0f; }
            // dirToEdge apunta HACIA el borde; adentro eso ya es la normal exterior; afuera se invierte.
            s.burstXY[i * 2 + 0] = inside ? dx : -dx;
            s.burstXY[i * 2 + 1] = inside ? dy : -dy;
        }
    return s;
}

std::vector<float> ImageField::computeDepth (const uint8_t* rgba, int imgW, int imgH,
                                             const std::vector<float>& mask01,
                                             const std::vector<float>& saliency01,
                                             int gridW, int gridH)
{
    const size_t cells = (size_t) gridW * (size_t) gridH;
    std::vector<float> depth (cells, 0.5f);
    if (gridW < 4 || gridH < 4) return depth;

    // Luma por celda (mismo mapeo celda→píxel que uploadImage). Sin imagen → 0.5 plano.
    std::vector<float> luma (cells, 0.5f);
    if (rgba != nullptr && imgW > 0 && imgH > 0)
        for (int gy = 0; gy < gridH; ++gy)
            for (int gx = 0; gx < gridW; ++gx)
            {
                const int ix = clampi ((int) ((gx + 0.5f) / gridW * imgW), 0, imgW - 1);
                const int iy = clampi ((int) ((gy + 0.5f) / gridH * imgH), 0, imgH - 1);
                luma[(size_t) gy * gridW + gx] = lumaOf (rgba + ((size_t) iy * imgW + ix) * 4);
            }

    const bool haveMask = (mask01.size() == cells);
    size_t inCount = 0;
    if (haveMask)
        for (size_t i = 0; i < cells; ++i) if (mask01[i] >= 0.5f) ++inCount;

    if (haveMask && inCount >= cells / 400 && inCount <= cells - cells / 400)
    {
        // ALMOHADA: distancia interior a la silueta (BFS multi-fuente desde el fondo, O(n)) → dome sqrt.
        constexpr float kInf = 1e9f;
        std::vector<float> dist (cells, kInf);
        std::vector<uint32_t> queue;
        queue.reserve (cells);
        for (size_t i = 0; i < cells; ++i)
            if (mask01[i] < 0.5f) { dist[i] = 0.0f; queue.push_back ((uint32_t) i); }
        for (size_t h = 0; h < queue.size(); ++h)
        {
            const uint32_t i = queue[h];
            const int x = (int) (i % (uint32_t) gridW), y = (int) (i / (uint32_t) gridW);
            const float nd = dist[i] + 1.0f;
            const int nx[4] = { x - 1, x + 1, x, x };
            const int ny[4] = { y, y, y - 1, y + 1 };
            for (int k = 0; k < 4; ++k)
            {
                if (nx[k] < 0 || nx[k] >= gridW || ny[k] < 0 || ny[k] >= gridH) continue;
                const size_t j = (size_t) ny[k] * gridW + nx[k];
                if (dist[j] > nd) { dist[j] = nd; queue.push_back ((uint32_t) j); }
            }
        }
        float dMax = 1.0f;
        for (size_t i = 0; i < cells; ++i)
            if (dist[i] < kInf && dist[i] > dMax) dMax = dist[i];

        for (size_t i = 0; i < cells; ++i)
        {
            if (mask01[i] >= 0.5f)
            {
                const float dome = std::sqrt (std::min (1.0f, dist[i] / dMax));   // perfil circular, sin meseta
                depth[i] = 0.5f + dome * 0.34f + (luma[i] - 0.5f) * 0.14f;        // forma + detalle por luz
            }
            else
                depth[i] = 0.38f + (luma[i] - 0.5f) * 0.10f;                       // losa del fondo, apenas atrás
        }
    }
    else
    {
        // MODO FOTO (sin máscara útil): relieve suave por luma + saliencia — lo claro/importante viene adelante.
        const bool haveSal = (saliency01.size() == cells);
        for (size_t i = 0; i < cells; ++i)
            depth[i] = 0.5f + (luma[i] - 0.5f) * 0.22f
                            + (haveSal ? (saliency01[i] - 0.5f) * 0.12f : 0.0f);
    }

    // Suavizado 3×3 (una pasada): ablanda el acantilado de la silueta y el ruido celda a celda.
    std::vector<float> soft (cells);
    for (int y = 0; y < gridH; ++y)
        for (int x = 0; x < gridW; ++x)
        {
            float sum = 0.0f; int n = 0;
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox)
                {
                    const int sx = x + ox, sy = y + oy;
                    if (sx < 0 || sx >= gridW || sy < 0 || sy >= gridH) continue;
                    sum += depth[(size_t) sy * gridW + sx]; ++n;
                }
            soft[(size_t) y * gridW + x] = std::min (1.0f, std::max (0.0f, sum / (float) n));
        }
    return soft;
}
}
