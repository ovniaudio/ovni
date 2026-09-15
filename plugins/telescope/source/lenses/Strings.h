#pragma once
#include <array>
#include <vector>
#include <juce_data_structures/juce_data_structures.h>

// ========================================================================================================
// Strings — los RÓTULOS de TELESCOPE en todos los idiomas (prompt 56 + addendum D-50 de Joaquín).
//
// LA DECISIÓN (D-50, 8-sep-2026): inglés POR DEFECTO y posibilidad en TODOS los idiomas. Por eso el
// setting `language` NO es un enum de dos valores sino un código ISO 639-1 (`en`, `es`, `pt`, `fr`, `de`,
// `it`, `ja`…): agregar un idioma es agregar una TABLA acá abajo, cero código en otro lado.
//
// FALLBACK POR CLAVE, no por tabla. Si a un idioma le falta una frase, esa frase sale en inglés y el
// resto del idioma queda intacto. Nunca sale vacía y nunca sale una plantilla sin resolver: una etiqueta
// en blanco en un medidor es peor que una etiqueta en el idioma equivocado — el usuario no sabe si es un
// bug del plugin o un valor que no existe.
//
// LOS TÉRMINOS SON LOS DE LA INDUSTRIA, no traducciones inventadas. En inglés son los de Insight / SPAN /
// la BS.1770: Integrated · Short-term · Momentary · Loudness Range · True Peak · Correlation · Width ·
// Mono compatibility · Tonal balance · Key · Confidence. En castellano se traduce lo que SE TRADUCE en un
// estudio ("ventana", "canal", "promedio") y se deja tal cual lo que nadie traduce ("LUFS", "True Peak",
// "LRA", "PLR", "loudness"): escribir "PICO REAL" en un medidor sería más raro que dejarlo en inglés.
//
// COMPARTIDO CON EL 55: el nombre del setting (`language`) y la firma `get (clave, idioma)` son los
// mismos que usa VERDICT. Las dos tablas son independientes; el contrato es el nombre y la semántica del
// fallback.
// ========================================================================================================
namespace telescope::strings
{
// La propiedad del ValueTree del APVTS donde vive el idioma. MISMO nombre que usa el 55.
inline constexpr const char* kLanguageProperty = "language";
inline constexpr const char* kDefaultLanguage  = "en";

enum class Key
{
    // --- controles y estados compartidos por varias lentes ---
    window = 0, channel, decay, lines, tilt, range, overlap, slope, average, hold, smoothing,
    reset, pause, resume, load, remove, row, bands, target, history, free, off, on, now,
    noSignal, noTarget, measuring, analysing, analysisBehind, threshold, events, lastMinutes,

    // --- LOUDNESS ---
    integrated, shortTerm, momentary, loudnessRange, truePeak, maxShortTerm, maxMomentary,
    aboveTarget, belowTarget, matchesTarget, theyTurnYouDown, theyTurnYouUp, program,

    // --- DYNAMICS ---
    dynamics, crestPlr, crestPsr, histogram, clips, clipThreshold,

    // --- SPECTRUM / SPECTROGRAM ---
    spectrum, fftSize, windowFn, peakHold, bandsThirdOctave, bandsBark, bandsFree, resolution,
    noBinsHere, perOctave,

    // --- SCOPE / estéreo ---
    // 57c (D-34) — `polar` se MUESTRA como POLAR SAMPLE y `hemisphere` como POLAR LEVEL: son los
    // nombres con los que un ingeniero los busca (son los de Insight). Las CLAVES no cambian de nombre
    // —renombrarlas movería las 6 tablas posicionales sin que nadie gane nada— y el manual conserva
    // "hemisferio" para el PLEGADO, que es otra cosa: el modo se llama por lo que muestra, el plegado por
    // lo que hace.
    correlation, width, balance, monoCompatibility, monoLoss, lissajous, polar, hemisphere,
    trigger, oscilloscope, outOfPhase, mono, left, right, mid, side, noCorrelation, peakEdge,
    hemisphereDecay, inPhaseAbove, outOfPhaseBelow,

    // --- BAND CORRELATION / STEREO SPECTROGRAM ---
    perBand, coherence, phase, measuredBands, fewBins,

    // --- CQT / SPIRAL ---
    key, confidence, chroma, octave, noteClass, estimated, tonic,

    // --- FIELD / WATERFALL ---
    panDirection, energyByDirection, notLocalisation, depth, trails, frontLine,

