#include "engine/AuroraEngine.h"
#include <cmath>

namespace aurora
{

// =====================================================================================
// Constantes de la física del despliegue (ver header). Nombradas acá (no mágicas).
// =====================================================================================
namespace
{
    // COTA ANTI-AM CABLEADA (spec §3.2, curaduría): el ángulo por bin NO puede variar a
    // ≥ 20 Hz o el reparto de potencia se vuelve modulación de amplitud AUDIBLE (tremolo).
    // No es opción de usuario: es física. El rate efectivo (FREE o SYNC) se capa acá.
    constexpr float kAntiAmCapHz = 19.0f;

    // Rango log-f del despliegue: por debajo de kFLo todo es "graves" (u=0), por encima
    // de kFHi todo es "aire" (u=1). 40 Hz→16 kHz cubre ~8.6 octavas útiles.
    constexpr float kFLoHz = 40.0f;
    constexpr float kFHiHz = 16000.0f;

    // Serpenteo del abanico: cuántas alternancias L↔R hace el espectro al subir en
    // frecuencia (la "cinta" de la aurora). 2.75 ciclos ≈ una alternancia por ~3 octavas
    // (bandas vecinas NO se separan; el despliegue se LEE como posición, no como blur).
    constexpr float kWeaveCycles = 2.75f;

    // COTA DE PENDIENTE del serpenteo en el dominio de BINS (anti-espurias del OLA):
    // el log-f comprime ciclos del weave en los primeros bins → sin cota, bins graves
    // VECINOS caen a ángulos muy distintos y la curva de ganancia "salta" dentro del
    // skirt de leakage de un tono no alineado → el OLA reconstruye con espurias de
    // frame-rate (medido: −79 dBFS @110 Hz; ≥1 kHz el floor ya daba ≤ −117). Con la
    // fase capada por Hz los graves se reparten COHERENTES (vecinos casi al mismo
    // ángulo — además perceptualmente correcto abajo de ~1 kHz, §2.1) y el floor
    // queda bajo el gate H7 (< −96 dBFS) sin tocar el carácter del despliegue arriba.
    constexpr float kMaxWeavePhasePerHz = 0.06f / 23.4375f;   // rad/Hz (0.06 rad/bin @48k/2048)

    // MONO SAFE: corte de colapso-al-centro 50→400 Hz log (0 = sin red; default 50 ≈ 141 Hz
    // = sqrt(50·400); high 100 = 400 Hz). RANGO BAJADO del 60→700 viejo (2026-06-10): con el
    // ENSANCHADOR REAL de fase, una red hasta 205 Hz con transición de OCTAVA se tragaba la
    // masa de energía de un beat (kick+snare+bajo viven 80–300 Hz) y dejaba el goniómetro
    // MONO al default (medido: def55+ms50 CORR 0.97; def55+ms0 CORR 0.83). Ahora el default
    // protege sólo el SUB/kick (<~140 Hz, mono-compatible de verdad) y deja abrir los low-mids;
    // Mono Safe ALTO sube el corte a 400 Hz para material con bajo-medio fuerte. El kick/sub
    // quedan al centro en TODO el rango útil (RealWorldTest mide <80 Hz dentro de 1 dB del mono).
    constexpr float kMonoSafeLoHz = 50.0f;
    constexpr float kMonoSafeHiHz = 400.0f;

    // ANCHO DE TRANSICIÓN del Mono Safe (octavas sobre el corte hasta protección plena → arriba
    // = sin red). MEDIO de octava (0.5) era demasiado: con el rango viejo la rampa cubría 2
    // octavas. Un CUARTO de octava deja la red angosta — el sub colapsa, los low-mids justo
    // arriba del corte abren — sin escalón espectral audible (el solape 75 % del OLA lo suaviza).
    constexpr float kMonoSafeTransOct = 0.35f;

    // DUCK: ballistics del envelope-follower del dry + referencia de normalización.
    // Una pegada a −12 dBFS (0.25) cierra el abanico del todo con DUCK=100.
    constexpr float kDuckAttackS  = 0.005f;
    constexpr float kDuckReleaseS = 0.150f;
    constexpr float kDuckRefAmp   = 0.25f;

    // Drive del TILT positivo (RE-CURVA del QA fino, 2026-06-10): además de bajar el
    // knee (abrir antes en frecuencia), el lado positivo EMPUJA el despliegue hacia los
    // bordes (θ se clampea a ±1: los bins cerca de los antinodos saturan a hard-pan).
    // Sin esto el último cuarto (+50→+100) era zona muerta MEDIDA (~0.4 dB: lo único
    // nuevo que abría el knee eran graves con el weave capado = inaudible).
    // BAND-LIMITADO (anti-espurias): el drive entra en rampa log 350 Hz→1.4 kHz y es
    // CERO abajo — en los bins graves la pendiente de ganancia queda EXACTAMENTE la del
    // cap kMaxWeavePhasePerHz (un drive full-range ahí subía el floor del gate H7 a
    // −90 dBFS, medido; band-limitado el floor vuelve al de la curva base).
    constexpr float kTiltDrive     = 0.5f;
    constexpr float kTiltDriveLoHz = 350.0f;
    constexpr float kTiltDriveHiHz = 1400.0f;

    // ════════════════════════════════════════════════════════════════════════════════════
    // DRIVE DE EXCURSIÓN — la causa raíz de "AURORA no hacía nada" (medido 2026-06-10).
    // ════════════════════════════════════════════════════════════════════════════════════
    // El ángulo por bin valía θ = γ · e · weave, con γ = spread01 ∈ [0,1]. El TECHO de
    // excursión (γ máx 1.0 × la envolvente que amarra graves) daba CORR ≈ 0.66 a SPREAD 100
    // y CORR +0.92 (casi mono) al default 55 → INAUDIBLE. El fix sube la excursión efectiva
    // empujando MÁS bins hacia el hard-pan, SIN tocar la fase ni el reparto de potencia (el
    // mono-recovery por IN PHASE / Mono Safe queda intacto: siguen multiplicando θ ANTES del
    // reparto). Honestidad: el control sigue haciendo lo que dice, sólo con rango audible.
    //
    // BAND-LIMITADO Y AL CUADRADO (anti-alias, gate H7): un drive lineal full-range subía el
    // floor de espurias del OLA a −89 dBFS en la zona 350–700 Hz (donde el log-f cambia rápido
    // entre bins vecinos → la curva de ganancia "salta" dentro del skirt de leakage). Con la
    // rampa AL CUADRADO el knee del drive sube a ~700 Hz+, donde el log ya se aplanó y los bins
    // vecinos son coherentes → floor recuperado a −103 dBFS. Es la MISMA disciplina del
    // kTiltDrive (band-limitado), pero a nivel global del despliegue. La excursión la sube el
    // drive; la PROPORCIONALIDAD del knob la conserva γ lineal en spread (sin re-curva): así
    // la SPREAD honesta exige ≥10%/paso y el último cuarto NO se aplana por saturación.
    // NIVEL del drive (medido 2026-06-10): 0.9 deja el DEFAULT (SPREAD 55) claramente abierto
    // (banda ancha CORR +0.92→+0.61, WIDTH 0.34→0.63) y SPREAD 100 dramático (CORR +0.62→+0.41,
    // WIDTH 0.78), comparable/mejor que DUST — SIN tocar la PROPORCIONALIDAD del knob (γ sigue
    // LINEAL en spread → SPREAD honesta ≥10%/paso, incluido el último cuarto). Subirlo más
    // empezaba a SATURAR el softLimitTheta arriba (75→100 caía a +8% → fallaba [honestidad]):
    // la audibilidad la da el drive, la honestidad la da γ lineal. NO re-curvar γ.
    constexpr float kSpreadDrive   = 0.35f;

