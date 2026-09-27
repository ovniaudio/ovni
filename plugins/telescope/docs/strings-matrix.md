# TELESCOPE — matriz de idiomas / language matrix

> **Generado, no escrito a mano.** Sale de `OvniTelescopeTests "[strings-dump]"`, que lee las
> tablas del código (`source/lenses/Strings.h` y `source/data/Rules.h`) y las vuelca acá. El test
> de ctest `telescope-strings-matrix` lo regenera a un temporal y compara: si alguien agrega una
> clave o toca una traducción y no regenera este archivo, se pone rojo.
>
> **Se muestra el valor CRUDO de cada tabla, no el resuelto.** En pantalla, una clave que le falta
> a un idioma sale en inglés (fallback por clave). Acá eso se marca `— (fallback en)`: pintar el
> inglés en la celda del portugués haría pasar por traducido lo que no lo está.

## Estado por idioma

| Código | Idioma | Revisión |
|---|---|---|
| `en` | English | **revisado** |
| `es` | Español | **revisado** |
| `pt` | Português | traducido, **pendiente de revisión de hablante nativo** |
| `fr` | Français | traducido, **pendiente de revisión de hablante nativo** |
| `de` | Deutsch | traducido, **pendiente de revisión de hablante nativo** |
| `it` | Italiano | traducido, **pendiente de revisión de hablante nativo** |

## 1 · Rótulos de la interfaz — `source/lenses/Strings.h`

150 claves × 6 idiomas.