    // --- TONAL BALANCE ---
    tonalBalance, referenceLabel, chooseReference, referenceMissing, noReferenceInBand, delta,
    dynamicRefRange,

    // --- nombres de lente (la tira) ---
    lensLoudness, lensDynamics, lensSpectrum, lensSpectrogram, lensWaterfall, lensCqt, lensSpiral,
    lensScope, lensBandCorrelation, lensStereoSpectrogram, lensField, lensTonalBalance, lensVerdict,

    // --- accesibilidad / ayuda ---
    whatAmILooking, reducedMotion, language,

    // --- 56b: lo que quedaba en castellano y a mano en las diez lentes que no leían de acá (D-50) ---
    mostMonoLoss, bassLatency, noKeyEstimated, ofTheTime, histogramShortTerm, dynamicRefShort,
    plrSinceReset, eventsAbove, lastMinutes3, targetLower, youAre, dbBelow, dbBelowNoRaise,
    polarLegend, noReferenceDrag, tiltEqualLoudness, refPlusMinus,

    // --- 57b: la paleta de los mapas de calor (una sola para las cuatro lentes de nivel) ---
    palette, hemiLegend,

    // --- 57c: TONAL BALANCE · lo que la lente dice cuando no puede dibujar una banda ---
    refBelowRange, binsAtThisFft,

    // --- 57c: LOUDNESS · los rótulos CORTOS de las barras. Nunca una letra sola para las LUFS: "M" y "S"
    //     debajo de dos medidores de loudness se leen mid/side, que es otra cosa y está al lado. ---
    momentaryBar, shortTermBar,