    // ════════════════════════════════════════════════════════════════════════════════════
    // ENSANCHADOR REAL — DECORRELACIÓN DE FASE por bin (el fix audible 2026-06-10).
    // ════════════════════════════════════════════════════════════════════════════════════
    // El paneo SOLO de magnitud (kSpreadDrive arriba) era INAUDIBLE sobre música real
    // concentrada: un único espectro coherente X[k] escalado por dos ganancias REALES sigue
    // 100 % correlacionado L/R (CORR ~0.96 en el goniómetro = línea vertical), por más que
    // muevas SPREAD. La ÚNICA forma de bajar CORR de verdad sobre material concentrado es
    // introducir DIFERENCIAS DE FASE inter-canal — el patrón que HORIZON (su spread) y PULSAR
    // (HRIR + M/S) ya usan para "abrir en serio".
    //
    // φ_off[k] = γ · spread · kSpreadPhaseMaxScale · π · decorr[k], aplicado OPUESTO en L/R:
    //   L *= e^(−jφ_off) ;  R *= e^(+jφ_off).  decorr[k] = random fijo ∈ [−1,1] (≠ offset
    // constante: un π igual en todos los bins re-colapsaría a mono; random por bin → cada bin
    // sale a un ángulo distinto → CORR cae MONÓTONA y nunca se re-mono-fica). DC y Nyquist = 0
    // (deben quedar reales tras el IFFT). MAGNITUD por bin INTACTA (|L|=|R|·ratio del reparto):
    // no toca el balance de energía ni el alias floor (el OLA reconstruye igual; sólo cambia la
    // fase relativa, que el solape 75 % crossfadea suave entre frames).
    //
    // NIVEL (kSpreadPhaseMaxScale, calibrado sobre el BEAT REAL realbeat_90.wav, NO ruido rosa):
    // 0.62 deja el DEFAULT (Spread 55) claramente abierto en el goniómetro (CORR 0.96→~0.74) y
    // SPREAD 100 dramático (CORR→~0.45, WIDTH alto), comparable a PULSAR/HORIZON, sin que la
    // WIDTH se dé vuelta cerca del tope (deshonesto) ni que el true-peak pase 0.85 (el limiter
    // del sello contiene el residuo). MONO SAFE escala el offset a 0 bajo el corte (kick/bajo
    // al centro) y IN PHASE lo apaga del todo (suma mono ~0 dB = escape mono-safe).
    constexpr float kSpreadPhaseMaxScale = 0.62f;   // ±0.62π por bin a spread·γ=1 (abre fuerte, WIDTH monótona, margen TP)

    // Constante del cap de pendiente del camino de decorrelación (group delay ~constante en Hz).
    // Mayor = la curva persigue su destino random más rápido (más decorrelación pero más group
    // delay → más espurias del OLA). Calibrado sobre [alias] (piso quieto < −96 dBFS) + [realbeat]
    // (spread100 → CORR ≤0.5): el cap por bin = kDecorrGroupDelayK·binHz/f mantiene el group delay
    // bajo control en TODA la banda (graves = pendiente diminuta = OLA limpio; agudos = libre).
    constexpr float kDecorrGroupDelayK = 0.9f;

    // Rampa de AMPLITUD de la decorrelación en graves: 0 bajo kDecorrLoHz (el skirt de leakage
    // de un tono grave es angualtísimo — 2-3 bins — y CUALQUIER offset ahí rompe el OLA: alias
    // 111 Hz medido a −72/−82 dBFS), sube a 1 en kDecorrFullHz. El kick/sub queda al centro por
    // construcción (extra al knob Mono Safe) y el piso de alias vuelve bajo el gate H7 (< −96).
    // La masa del beat (snare/bajo/cuerpo) vive 200 Hz+ → ahí abre fuerte igual.
    constexpr float kDecorrLoHz   = 350.0f;
    constexpr float kDecorrFullHz = 800.0f;

    // ENSANCHADOR DE LOW-MIDS (post-STFT, dominio del tiempo, OLA-inmune — ver header). El
    // STFT sin zero-pad no puede decorrelar el cuerpo del beat (80–350 Hz) sin time-alias, así
    // que ahí abre este: réplica del mid por FIR disperso inyectada como side puro (mono-safe).
    // kDecorWidthDepth = side máx a SPREAD 100 (calibrado sobre el BEAT REAL: spread100 → CORR
    // ≤0.5, default 55 → ≤0.85 — sin que WIDTH se dé vuelta ni el true-peak pase 0.85, lo
    // contiene el limiter del sello). kDecorHpFloorHz = corner del HP de 4 polos del side aunque
    // Mono Safe = 0 (el kick/sub <~200 Hz SIEMPRE al centro: compatibilidad club/vinilo, física
    // no-opcional; sube con el knob Mono Safe). El HP corre por RESTA (dec − LP4) → necesita el
    // corner alto para que la LP cubra el grave y la resta lo deje ~0 en el side.
    constexpr float kDecorWidthDepth = 0.85f;
    constexpr float kDecorHpFloorHz  = 200.0f;
    // Geometría del FIR disperso: ventana de difusión (ms) y número de taps. ~22 ms / 24 taps
    // da una réplica densa y difusa (no eco) sin colorear demasiado. Más larga = más latencia
    // perceptual de la réplica (no del plugin: es un FIR sumado, latencia declarada NO cambia).
    constexpr float kDecorFirMs   = 22.0f;
    constexpr int   kDecorFirTaps = 24;
    // Factor de fase del lock de SYNC para el ensanchador FIR (calibrado a ~0° sobre
    // SyncPhaseTest, [diccionario]). NEGATIVO: el ensanchador lee γ por BLOQUE (último frame del
    // bloque) y rampa la profundidad por-sample → la envolvente de POTENCIA del side ADELANTA
    // respecto al centroide de retardo del FIR (no es un delay puro). El producto
    // kDecorDelayCompFrac·decorMeanDelaySamples lleva el abanico OÍDO al beat (±5°).
    constexpr double kDecorDelayCompFrac = -2.0;

