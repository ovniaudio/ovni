#pragma once
// SUPERNOVA — IDs de parámetros (fuente única, minúsculas). Los 6 automatizables del spec RF8 + bypass.
// NO declarar inGain/output/monoSafe: los inyecta el chasis (ovni::PluginProcessorBase); SUPERNOVA no los
// expone en el editor (es un plugin visual, no toca el audio).
namespace supernova::params::id
{
    inline constexpr const char* INTENSITY     = "intensity";     // energía global (héroe)
    inline constexpr const char* CHAOS         = "chaos";         // turbulencia / curl (héroe)
    inline constexpr const char* PARTICLE_SIZE = "particleSize";  // tamaño de partícula (héroe)
    inline constexpr const char* GLOW          = "glow";          // bloom (héroe)
    inline constexpr const char* EXPLODE       = "explode";       // trigger de explosión (RF8)
    inline constexpr const char* CUTOUT        = "cutout";        // borrar fondo gradual (escultura)
    inline constexpr const char* PRESET        = "preset";        // selección de preset (RF8)
    inline constexpr const char* BYPASS        = "bypass";
    // SENSIBILIDAD VISUAL (fader IN de la TopBar, app y plugin): escala SOLO la mezcla mono del ANÁLISIS
    // (processAudio → FIFO). NO es el inGain del chasis (drive real del audio): el insert sigue bit-exacto.
    inline constexpr const char* VIS_GAIN      = "visGain";       // −24..+24 dB · default 0 = identidad

    // Física por-preset (M3): automatizables pero NO en la franja de controles (preset-driven / avanzados).
    inline constexpr const char* CURL_SCALE    = "curlScale";     // frecuencia espacial del curl
    inline constexpr const char* HOME_STRENGTH = "homeStrength";  // atracción al hogar / re-armado
    inline constexpr const char* GRAVITY       = "gravity";       // tiro constante (bipolar)
    inline constexpr const char* MOMENTUM      = "momentum";      // retención de velocidad
    inline constexpr const char* RADIAL_GAIN   = "radialGain";    // fuerza de la explosión
    inline constexpr const char* JITTER_GAIN   = "jitterGain";    // agitación por agudos
    inline constexpr const char* BREATHE_GAIN  = "breatheGain";   // respiración por RMS

    // VOCABULARIO VISUAL (capas 1-3 + paleta) — el pedido "nivel TouchDesigner": cada preset arma un MUNDO
    // (movimiento + física + geometría + color) y estos knobs lo hacen tuyo en vivo.
    inline constexpr const char* MOTION        = "motion";        // choice: Contornos/Materia/Onda/Vórtices/Radial
    inline constexpr const char* SHAPE         = "shape";         // choice: glifo (Dot..Spark)
    inline constexpr const char* TRAILS        = "trails";        // estela 0-100 (caminos que se desvanecen)
    inline constexpr const char* LINKS         = "links";         // plexus 0-100 (conexiones entre cercanos)
    inline constexpr const char* SAT           = "sat";           // saturación 0-100 (0=B/N, 50=neutro, 100=vívido)
    inline constexpr const char* HUE           = "hue";           // rotación de tono −180..+180°
    inline constexpr const char* VARIATION     = "variation";     // randomización CURADA dentro del preset (0=puro)

    // HOTKNOBS DE VARIACIÓN POR DOMINIO (pedido "un hotknob de cada parámetro gral"): tomas deterministas
    // SOLO de su fila; VARIATION global es el master (se suman — fase periódica). 0 = neutro exacto.
    inline constexpr const char* VAR_MOVEMENT  = "varMovement";   // tomas del movimiento (física/tiempo)
    inline constexpr const char* VAR_MATTER    = "varMatter";     // tomas de la materia (glifo/estructura)
    inline constexpr const char* VAR_CAMERA    = "varCamera";     // tomas de la cámara (3D/giro)
    inline constexpr const char* VAR_COLOR     = "varColor";      // tomas del color (paleta/tono)