| Clave | en | es | pt ⚠ | fr ⚠ | de ⚠ | it ⚠ |
|---|---|---|---|---|---|---|
| `window` | WINDOW | VENTANA | JANELA | FENÊTRE | FENSTER | FINESTRA |
| `channel` | CHANNEL | CANAL | CANAL | CANAL | KANAL | CANALE |
| `decay` | DECAY | DECAIMIENTO | DECAIMENTO | DÉCROISSANCE | ABKLINGEN | DECADIMENTO |
| `lines` | LINES | LÍNEAS | LINHAS | LIGNES | LINIEN | LINEE |
| `tilt` | TILT | INCLINACIÓN | INCLINAÇÃO | INCLINAISON | NEIGUNG | INCLINAZIONE |
| `range` | RANGE | RANGO | FAIXA | PLAGE | BEREICH | GAMMA |
| `overlap` | OVERLAP | SOLAPE | SOBREPOSIÇÃO | RECOUVREMENT | ÜBERLAPPUNG | SOVRAPPOSIZIONE |
| `slope` | SLOPE | PENDIENTE | INCLINAÇÃO | PENTE | FLANKE | PENDENZA |
| `average` | AVERAGE | PROMEDIO | MÉDIA | MOYENNE | MITTELUNG | MEDIA |
| `hold` | HOLD | RETENCIÓN | HOLD | MAINTIEN | HALTEN | TENUTA |
| `smoothing` | SMOOTHING | SUAVIZADO | SUAVIZAÇÃO | LISSAGE | GLÄTTUNG | LISCIATURA |
| `reset` | RESET | REINICIAR | REINICIAR | RÉINITIALISER | ZURÜCKSETZEN | AZZERA |
| `pause` | PAUSE | PAUSA | PAUSA | PAUSE | PAUSE | PAUSA |
| `resume` | RESUME | SEGUIR | CONTINUAR | REPRENDRE | FORTSETZEN | RIPRENDI |
| `load` | LOAD | CARGAR | CARREGAR | CHARGER | LADEN | CARICA |
| `remove` | REMOVE | QUITAR | REMOVER | RETIRER | ENTFERNEN | RIMUOVI |
| `row` | ROW | FILA | LINHA | LIGNE | ZEILE | RIGA |
| `bands` | BANDS | BANDAS | BANDAS | BANDES | BÄNDER | BANDE |
| `target` | TARGET | OBJETIVO | ALVO | CIBLE | ZIEL | OBIETTIVO |
| `history` | HISTORY | HISTORIA | HISTÓRICO | HISTORIQUE | VERLAUF | STORICO |
| `free` | FREE | LIBRE | LIVRE | LIBRE | FREI | LIBERO |
| `off` | OFF | OFF | OFF | OFF | AUS | OFF |
| `on` | ON | ON | ON | ON | EIN | ON |
| `now` | now | ahora | agora | maintenant | jetzt | adesso |
| `noSignal` | no signal | sin señal | sem sinal | pas de signal | kein Signal | nessun segnale |
| `noTarget` | no platform target | sin objetivo de plataforma | sem alvo de plataforma | pas de cible de plateforme | kein Plattform-Ziel | nessun obiettivo di piattaforma |
| `measuring` | measuring | midiendo | medindo | mesure en cours | messe | misurazione |
| `analysing` | analysing | analizando | analisando | analyse en cours | analysiere | analisi |
| `analysisBehind` | analysis behind | análisis atrasado | análise atrasada | analyse en retard | Analyse im Rückstand | analisi in ritardo |
| `threshold` | threshold | umbral | limiar | seuil | Schwelle | soglia |
| `events` | events | eventos | eventos | événements | Ereignisse | eventi |
| `lastMinutes` | last 10 min | últimos 10 min | últimos 10 min | 10 dernières min | letzte 10 Min | ultimi 10 min |
| `integrated` | INTEGRATED | INTEGRADO | INTEGRADO | INTÉGRÉ | INTEGRATED | INTEGRATO |
| `shortTerm` | SHORT-TERM | SHORT-TERM | SHORT-TERM | SHORT-TERM | SHORT-TERM | SHORT-TERM |
| `momentary` | MOMENTARY | MOMENTARY | MOMENTARY | MOMENTARY | MOMENTARY | MOMENTARY |
| `loudnessRange` | LOUDNESS RANGE | LOUDNESS RANGE | LOUDNESS RANGE | LOUDNESS RANGE | LOUDNESS RANGE | LOUDNESS RANGE |
| `truePeak` | TRUE PEAK | TRUE PEAK | TRUE PEAK | TRUE PEAK | TRUE PEAK | TRUE PEAK |
| `maxShortTerm` | MAX SHORT-TERM | MÁX SHORT-TERM | MÁX SHORT-TERM | MAX SHORT-TERM | MAX SHORT-TERM | MAX SHORT-TERM |
| `maxMomentary` | MAX MOMENTARY | MÁX MOMENTARY | MÁX MOMENTARY | MAX MOMENTARY | MAX MOMENTARY | MAX MOMENTARY |
| `aboveTarget` | above target | por encima del objetivo | acima do alvo | au-dessus de la cible | über dem Ziel | sopra l'obiettivo |
| `belowTarget` | below target | por debajo del objetivo | abaixo do alvo | en dessous de la cible | unter dem Ziel | sotto l'obiettivo |
| `matchesTarget` | matches the target | coincide con el objetivo | coincide com o alvo | correspond à la cible | trifft das Ziel | corrisponde all'obiettivo |
| `theyTurnYouDown` | they turn you down | te bajan | abaixam você | ils vous baissent | sie regeln dich herunter | ti abbassano |
| `theyTurnYouUp` | they turn you up | te suben | levantam você | ils vous montent | sie regeln dich herauf | ti alzano |
| `program` | PROGRAMME | PROGRAMA | PROGRAMA | PROGRAMME | PROGRAMM | PROGRAMMA |
| `dynamics` | DYNAMICS | DINÁMICA | DINÂMICA | DYNAMIQUE | DYNAMIK | DINAMICA |
| `crestPlr` | PLR | PLR | PLR | PLR | PLR | PLR |
| `crestPsr` | PSR | PSR | PSR | PSR | PSR | PSR |
| `histogram` | HISTOGRAM | HISTOGRAMA | HISTOGRAMA | HISTOGRAMME | HISTOGRAMM | ISTOGRAMMA |
| `clips` | CLIPS | CLIPS | CLIPS | CLIPS | CLIPS | CLIP |
| `clipThreshold` | CLIP THRESHOLD | UMBRAL DE CLIP | LIMIAR DE CLIP | SEUIL DE CLIP | CLIP-SCHWELLE | SOGLIA DI CLIP |
| `spectrum` | SPECTRUM | ESPECTRO | ESPECTRO | SPECTRE | SPEKTRUM | SPETTRO |
| `fftSize` | FFT | FFT | FFT | FFT | FFT | FFT |
| `windowFn` | WINDOW FN | VENTANA | JANELA | FENÊTRE | FENSTER | FINESTRA |
| `peakHold` | PEAK HOLD | PEAK HOLD | PEAK HOLD | PEAK HOLD | PEAK HOLD | PEAK HOLD |
| `bandsThirdOctave` | 1/3 OCT | 1/3 OCT | 1/3 OCT | 1/3 OCT | 1/3 OKT | 1/3 OTT |
| `bandsBark` | BARK | BARK | BARK | BARK | BARK | BARK |
| `bandsFree` | FREE | LIBRE | LIVRE | LIBRE | FREI | LIBERO |
| `resolution` | RESOLUTION | RESOLUCIÓN | RESOLUÇÃO | RÉSOLUTION | AUFLÖSUNG | RISOLUZIONE |
| `noBinsHere` | no bins at this resolution | sin bins a esta resolución | sem bins nesta resolução | aucun bin à cette résolution | keine Bins bei dieser Auflösung | nessun bin a questa risoluzione |
| `perOctave` | per octave | por octava | por oitava | par octave | pro Oktave | per ottava |
| `correlation` | CORRELATION | CORRELACIÓN | CORRELAÇÃO | CORRÉLATION | KORRELATION | CORRELAZIONE |
| `width` | WIDTH | ANCHO | LARGURA | LARGEUR | BREITE | LARGHEZZA |
| `balance` | BALANCE | BALANCE | BALANÇO | BALANCE | BALANCE | BILANCIAMENTO |
| `monoCompatibility` | MONO COMPATIBILITY | COMPATIBILIDAD MONO | COMPATIBILIDADE MONO | COMPATIBILITÉ MONO | MONOKOMPATIBILITÄT | COMPATIBILITÀ MONO |
| `monoLoss` | MONO LOSS | PÉRDIDA MONO | PERDA MONO | PERTE MONO | MONO-VERLUST | PERDITA MONO |
| `lissajous` | LISSAJOUS | LISSAJOUS | LISSAJOUS | LISSAJOUS | LISSAJOUS | LISSAJOUS |
| `polar` | POLAR SAMPLE | MUESTRAS POLARES | AMOSTRAS POLARES | ÉCHANTILLONS POLAIRES | POLAR-SAMPLES | CAMPIONI POLARI |
| `hemisphere` | POLAR LEVEL | NIVEL POLAR | NÍVEL POLAR | NIVEAU POLAIRE | POLARPEGEL | LIVELLO POLARE |
| `trigger` | TRIGGER | TRIGGER | TRIGGER | DÉCLENCHEMENT | TRIGGER | TRIGGER |
| `oscilloscope` | OSCILLOSCOPE | OSCILOSCOPIO | OSCILOSCÓPIO | OSCILLOSCOPE | OSZILLOSKOP | OSCILLOSCOPIO |
| `outOfPhase` | out of phase | fuera de fase | fora de fase | hors phase | phasenverkehrt | fuori fase |
| `mono` | MONO | MONO | MONO | MONO | MONO | MONO |
| `left` | L | L | L | G | L | L |
| `right` | R | R | R | D | R | R |
| `mid` | M | M | M | M | M | M |
| `side` | S | S | S | S | S | S |
| `noCorrelation` | no correlation | sin correlación | sem correlação | pas de corrélation | keine Korrelation | nessuna correlazione |
| `peakEdge` | edge = peak | borde = pico | borda = pico | bord = crête | Rand = Spitze | bordo = picco |
| `hemisphereDecay` | PEAK DECAY | DECAIM. PICO | DECAIM. PICO | DÉCR. CRÊTE | SPITZEN-ABKLINGEN | DECAD. PICCO |
| `inPhaseAbove` | in phase, above | en fase, arriba | em fase, acima | en phase, au-dessus | in Phase, oben | in fase, sopra |
| `outOfPhaseBelow` | out of phase, below | fuera de fase, abajo | fora de fase, abaixo | hors phase, en dessous | phasenverkehrt, unten | fuori fase, sotto |
| `perBand` | PER BAND | POR BANDA | POR BANDA | PAR BANDE | PRO BAND | PER BANDA |
| `coherence` | COHERENCE | COHERENCIA | COERÊNCIA | COHÉRENCE | KOHÄRENZ | COERENZA |
| `phase` | PHASE | FASE | FASE | PHASE | PHASE | FASE |
| `measuredBands` | bands measured | bandas medidas | bandas medidas | bandes mesurées | gemessene Bänder | bande misurate |
| `fewBins` | few bins in this band | pocos bins en esta banda | poucos bins nesta banda | peu de bins dans cette bande | wenige Bins in diesem Band | pochi bin in questa banda |
| `key` | KEY | TONALIDAD | TONALIDADE | TONALITÉ | TONART | TONALITÀ |
| `confidence` | CONFIDENCE | CONFIANZA | CONFIANÇA | CONFIANCE | KONFIDENZ | CONFIDENZA |
| `chroma` | CHROMA | CROMA | CROMA | CHROMA | CHROMA | CROMA |
| `octave` | OCTAVE | OCTAVA | OITAVA | OCTAVE | OKTAVE | OTTAVA |
| `noteClass` | NOTE CLASS | CLASE DE NOTA | CLASSE DE NOTA | CLASSE DE NOTE | TONKLASSE | CLASSE DI NOTA |
| `estimated` | estimated | estimado | estimado | estimé | geschätzt | stimato |
| `tonic` | TONIC | TÓNICA | TÔNICA | TONIQUE | TONIKA | TONICA |
| `panDirection` | PAN DIRECTION | DIRECCIÓN DE PANEO | DIREÇÃO DE PAN | DIRECTION DE PAN | PANORAMA-RICHTUNG | DIREZIONE DI PAN |
| `energyByDirection` | energy by pan direction | energía por dirección de paneo | energia por direção de pan | énergie par direction de pan | Energie nach Panorama-Richtung | energia per direzione di pan |
| `notLocalisation` | not localisation | no es localización | não é localização | ce n'est pas de la localisation | keine Lokalisation | non è localizzazione |
| `depth` | DEPTH | PROFUNDIDAD | PROFUNDIDADE | PROFONDEUR | TIEFE | PROFONDITÀ |
| `trails` | TRAILS | ESTELAS | RASTROS | TRAÎNÉES | SPUREN | SCIE |
| `frontLine` | FRONT | FRENTE | FRENTE | AVANT | VORN | FRONTE |
| `tonalBalance` | TONAL BALANCE | BALANCE TONAL | BALANÇO TONAL | ÉQUILIBRE TONAL | TONALE BALANCE | BILANCIAMENTO TONALE |
| `referenceLabel` | REFERENCE | REFERENCIA | REFERÊNCIA | RÉFÉRENCE | REFERENZ | RIFERIMENTO |
| `chooseReference` | Choose a reference file | Elegí un archivo de referencia | Escolha um arquivo de referência | Choisissez un fichier de référence | Referenzdatei wählen | Scegli un file di riferimento |
| `referenceMissing` | reference not found:  | referencia no encontrada:  | referência não encontrada:  | référence introuvable :  | Referenz nicht gefunden:  | riferimento non trovato:  |
| `noReferenceInBand` | no reference in this band | sin referencia en esta banda | sem referência nesta banda | pas de référence dans cette bande | keine Referenz in diesem Band | nessun riferimento in questa banda |
| `delta` | DELTA | DELTA | DELTA | DELTA | DELTA | DELTA |
| `dynamicRefRange` | dynamic reference range | rango dinámico de la referencia | faixa dinâmica da referência | plage dynamique de la référence | Dynamikbereich der Referenz | gamma dinamica del riferimento |
| `lensLoudness` | LOUDNESS | LOUDNESS | LOUDNESS | LOUDNESS | LOUDNESS | LOUDNESS |
| `lensDynamics` | DYNAMICS | DINÁMICA | DINÂMICA | DYNAMIQUE | DYNAMIK | DINAMICA |
| `lensSpectrum` | SPECTRUM | ESPECTRO | ESPECTRO | SPECTRE | SPEKTRUM | SPETTRO |
| `lensSpectrogram` | SPECTROGRAM | ESPECTROGRAMA | ESPECTROGRAMA | SPECTROGRAMME | SPEKTROGRAMM | SPETTROGRAMMA |
| `lensWaterfall` | WATERFALL | CASCADA | CASCATA | CASCADE | WASSERFALL | CASCATA |
| `lensCqt` | CQT | CQT | CQT | CQT | CQT | CQT |
| `lensSpiral` | SPIRAL | ESPIRAL | ESPIRAL | SPIRALE | SPIRALE | SPIRALE |
| `lensScope` | SCOPE | SCOPE | SCOPE | SCOPE | SCOPE | SCOPE |
| `lensBandCorrelation` | BAND CORRELATION | CORRELACIÓN POR BANDA | CORRELAÇÃO POR BANDA | CORRÉLATION PAR BANDE | BANDKORRELATION | CORRELAZIONE PER BANDA |
| `lensStereoSpectrogram` | STEREO SPECTROGRAM | ESPECTROGRAMA ESTÉREO | ESPECTROGRAMA ESTÉREO | SPECTROGRAMME STÉRÉO | STEREO-SPEKTROGRAMM | SPETTROGRAMMA STEREO |
| `lensField` | FIELD | CAMPO | CAMPO | CHAMP | FELD | CAMPO |
| `lensTonalBalance` | TONAL BALANCE | BALANCE TONAL | BALANÇO TONAL | ÉQUILIBRE TONAL | TONALE BALANCE | BILANCIAMENTO TONALE |
| `lensVerdict` | VERDICT | VEREDICTO | VEREDITO | VERDICT | URTEIL | VERDETTO |
| `whatAmILooking` | what am I looking at? | ¿qué estoy mirando? | o que estou vendo? | qu'est-ce que je regarde ? | Was sehe ich hier? | cosa sto guardando? |
| `reducedMotion` | reduced motion | menos movimiento | menos movimento | mouvement réduit | reduzierte Bewegung | movimento ridotto |
| `language` | LANGUAGE | IDIOMA | IDIOMA | LANGUE | SPRACHE | LINGUA |
| `mostMonoLoss` | most mono loss:  | más pérdida mono:  | maior perda mono:  | plus grande perte mono :  | größter Mono-Verlust:  | maggior perdita mono:  |
| `bassLatency` | BASS LATENCY | LATENCIA DEL GRAVE | LATÊNCIA DO GRAVE | LATENCE DU GRAVE | BASS-LATENZ | LATENZA DEL BASSO |
| `noKeyEstimated` | no key estimated — no chromagram to correlate | sin tonalidad estimada — no hay cromagrama que correlacionar | sem tonalidade estimada — não há cromagrama para correlacionar | aucune tonalité estimée — pas de chromagramme à corréler | keine Tonart geschätzt — kein Chromagramm zum Korrelieren | nessuna tonalità stimata — nessun cromagramma da correlare |
| `ofTheTime` | % of the time | % del tiempo | % do tempo | % du temps | % der Zeit | % del tempo |
| `histogramShortTerm` | SHORT-TERM HISTOGRAM · LUFS | HISTOGRAMA DE SHORT-TERM · LUFS | HISTOGRAMA DE SHORT-TERM · LUFS | HISTOGRAMME SHORT-TERM · LUFS | SHORT-TERM-HISTOGRAMM · LUFS | ISTOGRAMMA SHORT-TERM · LUFS |
| `dynamicRefShort` | dynamic ref.  | ref. dinámica  | ref. dinâmica  | réf. dynamique  | dyn. Referenz  | rif. dinamico  |
| `plrSinceReset` | dB since RESET TP max − integrated | dB desde el RESET TP máx − integrado | dB desde o RESET TP máx − integrado | dB depuis le RESET TP max − intégré | dB seit dem RESET TP max − Integrated | dB dal RESET TP max − integrato |
| `eventsAbove` | events above  | eventos sobre  | eventos acima de  | événements au-dessus de  | Ereignisse über  | eventi sopra  |
| `lastMinutes3` | last 3 min | últimos 3 min | últimos 3 min | 3 dernières min | letzte 3 Min | ultimi 3 min |
| `targetLower` | target | objetivo | alvo | cible | Ziel | obiettivo |
| `youAre` | you are  | estás  | você está  | vous êtes  | du bist  | sei  |
| `dbBelow` |  dB below |  dB por debajo |  dB abaixo |  dB en dessous |  dB darunter |  dB sotto |
| `dbBelowNoRaise` |  dB below · they don't turn you up |  dB por debajo · no te suben |  dB abaixo · não levantam você |  dB en dessous · ils ne vous montent pas |  dB darunter · sie regeln dich nicht herauf |  dB sotto · non ti alzano |
| `polarLegend` | polar sample · radius = level, edge 0 dBFS, centre −60 | muestras polares · radio = nivel, borde 0 dBFS, centro −60 | amostras polares · raio = nível, borda 0 dBFS, centro −60 | échantillons polaires · rayon = niveau, bord 0 dBFS, centre −60 | Polar-Samples · Radius = Pegel, Rand 0 dBFS, Mitte −60 | campioni polari · raggio = livello, bordo 0 dBFS, centro −60 |
| `noReferenceDrag` | no reference · drag a file in | sin referencia · arrastrá un archivo | sem referência · arraste um arquivo | pas de référence · glissez un fichier | keine Referenz · Datei hierher ziehen | nessun riferimento · trascina un file |
| `tiltEqualLoudness` | comparing the tilt at equal loudness · the delta sums to zero | comparando el tilt a igual loudness · el delta suma cero | comparando o tilt com o mesmo loudness · o delta soma zero | comparaison du tilt à loudness égal · le delta somme à zéro | Tilt-Vergleich bei gleicher Lautheit · das Delta summiert sich zu null | confronto del tilt a loudness uguale · il delta somma a zero |
| `refPlusMinus` | ref. ± | ref. ± | ref. ± | réf. ± | Ref. ± | rif. ± |
| `palette` | PALETTE | PALETA | PALETA | PALETTE | PALETTE | TAVOLOZZA |
| `hemiLegend` | radius = level relative to the strongest direction | radio = nivel relativo a la dirección más fuerte | raio = nível relativo à direção mais forte | rayon = niveau relatif à la direction la plus forte | Radius = Pegel relativ zur stärksten Richtung | raggio = livello relativo alla direzione più forte |
| `refBelowRange` | reference below the plot range (−42 LU) in this band | referencia por debajo del rango (−42 LU) en esta banda | referência abaixo da faixa (−42 LU) nesta banda | référence sous la plage (−42 LU) dans cette bande | Referenz unter dem Anzeigebereich (−42 LU) in diesem Band | riferimento sotto l'intervallo (−42 LU) in questa banda |
| `binsAtThisFft` |  bins at this FFT |  bins a esta FFT |  bins nesta FFT |  bins à cette FFT |  Bins bei dieser FFT |  bin a questa FFT |
| `momentaryBar` | MOM | MOM | MOM | MOM | MOM | MOM |
| `shortTermBar` | SHORT | CORTO | CURTO | COURT | KURZ | BREVE |
| `theme` | THEME | TEMA | TEMA | THÈME | DESIGN | TEMA |
| `themeDark` | Dark | Oscuro | Escuro | Sombre | Dunkel | Scuro |
| `themeLight` | Light | Claro | Claro | Clair | Hell | Chiaro |
| `refSpanHint` | drag to pick a section · double-click: whole file | arrastrá para elegir un tramo · doble clic: el archivo entero | arraste para escolher um trecho · clique duplo: o arquivo inteiro | glissez pour choisir un passage · double-clic : le fichier entier | ziehen, um einen Abschnitt zu wählen · Doppelklick: ganze Datei | trascina per scegliere un tratto · doppio clic: il file intero |