    // ════════════════════════════════════════════════════════════════════════════════════
    // MOVIMIENTO ESPECTRAL MONO-AUDIBLE — el fix de "no se oye en mono" (2026-06-13).
    // ════════════════════════════════════════════════════════════════════════════════════
    // TODO lo de arriba (paneo + decorrelación de fase + FIR disperso) vive en el SIDE:
    // L+R = 2·mid EXACTO → en mono se cancela y el TIMBRE del centro nunca cambia → "no hace
    // nada" (Joaquín, por oído, 2026-06-13). Pero AURORA *ES* movimiento espectral: acá lo
    // PROYECTAMOS a la SUMA MONO con un PEINE que BARRE el espectro (barber-pole), aplicado
    // IGUAL en L y R. Mono-compatible POR CONSTRUCCIÓN: una ganancia idéntica en ambos canales
    // no cambia |L|/|R| relativos → CORR/WIDTH intactos (los tests [audible] de ancho siguen
    // verdes) y la compat-mono no se rompe → IN PHASE lo CONSERVA (la aurora se sigue oyendo
    // en mono-safe, que es justo el punto: el carácter no vive en el side). La "cinta" ondula
    // y se OYE aunque sumes a mono.
    //   gMono[k] = clamp( 1 + depth · sin(2π·kSpecModCycles·u[k] − φmotion) )
    //   depth    = MOTION · SPREAD · kSpecModDepth · binDriveRamp[k]
    // · Lo MANEJA el MOTION (no γ): MOTION 0 → depth 0 → peine APAGADO (el piso de alias QUIETO
    //   queda intacto y la honestidad del MOTION sube monótona desde 0); MOTION arriba → la
    //   aurora ondula (el "abanico que se abre/cierra" AHORA también se oye en mono). El default
    //   de MOTION es >0 → "carga sonando" (curaduría). SPREAD escala la profundidad del barrido.
    // · binDriveRamp[k] (0 bajo 350 Hz → 1 sobre 1.4 kHz): MISMA disciplina anti-espurias del
    //   kTiltDrive — el peine vive donde el log-f ya se aplanó (bins vecinos coherentes → OLA
    //   limpio) y los graves quedan SÓLIDOS (sin wobble de sub/kick).
    // · φmotion = la fase del MOTION (rate, SYNC-able). El barrido va al rate del MOTION → su AM
    //   se declara como MOTION_SIDEBAND (peaje honesto del [alias]), NO como piso de alias.
    // clamp inferior kSpecModFloor (no nulls totales → sin huecos antinaturales ni flip de fase).
    constexpr float kSpecModDepth  = 0.4f;   // escala de profundidad del peine (× MOTION·SPREAD·ramp)
    constexpr float kSpecModCycles = 3.0f;   // picos/notches a lo largo del espectro log-f
    constexpr float kSpecModFloor  = 0.2f;   // piso de gMono (sin nulls totales)

    // Envolvente de apertura e(x, s): cuánto se va AL BORDE cada posición espectral x
    // (0=graves, 1=aire) con fuerza s (0..1). knee = dónde llega a apertura plena;
    // s sube → el knee BAJA en frecuencia y la curva se hace más convexa (abre antes).
    // A s=0 es la curva base "graves centro / agudos bordes" (tilt 0).
    inline float openShape (float x, float s) noexcept
    {
        const float knee = 0.55f - 0.45f * s;
        const float v    = juce::jlimit (0.0f, 1.0f, x / knee);
        return std::pow (v, 1.0f - 0.4f * s);
    }

    inline float clamp01 (float v) noexcept { return juce::jlimit (0.0f, 1.0f, v); }

    // Saturador SUAVE del ángulo θ (C1): identidad hasta 0.75, codo tanh asintótico a ±1.
    // El clamp DURO creaba ESQUINAS en la curva θ(bin) cuando el drive cruzaba ±1 a través
    // del skirt de leakage de un tono → espurias medidas a −90 dBFS (gate H7). El codo suave
    // elimina los corners (la curva de ganancia queda C1) y conserva el empuje a los bordes:
    // θ=1.0 → 0.94 · θ=1.5 → 0.999 (hard-pan efectivo). El knee 0.75 también mantiene el
    // floor de alias del OLA holgado bajo el gate (subirlo lo acercaba a −96, medido).
    inline float softLimitTheta (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 0.75f) return x;
        const float y = 0.75f + 0.25f * std::tanh ((a - 0.75f) / 0.25f);
        return x < 0.0f ? -y : y;
    }
}