    count
};

inline constexpr int kNumKeys = (int) Key::count;

// Los nombres de clave. Sólo se usan para DIAGNÓSTICO (el test nombra la clave que falta): una tabla
// incompleta tiene que decir CUÁL falta, no "algo falta".
inline const std::array<const char*, (size_t) kNumKeys>& keyNames()
{
    static const std::array<const char*, (size_t) kNumKeys> n = {
        "window","channel","decay","lines","tilt","range","overlap","slope","average","hold","smoothing",
        "reset","pause","resume","load","remove","row","bands","target","history","free","off","on","now",
        "noSignal","noTarget","measuring","analysing","analysisBehind","threshold","events","lastMinutes",
        "integrated","shortTerm","momentary","loudnessRange","truePeak","maxShortTerm","maxMomentary",
        "aboveTarget","belowTarget","matchesTarget","theyTurnYouDown","theyTurnYouUp","program",
        "dynamics","crestPlr","crestPsr","histogram","clips","clipThreshold",
        "spectrum","fftSize","windowFn","peakHold","bandsThirdOctave","bandsBark","bandsFree","resolution",
        "noBinsHere","perOctave",
        "correlation","width","balance","monoCompatibility","monoLoss","lissajous","polar","hemisphere",
        "trigger","oscilloscope","outOfPhase","mono","left","right","mid","side","noCorrelation","peakEdge",
        "hemisphereDecay","inPhaseAbove","outOfPhaseBelow",
        "perBand","coherence","phase","measuredBands","fewBins",
        "key","confidence","chroma","octave","noteClass","estimated","tonic",
        "panDirection","energyByDirection","notLocalisation","depth","trails","frontLine",
        "tonalBalance","referenceLabel","chooseReference","referenceMissing","noReferenceInBand","delta",
        "dynamicRefRange",
        "lensLoudness","lensDynamics","lensSpectrum","lensSpectrogram","lensWaterfall","lensCqt","lensSpiral",
        "lensScope","lensBandCorrelation","lensStereoSpectrogram","lensField","lensTonalBalance","lensVerdict",
        "whatAmILooking","reducedMotion","language",
        "mostMonoLoss","bassLatency","noKeyEstimated","ofTheTime","histogramShortTerm","dynamicRefShort",
        "plrSinceReset","eventsAbove","lastMinutes3","targetLower","youAre","dbBelow","dbBelowNoRaise",
        "polarLegend","noReferenceDrag","tiltEqualLoudness","refPlusMinus",
        "palette","hemiLegend",
        "refBelowRange","binsAtThisFft",
        "momentaryBar","shortTermBar"
    };
    return n;
}

// ========================================================================================================
// CÓMO SE NOMBRAN LAS NOTAS EN CADA IDIOMA (56c) — son TRES convenciones, no dos.
//
// No es una traducción: son sistemas distintos de nombrar la misma nota, y los tres están en uso hoy.
//
//   letters  C  C# D  D# E  F  F# G  G# A  A# B      anglosajón
//   solfege  Do Do# Re Re# Mi Fa Fa# Sol Sol# La La# Si   los románicos
//   german   C  C# D  D# E  F  F# G  G# A  B  H      germánico
//
// El alemán es el que faltaba, y no es un matiz. Hasta el 56b su tabla decía `solfege = false` y por eso
// compartía el array del inglés — o sea que el plugin le mostraba a un alemán "B" donde él lee H, y "A#"
// donde él lee B. No es una preferencia de escritura: en esa convención "B" ES el Si bemol, así que la
// pantalla estaba nombrando otra nota, un semitono más arriba de la que sonaba. Es la convención de Bach
// (B-A-C-H es una melodía real) y la que usan hoy los músicos alemanes.
//
// Lo lee lenses/NoteName.h, y por ahí CQT, SPIRAL y —desde el 56c— la cabecera de VERDICT.
enum class NoteNaming { letters = 0, solfege, german };

struct Table
{
    const char* code;        // ISO 639-1
    const char* endonym;     // cómo se llama el idioma EN ese idioma (es lo que muestra el selector)
    NoteNaming  notes;       // ver el bloque de arriba
    std::array<const char*, (size_t) kNumKeys> s;
};

// ========================================================================================================
// INGLÉS — la tabla COMPLETA y la red de todas las demás. Ninguna entrada puede quedar vacía acá.
// ========================================================================================================
inline const Table& tableEn()
{
    static const Table t { "en", "English", NoteNaming::letters, {
        "WINDOW","CHANNEL","DECAY","LINES","TILT","RANGE","OVERLAP","SLOPE","AVERAGE","HOLD","SMOOTHING",
        "RESET","PAUSE","RESUME","LOAD","REMOVE","ROW","BANDS","TARGET","HISTORY","FREE","OFF","ON","now",
        "no signal","no platform target","measuring","analysing","analysis behind","threshold","events","last 10 min",
        "INTEGRATED","SHORT-TERM","MOMENTARY","LOUDNESS RANGE","TRUE PEAK","MAX SHORT-TERM","MAX MOMENTARY",
        "above target","below target","matches the target","they turn you down","they turn you up","PROGRAMME",
        "DYNAMICS","PLR","PSR","HISTOGRAM","CLIPS","CLIP THRESHOLD",
        "SPECTRUM","FFT","WINDOW FN","PEAK HOLD","1/3 OCT","BARK","FREE","RESOLUTION",
        "no bins at this resolution","per octave",
        "CORRELATION","WIDTH","BALANCE","MONO COMPATIBILITY","MONO LOSS","LISSAJOUS","POLAR SAMPLE","POLAR LEVEL",
        "TRIGGER","OSCILLOSCOPE","out of phase","MONO","L","R","M","S","no correlation","edge = peak",
        "PEAK DECAY","in phase, above","out of phase, below",
        "PER BAND","COHERENCE","PHASE","bands measured","few bins in this band",
        "KEY","CONFIDENCE","CHROMA","OCTAVE","NOTE CLASS","estimated","TONIC",
        "PAN DIRECTION","energy by pan direction","not localisation","DEPTH","TRAILS","FRONT",
        "TONAL BALANCE","REFERENCE","Choose a reference file","reference not found: ","no reference in this band","DELTA",
        "dynamic reference range",
        "LOUDNESS","DYNAMICS","SPECTRUM","SPECTROGRAM","WATERFALL","CQT","SPIRAL",
        "SCOPE","BAND CORRELATION","STEREO SPECTROGRAM","FIELD","TONAL BALANCE","VERDICT",
        "what am I looking at?","reduced motion","LANGUAGE",
        "most mono loss: ","BASS LATENCY",
        "no key estimated — no chromagram to correlate","% of the time",
        "SHORT-TERM HISTOGRAM · LUFS","dynamic ref. ",
        "dB since RESET\nTP max − integrated","events above ",
        "last 3 min","target",
        "you are "," dB below",
        " dB below · they don't turn you up","polar sample · radius = level, edge 0 dBFS, centre −60",
        "no reference · drag a file in","comparing the tilt at equal loudness · the delta sums to zero",
        "ref. ±",
        "PALETTE",
        "radius = level relative to the strongest direction",
        "reference below the plot range (−42 LU) in this band"," bins at this FFT",
        "MOM","SHORT"
    }};
    return t;
}

// ========================================================================================================
// CASTELLANO — completo. Se traduce lo que se traduce en un estudio; "LUFS", "True Peak", "PLR", "PSR",
// "Bark", "Lissajous" y "loudness" quedan como están porque así se dicen.
// ========================================================================================================
inline const Table& tableEs()
{
    static const Table t { "es", "Español", NoteNaming::solfege, {
        "VENTANA","CANAL","DECAIMIENTO","LÍNEAS","INCLINACIÓN","RANGO","SOLAPE","PENDIENTE","PROMEDIO","RETENCIÓN","SUAVIZADO",
        "REINICIAR","PAUSA","SEGUIR","CARGAR","QUITAR","FILA","BANDAS","OBJETIVO","HISTORIA","LIBRE","OFF","ON","ahora",
        "sin señal","sin objetivo de plataforma","midiendo","analizando","análisis atrasado","umbral","eventos","últimos 10 min",
        "INTEGRADO","SHORT-TERM","MOMENTARY","LOUDNESS RANGE","TRUE PEAK","MÁX SHORT-TERM","MÁX MOMENTARY",
        "por encima del objetivo","por debajo del objetivo","coincide con el objetivo","te bajan","te suben","PROGRAMA",
        "DINÁMICA","PLR","PSR","HISTOGRAMA","CLIPS","UMBRAL DE CLIP",
        "ESPECTRO","FFT","VENTANA","PEAK HOLD","1/3 OCT","BARK","LIBRE","RESOLUCIÓN",
        "sin bins a esta resolución","por octava",
        "CORRELACIÓN","ANCHO","BALANCE","COMPATIBILIDAD MONO","PÉRDIDA MONO","LISSAJOUS","MUESTRAS POLARES","NIVEL POLAR",
        "TRIGGER","OSCILOSCOPIO","fuera de fase","MONO","L","R","M","S","sin correlación","borde = pico",
        "DECAIM. PICO","en fase, arriba","fuera de fase, abajo",
        "POR BANDA","COHERENCIA","FASE","bandas medidas","pocos bins en esta banda",
        "TONALIDAD","CONFIANZA","CROMA","OCTAVA","CLASE DE NOTA","estimado","TÓNICA",
        "DIRECCIÓN DE PANEO","energía por dirección de paneo","no es localización","PROFUNDIDAD","ESTELAS","FRENTE",
        "BALANCE TONAL","REFERENCIA","Elegí un archivo de referencia","referencia no encontrada: ","sin referencia en esta banda","DELTA",
        "rango dinámico de la referencia",
        "LOUDNESS","DINÁMICA","ESPECTRO","ESPECTROGRAMA","CASCADA","CQT","ESPIRAL",
        "SCOPE","CORRELACIÓN POR BANDA","ESPECTROGRAMA ESTÉREO","CAMPO","BALANCE TONAL","VEREDICTO",
        "¿qué estoy mirando?","menos movimiento","IDIOMA",
        "más pérdida mono: ","LATENCIA DEL GRAVE",
        "sin tonalidad estimada — no hay cromagrama que correlacionar","% del tiempo",
        "HISTOGRAMA DE SHORT-TERM · LUFS","ref. dinámica ",
        "dB desde el RESET\nTP máx − integrado","eventos sobre ",
        "últimos 3 min","objetivo",
        "estás "," dB por debajo",
        " dB por debajo · no te suben","muestras polares · radio = nivel, borde 0 dBFS, centro −60",
        "sin referencia · arrastrá un archivo","comparando el tilt a igual loudness · el delta suma cero",
        "ref. ±",
        "PALETA",
        "radio = nivel relativo a la dirección más fuerte",
        "referencia por debajo del rango (−42 LU) en esta banda"," bins a esta FFT",
        "MOM","CORTO"
    }};
    return t;
}

// ========================================================================================================
// PORTUGUÉS / FRANCÉS / ALEMÁN / ITALIANO — traducidos acá con los términos de la industria, pero SIN
// hablante nativo que los haya revisado. Está dicho en el reporte y está dicho acá: son "revisión
// pendiente de hablante nativo". Cualquier hueco cae a inglés por el fallback, así que un idioma a medio
// traducir sigue siendo usable.
// ========================================================================================================
inline const Table& tablePt()
{
    static const Table t { "pt", "Português", NoteNaming::solfege, {
        "JANELA","CANAL","DECAIMENTO","LINHAS","INCLINAÇÃO","FAIXA","SOBREPOSIÇÃO","INCLINAÇÃO","MÉDIA","HOLD","SUAVIZAÇÃO",
        "REINICIAR","PAUSA","CONTINUAR","CARREGAR","REMOVER","LINHA","BANDAS","ALVO","HISTÓRICO","LIVRE","OFF","ON","agora",
        "sem sinal","sem alvo de plataforma","medindo","analisando","análise atrasada","limiar","eventos","últimos 10 min",
        "INTEGRADO","SHORT-TERM","MOMENTARY","LOUDNESS RANGE","TRUE PEAK","MÁX SHORT-TERM","MÁX MOMENTARY",
        "acima do alvo","abaixo do alvo","coincide com o alvo","abaixam você","levantam você","PROGRAMA",
        "DINÂMICA","PLR","PSR","HISTOGRAMA","CLIPS","LIMIAR DE CLIP",
        "ESPECTRO","FFT","JANELA","PEAK HOLD","1/3 OCT","BARK","LIVRE","RESOLUÇÃO",
        "sem bins nesta resolução","por oitava",
        "CORRELAÇÃO","LARGURA","BALANÇO","COMPATIBILIDADE MONO","PERDA MONO","LISSAJOUS","AMOSTRAS POLARES","NÍVEL POLAR",
        "TRIGGER","OSCILOSCÓPIO","fora de fase","MONO","L","R","M","S","sem correlação","borda = pico",
        "DECAIM. PICO","em fase, acima","fora de fase, abaixo",
        "POR BANDA","COERÊNCIA","FASE","bandas medidas","poucos bins nesta banda",
        "TONALIDADE","CONFIANÇA","CROMA","OITAVA","CLASSE DE NOTA","estimado","TÔNICA",
        "DIREÇÃO DE PAN","energia por direção de pan","não é localização","PROFUNDIDADE","RASTROS","FRENTE",
        "BALANÇO TONAL","REFERÊNCIA","Escolha um arquivo de referência","referência não encontrada: ","sem referência nesta banda","DELTA",
        "faixa dinâmica da referência",
        "LOUDNESS","DINÂMICA","ESPECTRO","ESPECTROGRAMA","CASCATA","CQT","ESPIRAL",
        "SCOPE","CORRELAÇÃO POR BANDA","ESPECTROGRAMA ESTÉREO","CAMPO","BALANÇO TONAL","VEREDITO",
        "o que estou vendo?","menos movimento","IDIOMA",
        "maior perda mono: ","LATÊNCIA DO GRAVE",
        "sem tonalidade estimada — não há cromagrama para correlacionar","% do tempo",
        "HISTOGRAMA DE SHORT-TERM · LUFS","ref. dinâmica ",
        "dB desde o RESET\nTP máx − integrado","eventos acima de ",
        "últimos 3 min","alvo",
        "você está "," dB abaixo",
        " dB abaixo · não levantam você","amostras polares · raio = nível, borda 0 dBFS, centro −60",
        "sem referência · arraste um arquivo","comparando o tilt com o mesmo loudness · o delta soma zero",
        "ref. ±",
        "PALETA",
        "raio = nível relativo à direção mais forte",
        "referência abaixo da faixa (−42 LU) nesta banda"," bins nesta FFT",
        "MOM","CURTO"
    }};
    return t;
}

inline const Table& tableFr()
{
    static const Table t { "fr", "Français", NoteNaming::solfege, {
        "FENÊTRE","CANAL","DÉCROISSANCE","LIGNES","INCLINAISON","PLAGE","RECOUVREMENT","PENTE","MOYENNE","MAINTIEN","LISSAGE",
        "RÉINITIALISER","PAUSE","REPRENDRE","CHARGER","RETIRER","LIGNE","BANDES","CIBLE","HISTORIQUE","LIBRE","OFF","ON","maintenant",
        "pas de signal","pas de cible de plateforme","mesure en cours","analyse en cours","analyse en retard","seuil","événements","10 dernières min",
        "INTÉGRÉ","SHORT-TERM","MOMENTARY","LOUDNESS RANGE","TRUE PEAK","MAX SHORT-TERM","MAX MOMENTARY",
        "au-dessus de la cible","en dessous de la cible","correspond à la cible","ils vous baissent","ils vous montent","PROGRAMME",
        "DYNAMIQUE","PLR","PSR","HISTOGRAMME","CLIPS","SEUIL DE CLIP",
        "SPECTRE","FFT","FENÊTRE","PEAK HOLD","1/3 OCT","BARK","LIBRE","RÉSOLUTION",
        "aucun bin à cette résolution","par octave",
        "CORRÉLATION","LARGEUR","BALANCE","COMPATIBILITÉ MONO","PERTE MONO","LISSAJOUS","ÉCHANTILLONS POLAIRES","NIVEAU POLAIRE",
        "DÉCLENCHEMENT","OSCILLOSCOPE","hors phase","MONO","G","D","M","S","pas de corrélation","bord = crête",
        "DÉCR. CRÊTE","en phase, au-dessus","hors phase, en dessous",
        "PAR BANDE","COHÉRENCE","PHASE","bandes mesurées","peu de bins dans cette bande",
        "TONALITÉ","CONFIANCE","CHROMA","OCTAVE","CLASSE DE NOTE","estimé","TONIQUE",
        "DIRECTION DE PAN","énergie par direction de pan","ce n'est pas de la localisation","PROFONDEUR","TRAÎNÉES","AVANT",
        "ÉQUILIBRE TONAL","RÉFÉRENCE","Choisissez un fichier de référence","référence introuvable : ","pas de référence dans cette bande","DELTA",
        "plage dynamique de la référence",
        "LOUDNESS","DYNAMIQUE","SPECTRE","SPECTROGRAMME","CASCADE","CQT","SPIRALE",
        "SCOPE","CORRÉLATION PAR BANDE","SPECTROGRAMME STÉRÉO","CHAMP","ÉQUILIBRE TONAL","VERDICT",
        "qu'est-ce que je regarde ?","mouvement réduit","LANGUE",
        "plus grande perte mono : ","LATENCE DU GRAVE",
        "aucune tonalité estimée — pas de chromagramme à corréler","% du temps",
        "HISTOGRAMME SHORT-TERM · LUFS","réf. dynamique ",
        "dB depuis le RESET\nTP max − intégré","événements au-dessus de ",
        "3 dernières min","cible",
        "vous êtes "," dB en dessous",
        " dB en dessous · ils ne vous montent pas","échantillons polaires · rayon = niveau, bord 0 dBFS, centre −60",
        "pas de référence · glissez un fichier","comparaison du tilt à loudness égal · le delta somme à zéro",
        "réf. ±",
        "PALETTE",
        "rayon = niveau relatif à la direction la plus forte",
        "référence sous la plage (−42 LU) dans cette bande"," bins à cette FFT",
        "MOM","COURT"
    }};
    return t;
}

inline const Table& tableDe()
{
    static const Table t { "de", "Deutsch", NoteNaming::german, {
        "FENSTER","KANAL","ABKLINGEN","LINIEN","NEIGUNG","BEREICH","ÜBERLAPPUNG","FLANKE","MITTELUNG","HALTEN","GLÄTTUNG",
        "ZURÜCKSETZEN","PAUSE","FORTSETZEN","LADEN","ENTFERNEN","ZEILE","BÄNDER","ZIEL","VERLAUF","FREI","AUS","EIN","jetzt",
        "kein Signal","kein Plattform-Ziel","messe","analysiere","Analyse im Rückstand","Schwelle","Ereignisse","letzte 10 Min",
        "INTEGRATED","SHORT-TERM","MOMENTARY","LOUDNESS RANGE","TRUE PEAK","MAX SHORT-TERM","MAX MOMENTARY",
        "über dem Ziel","unter dem Ziel","trifft das Ziel","sie regeln dich herunter","sie regeln dich herauf","PROGRAMM",
        "DYNAMIK","PLR","PSR","HISTOGRAMM","CLIPS","CLIP-SCHWELLE",
        "SPEKTRUM","FFT","FENSTER","PEAK HOLD","1/3 OKT","BARK","FREI","AUFLÖSUNG",
        "keine Bins bei dieser Auflösung","pro Oktave",
        "KORRELATION","BREITE","BALANCE","MONOKOMPATIBILITÄT","MONO-VERLUST","LISSAJOUS","POLAR-SAMPLES","POLARPEGEL",
        "TRIGGER","OSZILLOSKOP","phasenverkehrt","MONO","L","R","M","S","keine Korrelation","Rand = Spitze",
        "SPITZEN-ABKLINGEN","in Phase, oben","phasenverkehrt, unten",
        "PRO BAND","KOHÄRENZ","PHASE","gemessene Bänder","wenige Bins in diesem Band",
        "TONART","KONFIDENZ","CHROMA","OKTAVE","TONKLASSE","geschätzt","TONIKA",
        "PANORAMA-RICHTUNG","Energie nach Panorama-Richtung","keine Lokalisation","TIEFE","SPUREN","VORN",
        "TONALE BALANCE","REFERENZ","Referenzdatei wählen","Referenz nicht gefunden: ","keine Referenz in diesem Band","DELTA",
        "Dynamikbereich der Referenz",
        "LOUDNESS","DYNAMIK","SPEKTRUM","SPEKTROGRAMM","WASSERFALL","CQT","SPIRALE",
        "SCOPE","BANDKORRELATION","STEREO-SPEKTROGRAMM","FELD","TONALE BALANCE","URTEIL",
        "Was sehe ich hier?","reduzierte Bewegung","SPRACHE",
        "größter Mono-Verlust: ","BASS-LATENZ",
        "keine Tonart geschätzt — kein Chromagramm zum Korrelieren","% der Zeit",
        "SHORT-TERM-HISTOGRAMM · LUFS","dyn. Referenz ",
        "dB seit dem RESET\nTP max − Integrated","Ereignisse über ",
        "letzte 3 Min","Ziel",
        "du bist "," dB darunter",
        " dB darunter · sie regeln dich nicht herauf","Polar-Samples · Radius = Pegel, Rand 0 dBFS, Mitte −60",
        "keine Referenz · Datei hierher ziehen","Tilt-Vergleich bei gleicher Lautheit · das Delta summiert sich zu null",
        "Ref. ±",
        "PALETTE",
        "Radius = Pegel relativ zur stärksten Richtung",
        "Referenz unter dem Anzeigebereich (−42 LU) in diesem Band"," Bins bei dieser FFT",
        "MOM","KURZ"
    }};
    return t;
}

inline const Table& tableIt()
{
    static const Table t { "it", "Italiano", NoteNaming::solfege, {
        "FINESTRA","CANALE","DECADIMENTO","LINEE","INCLINAZIONE","GAMMA","SOVRAPPOSIZIONE","PENDENZA","MEDIA","TENUTA","LISCIATURA",
        "AZZERA","PAUSA","RIPRENDI","CARICA","RIMUOVI","RIGA","BANDE","OBIETTIVO","STORICO","LIBERO","OFF","ON","adesso",
        "nessun segnale","nessun obiettivo di piattaforma","misurazione","analisi","analisi in ritardo","soglia","eventi","ultimi 10 min",
        "INTEGRATO","SHORT-TERM","MOMENTARY","LOUDNESS RANGE","TRUE PEAK","MAX SHORT-TERM","MAX MOMENTARY",
        "sopra l'obiettivo","sotto l'obiettivo","corrisponde all'obiettivo","ti abbassano","ti alzano","PROGRAMMA",
        "DINAMICA","PLR","PSR","ISTOGRAMMA","CLIP","SOGLIA DI CLIP",
        "SPETTRO","FFT","FINESTRA","PEAK HOLD","1/3 OTT","BARK","LIBERO","RISOLUZIONE",
        "nessun bin a questa risoluzione","per ottava",
        "CORRELAZIONE","LARGHEZZA","BILANCIAMENTO","COMPATIBILITÀ MONO","PERDITA MONO","LISSAJOUS","CAMPIONI POLARI","LIVELLO POLARE",
        "TRIGGER","OSCILLOSCOPIO","fuori fase","MONO","L","R","M","S","nessuna correlazione","bordo = picco",
        "DECAD. PICCO","in fase, sopra","fuori fase, sotto",
        "PER BANDA","COERENZA","FASE","bande misurate","pochi bin in questa banda",
        "TONALITÀ","CONFIDENZA","CROMA","OTTAVA","CLASSE DI NOTA","stimato","TONICA",
        "DIREZIONE DI PAN","energia per direzione di pan","non è localizzazione","PROFONDITÀ","SCIE","FRONTE",
        "BILANCIAMENTO TONALE","RIFERIMENTO","Scegli un file di riferimento","riferimento non trovato: ","nessun riferimento in questa banda","DELTA",
        "gamma dinamica del riferimento",
        "LOUDNESS","DINAMICA","SPETTRO","SPETTROGRAMMA","CASCATA","CQT","SPIRALE",
        "SCOPE","CORRELAZIONE PER BANDA","SPETTROGRAMMA STEREO","CAMPO","BILANCIAMENTO TONALE","VERDETTO",
        "cosa sto guardando?","movimento ridotto","LINGUA",
        "maggior perdita mono: ","LATENZA DEL BASSO",
        "nessuna tonalità stimata — nessun cromagramma da correlare","% del tempo",
        "ISTOGRAMMA SHORT-TERM · LUFS","rif. dinamico ",
        "dB dal RESET\nTP max − integrato","eventi sopra ",
        "ultimi 3 min","obiettivo",
        "sei "," dB sotto",
        " dB sotto · non ti alzano","campioni polari · raggio = livello, bordo 0 dBFS, centro −60",
        "nessun riferimento · trascina un file","confronto del tilt a loudness uguale · il delta somma a zero",
        "rif. ±",
        "TAVOLOZZA",
        "raggio = livello relativo alla direzione più forte",
        "riferimento sotto l'intervallo (−42 LU) in questa banda"," bin a questa FFT",
        "MOM","BREVE"
    }};
    return t;
}

// El catálogo. Agregar un idioma = agregar su tabla y meterla acá. Nada más.
inline const std::vector<const Table*>& tables()
{
    static const std::vector<const Table*> v { &tableEn(), &tableEs(), &tablePt(), &tableFr(),
                                               &tableDe(), &tableIt() };
    return v;
}

inline const Table* tableFor (const juce::String& code)
{
    const auto c = code.trim().toLowerCase();
    for (const auto* t : tables())
        if (c == t->code) return t;
    return nullptr;
}

// Los códigos presentes en la tabla, en orden. Es lo que lista el selector de idioma: si mañana hay
// japonés, aparece solo.
inline juce::StringArray availableLanguages()
{
    juce::StringArray a;
    for (const auto* t : tables()) a.add (t->code);
    return a;
}

// Con qué convención nombra las notas ese idioma. Ver el bloque de `NoteNaming`. Un código que esta
// versión no tiene cae en letras, que es el default de D-50 (inglés).
inline NoteNaming noteNamingOf (const juce::String& code)
{
    const auto* t = tableFor (code);
    return t != nullptr ? t->notes : NoteNaming::letters;
}

inline juce::String endonymOf (const juce::String& code)
{
    const auto* t = tableFor (code);
    return t != nullptr ? juce::String::fromUTF8 (t->endonym) : code;
}

// ========================================================================================================
// LA CONSULTA. Fallback por CLAVE: idioma pedido → inglés. Una entrada nula o vacía cuenta como ausente.
// ========================================================================================================
// EL FALLBACK, en una función que se puede llamar (56b, M1 del revisor del 56). Antes la regla vivía sólo
// adentro de `get()` y el test la re-implementaba en una lambda sobre una tabla fabricada: comprobaba que
// SU copia hacía lo correcto, no que el plugin lo hiciera. Ahora es esta función, y el test la llama con
// una tabla incompleta de verdad.
//
// Devuelve el valor de `t` si la clave está y no está vacía; si no, el de inglés. Una entrada nula o
// vacía cuenta como ausente: una etiqueta en blanco en un medidor es peor que una en el idioma
// equivocado — el usuario no sabe si es un bug del plugin o un valor que no existe.
inline juce::String getFrom (const Table& t, Key k)
{
    const auto i = (size_t) (int) k;
    if ((int) k < 0 || (int) k >= kNumKeys) return {};

    const auto* v = t.s[i];
    if (v != nullptr && *v != '\0') return juce::String::fromUTF8 (v);
    return juce::String::fromUTF8 (tableEn().s[i]);
}

inline juce::String get (Key k, const juce::String& language)
{
    if ((int) k < 0 || (int) k >= kNumKeys) return {};
    const auto* t = tableFor (language);
    return getFrom (t != nullptr ? *t : tableEn(), k);
}

// El idioma vigente, leído del ValueTree del APVTS. Si la propiedad no está o trae un código que no
// existe, es inglés: el default de D-50, y nunca una pantalla vacía.
inline juce::String languageOf (const juce::ValueTree& state)
{
    const auto code = state.getProperty (kLanguageProperty, juce::String (kDefaultLanguage)).toString();
    return tableFor (code) != nullptr ? code.trim().toLowerCase() : juce::String (kDefaultLanguage);
}

inline void setLanguage (juce::ValueTree& state, const juce::String& code)
{
    if (tableFor (code) != nullptr)
        state.setProperty (kLanguageProperty, code.trim().toLowerCase(), nullptr);
}
}