    // TIER 1 PRO (fila 3): estructura, tiempo y cámara — todos con default = comportamiento histórico exacto.
    inline constexpr const char* DENSITY       = "density";       // fracción de partículas 1-100 (100=todas)
    inline constexpr const char* SCATTER       = "scatter";       // dispersión de hogares 0-100 (imagen↔nube)
    inline constexpr const char* SPEED         = "speed";         // timewarp 0-100 log (50 = ×1)
    inline constexpr const char* ROTATE        = "rotate";        // giro de vista −100..+100 → ±30°/s
    inline constexpr const char* PUMP          = "pump";          // sidechain visual del kick 0-100 (30 = clásico)
    inline constexpr const char* HUE_CYCLE     = "hueCycle";      // deriva de tono −100..+100 → ±60°/s
    inline constexpr const char* KALEIDO       = "kaleido";       // choice: Off/2/4/6/8 espejos

    // 3D + MOTOR GEOMÉTRICO (fila 4): profundidad, cámara orbital y figura. Defaults = identidad byte-exacta.
    inline constexpr const char* DEPTH         = "depth";         // volumen 0-100 (0 = plano, camino legacy)
    inline constexpr const char* ROT_X         = "rotX";          // pitch −180..+180° ("tumbarlo")
    inline constexpr const char* ROT_Y         = "rotY";          // yaw   −180..+180° ("darlo vuelta")
    inline constexpr const char* ORBIT         = "orbit";         // auto-órbita −100..+100 → ±45°/s de yaw
    inline constexpr const char* FIGURE        = "figure";        // choice: Imagen/Esfera/Espiral/Anillos/Grilla/Hélice
    inline constexpr const char* FORM          = "form";          // fader imagen↔figura 0-100 (def 100: el stepper manda)

    // COLOR LAB (fila 4): la paleta como instrumento. PALETTE = choice del banco LookRamps (0 = Original,
    // identidad exacta); AMOUNT = mix del gradient map (def 100: el stepper manda); BG = lienzo de papel.
    inline constexpr const char* PALETTE       = "palette";       // choice: Original + 16 looks curados
    inline constexpr const char* COLOR_AMOUNT  = "colorAmt";      // mix del map 0-100 (def 100)
    inline constexpr const char* BG            = "bg";            // papel 0-100 (def 0 = negro clásico)

    // Defaults en UNIDADES de param (fuente única editor↔render tool: el fallback de un preset que omite un id
    // DEBE coincidir con el default del APVTS o el tool y el plugin renderizan distinto).
    inline constexpr float paramDefault (const char* id) noexcept
    {
        auto eq = [] (const char* a, const char* b) noexcept
        {
            while (*a && *a == *b) { ++a; ++b; }
            return *a == *b;
        };
        if (eq (id, CHAOS))         return 30.0f;
        if (eq (id, PARTICLE_SIZE)) return 40.0f;
        if (eq (id, DENSITY))       return 100.0f;
        if (eq (id, PUMP))          return 30.0f;   // = el flash histórico del kick (0.6)
        if (eq (id, FORM))          return 100.0f;  // el stepper FIGURA decide SI; el fader arranca a fondo
        if (eq (id, COLOR_AMOUNT))  return 100.0f;  // ídem PALETTE: el stepper decide SI, el fader CUÁNTO
        if (eq (id, GRAVITY) || eq (id, EXPLODE) || eq (id, CUTOUT) || eq (id, VIS_GAIN)
            || eq (id, MOTION) || eq (id, SHAPE)
            || eq (id, TRAILS) || eq (id, LINKS) || eq (id, HUE) || eq (id, VARIATION)
            || eq (id, VAR_MOVEMENT) || eq (id, VAR_MATTER) || eq (id, VAR_CAMERA) || eq (id, VAR_COLOR)
            || eq (id, SCATTER) || eq (id, ROTATE) || eq (id, HUE_CYCLE) || eq (id, KALEIDO)
            || eq (id, DEPTH) || eq (id, ROT_X) || eq (id, ROT_Y) || eq (id, ORBIT) || eq (id, FIGURE)
            || eq (id, PALETTE) || eq (id, BG))
            return 0.0f;
        return 50.0f;   // intensity/glow/física/sat/speed(×1 en el mapeo log)
    }
}