// ------------------------------------------------------------------------------ prepare
void AuroraEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sr       = spec.sampleRate;
    maxBlock = (int) spec.maximumBlockSize;

    // N por sample-rate (curaduría): 2048 @44.1/48k; a 96k se duplica para mantener
    // ~el mismo TIEMPO de frame (~43 ms) → misma resolución espectral percibida.
    const int fftN = (sr >= 88200.0) ? 4096 : 2048;

    // 2 canales con el MISMO mid: cada frame ve el espectro X[k] duplicado y el
    // FrameProcessor lo reparte L/R (ver header). El FrameProcessor se setea ACÁ
    // (setup-time: asignar un std::function puede alocar — nunca en el audio thread).
    stft.prepare (sr, maxBlock, 2, fftN);
    stft.setFrameProcessor ([this] (const ovni::engines::StftEngine::FrameView& f) { processFrame (f); });

    limiter.setTuning ({ 0.85f, 60.0f });   // techo del sello (margen true-peak ~−1.4 dB)
    limiter.prepare (sr);

    // Dry delay = latencia del STFT (alineación dry↔wet del MIX, cero comb).
    dryRingSz = stft.latencySamples() + maxBlock;
    for (auto& ring : dryRing) { ring.assign ((size_t) dryRingSz, 0.0f); }
    dryWrite = 0;

    dryScratch.setSize (2, maxBlock);
    dryScratch.clear();

    // Tablas por bin (el frame sólo las LEE; nada se computa con pow/log en el hot path
    // que pueda precalcularse acá).
    const int nb = stft.numBins();
    binU.resize ((size_t) nb);
    binWeave.resize ((size_t) nb);
    binLog2F.resize ((size_t) nb);
    binDriveRamp.resize ((size_t) nb);
    binSpreadDrive.resize ((size_t) nb);
    binDecorr.resize ((size_t) nb);
    binVizBand.resize ((size_t) nb);
    const float  logSpan      = std::log2 (kFHiHz / kFLoHz);
    const double binHz        = sr / (double) stft.fftSize();
    const double maxPsiPerBin = (double) kMaxWeavePhasePerHz * binHz;   // cota de pendiente (rad/bin)
    double psi = 0.0;   // fase ACUMULADA del weave: sigue a 2π·cycles·u pero capada por bin
    for (int k = 0; k < nb; ++k)
    {
        const float fk = juce::jmax (1.0f, (float) (k * binHz));
        const float u  = clamp01 (std::log2 (fk / kFLoHz) / logSpan);
        binU[(size_t) k]     = u;

        // Fase del serpenteo BANDLIMITADA en bins (ver kMaxWeavePhasePerHz): monotónica,
        // persigue el objetivo log-f y lo alcanza donde el log se aplana (medios/agudos).
        const double psiTarget = juce::MathConstants<double>::twoPi * (double) kWeaveCycles * (double) u;
        psi = juce::jmin (psiTarget, psi + maxPsiPerBin);
        // −sin → la octava más alta del serpenteo cae al borde DERECHO (convención
        // pantalla-derecha = canal-derecho). En el bin 0 vale 0 → el DC queda SIEMPRE al centro.
        binWeave[(size_t) k] = -(float) std::sin (psi);

        binLog2F[(size_t) k] = std::log2 (fk);
        binVizBand[(size_t) k] = juce::jlimit (0, kVizBands - 1, (int) (u * (float) kVizBands));

        // Rampa del drive del TILT positivo (0 bajo 350 Hz → 1 sobre 1.4 kHz, log).
        const float ramp = clamp01 (std::log2 (fk / kTiltDriveLoHz)
                                    / std::log2 (kTiltDriveHiHz / kTiltDriveLoHz));
        binDriveRamp[(size_t) k] = ramp;

        // Drive de EXCURSIÓN (causa raíz del fix audible): band-limitado AL CUADRADO. El
        // cuadrado empuja el knee efectivo a ~700 Hz+ (los graves <~500 Hz quedan casi sin
        // drive → el floor de alias del OLA, gate H7, no sube; medido). Multiplica la
        // envolvente e(u) en el frame → más bins llegan al hard-pan → CORR cae de verdad.
        binSpreadDrive[(size_t) k] = 1.0f + kSpreadDrive * ramp * ramp;
    }

    // ENSANCHADOR REAL: curva de offset de fase pseudo-random SUAVE EN FRECUENCIA (semilla
    // fija → reproducible). DOS requisitos en tensión, resueltos por una curva suave:
    //   · DECORRELACIÓN: bins LEJANOS deben caer a ángulos DISTINTOS (→ CORR L/R baja sobre
    //     material de banda ancha y la WIDTH sube monótona con spread).
    //   · OLA LIMPIO (gate alias H7 < −96 dBFS): bins VECINOS deben caer a ángulos PARECIDOS.
    //     Un random POR BIN rompía el OLA: el skirt de leakage de un tono recibía fases muy
    //     distintas entre bins contiguos → la reconstrucción Hann² dejaba espurias a −46 dBFS
    //     (medido). La curva suave mantiene el skirt coherente → el tono reconstruye limpio.
    // Se genera ruido en NODOS log-f gruesos (cada ~kDecorrNodeOct octavas) y se INTERPOLA
    // (smoothstep) entre nodos. Definida sobre FRECUENCIA (no índice de bin) → idéntica a
    // 44.1/48/96k ([consistency]). DC/Nyquist = 0 (deben quedar reales tras el IFFT).
    // El offset es un ÁNGULO ABSOLUTO por bin (binDecorr·spreadPhaseMax). Para que el OLA
    // reconstruya LIMPIO (sin zero-pad: N análisis = N síntesis) el RETARDO DE GRUPO efectivo
    // (la derivada de la fase respecto a ω) debe quedar ≪ que la ventana → kernel corto, sin
    // time-aliasing. Construimos binDecorr como un camino aleatorio SUAVE con la PENDIENTE
    // CAPADA por Hz (igual disciplina que kMaxWeavePhasePerHz para el weave): random de destino
    // por nodo log-f, pero la curva sólo puede MOVERSE kMaxDecorrSlopePerBin por bin → group
    // delay acotado → espurias del OLA bajo el gate H7 (< −96 dBFS). La aleatoriedad sigue
    // decorrelando (bins lejanos caen a ángulos muy distintos) pero los VECINOS son coherentes.
    {
        juce::Random decorrRng (0x41524E44);   // "ARND" (AURORA random) — semilla fija
        // Pendiente máx del camino por bin: φ·spreadPhaseMax es el ángulo; capamos el cambio de
        // binDecorr (normalizado) a ~0.02/bin → con spreadPhaseMax≈π·0.62 el group delay queda
        // ~N/50 ≪ N (medido: alias < −96 dBFS). Más pendiente abría más pero ensuciaba el OLA.
        const float logSpanDc = std::log2 (kFHiHz / kFLoHz);
        float cur = 0.0f;
        float target = decorrRng.nextFloat() * 2.0f - 1.0f;
        float nextNodeU = 0.0f;
        for (int k = 0; k < nb; ++k)
        {
            if (k == 0 || k == nb - 1) { binDecorr[(size_t) k] = 0.0f; continue; }
            const float fk = juce::jmax (1.0f, (float) (k * binHz));
            const float uu = clamp01 (std::log2 (fk / kFLoHz) / logSpanDc);
            // PENDIENTE MÁX por bin = group delay ~CONSTANTE: en log-f la densidad de bins por
            // octava sube con f, así que para mantener el MISMO group delay (kAuroraDecorrGroupDelayFrac
            // de la ventana) la pendiente POR BIN debe escalar con 1/k (∝ binHz/fk). En graves
            // (skirt angosto, pocos bins/oct) la curva se mueve MUY lento → OLA limpio; en agudos
            // se mueve más → decorrela fuerte donde el oído ubica el ancho. Sin time-aliasing.
            const float slopeCap = juce::jlimit (0.004f, 0.5f, kDecorrGroupDelayK * (float) (binHz) / fk);
            if (uu >= nextNodeU) { target = decorrRng.nextFloat() * 2.0f - 1.0f; nextNodeU += (0.25f / logSpanDc); }
            cur += juce::jlimit (-slopeCap, slopeCap, target - cur);
            // TAPER de GRAVES de la decorrelación (anti-alias del OLA + bass más centrado): la
            // AMPLITUD del offset sube de 0 (en kFLoHz) a 1 (en kDecorrFullHz) — abajo el skirt
            // es angosto y un offset grande mete espurias (alias 111 Hz medido a −72 dBFS); con
            // el taper el grave se decorrela suave (sin escalón) y el piso vuelve bajo el gate.
            const float lfTaper = clamp01 (std::log2 (fk / kDecorrLoHz) / std::log2 (kDecorrFullHz / kDecorrLoHz));
            binDecorr[(size_t) k] = cur * lfTaper * lfTaper;   // ² → arranque MUY suave en graves (skirt angosto), full en medios
        }
    }

    // Ballistics del duck + coeficientes de suavizado por frame (post-mapeo).
    envAtkCoef = 1.0f - std::exp (-1.0f / (kDuckAttackS  * (float) sr));
    envRelCoef = 1.0f - std::exp (-1.0f / (kDuckReleaseS * (float) sr));
    const float hopDur = (float) stft.hopSize() / (float) sr;
    smoothCoef = 1.0f - std::exp (-hopDur / 0.030f);   // macros: τ ≈ 30 ms
    gammaCoef  = 1.0f - std::exp (-hopDur / 0.008f);   // γ: τ ≈ 8 ms (sigue el duck sin zipper)

    // Retardo de grupo del one-pole de γ a baja frecuencia (DT, por frame): hop·(1−g)/g.
    // El lock de fase del SYNC lo compensa para que el abanico OÍDO caiga en el beat.
    gammaLagSamples = (double) stft.hopSize() * (1.0 - (double) gammaCoef) / (double) gammaCoef;

    // ENSANCHADOR DE LOW-MIDS: FIR DISPERSO ("velvet") sembrado con semilla fija. Taps en
    // posiciones pseudo-random dentro de una ventana de ~kDecorFirMs ms, signo random, amplitud
    // que decae lineal con el retardo (réplica DIFUSA del mid, no eco discreto). FIR puro →
    // estable y sin espurias (gate [alias] limpio; un allpass IIR de Schroeder daba espurias
    // medidas sobre tono puro). El tap 0 (delay 0, signo +, peso mayor) deja la réplica
    // correlacionada de arranque y la difunde → a baja profundidad abre suave. Normalizado a
    // Σ|gain| = 1 (la réplica no sube de nivel respecto al mid). Escalado con el sample-rate.
    {
        const int   maxDelay = juce::jmax (1, juce::roundToInt (kDecorFirMs * 0.001f * (float) sr));
        const int   nTaps    = kDecorFirTaps;
        decorFir.prepareLine (maxDelay);
        decorFir.tapPos.resize ((size_t) nTaps);
        decorFir.tapGain.resize ((size_t) nTaps);
        juce::Random firRng (0x41464952);   // "AFIR" — semilla fija (reproducible)
        // NINGÚN tap domina (tap 0 NO especial): la réplica es DIFUSA y DECORRELADA del mid (no
        // "el mid otra vez con un poco de cola"). Posiciones repartidas en la ventana con jitter,
        // signo random, amplitud que decae suave. Normalizada por POTENCIA (Σg² = 1) → energía
        // ~igual al mid sin que un solo tap re-correlacione. Así el side inyectado abre de verdad.
        float gainSq = 0.0f;
        for (int t = 0; t < nTaps; ++t)
        {
            const int pos = juce::jlimit (1, maxDelay,
                              juce::roundToInt (((float) t + firRng.nextFloat()) / (float) nTaps * (float) maxDelay));
            const float decay = 1.0f - 0.6f * ((float) pos / (float) maxDelay);   // decae suave con el retardo
            const float sign  = firRng.nextBool() ? 1.0f : -1.0f;
            decorFir.tapPos [(size_t) t] = pos;
            decorFir.tapGain[(size_t) t] = sign * decay;
            gainSq += decorFir.tapGain[(size_t) t] * decorFir.tapGain[(size_t) t];
        }
        const float norm = 1.0f / std::sqrt (juce::jmax (1.0e-6f, gainSq));
        for (auto& g : decorFir.tapGain) g *= norm;

        // Retardo de grupo MEDIO del FIR (centroide ponderado por |gain|²): el side inyectado
        // sale ~este retardo después del mid → el lock de SYNC lo compensa para que el MOTION
        // (que ahora late vía la profundidad del FIR) caiga EN el beat ([diccionario] phase ±5°).
        double wsum = 0.0, dsum = 0.0;
        for (int t = 0; t < nTaps; ++t)
        {
            const double wgt = (double) decorFir.tapGain[(size_t) t] * decorFir.tapGain[(size_t) t];
            wsum += wgt; dsum += wgt * (double) decorFir.tapPos[(size_t) t];
        }
        decorMeanDelaySamples = (wsum > 0.0) ? dsum / wsum : 0.0;
    }
    // one-pole por-sample de la profundidad del ensanchador (τ ≈ 5 ms): invariante al block-size.
    decorDepCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) sr));

    reset();
}