## 2 · Frases de VERDICT — `source/data/Rules.h`

96 claves × 6 idiomas.

| Clave | en | es | pt ⚠ | fr ⚠ | de ⚠ | it ⚠ |
|---|---|---|---|---|---|---|
| `ui.reset` | RESET | RESET | RESET | RESET | RESET | RESET |
| `ui.reset.value` | from 0 | desde 0 | do 0 | depuis 0 | ab 0 | da 0 |
| `ui.mode` | MODE | MODO | MODO | MODE | MODUS | MODO |
| `ui.file` | FILE | ARCHIVO | ARQUIVO | FICHIER | DATEI | FILE |
| `ui.file.value` | LOAD | CARGAR | CARREGAR | CHARGER | LADEN | CARICA |
| `ui.language` | LANGUAGE | IDIOMA | IDIOMA | LANGUE | SPRACHE | LINGUA |
| `ui.drop` | drag a file here, or press LOAD | arrastrá un archivo acá, o tocá CARGAR | arraste um arquivo aqui, ou toque CARREGAR | glissez un fichier ici, ou appuyez sur CHARGER | Datei hierher ziehen, oder LADEN drücken | trascina un file qui, o premi CARICA |
| `ui.nosecs` | no complete seconds yet: let the track play from the start, or use FILE | todavía no hay segundos completos: dejá sonar el tema desde el principio, o usá ARCHIVO | ainda não há segundos completos: deixe a faixa tocar desde o início, ou use ARQUIVO | pas encore de secondes complètes : laissez le morceau jouer depuis le début, ou utilisez FICHIER | noch keine vollständigen Sekunden: den Track von Anfang an abspielen, oder DATEI benutzen | non ci sono ancora secondi completi: fai suonare il brano dall'inizio, o usa FILE |
| `ui.waiting` | waiting for audio | esperando audio | aguardando áudio | en attente d'audio | warten auf Audio | in attesa di audio |
| `ui.silent` | no audio above -70 LUFS in this file | sin audio sobre -70 LUFS en este archivo | sem áudio acima de -70 LUFS neste arquivo | aucun son au-dessus de -70 LUFS dans ce fichier | kein Audio über -70 LUFS in dieser Datei | nessun audio sopra -70 LUFS in questo file |
| `ui.choose` | Choose the track VERDICT has to analyse | Elegí el tema que VERDICT tiene que analizar | Escolha a faixa que o VERDICT tem que analisar | Choisissez le morceau que VERDICT doit analyser | Wähle den Track, den VERDICT analysieren soll | Scegli il brano che VERDICT deve analizzare |
| `ui.analysing` | analysing  | analizando  | analisando  | analyse en cours  | analysiere  | analisi  |
| `ui.unreadable` | could not read:  | no se pudo leer:  | não foi possível ler:  | lecture impossible :  | konnte nicht gelesen werden:  | impossibile leggere:  |
| `ui.notafile` | that is not a file | eso no es un archivo | isso não é um arquivo | ce n'est pas un fichier | das ist keine Datei | quello non è un file |
| `ui.notaudio` | " is not audio ( | " no es audio ( | " não é áudio ( | " n'est pas de l'audio ( | " ist kein Audio ( | " non è audio ( |
| `section.within` | Within range | Dentro de rango | Dentro da faixa | Dans la plage | Im Bereich | Nel range |
| `section.feel` | How it will feel | Cómo se va a sentir | Como vai soar | Comment ça va sonner | Wie es sich anfühlen wird | Come suonerà |
| `section.translate` | Where it translates | Dónde traduce | Onde traduz | Où ça se traduit | Wo es übersetzt | Dove traduce |
| `section.missing` | What to check, and where | Qué revisar y dónde | O que verificar, e onde | Quoi vérifier, et où | Was prüfen, und wo | Cosa controllare, e dove |
| `footer` | Measurement, not taste. Device checks are generic. Re-run the analysis after every change. | Medición, no gusto. Chequeos por dispositivo genéricos. Rehacé el análisis tras cada cambio. | Medição, não gosto. Verificações por dispositivo genéricas. Refaça a análise após cada mudanca. | Mesure, pas goût. Vérifications par appareil génériques. Relancez l'analyse après chaque modification. | Messung, kein Geschmack. Geräteprüfungen sind generisch. Analyse nach jeder Änderung neu laufen lassen. | Misura, non gusto. Controlli per dispositivo generici. Rifai l'analisi dopo ogni modifica. |
| `none` | Nothing outside the ranges of these rules. They measure; what they can't hear is yours. | Nada fuera de rango en estas reglas. Miden; lo que no escuchan es tuyo. | Nada fora da faixa nestas regras. Elas medem; o que não escutam é seu. | Rien hors des plages de ces règles. Elles mesurent ; ce qu'elles n'entendent pas vous appartient. | Nichts außerhalb der Bereiche dieser Regeln. Sie messen; was sie nicht hören, gehört dir. | Niente fuori dai range di queste regole. Misurano; quello che non sentono è tuo. |
| `mode.live` | LIVE (since RESET) | EN VIVO (desde RESET) | AO VIVO (desde o RESET) | EN DIRECT (depuis le RESET) | LIVE (seit RESET) | DAL VIVO (dal RESET) |
| `mode.file` | FILE | ARCHIVO | ARQUIVO | FICHIER | DATEI | FILE |
| `mode.live.short` | LIVE | EN VIVO | AO VIVO | EN DIRECT | LIVE | DAL VIVO |
| `mode.file.short` | FILE | ARCHIVO | ARQUIVO | FICHIER | DATEI | FILE |
| `summary` | {valor} s analysed | {valor} s analizados | {valor} s analisados | {valor} s analysées | {valor} s analysiert | {valor} s analizzati |
| `baseline.trend` | vs the material's own spectral trend | contra la tendencia espectral del propio material | em relação à tendência espectral do próprio material | par rapport à la tendance spectrale du matériau lui-même | gegen den eigenen spektralen Trend des Materials | rispetto alla tendenza spettrale del materiale stesso |
| `baseline.reference` | vs the loaded reference | contra la referencia cargada | em relação à referência carregada | par rapport à la référence chargée | gegen die geladene Referenz | rispetto al riferimento caricato |
| `baseline.trend.short` | vs the trend | contra la tendencia | em relação à tendência | par rapport à la tendance | gegen den Trend | rispetto alla tendenza |
| `baseline.reference.short` | vs the reference | contra la referencia | em relação à referência | par rapport à la référence | gegen die Referenz | rispetto al riferimento |
| `headline` | {valor} checks within range · {valor2} to look at, the first at {t0} | {valor} chequeos dentro de rango · {valor2} para revisar, el primero en {t0} | {valor} verificações dentro da faixa · {valor2} para verificar, a primeira em {t0} | {valor} contrôles dans la plage · {valor2} à vérifier, le premier à {t0} | {valor} Prüfungen im Bereich · {valor2} zum Prüfen, die erste bei {t0} | {valor} controlli nel range · {valor2} da controllare, il primo a {t0} |
| `headline.untimed` | {valor} checks within range · {valor2} to look at | {valor} chequeos dentro de rango · {valor2} para revisar | {valor} verificações dentro da faixa · {valor2} para verificar | {valor} contrôles dans la plage · {valor2} à vérifier | {valor} Prüfungen im Bereich · {valor2} zum Prüfen | {valor} controlli nel range · {valor2} da controllare |
| `headline.one` | {valor} checks within range · 1 to look at, at {t0} | {valor} chequeos dentro de rango · 1 para revisar, en {t0} | {valor} verificações dentro da faixa · 1 para verificar, em {t0} | {valor} contrôles dans la plage · 1 à vérifier, à {t0} | {valor} Prüfungen im Bereich · 1 zum Prüfen, bei {t0} | {valor} controlli nel range · 1 da controllare, a {t0} |
| `headline.one.untimed` | {valor} checks within range · 1 to look at | {valor} chequeos dentro de rango · 1 para revisar | {valor} verificações dentro da faixa · 1 para verificar | {valor} contrôles dans la plage · 1 à vérifier | {valor} Prüfungen im Bereich · 1 zum Prüfen | {valor} controlli nel range · 1 da controllare |
| `headline.none` | {valor} checks within range · nothing outside these rules | {valor} chequeos dentro de rango · nada fuera de estas reglas | {valor} verificações dentro da faixa · nada fora destas regras | {valor} contrôles dans la plage · rien hors de ces règles | {valor} Prüfungen im Bereich · nichts außerhalb dieser Regeln | {valor} controlli nel range · niente fuori da queste regole |
| `crushed` | PSR {valor} dB, under the {valor2} dB floor: transients are flattened (crushed). Check the limiter's input. | PSR {valor} dB, por debajo del piso de {valor2} dB: los transitorios quedan aplastados. Revisá la entrada del limitador. | PSR {valor} dB, abaixo do piso de {valor2} dB: os transientes ficam achatados. Verifique a entrada do limitador. | PSR {valor} dB, sous le plancher de {valor2} dB : les transitoires sont écrasés. Vérifiez l'entrée du limiteur. | PSR {valor} dB, unter der Grenze von {valor2} dB: die Transienten sind plattgedrückt. Prüfe den Eingang des Limiters. | PSR {valor} dB, sotto il minimo di {valor2} dB: i transienti sono schiacciati. Controlla l'ingresso del limiter. |
| `thin` | 150-400 Hz sits {valor} dB low {valor3} (what mixers call thin). Check what gives that range its body. | 150-400 Hz está {valor} dB abajo {valor3} (lo que en mezcla se llama delgado). Revisá qué le da cuerpo a ese rango. | 150-400 Hz está {valor} dB abaixo {valor3} (o que na mixagem se chama fino). Verifique o que dá corpo a essa faixa. | 150-400 Hz est {valor} dB en dessous {valor3} (ce qu'on appelle maigre au mixage). Vérifiez ce qui donne du corps à cette zone. | 150-400 Hz liegt {valor} dB tiefer {valor3} (was man beim Mischen dünn nennt). Prüfe, was diesem Bereich Körper gibt. | 150-400 Hz sta {valor} dB sotto {valor3} (quello che nel mix si chiama sottile). Controlla cosa dà corpo a quella zona. |
| `muddy` | 200-500 Hz sits {valor} dB high {valor3} (what mixers call muddy). Check what builds up in that range. | 200-500 Hz está {valor} dB arriba {valor3} (lo que en mezcla se llama turbio). Revisá qué se acumula en ese rango. | 200-500 Hz está {valor} dB acima {valor3} (o que na mixagem se chama embolado). Verifique o que se acumula nessa faixa. | 200-500 Hz est {valor} dB au-dessus {valor3} (ce qu'on appelle boueux au mixage). Vérifiez ce qui s'accumule dans cette zone. | 200-500 Hz liegt {valor} dB höher {valor3} (was man beim Mischen matschig nennt). Prüfe, was sich in diesem Bereich staut. | 200-500 Hz sta {valor} dB sopra {valor3} (quello che nel mix si chiama impastato). Controlla cosa si accumula in quella zona. |
| `harsh` | 2-5 kHz sits {valor} dB high {valor3} for {valor2} % of the time (what mixers call harsh). Check the presence range. | 2-5 kHz está {valor} dB arriba {valor3} el {valor2} % del tiempo (lo que en mezcla se llama áspero). Revisá el rango de presencia. | 2-5 kHz está {valor} dB acima {valor3} durante {valor2} % do tempo (o que na mixagem se chama áspero). Verifique a faixa de presença. | 2-5 kHz est {valor} dB au-dessus {valor3} pendant {valor2} % du temps (ce qu'on appelle agressif au mixage). Vérifiez la zone de présence. | 2-5 kHz liegt {valor} dB höher {valor3} während {valor2} % der Zeit (was man beim Mischen hart nennt). Prüfe den Präsenzbereich. | 2-5 kHz sta {valor} dB sopra {valor3} per il {valor2} % del tempo (quello che nel mix si chiama aspro). Controlla la zona di presenza. |
| `harsh.loud` | 2-5 kHz sits {valor} dB high {valor3} for {valor2} % of the time, at {t0} LUFS integrated (harsh and loud). Check the presence range and the limiter's input. | 2-5 kHz está {valor} dB arriba {valor3} el {valor2} % del tiempo, a {t0} LUFS integrados (áspero y fuerte). Revisá el rango de presencia y la entrada del limitador. | 2-5 kHz está {valor} dB acima {valor3} durante {valor2} % do tempo, a {t0} LUFS integrados (áspero e alto). Verifique a faixa de presença e a entrada do limitador. | 2-5 kHz est {valor} dB au-dessus {valor3} pendant {valor2} % du temps, à {t0} LUFS intégrés (agressif et fort). Vérifiez la zone de présence et l'entrée du limiteur. | 2-5 kHz liegt {valor} dB höher {valor3} während {valor2} % der Zeit, bei {t0} LUFS integriert (hart und laut). Prüfe den Präsenzbereich und den Eingang des Limiters. | 2-5 kHz sta {valor} dB sopra {valor3} per il {valor2} % del tempo, a {t0} LUFS integrati (aspro e forte). Controlla la zona di presenza e l'ingresso del limiter. |
| `no-air` | Above 10 kHz the level sits {valor} dB low {valor3} (what mixers call no air). Check the top end. | Por encima de 10 kHz el nivel está {valor} dB abajo {valor3} (lo que en mezcla se llama sin aire). Revisá los agudos. | Acima de 10 kHz o nível está {valor} dB abaixo {valor3} (o que na mixagem se chama sem ar). Verifique os agudos. | Au-dessus de 10 kHz le niveau est {valor} dB en dessous {valor3} (ce qu'on appelle sans air au mixage). Vérifiez le haut du spectre. | Oberhalb 10 kHz liegt der Pegel {valor} dB tiefer {valor3} (was man beim Mischen keine Luft nennt). Prüfe die Höhen. | Sopra i 10 kHz il livello sta {valor} dB sotto {valor3} (quello che nel mix si chiama senza aria). Controlla gli acuti. |
| `hollow-centre` | Width {valor} in the mids (300 Hz-2 kHz) against {valor2} in the highs (what mixers call a hollow centre). Check how the highs are widened against the mids. | Width {valor} en los medios (300 Hz-2 kHz) contra {valor2} en los agudos (lo que en mezcla se llama centro vacío). Revisá cómo se abren los agudos contra los medios. | Width {valor} nos médios (300 Hz-2 kHz) contra {valor2} nos agudos (o que na mixagem se chama centro vazio). Verifique como os agudos se abrem em relação aos médios. | Width {valor} dans les médiums (300 Hz-2 kHz) contre {valor2} dans les aigus (ce qu'on appelle un centre vide au mixage). Vérifiez comment les aigus s'ouvrent par rapport aux médiums. | Width {valor} in den Mitten (300 Hz-2 kHz) gegen {valor2} in den Höhen (was man beim Mischen eine hohle Mitte nennt). Prüfe, wie die Höhen gegen die Mitten verbreitert sind. | Width {valor} nei medi (300 Hz-2 kHz) contro {valor2} negli acuti (quello che nel mix si chiama centro vuoto). Controlla come si aprono gli acuti rispetto ai medi. |
| `key` | Estimated key: {banda}, confidence {valor}, best for {valor2} % of the time. | Tonalidad estimada: {banda}, confianza {valor}, la mejor el {valor2} % del tiempo. | Tonalidade estimada: {banda}, confiança {valor}, a melhor em {valor2} % do tempo. | Tonalité estimée : {banda}, confiance {valor}, la meilleure {valor2} % du temps. | Geschätzte Tonart: {banda}, Konfidenz {valor}, in {valor2} % der Zeit die beste. | Tonalità stimata: {banda}, confidenza {valor}, la migliore per il {valor2} % del tempo. |
| `key.major` | major | mayor | maior | majeur | Dur | maggiore |
| `key.minor` | minor | menor | menor | mineur | Moll | minore |
| `phone.ok` | Phone: {valor} % of the energy sits below 300 Hz, which a phone speaker does not reproduce, but the bass harmonics in 300-1200 Hz are only {valor2} dB down: the bass line survives. | Celular: el {valor} % de la energía está por debajo de 300 Hz, que un parlante de teléfono no reproduce, pero los armónicos del bajo en 300-1200 Hz están solo {valor2} dB abajo: la línea de bajo se escucha igual. | Celular: {valor} % da energia está abaixo de 300 Hz, que um alto-falante de telefone não reproduz, mas os harmônicos do baixo em 300-1200 Hz estão apenas {valor2} dB abaixo: a linha de baixo se mantém. | Téléphone : {valor} % de l'énergie est sous 300 Hz, ce qu'un haut-parleur de téléphone ne reproduit pas, mais les harmoniques de la basse entre 300 et 1200 Hz ne sont qu'à {valor2} dB : la ligne de basse tient. | Handy: {valor} % der Energie liegt unter 300 Hz, was ein Handylautsprecher nicht wiedergibt, aber die Bass-Obertöne in 300-1200 Hz liegen nur {valor2} dB darunter: die Basslinie bleibt hörbar. | Telefono: il {valor} % dell'energia sta sotto i 300 Hz, che un altoparlante di telefono non riproduce, ma le armoniche del basso in 300-1200 Hz sono solo {valor2} dB sotto: la linea di basso regge. |
| `phone.bad` | Phone: {valor} % of the energy is below 300 Hz and the bass harmonics in 300-1200 Hz are {valor2} dB down (the bass disappears). Check the bass harmonics above 300 Hz. | Celular: el {valor} % de la energía está por debajo de 300 Hz y los armónicos del bajo en 300-1200 Hz están {valor2} dB abajo (el bajo desaparece). Revisá los armónicos del bajo por encima de 300 Hz. | Celular: {valor} % da energia está abaixo de 300 Hz e os harmônicos do baixo em 300-1200 Hz estão {valor2} dB abaixo (o baixo desaparece). Verifique os harmônicos do baixo acima de 300 Hz. | Téléphone : {valor} % de l'énergie est sous 300 Hz et les harmoniques de la basse entre 300 et 1200 Hz sont à {valor2} dB (la basse disparaît). Vérifiez les harmoniques de la basse au-dessus de 300 Hz. | Handy: {valor} % der Energie liegt unter 300 Hz und die Bass-Obertöne in 300-1200 Hz liegen {valor2} dB darunter (der Bass verschwindet). Prüfe die Bass-Obertöne über 300 Hz. | Telefono: il {valor} % dell'energia sta sotto i 300 Hz e le armoniche del basso in 300-1200 Hz sono {valor2} dB sotto (il basso sparisce). Controlla le armoniche del basso sopra i 300 Hz. |
| `headphones.ok` | Headphones: {valor} dB in 2-5 kHz and no band above 8 kHz out of phase. | Auriculares: {valor} dB en 2-5 kHz y ninguna banda por encima de 8 kHz fuera de fase. | Fones: {valor} dB em 2-5 kHz e nenhuma banda acima de 8 kHz fora de fase. | Casque : {valor} dB entre 2 et 5 kHz et aucune bande au-dessus de 8 kHz hors phase. | Kopfhörer: {valor} dB in 2-5 kHz und kein Band über 8 kHz außer Phase. | Cuffie: {valor} dB in 2-5 kHz e nessuna banda sopra gli 8 kHz fuori fase. |
| `headphones.bad` | Headphones: {valor} dB high in 2-5 kHz, and a band above 8 kHz is out of phase {valor2} % of the time (tiring). Check the presence range and the width above 8 kHz. | Auriculares: {valor} dB arriba en 2-5 kHz, y una banda por encima de 8 kHz está fuera de fase el {valor2} % del tiempo (cansador). Revisá el rango de presencia y el ancho por encima de 8 kHz. | Fones: {valor} dB acima em 2-5 kHz, e uma banda acima de 8 kHz fica fora de fase {valor2} % do tempo (cansativo). Verifique a faixa de presença e a largura acima de 8 kHz. | Casque : {valor} dB de trop entre 2 et 5 kHz, et une bande au-dessus de 8 kHz est hors phase {valor2} % du temps (fatigant). Vérifiez la zone de présence et la largeur au-dessus de 8 kHz. | Kopfhörer: {valor} dB zu viel in 2-5 kHz, und ein Band über 8 kHz ist {valor2} % der Zeit außer Phase (ermüdend). Prüfe den Präsenzbereich und die Breite über 8 kHz. | Cuffie: {valor} dB di troppo in 2-5 kHz, e una banda sopra gli 8 kHz è fuori fase per il {valor2} % del tempo (affaticante). Controlla la zona di presenza e l'ampiezza sopra gli 8 kHz. |
| `laptop.ok` | Laptop: presence at 2-4 kHz is {valor} dB, so the voice holds up on a small speaker. | Laptop: la presencia en 2-4 kHz está {valor} dB, así que la voz aguanta en un parlante chico. | Laptop: a presença em 2-4 kHz está {valor} dB, então a voz aguenta num alto-falante pequeno. | Portable : la présence entre 2 et 4 kHz est à {valor} dB, la voix tient sur un petit haut-parleur. | Laptop: die Präsenz bei 2-4 kHz liegt bei {valor} dB, die Stimme hält sich auf kleinen Lautsprechern. | Laptop: la presenza in 2-4 kHz è a {valor} dB, la voce regge su un altoparlante piccolo. |
| `laptop.bad` | Laptop: presence at 2-4 kHz is {valor} dB under the material's own trend (the voice sinks). Check the voice's presence. | Laptop: la presencia en 2-4 kHz está {valor} dB por debajo de la tendencia del propio material (la voz se hunde). Revisá la presencia de la voz. | Laptop: a presença em 2-4 kHz está {valor} dB abaixo da tendência do próprio material (a voz afunda). Verifique a presença da voz. | Portable : la présence entre 2 et 4 kHz est {valor} dB sous la tendance du matériau (la voix s'enfonce). Vérifiez la présence de la voix. | Laptop: die Präsenz bei 2-4 kHz liegt {valor} dB unter dem eigenen Trend des Materials (die Stimme versinkt). Prüfe die Präsenz der Stimme. | Laptop: la presenza in 2-4 kHz è {valor} dB sotto la tendenza del materiale (la voce affonda). Controlla la presenza della voce. |
| `car.ok` | Car: {valor} dB below 100 Hz, which a cabin will not turn into boom. | Auto: {valor} dB por debajo de 100 Hz, que un habitáculo no va a convertir en retumbe. | Carro: {valor} dB abaixo de 100 Hz, que uma cabine não vai transformar em ronco. | Voiture : {valor} dB sous 100 Hz, qu'un habitacle ne transformera pas en boum. | Auto: {valor} dB unter 100 Hz, daraus macht ein Innenraum kein Dröhnen. | Auto: {valor} dB sotto i 100 Hz, che un abitacolo non trasformerà in rimbombo. |
| `car.bad` | Car: {valor} dB high below 100 Hz, and a cabin adds its own (boomy). Check the sub and the kick below 100 Hz. | Auto: {valor} dB arriba por debajo de 100 Hz, y el habitáculo agrega los suyos (retumba). Revisá el sub y el bombo por debajo de 100 Hz. | Carro: sobram {valor} dB abaixo de 100 Hz, e a cabine acrescenta os dela (ronca). Verifique o sub e o bumbo abaixo de 100 Hz. | Voiture : {valor} dB de trop sous 100 Hz, et l'habitacle ajoute les siens (ça résonne). Vérifiez le sub et la grosse caisse sous 100 Hz. | Auto: {valor} dB zu viel unter 100 Hz, und der Innenraum legt noch drauf (es dröhnt). Prüfe Sub und Kick unter 100 Hz. | Auto: {valor} dB di troppo sotto i 100 Hz, e l'abitacolo ci mette i suoi (rimbomba). Controlla sub e cassa sotto i 100 Hz. |
| `club.ok` | Club: {valor} dB of low end goes when the sub sums to mono. | Club: se pierden {valor} dB de graves cuando el sub suma en mono. | Club: {valor} dB de graves se perdem quando o sub soma em mono. | Club : {valor} dB de grave se perdent quand le sub passe en mono. | Club: {valor} dB Bass gehen verloren, wenn der Sub mono summiert. | Club: si perdono {valor} dB di bassi quando il sub somma in mono. |
| `club.bad` | Club: {valor} dB of low end goes when the sub sums to mono. Check the bass below 120 Hz in mono. | Club: se pierden {valor} dB de graves cuando el sub suma en mono. Revisá el bajo por debajo de 120 Hz en mono. | Club: {valor} dB de graves se perdem quando o sub soma em mono. Verifique o baixo abaixo de 120 Hz em mono. | Club : {valor} dB de grave se perdent quand le sub passe en mono. Vérifiez la basse sous 120 Hz en mono. | Club: {valor} dB Bass gehen verloren, wenn der Sub mono summiert. Prüfe den Bass unter 120 Hz in Mono. | Club: si perdono {valor} dB di bassi quando il sub somma in mono. Controlla il basso sotto i 120 Hz in mono. |
| `hi-fi.ok` | Hi-fi: full range hides nothing, and "How it will feel" has {valor} findings. | Hi-fi: un equipo full-range no esconde nada, y "Cómo se va a sentir" tiene {valor} hallazgos. | Hi-fi: um sistema full-range não esconde nada, e "Como vai soar" tem {valor} achados. | Hi-fi : un système full-range ne cache rien, et "Comment ça va sonner" compte {valor} constats. | Hi-Fi: ein Full-Range-System versteckt nichts, und "Wie es sich anfühlen wird" hat {valor} Befunde. | Hi-fi: un sistema full-range non nasconde niente, e "Come suonerà" ha {valor} rilievi. |
| `hi-fi.bad.one` | Hi-fi: full range hides nothing, and "How it will feel" has 1 finding. Check it above. | Hi-fi: un equipo full-range no esconde nada, y "Cómo se va a sentir" tiene 1 hallazgo. Revisalo arriba. | Hi-fi: um sistema full-range não esconde nada, e "Como vai soar" tem 1 achado. Verifique-o acima. | Hi-fi : un système full-range ne cache rien, et "Comment ça va sonner" compte 1 constat. Vérifiez-le plus haut. | Hi-Fi: ein Full-Range-System versteckt nichts, und "Wie es sich anfühlen wird" hat 1 Befund. Prüfe ihn weiter oben. | Hi-fi: un sistema full-range non nasconde niente, e "Come suonerà" ha 1 rilievo. Controllalo più in alto. |
| `hi-fi.bad.many` | Hi-fi: full range hides nothing, and "How it will feel" has {valor} findings. Check them above. | Hi-fi: un equipo full-range no esconde nada, y "Cómo se va a sentir" tiene {valor} hallazgos. Revisalos arriba. | Hi-fi: um sistema full-range não esconde nada, e "Como vai soar" tem {valor} achados. Verifique-os acima. | Hi-fi : un système full-range ne cache rien, et "Comment ça va sonner" compte {valor} constats. Vérifiez-les plus haut. | Hi-Fi: ein Full-Range-System versteckt nichts, und "Wie es sich anfühlen wird" hat {valor} Befunde. Prüfe sie weiter oben. | Hi-fi: un sistema full-range non nasconde niente, e "Come suonerà" ha {valor} rilievi. Controllali più in alto. |
| `hole` | {banda} Hz dips {valor} dB below its own average (deepest band of a {valor2}-{valor3} Hz stretch that dips between {t0} and {t1}). Check what plays in that range there. | {banda} Hz cae {valor} dB bajo su propia media (la banda más honda de un tramo de {valor2}-{valor3} Hz que cae entre {t0} y {t1}). Revisá qué suena en ese rango ahí. | {banda} Hz cai {valor} dB abaixo da própria média (a banda mais funda de um trecho de {valor2}-{valor3} Hz que cai entre {t0} e {t1}). Verifique o que toca nessa faixa ali. | {banda} Hz descend de {valor} dB sous sa propre moyenne (la bande la plus creuse d'une zone de {valor2}-{valor3} Hz qui descend entre {t0} et {t1}). Vérifiez ce qui joue dans cette zone à ce moment. | {banda} Hz fällt {valor} dB unter das eigene Mittel (das tiefste Band eines Bereichs von {valor2}-{valor3} Hz, der zwischen {t0} und {t1} fällt). Prüfe, was dort in diesem Bereich spielt. | {banda} Hz scende di {valor} dB sotto la sua media (la banda più profonda di una zona di {valor2}-{valor3} Hz che scende tra {t0} e {t1}). Controlla cosa suona in quella zona in quel punto. |
| `hole.one` | {banda} Hz dips {valor} dB below its own average between {t0} and {t1}. Check what plays in that band there. | {banda} Hz cae {valor} dB bajo su propia media entre {t0} y {t1}. Revisá qué suena en esa banda ahí. | {banda} Hz cai {valor} dB abaixo da própria média entre {t0} e {t1}. Verifique o que toca nessa banda ali. | {banda} Hz descend de {valor} dB sous sa propre moyenne entre {t0} et {t1}. Vérifiez ce qui joue dans cette bande à ce moment. | {banda} Hz fällt {valor} dB unter das eigene Mittel zwischen {t0} und {t1}. Prüfe, was dort in diesem Band spielt. | {banda} Hz scende di {valor} dB sotto la sua media tra {t0} e {t1}. Controlla cosa suona in quella banda in quel punto. |
| `quiet-section` | {valor} LU under the integrated level between {t0} and {t1} (a quiet section). Check the arrangement and the gain there. | {valor} LU por debajo del integrado entre {t0} y {t1} (una sección baja). Revisá el arreglo y la ganancia en ese tramo. | {valor} LU abaixo do integrado entre {t0} e {t1} (um trecho baixo). Verifique o arranjo e o ganho nesse trecho. | {valor} LU sous le niveau intégré entre {t0} et {t1} (un passage bas). Vérifiez l'arrangement et le gain dans ce passage. | {valor} LU unter dem integrierten Pegel zwischen {t0} und {t1} (eine leise Passage). Prüfe Arrangement und Gain in dieser Passage. | {valor} LU sotto il livello integrato tra {t0} e {t1} (una sezione bassa). Controlla arrangiamento e guadagno in quel tratto. |
| `loud-section` | {valor} LU over the integrated level between {t0} and {t1} (a loud section). | {valor} LU por encima del integrado entre {t0} y {t1} (una sección alta). | {valor} LU acima do integrado entre {t0} e {t1} (um trecho alto). | {valor} LU au-dessus du niveau intégré entre {t0} et {t1} (un passage fort). | {valor} LU über dem integrierten Pegel zwischen {t0} und {t1} (eine laute Passage). | {valor} LU sopra il livello integrato tra {t0} e {t1} (una sezione alta). |
| `peaks` | {valor} clip events over {valor2} dBTP between {t0} and {t1} (a peak burst). Check the limiter's ceiling there. | {valor} eventos de clip sobre {valor2} dBTP entre {t0} y {t1} (una ráfaga de picos). Revisá el techo del limitador en ese tramo. | {valor} eventos de clip acima de {valor2} dBTP entre {t0} e {t1} (uma rajada de picos). Verifique o teto do limitador nesse trecho. | {valor} événements de clip au-dessus de {valor2} dBTP entre {t0} et {t1} (une rafale de crêtes). Vérifiez le plafond du limiteur dans ce passage. | {valor} Clip-Ereignisse über {valor2} dBTP zwischen {t0} und {t1} (eine Peak-Salve). Prüfe die Obergrenze des Limiters in dieser Passage. | {valor} eventi di clip sopra {valor2} dBTP tra {t0} e {t1} (una raffica di picchi). Controlla il tetto del limiter in quel tratto. |
| `out-of-phase` | {banda} Hz out of phase {valor} % of the time: that band cancels when summed to mono. Check that band in mono. | {banda} Hz fuera de fase el {valor} % del tiempo: esa banda se cancela al monoficar. Revisá esa banda en mono. | {banda} Hz fora de fase em {valor} % do tempo: essa banda se cancela ao somar em mono. Verifique essa banda em mono. | {banda} Hz hors phase {valor} % du temps : cette bande s'annule en mono. Vérifiez cette bande en mono. | {banda} Hz in {valor} % der Zeit außer Phase: dieses Band löscht sich in Mono aus. Prüfe dieses Band in Mono. | {banda} Hz fuori fase per il {valor} % del tempo: quella banda si cancella in mono. Controlla quella banda in mono. |
| `imbalance` | {valor} dB of L/R imbalance held for {valor2} s, from {t0} to {t1}. Check the panning there. | {valor} dB de desbalance L/R sostenido {valor2} s, de {t0} a {t1}. Revisá el paneo en ese tramo. | {valor} dB de desequilíbrio L/R sustentado por {valor2} s, de {t0} a {t1}. Verifique o panorama nesse trecho. | {valor} dB de déséquilibre L/R tenu {valor2} s, de {t0} à {t1}. Vérifiez le panoramique dans ce passage. | {valor} dB L/R-Ungleichgewicht über {valor2} s, von {t0} bis {t1}. Prüfe das Panning in dieser Passage. | {valor} dB di sbilanciamento L/R per {valor2} s, da {t0} a {t1}. Controlla il panning in quel tratto. |
| `dc` | DC offset {valor} on L and {valor2} on R: it eats headroom and does not sound. Check the source files for an offset. | Continua de {valor} en L y {valor2} en R: se come headroom y no suena. Revisá si los archivos de origen tienen continua. | Componente contínua de {valor} em L e {valor2} em R: come headroom e não soa. Verifique se os arquivos de origem têm componente contínua. | Composante continue de {valor} sur L et {valor2} sur R : elle mange de la marge et ne s'entend pas. Vérifiez si les fichiers source ont une composante continue. | Gleichanteil von {valor} auf L und {valor2} auf R: frisst Headroom und klingt nicht. Prüfe, ob die Quelldateien einen Gleichanteil haben. | Componente continua di {valor} su L e {valor2} su R: mangia headroom e non suona. Controlla se i file di origine hanno componente continua. |
| `platform` | {banda}: {valor} dB {valor3} at {valor2} LUFS integrated. | {banda}: {valor} dB {valor3} a {valor2} LUFS integrados. | {banda}: {valor} dB {valor3} a {valor2} LUFS integrados. | {banda} : {valor} dB {valor3} à {valor2} LUFS intégrés. | {banda}: {valor} dB {valor3} bei {valor2} LUFS integriert. | {banda}: {valor} dB {valor3} a {valor2} LUFS integrati. |
| `platform.down` | turns you down | te baja | te abaixa | vous baisse de | dreht dich runter um | ti abbassa di |
| `platform.up` | would turn you up | te subiría | te aumentaria | vous monterait de | würde dich hochdrehen um | ti alzerebbe di |
| `platform.only` | away from the target (it only turns things down) | del objetivo (solo atenúa) | do alvo (só atenua) | de la cible (il ne fait qu'atténuer) | vom Ziel entfernt (es senkt nur ab) | dall'obiettivo (attenua soltanto) |
| `platform.tp` | {banda}: true peak {valor} dBTP is over its {valor2} dBTP ceiling. Check the limiter's ceiling. | {banda}: el pico real de {valor} dBTP se pasa de su techo de {valor2} dBTP. Revisá el techo del limitador. | {banda}: o pico real de {valor} dBTP passa do seu teto de {valor2} dBTP. Verifique o teto do limitador. | {banda} : le vrai pic de {valor} dBTP dépasse son plafond de {valor2} dBTP. Vérifiez le plafond du limiteur. | {banda}: der True Peak von {valor} dBTP überschreitet seine Grenze von {valor2} dBTP. Prüfe die Obergrenze des Limiters. | {banda}: il true peak di {valor} dBTP supera il suo tetto di {valor2} dBTP. Controlla il tetto del limiter. |
| `within.crushed` | transients | transitorios | transientes | transitoires | Transienten | transienti |
| `within.thin` | body | cuerpo | corpo | corps | Körper | corpo |
| `within.muddy` | low mids | medios graves | médios graves | bas-médium | tiefe Mitten | medio-bassi |
| `within.harsh` | presence | presencia | presença | présence | Präsenz | presenza |
| `within.no-air` | top end | agudos | agudos | aigus | Höhen | acuti |
| `within.hollow-centre` | centre | centro | centro | centre | Mitte | centro |
| `within.hole` | bands | bandas | bandas | bandes | Bänder | bande |
| `within.quiet-section` | loudness | loudness | loudness | loudness | Lautheit | loudness |
| `within.peaks` | peaks | picos | picos | crêtes | Peaks | picchi |
| `within.out-of-phase` | mono | mono | mono | mono | Mono | mono |
| `within.imbalance` | balance | balance | equilíbrio | équilibre | Balance | bilanciamento |
| `within.dc` | DC | continua | contínua | continu | Gleichanteil | continua |
| `crushed.ok` | Transients have room: PSR {valor} dB (floor {valor2} dB) | Los transitorios tienen aire: PSR {valor} dB (piso {valor2} dB) | Os transientes têm espaço: PSR {valor} dB (piso {valor2} dB) | Les transitoires ont de la place : PSR {valor} dB (plancher {valor2} dB) | Die Transienten haben Raum: PSR {valor} dB (Grenze {valor2} dB) | I transienti hanno spazio: PSR {valor} dB (minimo {valor2} dB) |
| `thin.ok` | Body 150-400 Hz: {valor} dB {valor3} (thin from -{valor2} dB) | Cuerpo 150-400 Hz: {valor} dB {valor3} (delgado desde -{valor2} dB) | Corpo 150-400 Hz: {valor} dB {valor3} (fino a partir de -{valor2} dB) | Corps 150-400 Hz : {valor} dB {valor3} (maigre à partir de -{valor2} dB) | Körper 150-400 Hz: {valor} dB {valor3} (dünn ab -{valor2} dB) | Corpo 150-400 Hz: {valor} dB {valor3} (sottile da -{valor2} dB) |
| `muddy.ok` | Low mids 200-500 Hz: {valor} dB {valor3} (muddy from +{valor2} dB) | Medios graves 200-500 Hz: {valor} dB {valor3} (turbio desde +{valor2} dB) | Médios graves 200-500 Hz: {valor} dB {valor3} (embolado a partir de +{valor2} dB) | Bas-médium 200-500 Hz : {valor} dB {valor3} (boueux à partir de +{valor2} dB) | Tiefe Mitten 200-500 Hz: {valor} dB {valor3} (matschig ab +{valor2} dB) | Medio-bassi 200-500 Hz: {valor} dB {valor3} (impastato da +{valor2} dB) |
| `harsh.ok` | Presence 2-5 kHz: {valor} dB {valor3} (harsh from +{valor2} dB) | Presencia 2-5 kHz: {valor} dB {valor3} (áspero desde +{valor2} dB) | Presença 2-5 kHz: {valor} dB {valor3} (áspero a partir de +{valor2} dB) | Présence 2-5 kHz : {valor} dB {valor3} (agressif à partir de +{valor2} dB) | Präsenz 2-5 kHz: {valor} dB {valor3} (hart ab +{valor2} dB) | Presenza 2-5 kHz: {valor} dB {valor3} (aspro da +{valor2} dB) |
| `harsh.ok.brief` | Presence 2-5 kHz: over +{valor3} dB only {valor} % of the time (harsh from {valor2} %) | Presencia 2-5 kHz: pasa +{valor3} dB solo el {valor} % del tiempo (áspero desde el {valor2} %) | Presença 2-5 kHz: passa de +{valor3} dB só {valor} % do tempo (áspero a partir de {valor2} %) | Présence 2-5 kHz : au-dessus de +{valor3} dB seulement {valor} % du temps (agressif à partir de {valor2} %) | Präsenz 2-5 kHz: über +{valor3} dB nur {valor} % der Zeit (hart ab {valor2} %) | Presenza 2-5 kHz: sopra +{valor3} dB solo il {valor} % del tempo (aspro dal {valor2} %) |
| `no-air.ok` | Top end open: {valor} dB above 10 kHz {valor3} (no air from -{valor2} dB) | Agudos abiertos: {valor} dB por encima de 10 kHz {valor3} (sin aire desde -{valor2} dB) | Agudos abertos: {valor} dB acima de 10 kHz {valor3} (sem ar a partir de -{valor2} dB) | Aigus ouverts : {valor} dB au-dessus de 10 kHz {valor3} (sans air à partir de -{valor2} dB) | Offene Höhen: {valor} dB oberhalb 10 kHz {valor3} (keine Luft ab -{valor2} dB) | Acuti aperti: {valor} dB sopra i 10 kHz {valor3} (senza aria da -{valor2} dB) |
| `hollow-centre.ok` | Width {valor} in the mids, {valor2} in the highs (hollow centre below {t0} with over {t1}) | Width {valor} en los medios, {valor2} en los agudos (centro vacío con menos de {t0} y más de {t1}) | Width {valor} nos médios, {valor2} nos agudos (centro vazio abaixo de {t0} com mais de {t1}) | Width {valor} dans les médiums, {valor2} dans les aigus (centre vide sous {t0} avec plus de {t1}) | Width {valor} in den Mitten, {valor2} in den Höhen (hohle Mitte unter {t0} mit über {t1}) | Width {valor} nei medi, {valor2} negli acuti (centro vuoto sotto {t0} con oltre {t1}) |
| `hole.ok` | No band dips {valor} dB below its own average for {valor2} s (longest run {t0} s) | Ninguna banda cae {valor} dB bajo su propia media durante {valor2} s (racha más larga {t0} s) | Nenhuma banda cai {valor} dB abaixo da própria média por {valor2} s (sequência mais longa {t0} s) | Aucune bande ne descend de {valor} dB sous sa moyenne pendant {valor2} s (plus longue série {t0} s) | Kein Band fällt {valor} dB unter sein Mittel für {valor2} s (längste Strecke {t0} s) | Nessuna banda scende di {valor} dB sotto la sua media per {valor2} s (serie più lunga {t0} s) |
| `quiet-section.ok` | Loudness holds: LRA {valor} LU, no section {valor2} LU under the integrated for {t0} s | El loudness se sostiene: LRA {valor} LU, ninguna sección {valor2} LU bajo el integrado durante {t0} s | O loudness se mantém: LRA {valor} LU, nenhum trecho {valor2} LU abaixo do integrado por {t0} s | Le loudness tient : LRA {valor} LU, aucun passage {valor2} LU sous l'intégré pendant {t0} s | Die Lautheit hält: LRA {valor} LU, keine Passage {valor2} LU unter dem Integrierten für {t0} s | Il loudness tiene: LRA {valor} LU, nessuna sezione {valor2} LU sotto l'integrato per {t0} s |
| `peaks.ok` | No clip bursts over {valor} dBTP ({valor2} clip events in total) | Sin ráfagas de clip sobre {valor} dBTP ({valor2} eventos de clip en total) | Sem rajadas de clip acima de {valor} dBTP ({valor2} eventos de clip no total) | Aucune rafale de clip au-dessus de {valor} dBTP ({valor2} événements de clip au total) | Keine Clip-Salven über {valor} dBTP ({valor2} Clip-Ereignisse insgesamt) | Nessuna raffica di clip sopra {valor} dBTP ({valor2} eventi di clip in totale) |
| `out-of-phase.ok` | No band cancels in mono (lowest mean correlation {valor}) | Ninguna banda se cancela en mono (correlación media más baja {valor}) | Nenhuma banda se cancela em mono (correlação média mais baixa {valor}) | Aucune bande ne s'annule en mono (corrélation moyenne la plus basse {valor}) | Kein Band löscht sich in Mono aus (niedrigste mittlere Korrelation {valor}) | Nessuna banda si cancella in mono (correlazione media più bassa {valor}) |
| `imbalance.ok` | L/R balanced: {valor} dB on average (imbalance from {valor2} dB held {t0} s) | L/R balanceado: {valor} dB de media (desbalance desde {valor2} dB durante {t0} s) | L/R equilibrado: {valor} dB em média (desequilíbrio a partir de {valor2} dB por {t0} s) | L/R équilibré : {valor} dB en moyenne (déséquilibre à partir de {valor2} dB tenu {t0} s) | L/R ausgeglichen: {valor} dB im Mittel (Ungleichgewicht ab {valor2} dB über {t0} s) | L/R bilanciato: {valor} dB in media (sbilanciamento da {valor2} dB per {t0} s) |
| `dc.ok` | No DC offset: {valor} on L, {valor2} on R (limit {t0}) | Sin continua: {valor} en L, {valor2} en R (límite {t0}) | Sem componente contínua: {valor} em L, {valor2} em R (limite {t0}) | Pas de composante continue : {valor} sur L, {valor2} sur R (limite {t0}) | Kein Gleichanteil: {valor} auf L, {valor2} auf R (Grenze {t0}) | Nessuna componente continua: {valor} su L, {valor2} su R (limite {t0}) |

## 3 · Resumen

| | |
|---|---|
| Claves de UI sin valor propio (salen por fallback a inglés) | **0** |
| Frases de VERDICT sin valor propio (idem) | **0** |
| Claves que existen en otro idioma pero no en inglés (texto muerto) | **0** |
