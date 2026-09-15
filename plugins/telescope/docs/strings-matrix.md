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

146 claves × 6 idiomas.

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

## 2 · Frases de VERDICT — `source/data/Rules.h`

92 claves × 6 idiomas.

| Clave | en | es | pt ⚠ | fr ⚠ | de ⚠ | it ⚠ |
|---|---|---|---|---|---|---|
| `ui.reset` | RESET | RESET | RESET | RESET | RESET | RESET |
| `ui.reset.value` | from 0 | desde 0 | do 0 | depuis 0 | ab 0 | da 0 |
| `ui.mode` | MODE | MODO | MODO | MODE | MODUS | MODO |
| `ui.file` | FILE | ARCHIVO | ARQUIVO | FICHIER | DATEI | FILE |
| `ui.file.value` | LOAD | CARGAR | CARREGAR | CHARGER | LADEN | CARICA |
| `ui.language` | LANGUAGE | IDIOMA | IDIOMA | LANGUE | SPRACHE | LINGUA |
| `ui.drop` | drag a file here, or press LOAD | arrastra un archivo aca, o toca CARGAR | arraste um arquivo aqui, ou toque CARREGAR | glissez un fichier ici, ou appuyez sur CHARGER | Datei hierher ziehen, oder LADEN druecken | trascina un file qui, o premi CARICA |
| `ui.nosecs` | no complete seconds yet: let the track play from the start, or use FILE | todavia no hay segundos completos: deja sonar el tema desde el principio, o usa ARCHIVO | ainda nao ha segundos completos: deixe a faixa tocar desde o inicio, ou use ARQUIVO | pas encore de secondes completes : laissez le morceau jouer depuis le debut, ou utilisez FICHIER | noch keine vollstaendigen Sekunden: den Track von Anfang an abspielen, oder DATEI benutzen | non ci sono ancora secondi completi: fai suonare il brano dall'inizio, o usa FILE |
| `ui.choose` | Choose the track VERDICT has to analyse | Elegi el tema que VERDICT tiene que analizar | Escolha a faixa que o VERDICT tem que analisar | Choisissez le morceau que VERDICT doit analyser | Waehle den Track, den VERDICT analysieren soll | Scegli il brano che VERDICT deve analizzare |
| `ui.analysing` | analysing  | analizando  | analisando  | analyse en cours  | analysiere  | analisi  |
| `ui.unreadable` | could not read:  | no se pudo leer:  | nao foi possivel ler:  | lecture impossible :  | konnte nicht gelesen werden:  | impossibile leggere:  |
| `ui.notafile` | that is not a file | eso no es un archivo | isso nao e um arquivo | ce n'est pas un fichier | das ist keine Datei | quello non e un file |
| `ui.notaudio` | " is not audio ( | " no es audio ( | " nao e audio ( | " n'est pas de l'audio ( | " ist kein Audio ( | " non e audio ( |
| `section.within` | Within range | Dentro de rango | Dentro da faixa | Dans la plage | Im Bereich | Nel range |
| `section.feel` | How it will feel | Como se va a sentir | Como vai soar | Comment ca va sonner | Wie es sich anfuehlen wird | Come suonera |
| `section.translate` | Where it translates | Donde traduce | Onde traduz | Ou ca se traduit | Wo es uebersetzt | Dove traduce |
| `section.missing` | What to check, and where | Que revisar y donde | O que verificar, e onde | Quoi verifier, et ou | Was pruefen, und wo | Cosa controllare, e dove |
| `footer` | Measurement, not taste. Device checks are generic. Re-run the analysis after every change. | Medicion, no gusto. Chequeos por dispositivo genericos. Rehace el analisis tras cada cambio. | Medicao, nao gosto. Verificacoes por dispositivo genericas. Refaca a analise apos cada mudanca. | Mesure, pas gout. Verifications par appareil generiques. Relancez l'analyse apres chaque modification. | Messung, kein Geschmack. Geraetepruefungen sind generisch. Analyse nach jeder Aenderung neu laufen lassen. | Misura, non gusto. Controlli per dispositivo generici. Rifai l'analisi dopo ogni modifica. |
| `none` | Nothing outside the ranges of these rules. They measure; what they can't hear is yours. | Nada fuera de rango en estas reglas. Miden; lo que no escuchan es tuyo. | Nada fora da faixa nestas regras. Elas medem; o que nao escutam e seu. | Rien hors des plages de ces regles. Elles mesurent ; ce qu'elles n'entendent pas vous appartient. | Nichts ausserhalb der Bereiche dieser Regeln. Sie messen; was sie nicht hoeren, gehoert dir. | Niente fuori dai range di queste regole. Misurano; quello che non sentono e tuo. |
| `mode.live` | LIVE (since RESET) | EN VIVO (desde RESET) | AO VIVO (desde o RESET) | EN DIRECT (depuis le RESET) | LIVE (seit RESET) | DAL VIVO (dal RESET) |
| `mode.file` | FILE | ARCHIVO | ARQUIVO | FICHIER | DATEI | FILE |
| `summary` | {valor} s analysed | {valor} s analizados | {valor} s analisados | {valor} s analysees | {valor} s analysiert | {valor} s analizzati |
| `baseline.trend` | vs the material's own spectral trend | contra la tendencia espectral del propio material | em relacao a tendencia espectral do proprio material | par rapport a la tendance spectrale du materiau lui-meme | gegen den eigenen spektralen Trend des Materials | rispetto alla tendenza spettrale del materiale stesso |
| `baseline.reference` | vs the loaded reference | contra la referencia cargada | em relacao a referencia carregada | par rapport a la reference chargee | gegen die geladene Referenz | rispetto al riferimento caricato |
| `baseline.trend.short` | vs the trend | contra la tendencia | em relacao a tendencia | par rapport a la tendance | gegen den Trend | rispetto alla tendenza |
| `baseline.reference.short` | vs the reference | contra la referencia | em relacao a referencia | par rapport a la reference | gegen die Referenz | rispetto al riferimento |
| `headline` | {valor} checks within range · {valor2} to look at, the first at {t0} | {valor} chequeos dentro de rango · {valor2} para revisar, el primero en {t0} | {valor} verificacoes dentro da faixa · {valor2} para verificar, a primeira em {t0} | {valor} controles dans la plage · {valor2} a verifier, le premier a {t0} | {valor} Pruefungen im Bereich · {valor2} zum Pruefen, die erste bei {t0} | {valor} controlli nel range · {valor2} da controllare, il primo a {t0} |
| `headline.untimed` | {valor} checks within range · {valor2} to look at | {valor} chequeos dentro de rango · {valor2} para revisar | {valor} verificacoes dentro da faixa · {valor2} para verificar | {valor} controles dans la plage · {valor2} a verifier | {valor} Pruefungen im Bereich · {valor2} zum Pruefen | {valor} controlli nel range · {valor2} da controllare |
| `headline.one` | {valor} checks within range · 1 to look at, at {t0} | {valor} chequeos dentro de rango · 1 para revisar, en {t0} | {valor} verificacoes dentro da faixa · 1 para verificar, em {t0} | {valor} controles dans la plage · 1 a verifier, a {t0} | {valor} Pruefungen im Bereich · 1 zum Pruefen, bei {t0} | {valor} controlli nel range · 1 da controllare, a {t0} |
| `headline.one.untimed` | {valor} checks within range · 1 to look at | {valor} chequeos dentro de rango · 1 para revisar | {valor} verificacoes dentro da faixa · 1 para verificar | {valor} controles dans la plage · 1 a verifier | {valor} Pruefungen im Bereich · 1 zum Pruefen | {valor} controlli nel range · 1 da controllare |
| `headline.none` | {valor} checks within range · nothing outside these rules | {valor} chequeos dentro de rango · nada fuera de estas reglas | {valor} verificacoes dentro da faixa · nada fora destas regras | {valor} controles dans la plage · rien hors de ces regles | {valor} Pruefungen im Bereich · nichts ausserhalb dieser Regeln | {valor} controlli nel range · niente fuori da queste regole |
| `crushed` | PSR {valor} dB, under the {valor2} dB floor: transients are flattened (crushed). Check the limiter's input. | PSR {valor} dB, por debajo del piso de {valor2} dB: los transitorios quedan aplastados. Revisa la entrada del limitador. | PSR {valor} dB, abaixo do piso de {valor2} dB: os transientes ficam achatados. Verifique a entrada do limitador. | PSR {valor} dB, sous le plancher de {valor2} dB : les transitoires sont ecrases. Verifiez l'entree du limiteur. | PSR {valor} dB, unter der Grenze von {valor2} dB: die Transienten sind plattgedrueckt. Pruefe den Eingang des Limiters. | PSR {valor} dB, sotto il minimo di {valor2} dB: i transienti sono schiacciati. Controlla l'ingresso del limiter. |
| `thin` | 150-400 Hz sits {valor} dB low {valor3} (what mixers call thin). Check what gives that range its body. | 150-400 Hz esta {valor} dB abajo {valor3} (lo que en mezcla se llama delgado). Revisa que le da cuerpo a ese rango. | 150-400 Hz esta {valor} dB abaixo {valor3} (o que na mixagem se chama fino). Verifique o que da corpo a essa faixa. | 150-400 Hz est {valor} dB en dessous {valor3} (ce qu'on appelle maigre au mixage). Verifiez ce qui donne du corps a cette zone. | 150-400 Hz liegt {valor} dB tiefer {valor3} (was man beim Mischen duenn nennt). Pruefe, was diesem Bereich Koerper gibt. | 150-400 Hz sta {valor} dB sotto {valor3} (quello che nel mix si chiama sottile). Controlla cosa da corpo a quella zona. |
| `muddy` | 200-500 Hz sits {valor} dB high {valor3} (what mixers call muddy). Check what builds up in that range. | 200-500 Hz esta {valor} dB arriba {valor3} (lo que en mezcla se llama turbio). Revisa que se acumula en ese rango. | 200-500 Hz esta {valor} dB acima {valor3} (o que na mixagem se chama embolado). Verifique o que se acumula nessa faixa. | 200-500 Hz est {valor} dB au-dessus {valor3} (ce qu'on appelle boueux au mixage). Verifiez ce qui s'accumule dans cette zone. | 200-500 Hz liegt {valor} dB hoeher {valor3} (was man beim Mischen matschig nennt). Pruefe, was sich in diesem Bereich staut. | 200-500 Hz sta {valor} dB sopra {valor3} (quello che nel mix si chiama impastato). Controlla cosa si accumula in quella zona. |
| `harsh` | 2-5 kHz sits {valor} dB high {valor3} for {valor2} % of the time (what mixers call harsh). Check the presence range. | 2-5 kHz esta {valor} dB arriba {valor3} el {valor2} % del tiempo (lo que en mezcla se llama aspero). Revisa el rango de presencia. | 2-5 kHz esta {valor} dB acima {valor3} durante {valor2} % do tempo (o que na mixagem se chama aspero). Verifique a faixa de presenca. | 2-5 kHz est {valor} dB au-dessus {valor3} pendant {valor2} % du temps (ce qu'on appelle agressif au mixage). Verifiez la zone de presence. | 2-5 kHz liegt {valor} dB hoeher {valor3} waehrend {valor2} % der Zeit (was man beim Mischen hart nennt). Pruefe den Praesenzbereich. | 2-5 kHz sta {valor} dB sopra {valor3} per il {valor2} % del tempo (quello che nel mix si chiama aspro). Controlla la zona di presenza. |
| `harsh.loud` | 2-5 kHz sits {valor} dB high {valor3} for {valor2} % of the time, at {t0} LUFS integrated (harsh and loud). Check the presence range and the limiter's input. | 2-5 kHz esta {valor} dB arriba {valor3} el {valor2} % del tiempo, a {t0} LUFS integrados (aspero y fuerte). Revisa el rango de presencia y la entrada del limitador. | 2-5 kHz esta {valor} dB acima {valor3} durante {valor2} % do tempo, a {t0} LUFS integrados (aspero e alto). Verifique a faixa de presenca e a entrada do limitador. | 2-5 kHz est {valor} dB au-dessus {valor3} pendant {valor2} % du temps, a {t0} LUFS integres (agressif et fort). Verifiez la zone de presence et l'entree du limiteur. | 2-5 kHz liegt {valor} dB hoeher {valor3} waehrend {valor2} % der Zeit, bei {t0} LUFS integriert (hart und laut). Pruefe den Praesenzbereich und den Eingang des Limiters. | 2-5 kHz sta {valor} dB sopra {valor3} per il {valor2} % del tempo, a {t0} LUFS integrati (aspro e forte). Controlla la zona di presenza e l'ingresso del limiter. |
| `no-air` | Above 10 kHz the level sits {valor} dB low {valor3} (what mixers call no air). Check the top end. | Por encima de 10 kHz el nivel esta {valor} dB abajo {valor3} (lo que en mezcla se llama sin aire). Revisa los agudos. | Acima de 10 kHz o nivel esta {valor} dB abaixo {valor3} (o que na mixagem se chama sem ar). Verifique os agudos. | Au-dessus de 10 kHz le niveau est {valor} dB en dessous {valor3} (ce qu'on appelle sans air au mixage). Verifiez le haut du spectre. | Oberhalb 10 kHz liegt der Pegel {valor} dB tiefer {valor3} (was man beim Mischen keine Luft nennt). Pruefe die Hoehen. | Sopra i 10 kHz il livello sta {valor} dB sotto {valor3} (quello che nel mix si chiama senza aria). Controlla gli acuti. |
| `hollow-centre` | Width {valor} in the mids (300 Hz-2 kHz) against {valor2} in the highs (what mixers call a hollow centre). Check how the highs are widened against the mids. | Width {valor} en los medios (300 Hz-2 kHz) contra {valor2} en los agudos (lo que en mezcla se llama centro vacio). Revisa como se abren los agudos contra los medios. | Width {valor} nos medios (300 Hz-2 kHz) contra {valor2} nos agudos (o que na mixagem se chama centro vazio). Verifique como os agudos se abrem em relacao aos medios. | Width {valor} dans les mediums (300 Hz-2 kHz) contre {valor2} dans les aigus (ce qu'on appelle un centre vide au mixage). Verifiez comment les aigus s'ouvrent par rapport aux mediums. | Width {valor} in den Mitten (300 Hz-2 kHz) gegen {valor2} in den Hoehen (was man beim Mischen eine hohle Mitte nennt). Pruefe, wie die Hoehen gegen die Mitten verbreitert sind. | Width {valor} nei medi (300 Hz-2 kHz) contro {valor2} negli acuti (quello che nel mix si chiama centro vuoto). Controlla come si aprono gli acuti rispetto ai medi. |
| `key` | Estimated key: {banda}, confidence {valor}, best for {valor2} % of the time. | Tonalidad estimada: {banda}, confianza {valor}, la mejor el {valor2} % del tiempo. | Tonalidade estimada: {banda}, confianca {valor}, a melhor em {valor2} % do tempo. | Tonalite estimee : {banda}, confiance {valor}, la meilleure {valor2} % du temps. | Geschaetzte Tonart: {banda}, Konfidenz {valor}, in {valor2} % der Zeit die beste. | Tonalita stimata: {banda}, confidenza {valor}, la migliore per il {valor2} % del tempo. |
| `key.major` | major | mayor | maior | majeur | Dur | maggiore |
| `key.minor` | minor | menor | menor | mineur | Moll | minore |
| `phone.ok` | Phone: {valor} % of the energy sits below 300 Hz, which a phone speaker does not reproduce, but the bass harmonics in 300-1200 Hz are only {valor2} dB down: the bass line survives. | Celular: el {valor} % de la energia esta por debajo de 300 Hz, que un parlante de telefono no reproduce, pero los armonicos del bajo en 300-1200 Hz estan solo {valor2} dB abajo: la linea de bajo se escucha igual. | Celular: {valor} % da energia esta abaixo de 300 Hz, que um alto-falante de telefone nao reproduz, mas os harmonicos do baixo em 300-1200 Hz estao apenas {valor2} dB abaixo: a linha de baixo se mantem. | Telephone : {valor} % de l'energie est sous 300 Hz, ce qu'un haut-parleur de telephone ne reproduit pas, mais les harmoniques de la basse entre 300 et 1200 Hz ne sont qu'a {valor2} dB : la ligne de basse tient. | Handy: {valor} % der Energie liegt unter 300 Hz, was ein Handylautsprecher nicht wiedergibt, aber die Bass-Obertoene in 300-1200 Hz liegen nur {valor2} dB darunter: die Basslinie bleibt hoerbar. | Telefono: il {valor} % dell'energia sta sotto i 300 Hz, che un altoparlante di telefono non riproduce, ma le armoniche del basso in 300-1200 Hz sono solo {valor2} dB sotto: la linea di basso regge. |
| `phone.bad` | Phone: {valor} % of the energy is below 300 Hz and the bass harmonics in 300-1200 Hz are {valor2} dB down (the bass disappears). Check the bass harmonics above 300 Hz. | Celular: el {valor} % de la energia esta por debajo de 300 Hz y los armonicos del bajo en 300-1200 Hz estan {valor2} dB abajo (el bajo desaparece). Revisa los armonicos del bajo por encima de 300 Hz. | Celular: {valor} % da energia esta abaixo de 300 Hz e os harmonicos do baixo em 300-1200 Hz estao {valor2} dB abaixo (o baixo desaparece). Verifique os harmonicos do baixo acima de 300 Hz. | Telephone : {valor} % de l'energie est sous 300 Hz et les harmoniques de la basse entre 300 et 1200 Hz sont a {valor2} dB (la basse disparait). Verifiez les harmoniques de la basse au-dessus de 300 Hz. | Handy: {valor} % der Energie liegt unter 300 Hz und die Bass-Obertoene in 300-1200 Hz liegen {valor2} dB darunter (der Bass verschwindet). Pruefe die Bass-Obertoene ueber 300 Hz. | Telefono: il {valor} % dell'energia sta sotto i 300 Hz e le armoniche del basso in 300-1200 Hz sono {valor2} dB sotto (il basso sparisce). Controlla le armoniche del basso sopra i 300 Hz. |
| `headphones.ok` | Headphones: {valor} dB in 2-5 kHz and no band above 8 kHz out of phase. | Auriculares: {valor} dB en 2-5 kHz y ninguna banda por encima de 8 kHz fuera de fase. | Fones: {valor} dB em 2-5 kHz e nenhuma banda acima de 8 kHz fora de fase. | Casque : {valor} dB entre 2 et 5 kHz et aucune bande au-dessus de 8 kHz hors phase. | Kopfhoerer: {valor} dB in 2-5 kHz und kein Band ueber 8 kHz ausser Phase. | Cuffie: {valor} dB in 2-5 kHz e nessuna banda sopra gli 8 kHz fuori fase. |
| `headphones.bad` | Headphones: {valor} dB high in 2-5 kHz, and a band above 8 kHz is out of phase {valor2} % of the time (tiring). Check the presence range and the width above 8 kHz. | Auriculares: {valor} dB arriba en 2-5 kHz, y una banda por encima de 8 kHz esta fuera de fase el {valor2} % del tiempo (cansador). Revisa el rango de presencia y el ancho por encima de 8 kHz. | Fones: {valor} dB acima em 2-5 kHz, e uma banda acima de 8 kHz fica fora de fase {valor2} % do tempo (cansativo). Verifique a faixa de presenca e a largura acima de 8 kHz. | Casque : {valor} dB de trop entre 2 et 5 kHz, et une bande au-dessus de 8 kHz est hors phase {valor2} % du temps (fatigant). Verifiez la zone de presence et la largeur au-dessus de 8 kHz. | Kopfhoerer: {valor} dB zu viel in 2-5 kHz, und ein Band ueber 8 kHz ist {valor2} % der Zeit ausser Phase (ermuedend). Pruefe den Praesenzbereich und die Breite ueber 8 kHz. | Cuffie: {valor} dB di troppo in 2-5 kHz, e una banda sopra gli 8 kHz e fuori fase per il {valor2} % del tempo (affaticante). Controlla la zona di presenza e l'ampiezza sopra gli 8 kHz. |
| `laptop.ok` | Laptop: presence at 2-4 kHz is {valor} dB, so the voice holds up on a small speaker. | Laptop: la presencia en 2-4 kHz esta {valor} dB, asi que la voz aguanta en un parlante chico. | Laptop: a presenca em 2-4 kHz esta {valor} dB, entao a voz aguenta num alto-falante pequeno. | Portable : la presence entre 2 et 4 kHz est a {valor} dB, la voix tient sur un petit haut-parleur. | Laptop: die Praesenz bei 2-4 kHz liegt bei {valor} dB, die Stimme haelt sich auf kleinen Lautsprechern. | Laptop: la presenza in 2-4 kHz e a {valor} dB, la voce regge su un altoparlante piccolo. |
| `laptop.bad` | Laptop: presence at 2-4 kHz is {valor} dB under the material's own trend (the voice sinks). Check the voice's presence. | Laptop: la presencia en 2-4 kHz esta {valor} dB por debajo de la tendencia del propio material (la voz se hunde). Revisa la presencia de la voz. | Laptop: a presenca em 2-4 kHz esta {valor} dB abaixo da tendencia do proprio material (a voz afunda). Verifique a presenca da voz. | Portable : la presence entre 2 et 4 kHz est {valor} dB sous la tendance du materiau (la voix s'enfonce). Verifiez la presence de la voix. | Laptop: die Praesenz bei 2-4 kHz liegt {valor} dB unter dem eigenen Trend des Materials (die Stimme versinkt). Pruefe die Praesenz der Stimme. | Laptop: la presenza in 2-4 kHz e {valor} dB sotto la tendenza del materiale (la voce affonda). Controlla la presenza della voce. |
| `car.ok` | Car: {valor} dB below 100 Hz, which a cabin will not turn into boom. | Auto: {valor} dB por debajo de 100 Hz, que un habitaculo no va a convertir en retumbe. | Carro: {valor} dB abaixo de 100 Hz, que uma cabine nao vai transformar em ronco. | Voiture : {valor} dB sous 100 Hz, qu'un habitacle ne transformera pas en boum. | Auto: {valor} dB unter 100 Hz, daraus macht ein Innenraum kein Droehnen. | Auto: {valor} dB sotto i 100 Hz, che un abitacolo non trasformera in rimbombo. |
| `car.bad` | Car: {valor} dB high below 100 Hz, and a cabin adds its own (boomy). Check the sub and the kick below 100 Hz. | Auto: {valor} dB arriba por debajo de 100 Hz, y el habitaculo agrega los suyos (retumba). Revisa el sub y el bombo por debajo de 100 Hz. | Carro: sobram {valor} dB abaixo de 100 Hz, e a cabine acrescenta os dela (ronca). Verifique o sub e o bumbo abaixo de 100 Hz. | Voiture : {valor} dB de trop sous 100 Hz, et l'habitacle ajoute les siens (ca resonne). Verifiez le sub et la grosse caisse sous 100 Hz. | Auto: {valor} dB zu viel unter 100 Hz, und der Innenraum legt noch drauf (es droehnt). Pruefe Sub und Kick unter 100 Hz. | Auto: {valor} dB di troppo sotto i 100 Hz, e l'abitacolo ci mette i suoi (rimbomba). Controlla sub e cassa sotto i 100 Hz. |
| `club.ok` | Club: {valor} dB of low end goes when the sub sums to mono. | Club: se pierden {valor} dB de graves cuando el sub suma en mono. | Club: {valor} dB de graves se perdem quando o sub soma em mono. | Club : {valor} dB de grave se perdent quand le sub passe en mono. | Club: {valor} dB Bass gehen verloren, wenn der Sub mono summiert. | Club: si perdono {valor} dB di bassi quando il sub somma in mono. |
| `club.bad` | Club: {valor} dB of low end goes when the sub sums to mono. Check the bass below 120 Hz in mono. | Club: se pierden {valor} dB de graves cuando el sub suma en mono. Revisa el bajo por debajo de 120 Hz en mono. | Club: {valor} dB de graves se perdem quando o sub soma em mono. Verifique o baixo abaixo de 120 Hz em mono. | Club : {valor} dB de grave se perdent quand le sub passe en mono. Verifiez la basse sous 120 Hz en mono. | Club: {valor} dB Bass gehen verloren, wenn der Sub mono summiert. Pruefe den Bass unter 120 Hz in Mono. | Club: si perdono {valor} dB di bassi quando il sub somma in mono. Controlla il basso sotto i 120 Hz in mono. |
| `hi-fi.ok` | Hi-fi: full range hides nothing, and "How it will feel" has {valor} findings. | Hi-fi: un equipo full-range no esconde nada, y "Como se va a sentir" tiene {valor} hallazgos. | Hi-fi: um sistema full-range nao esconde nada, e "Como vai soar" tem {valor} achados. | Hi-fi : un systeme full-range ne cache rien, et "Comment ca va sonner" compte {valor} constats. | Hi-Fi: ein Full-Range-System versteckt nichts, und "Wie es sich anfuehlen wird" hat {valor} Befunde. | Hi-fi: un sistema full-range non nasconde niente, e "Come suonera" ha {valor} rilievi. |
| `hi-fi.bad.one` | Hi-fi: full range hides nothing, and "How it will feel" has 1 finding. Check it above. | Hi-fi: un equipo full-range no esconde nada, y "Como se va a sentir" tiene 1 hallazgo. Revisalo arriba. | Hi-fi: um sistema full-range nao esconde nada, e "Como vai soar" tem 1 achado. Verifique-o acima. | Hi-fi : un systeme full-range ne cache rien, et "Comment ca va sonner" compte 1 constat. Verifiez-le plus haut. | Hi-Fi: ein Full-Range-System versteckt nichts, und "Wie es sich anfuehlen wird" hat 1 Befund. Pruefe ihn weiter oben. | Hi-fi: un sistema full-range non nasconde niente, e "Come suonera" ha 1 rilievo. Controllalo piu in alto. |
| `hi-fi.bad.many` | Hi-fi: full range hides nothing, and "How it will feel" has {valor} findings. Check them above. | Hi-fi: un equipo full-range no esconde nada, y "Como se va a sentir" tiene {valor} hallazgos. Revisalos arriba. | Hi-fi: um sistema full-range nao esconde nada, e "Como vai soar" tem {valor} achados. Verifique-os acima. | Hi-fi : un systeme full-range ne cache rien, et "Comment ca va sonner" compte {valor} constats. Verifiez-les plus haut. | Hi-Fi: ein Full-Range-System versteckt nichts, und "Wie es sich anfuehlen wird" hat {valor} Befunde. Pruefe sie weiter oben. | Hi-fi: un sistema full-range non nasconde niente, e "Come suonera" ha {valor} rilievi. Controllali piu in alto. |
| `hole` | {banda} Hz dips {valor} dB below its own average (deepest band of a {valor2}-{valor3} Hz stretch that dips between {t0} and {t1}). Check what plays in that range there. | {banda} Hz cae {valor} dB bajo su propia media (la banda mas honda de un tramo de {valor2}-{valor3} Hz que cae entre {t0} y {t1}). Revisa que suena en ese rango ahi. | {banda} Hz cai {valor} dB abaixo da propria media (a banda mais funda de um trecho de {valor2}-{valor3} Hz que cai entre {t0} e {t1}). Verifique o que toca nessa faixa ali. | {banda} Hz descend de {valor} dB sous sa propre moyenne (la bande la plus creuse d'une zone de {valor2}-{valor3} Hz qui descend entre {t0} et {t1}). Verifiez ce qui joue dans cette zone a ce moment. | {banda} Hz faellt {valor} dB unter das eigene Mittel (das tiefste Band eines Bereichs von {valor2}-{valor3} Hz, der zwischen {t0} und {t1} faellt). Pruefe, was dort in diesem Bereich spielt. | {banda} Hz scende di {valor} dB sotto la sua media (la banda piu profonda di una zona di {valor2}-{valor3} Hz che scende tra {t0} e {t1}). Controlla cosa suona in quella zona in quel punto. |
| `hole.one` | {banda} Hz dips {valor} dB below its own average between {t0} and {t1}. Check what plays in that band there. | {banda} Hz cae {valor} dB bajo su propia media entre {t0} y {t1}. Revisa que suena en esa banda ahi. | {banda} Hz cai {valor} dB abaixo da propria media entre {t0} e {t1}. Verifique o que toca nessa banda ali. | {banda} Hz descend de {valor} dB sous sa propre moyenne entre {t0} et {t1}. Verifiez ce qui joue dans cette bande a ce moment. | {banda} Hz faellt {valor} dB unter das eigene Mittel zwischen {t0} und {t1}. Pruefe, was dort in diesem Band spielt. | {banda} Hz scende di {valor} dB sotto la sua media tra {t0} e {t1}. Controlla cosa suona in quella banda in quel punto. |
| `quiet-section` | {valor} LU under the integrated level between {t0} and {t1} (a quiet section). Check the arrangement and the gain there. | {valor} LU por debajo del integrado entre {t0} y {t1} (una seccion baja). Revisa el arreglo y la ganancia en ese tramo. | {valor} LU abaixo do integrado entre {t0} e {t1} (um trecho baixo). Verifique o arranjo e o ganho nesse trecho. | {valor} LU sous le niveau integre entre {t0} et {t1} (un passage bas). Verifiez l'arrangement et le gain dans ce passage. | {valor} LU unter dem integrierten Pegel zwischen {t0} und {t1} (eine leise Passage). Pruefe Arrangement und Gain in dieser Passage. | {valor} LU sotto il livello integrato tra {t0} e {t1} (una sezione bassa). Controlla arrangiamento e guadagno in quel tratto. |
| `loud-section` | {valor} LU over the integrated level between {t0} and {t1} (a loud section). | {valor} LU por encima del integrado entre {t0} y {t1} (una seccion alta). | {valor} LU acima do integrado entre {t0} e {t1} (um trecho alto). | {valor} LU au-dessus du niveau integre entre {t0} et {t1} (un passage fort). | {valor} LU ueber dem integrierten Pegel zwischen {t0} und {t1} (eine laute Passage). | {valor} LU sopra il livello integrato tra {t0} e {t1} (una sezione alta). |
| `peaks` | {valor} clip events over {valor2} dBTP between {t0} and {t1} (a peak burst). Check the limiter's ceiling there. | {valor} eventos de clip sobre {valor2} dBTP entre {t0} y {t1} (una rafaga de picos). Revisa el techo del limitador en ese tramo. | {valor} eventos de clip acima de {valor2} dBTP entre {t0} e {t1} (uma rajada de picos). Verifique o teto do limitador nesse trecho. | {valor} evenements de clip au-dessus de {valor2} dBTP entre {t0} et {t1} (une rafale de cretes). Verifiez le plafond du limiteur dans ce passage. | {valor} Clip-Ereignisse ueber {valor2} dBTP zwischen {t0} und {t1} (eine Peak-Salve). Pruefe die Obergrenze des Limiters in dieser Passage. | {valor} eventi di clip sopra {valor2} dBTP tra {t0} e {t1} (una raffica di picchi). Controlla il tetto del limiter in quel tratto. |
| `out-of-phase` | {banda} Hz out of phase {valor} % of the time: that band cancels when summed to mono. Check that band in mono. | {banda} Hz fuera de fase el {valor} % del tiempo: esa banda se cancela al monoficar. Revisa esa banda en mono. | {banda} Hz fora de fase em {valor} % do tempo: essa banda se cancela ao somar em mono. Verifique essa banda em mono. | {banda} Hz hors phase {valor} % du temps : cette bande s'annule en mono. Verifiez cette bande en mono. | {banda} Hz in {valor} % der Zeit ausser Phase: dieses Band loescht sich in Mono aus. Pruefe dieses Band in Mono. | {banda} Hz fuori fase per il {valor} % del tempo: quella banda si cancella in mono. Controlla quella banda in mono. |
| `imbalance` | {valor} dB of L/R imbalance held for {valor2} s, from {t0} to {t1}. Check the panning there. | {valor} dB de desbalance L/R sostenido {valor2} s, de {t0} a {t1}. Revisa el paneo en ese tramo. | {valor} dB de desequilibrio L/R sustentado por {valor2} s, de {t0} a {t1}. Verifique o panorama nesse trecho. | {valor} dB de desequilibre L/R tenu {valor2} s, de {t0} a {t1}. Verifiez le panoramique dans ce passage. | {valor} dB L/R-Ungleichgewicht ueber {valor2} s, von {t0} bis {t1}. Pruefe das Panning in dieser Passage. | {valor} dB di sbilanciamento L/R per {valor2} s, da {t0} a {t1}. Controlla il panning in quel tratto. |
| `dc` | DC offset {valor} on L and {valor2} on R: it eats headroom and does not sound. Check the source files for an offset. | Continua de {valor} en L y {valor2} en R: se come headroom y no suena. Revisa si los archivos de origen tienen continua. | Componente continua de {valor} em L e {valor2} em R: come headroom e nao soa. Verifique se os arquivos de origem tem componente continua. | Composante continue de {valor} sur L et {valor2} sur R : elle mange de la marge et ne s'entend pas. Verifiez si les fichiers source ont une composante continue. | Gleichanteil von {valor} auf L und {valor2} auf R: frisst Headroom und klingt nicht. Pruefe, ob die Quelldateien einen Gleichanteil haben. | Componente continua di {valor} su L e {valor2} su R: mangia headroom e non suona. Controlla se i file di origine hanno componente continua. |
| `platform` | {banda}: {valor} dB {valor3} at {valor2} LUFS integrated. | {banda}: {valor} dB {valor3} a {valor2} LUFS integrados. | {banda}: {valor} dB {valor3} a {valor2} LUFS integrados. | {banda} : {valor} dB {valor3} a {valor2} LUFS integres. | {banda}: {valor} dB {valor3} bei {valor2} LUFS integriert. | {banda}: {valor} dB {valor3} a {valor2} LUFS integrati. |
| `platform.down` | turns you down | te baja | te abaixa | vous baisse de | dreht dich runter um | ti abbassa di |
| `platform.up` | would turn you up | te subiria | te aumentaria | vous monterait de | wuerde dich hochdrehen um | ti alzerebbe di |
| `platform.only` | away from the target (it only turns things down) | del objetivo (solo atenua) | do alvo (so atenua) | de la cible (il ne fait qu'attenuer) | vom Ziel entfernt (es senkt nur ab) | dall'obiettivo (attenua soltanto) |
| `platform.tp` | {banda}: true peak {valor} dBTP is over its {valor2} dBTP ceiling. Check the limiter's ceiling. | {banda}: el pico real de {valor} dBTP se pasa de su techo de {valor2} dBTP. Revisa el techo del limitador. | {banda}: o pico real de {valor} dBTP passa do seu teto de {valor2} dBTP. Verifique o teto do limitador. | {banda} : le vrai pic de {valor} dBTP depasse son plafond de {valor2} dBTP. Verifiez le plafond du limiteur. | {banda}: der True Peak von {valor} dBTP ueberschreitet seine Grenze von {valor2} dBTP. Pruefe die Obergrenze des Limiters. | {banda}: il true peak di {valor} dBTP supera il suo tetto di {valor2} dBTP. Controlla il tetto del limiter. |
| `within.crushed` | transients | transitorios | transientes | transitoires | Transienten | transienti |
| `within.thin` | body | cuerpo | corpo | corps | Koerper | corpo |
| `within.muddy` | low mids | medios graves | medios graves | bas-medium | tiefe Mitten | medio-bassi |
| `within.harsh` | presence | presencia | presenca | presence | Praesenz | presenza |
| `within.no-air` | top end | agudos | agudos | aigus | Hoehen | acuti |
| `within.hollow-centre` | centre | centro | centro | centre | Mitte | centro |
| `within.hole` | bands | bandas | bandas | bandes | Baender | bande |
| `within.quiet-section` | loudness | loudness | loudness | loudness | Lautheit | loudness |
| `within.peaks` | peaks | picos | picos | cretes | Peaks | picchi |
| `within.out-of-phase` | mono | mono | mono | mono | Mono | mono |
| `within.imbalance` | balance | balance | equilibrio | equilibre | Balance | bilanciamento |
| `within.dc` | DC | continua | continua | continu | Gleichanteil | continua |
| `crushed.ok` | Transients have room: PSR {valor} dB (floor {valor2} dB) | Los transitorios tienen aire: PSR {valor} dB (piso {valor2} dB) | Os transientes tem espaco: PSR {valor} dB (piso {valor2} dB) | Les transitoires ont de la place : PSR {valor} dB (plancher {valor2} dB) | Die Transienten haben Raum: PSR {valor} dB (Grenze {valor2} dB) | I transienti hanno spazio: PSR {valor} dB (minimo {valor2} dB) |
| `thin.ok` | Body 150-400 Hz: {valor} dB {valor3} (thin from -{valor2} dB) | Cuerpo 150-400 Hz: {valor} dB {valor3} (delgado desde -{valor2} dB) | Corpo 150-400 Hz: {valor} dB {valor3} (fino a partir de -{valor2} dB) | Corps 150-400 Hz : {valor} dB {valor3} (maigre a partir de -{valor2} dB) | Koerper 150-400 Hz: {valor} dB {valor3} (duenn ab -{valor2} dB) | Corpo 150-400 Hz: {valor} dB {valor3} (sottile da -{valor2} dB) |
| `muddy.ok` | Low mids 200-500 Hz: {valor} dB {valor3} (muddy from +{valor2} dB) | Medios graves 200-500 Hz: {valor} dB {valor3} (turbio desde +{valor2} dB) | Medios graves 200-500 Hz: {valor} dB {valor3} (embolado a partir de +{valor2} dB) | Bas-medium 200-500 Hz : {valor} dB {valor3} (boueux a partir de +{valor2} dB) | Tiefe Mitten 200-500 Hz: {valor} dB {valor3} (matschig ab +{valor2} dB) | Medio-bassi 200-500 Hz: {valor} dB {valor3} (impastato da +{valor2} dB) |
| `harsh.ok` | Presence 2-5 kHz: {valor} dB {valor3} (harsh from +{valor2} dB) | Presencia 2-5 kHz: {valor} dB {valor3} (aspero desde +{valor2} dB) | Presenca 2-5 kHz: {valor} dB {valor3} (aspero a partir de +{valor2} dB) | Presence 2-5 kHz : {valor} dB {valor3} (agressif a partir de +{valor2} dB) | Praesenz 2-5 kHz: {valor} dB {valor3} (hart ab +{valor2} dB) | Presenza 2-5 kHz: {valor} dB {valor3} (aspro da +{valor2} dB) |
| `harsh.ok.brief` | Presence 2-5 kHz: over +{valor3} dB only {valor} % of the time (harsh from {valor2} %) | Presencia 2-5 kHz: pasa +{valor3} dB solo el {valor} % del tiempo (aspero desde el {valor2} %) | Presenca 2-5 kHz: passa de +{valor3} dB so {valor} % do tempo (aspero a partir de {valor2} %) | Presence 2-5 kHz : au-dessus de +{valor3} dB seulement {valor} % du temps (agressif a partir de {valor2} %) | Praesenz 2-5 kHz: ueber +{valor3} dB nur {valor} % der Zeit (hart ab {valor2} %) | Presenza 2-5 kHz: sopra +{valor3} dB solo il {valor} % del tempo (aspro dal {valor2} %) |
| `no-air.ok` | Top end open: {valor} dB above 10 kHz {valor3} (no air from -{valor2} dB) | Agudos abiertos: {valor} dB por encima de 10 kHz {valor3} (sin aire desde -{valor2} dB) | Agudos abertos: {valor} dB acima de 10 kHz {valor3} (sem ar a partir de -{valor2} dB) | Aigus ouverts : {valor} dB au-dessus de 10 kHz {valor3} (sans air a partir de -{valor2} dB) | Offene Hoehen: {valor} dB oberhalb 10 kHz {valor3} (keine Luft ab -{valor2} dB) | Acuti aperti: {valor} dB sopra i 10 kHz {valor3} (senza aria da -{valor2} dB) |
| `hollow-centre.ok` | Width {valor} in the mids, {valor2} in the highs (hollow centre below {t0} with over {t1}) | Width {valor} en los medios, {valor2} en los agudos (centro vacio con menos de {t0} y mas de {t1}) | Width {valor} nos medios, {valor2} nos agudos (centro vazio abaixo de {t0} com mais de {t1}) | Width {valor} dans les mediums, {valor2} dans les aigus (centre vide sous {t0} avec plus de {t1}) | Width {valor} in den Mitten, {valor2} in den Hoehen (hohle Mitte unter {t0} mit ueber {t1}) | Width {valor} nei medi, {valor2} negli acuti (centro vuoto sotto {t0} con oltre {t1}) |
| `hole.ok` | No band dips {valor} dB below its own average for {valor2} s (longest run {t0} s) | Ninguna banda cae {valor} dB bajo su propia media durante {valor2} s (racha mas larga {t0} s) | Nenhuma banda cai {valor} dB abaixo da propria media por {valor2} s (sequencia mais longa {t0} s) | Aucune bande ne descend de {valor} dB sous sa moyenne pendant {valor2} s (plus longue serie {t0} s) | Kein Band faellt {valor} dB unter sein Mittel fuer {valor2} s (laengste Strecke {t0} s) | Nessuna banda scende di {valor} dB sotto la sua media per {valor2} s (serie piu lunga {t0} s) |
| `quiet-section.ok` | Loudness holds: LRA {valor} LU, no section {valor2} LU under the integrated for {t0} s | El loudness se sostiene: LRA {valor} LU, ninguna seccion {valor2} LU bajo el integrado durante {t0} s | O loudness se mantem: LRA {valor} LU, nenhum trecho {valor2} LU abaixo do integrado por {t0} s | Le loudness tient : LRA {valor} LU, aucun passage {valor2} LU sous l'integre pendant {t0} s | Die Lautheit haelt: LRA {valor} LU, keine Passage {valor2} LU unter dem Integrierten fuer {t0} s | Il loudness tiene: LRA {valor} LU, nessuna sezione {valor2} LU sotto l'integrato per {t0} s |
| `peaks.ok` | No clip bursts over {valor} dBTP ({valor2} clip events in total) | Sin rafagas de clip sobre {valor} dBTP ({valor2} eventos de clip en total) | Sem rajadas de clip acima de {valor} dBTP ({valor2} eventos de clip no total) | Aucune rafale de clip au-dessus de {valor} dBTP ({valor2} evenements de clip au total) | Keine Clip-Salven ueber {valor} dBTP ({valor2} Clip-Ereignisse insgesamt) | Nessuna raffica di clip sopra {valor} dBTP ({valor2} eventi di clip in totale) |
| `out-of-phase.ok` | No band cancels in mono (lowest mean correlation {valor}) | Ninguna banda se cancela en mono (correlacion media mas baja {valor}) | Nenhuma banda se cancela em mono (correlacao media mais baixa {valor}) | Aucune bande ne s'annule en mono (correlation moyenne la plus basse {valor}) | Kein Band loescht sich in Mono aus (niedrigste mittlere Korrelation {valor}) | Nessuna banda si cancella in mono (correlazione media piu bassa {valor}) |
| `imbalance.ok` | L/R balanced: {valor} dB on average (imbalance from {valor2} dB held {t0} s) | L/R balanceado: {valor} dB de media (desbalance desde {valor2} dB durante {t0} s) | L/R equilibrado: {valor} dB em media (desequilibrio a partir de {valor2} dB por {t0} s) | L/R equilibre : {valor} dB en moyenne (desequilibre a partir de {valor2} dB tenu {t0} s) | L/R ausgeglichen: {valor} dB im Mittel (Ungleichgewicht ab {valor2} dB ueber {t0} s) | L/R bilanciato: {valor} dB in media (sbilanciamento da {valor2} dB per {t0} s) |
| `dc.ok` | No DC offset: {valor} on L, {valor2} on R (limit {t0}) | Sin continua: {valor} en L, {valor2} en R (limite {t0}) | Sem componente continua: {valor} em L, {valor2} em R (limite {t0}) | Pas de composante continue : {valor} sur L, {valor2} sur R (limite {t0}) | Kein Gleichanteil: {valor} auf L, {valor2} auf R (Grenze {t0}) | Nessuna componente continua: {valor} su L, {valor2} su R (limite {t0}) |

## 3 · Resumen

| | |
|---|---|
| Claves de UI sin valor propio (salen por fallback a inglés) | **0** |
| Frases de VERDICT sin valor propio (idem) | **0** |
| Claves que existen en otro idioma pero no en inglés (texto muerto) | **0** |