void AuroraEngine::reset()
{
    stft.reset();
    limiter.reset();
    for (auto& ring : dryRing) std::fill (ring.begin(), ring.end(), 0.0f);
    dryWrite = 0;
    dryScratch.clear();
    env = envHopMax = envNorm = envSm = 0.0f;
    inPhaseSm = inPhaseTgt = 0.0f;
    decorFir.reset();
    for (auto& s : decorHpLp) s = 0.0f;
    decorSpreadSm = 0.0f;
    hopPhase = 0;
    motionPhase = 0.0;
    vizEnergy.fill (0.0f);
    vizPos.fill (0.0f);
    primeSmoothing = true;
}

// ------------------------------------------------------------------------------ process
void AuroraEngine::process (juce::AudioBuffer<float>& buffer, const AuroraParams& p)
{
    const int n = buffer.getNumSamples();
    if (n <= 0 || dryRingSz <= 0) return;

    // Defensa contra hosts FUERA DE CONTRATO (n > maximumBlockSize del prepare): procesar
    // en sub-bloques de maxBlock. Sin esto, dryScratch.copyFrom escribiría fuera de rango
    // en release (el jassert sólo avisa en debug). El costo es cero en el camino normal.
    if (n > maxBlock)
    {
        jassertfalse;   // el host violó el contrato de prepareToPlay (se procesa igual, troceado)
        int done = 0;
        while (done < n)
        {
            const int len = juce::jmin (maxBlock, n - done);
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(),
                                          buffer.getNumChannels(), done, len);
            process (sub, p);
            done += len;
        }
        return;
    }

    const int numCh = juce::jmin (2, buffer.getNumChannels());

    // --- objetivos de los suavizadores (mapeo ACÁ; el suavizado corre por frame) -----
    spreadTgt   = clamp01 (p.spread01);
    tiltTgt     = juce::jlimit (-1.0f, 1.0f, p.tiltBi);
    motion01Tgt = clamp01 (p.motion01);
    duckTgt     = clamp01 (p.duck01);
    msAmtTgt    = clamp01 (p.monoSafe01);
    inPhaseTgt  = p.inPhase ? 1.0f : 0.0f;   // 1 = colapsar el ensanchador (escape mono-safe), suavizado por frame
    // Corte de Mono Safe en log2(Hz): 60→700 Hz log (suavizar DESPUÉS del mapeo, en el
    // dominio log del destino — house rule §4.3).
    {
        const float fCut = kMonoSafeLoHz * std::pow (kMonoSafeHiHz / kMonoSafeLoHz, msAmtTgt);
        cutLog2Tgt = std::log2 (fCut);
    }

    // --- MOTION: rate efectivo CAPADO a < 20 Hz (cota anti-AM cableada) --------------
    const float rateHz = juce::jlimit (0.0f, kAntiAmCapHz, p.motionRateHz);
    phaseIncPerFrame   = juce::MathConstants<double>::twoPi * (double) rateHz
                         * (double) stft.hopSize() / sr;
    if (p.motionSync && p.isPlaying)
    {
        // SYNC: fase ENGANCHADA a ppq (el abanico abre en el beat — late con el track).
        // Lock al inicio del bloque; los frames internos avanzan con phaseIncPerFrame.
        //
        // COMPENSACIÓN DE FASE vs PDC (hallazgo review 2026-06-10): la fase lockeada acá
        // la usa el PRIMER frame que dispare este bloque, y ese frame se OYE en otro
        // instante de la línea de tiempo del host:
        //   · el frame dispara (hop − hopPhase) samples DESPUÉS del inicio del bloque,
        //   · su sello de modulación se imprime con centroide N/2 samples ANTES en la
        //     línea de tiempo (la ventana Hann² del OLA pesa el frame centrado en N/2 y
        //     el host corre la salida N hacia atrás por el PDC) → sin compensar, el
        //     abanico llegaba ANTES del beat (medido: −9.2° = −12.8 ms @ 1/4 120 BPM),
        //   · y el one-pole de γ retrasa la envolvente oída gammaLagSamples.
        // El lock se evalúa en el instante en que ese frame SE OYE → late EN el beat.
        const double beats = (double) juce::jmax (0.01f, p.beatsPerCycle);
        const double heardOffset = (double) (stft.hopSize() - hopPhase)            // feed → 1er frame
                                 - 0.5 * (double) stft.latencySamples()           // centroide OLA tras PDC
                                 + gammaLagSamples                                // lag del suavizado de γ
                                 + kDecorDelayCompFrac * decorMeanDelaySamples;   // group delay del FIR del ensanchador (factor empírico: la envolvente de POTENCIA del side lo ve parcial)
        double cycles = p.ppqPosition / beats + (double) rateHz * heardOffset / sr;
        cycles -= std::floor (cycles);
        motionPhase = juce::MathConstants<double>::twoPi * cycles;
    }

    if (primeSmoothing)
    {
        spreadSm = spreadTgt; tiltSm = tiltTgt; duckSm = duckTgt;
        msAmtSm = msAmtTgt; cutLog2Sm = cutLog2Tgt; motion01Sm = motion01Tgt;
        inPhaseSm = inPhaseTgt;
        envSm = envNorm;
        gammaSm = spreadSm * (1.0f - duckSm * envSm);
        const float phi0 = clamp01 (p.mix01) * juce::MathConstants<float>::halfPi;
        gDry = std::cos (phi0); gWet = std::sin (phi0);
        primeSmoothing = false;
    }

    // --- 1)+2) preservar el DRY + construir el MID + STFT, en CHUNKS alineados al HOP.
    // El envNorm del DUCK se fija en cada FRONTERA DE HOP con el max del período recién
    // completado (posición GLOBAL fija) → cada frame ve la MISMA envolvente sin importar
    // el block-size del host (32…2048): invarianza estructural, medida por [buffersize].
    // (Antes era el max por BLOQUE del host → el duck cambiaba de timing según el DAW.)
    for (int ch = 0; ch < numCh; ++ch)
        dryScratch.copyFrom (ch, 0, buffer, ch, 0, n);

    const bool monoIn = (p.numInputChannels <= 1);
    {
        const float* inL = dryScratch.getReadPointer (0);
        const float* inR = dryScratch.getReadPointer (numCh > 1 ? 1 : 0);
        float* w0 = buffer.getWritePointer (0);
        float* w1 = (numCh > 1) ? buffer.getWritePointer (1) : nullptr;
        const int hop = stft.hopSize();

        int done = 0;
        while (done < n)
        {
            // El chunk termina EXACTO en la próxima frontera de hop (o al final del bloque).
            const int len = juce::jmin (n - done, hop - hopPhase);

            for (int i = done; i < done + len; ++i)
            {
                // Mono de entrada: el mid ES el canal único (sin el −6 dB de promediar con un R vacío).
                const float mid = monoIn ? inL[i] : 0.5f * (inL[i] + inR[i]);
                w0[i] = mid;
                if (w1 != nullptr) w1[i] = mid;

                // Envelope follower del DRY (attack 5 ms / release 150 ms) → el DUCK lo consume por frame.
                const float a = std::abs (mid);
                env += (a > env ? envAtkCoef : envRelCoef) * (a - env);
                envHopMax = juce::jmax (envHopMax, env);
            }

            hopPhase += len;
            if (hopPhase >= hop)
            {
                // Período de hop COMPLETO: el frame que dispare el feed de este chunk ve
                // el max del período entero. envHopMax arranca el siguiente desde env.
                envNorm   = clamp01 (envHopMax / kDuckRefAmp);
                envHopMax = env;
                hopPhase  = 0;
            }

            // Feed del chunk al STFT/OLA (sub-vista sin alocar; dispara processFrame en
            // la frontera). El ring interno del STFT lleva el MISMO conteo que hopPhase.
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), numCh, done, len);
            stft.process (sub);
            done += len;
        }
    }

    // --- 2.5) ENSANCHADOR DE LOW-MIDS (post-STFT, OLA-inmune) -------------------------
    // Sobre el WET reconstruido: réplica decorrelada del mid (FIR disperso) inyectada como SIDE
    // PURO opuesto en L/R. Mono-safe por construcción (L+R = 2·mid: el side cancela → suma mono
    // ~dry). HP de graves (sigue el corte Mono Safe + IN PHASE) mantiene el kick/sub al centro.
    // Profundidad = γ (spread·MOTION·DUCK, el mismo que late/respira el abanico del STFT) ×
    // escape IN PHASE → el ensanchador HEREDA el MOTION (el abanico se abre/cierra) y el DUCK
    // (se cierra cuando pega el dry). γ ya viene suavizado por frame (gammaSm).
    if (numCh > 1)
    {
        // γ post-frame = spread·motion·duck (mismo que el reparto del STFT) → el ensanchador
        // late con el MOTION y agacha con el DUCK ([diccionario]/[realworld]). × escape IN PHASE.
        // kDecorWidthDepth fija el side máx (calibrado sobre el beat real: spread100 → CORR ≤0.5).
        const float depthTgt = clamp01 (gammaSm) * (1.0f - inPhaseTgt) * kDecorWidthDepth;

        // HP del side inyectado: bajo el corte de Mono Safe (o un piso fijo) el side se atenúa
        // → kick/bajo al centro. Corte = max(piso, corte Mono Safe). One-pole HP por sample.
        const float fCutMs = std::pow (2.0f, cutLog2Tgt);                 // Hz del corte Mono Safe vigente
        const float hpHz   = juce::jmax (kDecorHpFloorHz, fCutMs);
        const float hpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hpHz / (float) sr);

        float* o0 = buffer.getWritePointer (0);
        float* o1 = buffer.getWritePointer (1);
        float dep = decorSpreadSm;
        for (int i = 0; i < n; ++i)
        {
            // profundidad: one-pole POR SAMPLE hacia el objetivo (τ ~5 ms) → INVARIANTE al
            // block-size (anti-zipper, [consistency]); una rampa /n dependía del tamaño de bloque.
            dep += decorDepCoef * (depthTgt - dep);

            const float mid  = 0.5f * (o0[i] + o1[i]);
            const float side = 0.5f * (o0[i] - o1[i]);

            // réplica decorrelada del mid (FIR disperso: magnitud ~plana, fase scrambleada)
            const float dec = decorFir.process (mid);

            // HP del side inyectado (4× one-pole en cascada → LP de 24 dB/oct; HP = dec − LP):
            // falda firme que ata el sub/kick fundamental al CENTRO (el side de graves NO se
            // dispara aunque Mono Safe = 0; medido: 80 Hz panNorm < 0.1).
            decorHpLp[0] += hpCoef * (dec          - decorHpLp[0]);
            decorHpLp[1] += hpCoef * (decorHpLp[0] - decorHpLp[1]);
            decorHpLp[2] += hpCoef * (decorHpLp[1] - decorHpLp[2]);
            decorHpLp[3] += hpCoef * (decorHpLp[2] - decorHpLp[3]);
            const float decHp = dec - decorHpLp[3];

            // side nuevo = side original + réplica decorrelada × profundidad. La suma mono NO
            // cambia (sólo toca el side) → mono-compatible. CORR baja al crecer dep.
            const float newSide = side + dep * decHp;
            o0[i] = mid + newSide;
            o1[i] = mid - newSide;
        }
        decorSpreadSm = dep;
    }

    // --- 3) dry delay (latencySamples) + 4) MIX por ley de potencia (rampa por-sample)
    const float phi     = clamp01 (p.mix01) * juce::MathConstants<float>::halfPi;
    const float gDryTgt = std::cos (phi);
    const float gWetTgt = std::sin (phi);
    const int   delay   = stft.latencySamples();
    {
        const float stepD = (gDryTgt - gDry) / (float) n;
        const float stepW = (gWetTgt - gWet) / (float) n;
        float gd = gDry, gw = gWet;
        int w = dryWrite;
        const float* dSrc0 = dryScratch.getReadPointer (0);
        const float* dSrc1 = dryScratch.getReadPointer (numCh > 1 ? 1 : 0);
        float* out0 = buffer.getWritePointer (0);
        float* out1 = (numCh > 1) ? buffer.getWritePointer (1) : nullptr;
        float* ring0 = dryRing[0].data();
        float* ring1 = dryRing[1].data();
        for (int i = 0; i < n; ++i)
        {
            // El dry de un input mono se duplica a ambos canales (igual que el bypass del chasis).
            const float d0 = dSrc0[i];
            const float d1 = monoIn ? dSrc0[i] : dSrc1[i];
            ring0[w] = d0;
            ring1[w] = d1;
            int r = w - delay; if (r < 0) r += dryRingSz;
            const float dly0 = ring0[r];
            const float dly1 = ring1[r];
            out0[i] = gd * dly0 + gw * out0[i];
            if (out1 != nullptr) out1[i] = gd * dly1 + gw * out1[i];
            if (++w == dryRingSz) w = 0;
            gd += stepD; gw += stepW;
        }
        dryWrite = w;
        gDry = gDryTgt;
        gWet = gWetTgt;
    }

    // --- 5) limiter de salida estéreo-linked (techo 0.85, red de seguridad del sello)
    if (numCh > 1)
        limiter.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    else
        limiter.process (buffer.getWritePointer (0), buffer.getWritePointer (0), n);
}

// -------------------------------------------------------------------------- processFrame
// El DSP espectral por bin. Corre DENTRO de stft.process() en cada frame (hop = N/4).
// Ambos canales traen el MISMO espectro X[k] del mid (el análisis re-ventanea los rings
// de entrada, que reciben mid en L y R) → acá se reparte la magnitud con fase preservada.
void AuroraEngine::processFrame (const ovni::engines::StftEngine::FrameView& f)
{
    // — suavizado por frame de las macros (post-mapeo, dominio del destino) —
    spreadSm   += (spreadTgt   - spreadSm)   * smoothCoef;
    tiltSm     += (tiltTgt     - tiltSm)     * smoothCoef;
    duckSm     += (duckTgt     - duckSm)     * smoothCoef;
    msAmtSm    += (msAmtTgt    - msAmtSm)    * smoothCoef;
    cutLog2Sm  += (cutLog2Tgt  - cutLog2Sm)  * smoothCoef;
    motion01Sm += (motion01Tgt - motion01Sm) * smoothCoef;
    inPhaseSm  += (inPhaseTgt  - inPhaseSm)  * smoothCoef;
    envSm      += (envNorm     - envSm)      * gammaCoef;

    // — MOTION: el abanico se abre/cierra. mod ∈ [1−motion, 1]; = 1 en fase 0 (downbeat
    //   en SYNC → abierto en el beat). La cota < 20 Hz ya está aplicada al rate.
    const float mod = 1.0f - motion01Sm * (0.5f - 0.5f * (float) std::cos (motionPhase));
    motionPhase += phaseIncPerFrame;
    if (motionPhase >= juce::MathConstants<double>::twoPi)
        motionPhase -= juce::MathConstants<double>::twoPi;

    // — γ efectivo: spread × motion × duck (multiplicativo hacia 0). LINEAL en spread (la
    //   audibilidad la da el drive de excursión por bin, NO una re-curva del knob → SPREAD
    //   queda honesta ≥10%/paso en TODO el rango). One-pole rápido —
    const float gammaNow = spreadSm * mod * (1.0f - duckSm * envSm);
    gammaSm += (gammaNow - gammaSm) * gammaCoef;
    const float gamma = gammaSm;

    const float t = tiltSm;
    const float msStrength = clamp01 (msAmtSm * 50.0f);   // fade-in de la red en el primer 2% del knob (sin escalón al salir de 0)

    // ENSANCHADOR REAL — magnitud del offset de fase del frame: γ (spread·motion·duck) ×
    // techo × (1 − IN PHASE). IN PHASE suavizado a 1 → offset 0 → la imagen vuelve ~al dry
    // (suma mono ~0 dB, escape mono-safe). γ ya trae spread/motion/duck → el ancho late,
    // respira con el duck y escala con SPREAD igual que el carácter del despliegue.
    const float spreadPhaseMax = gamma * kSpreadPhaseMaxScale
                               * juce::MathConstants<float>::pi * (1.0f - inPhaseSm);

    // — acumuladores del visualizador (energía + posición por banda) —
    float ez[kVizBands] = {};
    float pa[kVizBands] = {};

    float* s0 = f.spectra[0];
    float* s1 = f.spectra[1];
    const int nb = f.numBins;

    for (int k = 0; k < nb; ++k)
    {
        const float u = binU[(size_t) k];

        // C(k, tilt): envolvente de apertura × serpenteo. tilt ≥ 0 = curva base que se
        // vuelve más abierta/convexa; tilt < 0 = morph CONTINUO hacia el ESPEJO de la
        // curva base (graves a los bordes / agudos al centro) — sin salto en 0
        // (automatable sin clicks). El espejo usa la MISMA forma base (knee 0.55): así
        // a −100 la inversión es real (el aire queda amarrado al centro), no "todo
        // abierto menos el extremo" — honestidad medida por [measure] TILT.
        float e;
        float placeW = 1.0f;   // peso del offset de FASE por bin: sigue la curva de colocación (ver abajo)
        if (t >= 0.0f)
            // Lado positivo: knee que baja (abre antes) × drive hacia los bordes (band-
            // limitado, ver kTiltDrive): cada cuarto de vuelta se OYE — [honestidad] lo mide.
            e = openShape (u, t) * (1.0f + kTiltDrive * t * binDriveRamp[(size_t) k]);
        else
        {
            const float eBase = openShape (u, 0.0f);
            e = (1.0f + t) * eBase + (-t) * openShape (1.0f - u, 0.0f);
            // FIX TILT audible (MEDIDO 2026-07-02: ±100 movía el side ~0.7 dB y NUNCA invertía):
            // el ancho audible lo domina el offset de FASE (phi), que IGNORABA el TILT — el espejo
            // sólo movía θ (reparto de potencia), contribución menor al side. placeW = e/eBase
            // acopla la fase a la curva de colocación SOLO en la rama espejo: a t→0⁻ la razón → 1
            // (continuo con la rama positiva, que queda bit-idéntica = cero regresión de calibración);
            // a t=−100 los agudos → 0 (fase quieta = CENTRO real) y los graves conservan su fase
            // plena (clamp01: no amplifica — el techo sigue siendo el calibrado kSpreadPhaseMaxScale).
            // La razón cruda es BLANDA (la curva base llega a 1 recién en u=0.55 → en medios-altos
            // queda ~0.5 = −6 dB, inversión tímida, MEDIDA): el exponente 1+3·(−t) la agudiza
            // progresivamente (a −100: u=0.7 → −21 dB de fase; u=0.5 = pivote, queda 1). Continuo
            // en t→0⁻ (exponente → 1, razón → 1): la automatización no salta.
            placeW = clamp01 (e / juce::jmax (1.0e-6f, eBase));
            placeW = std::pow (placeW, 1.0f + 3.0f * (-t));
        }

        // DRIVE DE EXCURSIÓN (causa raíz del fix audible): empuja la envolvente hacia el
        // hard-pan en medios/agudos (band-limitado al cuadrado → graves intactos, alias OK).
        // Vive en AMBAS ramas de TILT → el despliegue abre con cualquier mapeo (default +15 y
        // el invertido −85). El softLimitTheta absorbe el exceso sin esquinas.
        e *= binSpreadDrive[(size_t) k];

        // En la rama espejo (t<0) el peso de colocación TAMBIÉN gobierna θ: sin esto el drive de
        // excursión devolvía los agudos al hard-pan por ILD aunque el espejo dijera "centro"
        // (por eso el TILT −100 medía side casi intacto). En t≥0 placeW=1 → bit-idéntico.
        float th = gamma * e * placeW * binWeave[(size_t) k];

        // MONO SAFE: colapso al centro bajo el corte, transición de kMonoSafeTransOct octavas
        // (suave en frecuencia, sin escalón espectral). m=0 en el corte … m=1 trans-oct arriba.
        const float m = clamp01 ((binLog2F[(size_t) k] - cutLog2Sm) / kMonoSafeTransOct);
        const float msKeep = 1.0f - msStrength * (1.0f - m);   // 0 bajo el corte (mono) … 1 arriba
        th *= msKeep;

        th = softLimitTheta (th);   // saturación SUAVE (C1, sin esquinas → sin espurias)

        // Reparto de POTENCIA CONSTANTE (el carácter del despliegue / TILT): ganancias REALES.
        const float alpha = (th + 1.0f) * (juce::MathConstants<float>::pi * 0.25f);

        // MOVIMIENTO ESPECTRAL MONO-AUDIBLE (ver constantes): peine barber-pole aplicado IGUAL
        // en L y R → toca la SUMA MONO (se oye sin estéreo) SIN cambiar |L|/|R| relativos
        // (CORR/WIDTH intactos). Lo MANEJA el MOTION (0 → apagado: piso de alias quieto intacto
        // y honestidad monótona) escalado por SPREAD; binDriveRamp lo band-limita (graves sólidos,
        // OLA limpio). Fase = MOTION → barre al rate del MOTION. Se pliega en gL/gR (writes igual).
        const float specDepth = motion01Sm * spreadSm * kSpecModDepth * binDriveRamp[(size_t) k];
        const float gMono = juce::jmax (kSpecModFloor,
            1.0f + specDepth
                 * std::sin (juce::MathConstants<float>::twoPi * kSpecModCycles * u - (float) motionPhase));

        const float gL = juce::MathConstants<float>::sqrt2 * std::cos (alpha) * gMono;
        const float gR = juce::MathConstants<float>::sqrt2 * std::sin (alpha) * gMono;

        const float re = s0[2 * k];
        const float im = s0[2 * k + 1];

        // ENSANCHADOR REAL: offset de fase OPUESTO L/R, random por bin, escalado por γ·spread y
        // por la MISMA red de Mono Safe (msKeep → bajo el corte el offset es 0 = graves al centro).
        // |L|=gL·|X|, |R|=gR·|X| sin cambiar (sólo rota la fase): balance de energía y alias floor
        // intactos; lo que cae es la CORRELACIÓN L/R (el goniómetro abre). Rotación por ±φ:
        //   L = (re,im)·gL·e^(−jφ) ;  R = (re,im)·gR·e^(+jφ).
        const float phi = spreadPhaseMax * binDecorr[(size_t) k] * msKeep * placeW;
        const float c = std::cos (phi);
        const float sN = std::sin (phi);
        // e^(−jφ)·(re+j·im) = (re·c + im·s) + j·(im·c − re·s)
        const float reL =  re * c + im * sN;
        const float imL =  im * c - re * sN;
        // e^(+jφ)·(re+j·im) = (re·c − im·s) + j·(im·c + re·s)
        const float reR =  re * c - im * sN;
        const float imR =  im * c + re * sN;
        s0[2 * k]     = reL * gL;
        s0[2 * k + 1] = imL * gL;
        s1[2 * k]     = reR * gR;
        s1[2 * k + 1] = imR * gR;

        // — telemetría: energía del mid + posición asignada, por banda —
        const float m2 = re * re + im * im;
        const int   b  = binVizBand[(size_t) k];
        ez[b] += m2;
        pa[b] += m2 * th;
    }

    // — volcado por banda (one-pole visual; el processor lo copia a los atomics) —
    const float ampScale = 2.0f / (float) f.fftSize;   // ~amplitud lineal (indep. de N)
    for (int b = 0; b < kVizBands; ++b)
    {
        const auto   bi  = (size_t) b;
        const float amp = std::sqrt (ez[b]) * ampScale;
        vizEnergy[bi] += 0.35f * (amp - vizEnergy[bi]);
        if (ez[b] > 1.0e-12f)
            vizPos[bi] += 0.35f * ((pa[b] / ez[b]) - vizPos[bi]);
        // sin energía: la posición previa decae sola hacia el centro con la energía en ~0
    }
}

} // namespace aurora
