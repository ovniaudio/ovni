# =============================================================================================
# tests/ — runners de test UNIFICADOS de la integración (S5). Nativos arm64 (no necesitan ser universales:
# corren acá para verificar, no se distribuyen). Se registran en CTest → `ctest` corre [s1]+[s2]+[s3]+[gain]
# "juntos".
#
# Por qué VARIOS ejecutables: `createPluginFilter()` y `factoryPresets()` son per-plugin (cada plugin define
# LOS SUYOS). El harness [s3] (PresetsTest.cpp) trae SU PROPIA `factoryPresets()` (IDs width/depth/mode), el
# _probe trae la suya (mix/bypass) y PULSAR la suya → no pueden convivir en el mismo binario (símbolo
# duplicado). Por eso el [gain] de PULSAR necesita un exe SEPARADO del _probe. Así que:
#   · OvniTests        = [s1] motores + [s2] ui-kit + [gain] _probe ensamblado   (createPluginFilter/factoryPresets del _probe)
#   · OvniPulsarTests  = [gain][pulsar] PulsarProcessor REAL ensamblado          (createPluginFilter/factoryPresets de PULSAR)
#   · OvniPresetsTests = [s3] infra de presets                                   (factoryPresets del harness)
# El gate anti-clip (tools/gain-staging-check.sh) prefiere OvniPulsarTests para medir PULSAR de verdad.
# =============================================================================================

set(_probe_src  ${CMAKE_SOURCE_DIR}/plugins/_probe/source)
set(_pulsar_src ${CMAKE_SOURCE_DIR}/plugins/pulsar/source)

# ---------------------------------------------------------------------------------------------
# Exe 1 — [s1] + [s2] + [gain].  Un solo main (TestMain de S3: inicializa el MessageManager de JUCE
# + corre la sesión Catch2). S1Tests/UiKitSnapshot/GainTest son TEST_CASEs puros (sin main).
# ---------------------------------------------------------------------------------------------
juce_add_console_app(OvniTests PRODUCT_NAME "OVNI Tests")

target_sources(OvniTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/presets/tests/TestMain.cpp           # main + ScopedJuceInitialiser_GUI
    ${CMAKE_SOURCE_DIR}/shared/engines/movement/tests/S1Tests.cpp   # [s1]
    ${CMAKE_SOURCE_DIR}/shared/engines/stft/tests/StftTests.cpp     # [stft] StftEngine (identity null + COLA + latencia + robustez + ley de potencia)
    ${CMAKE_SOURCE_DIR}/shared/ui-kit/tests/UiKitSnapshot.cpp       # [s2] (modo Catch2)
    ${CMAKE_SOURCE_DIR}/plugins/_probe/tests/GainTest.cpp           # [gain]
    ${CMAKE_SOURCE_DIR}/plugins/_probe/tests/SnapshotTest.cpp       # [snapshot] → /tmp/ovni_probe.png
    ${CMAKE_SOURCE_DIR}/plugins/pulsar/tests/TrajectoryBankTest.cpp # [pulsar] banco de trayectorias
    # el _probe ensamblado que el [gain] necesita (processor/editor + bases del template):
    ${_probe_src}/PluginProcessor.cpp
    ${_probe_src}/PluginEditor.cpp
    ${_probe_src}/FactoryPresets.cpp                                # factoryPresets() del _probe (única acá)
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

target_include_directories(OvniTests PRIVATE ${CMAKE_SOURCE_DIR}/shared ${_probe_src}
    ${CMAKE_SOURCE_DIR}/plugins/pulsar/source)
target_compile_features(OvniTests PRIVATE cxx_std_20)

target_compile_definitions(OvniTests PRIVATE
    OVNI_UIKIT_WITH_CATCH2=1        # [s2] → TEST_CASE en vez de main()
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

# [s1] SofaLoader contra un SOFA real (opcional: el test lo saltea con WARN si no está definido).
if (OVNI_WITH_SOFA AND DEFINED libmysofa_SOURCE_DIR)
    target_compile_definitions(OvniTests PRIVATE
        OVNI_TEST_SOFA="${libmysofa_SOURCE_DIR}/tests/CIPIC_subject_003_hrir_final_itdInDelayField.sofa")
endif ()

target_link_libraries(OvniTests PRIVATE
    Catch2::Catch2
    ovni_movement ovni_stft ovni_dsp ovni_data ovni_uikit ovni_presets
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)

set_target_properties(OvniTests PROPERTIES OSX_ARCHITECTURES "arm64")

# ---------------------------------------------------------------------------------------------
# Exe 2 — [s3] infra de presets (harness de S3, con su propia factoryPresets()). Linkea la lib REAL
# ovni_presets (prueba que archiva + linkea); PresetsTest provee el processor stub + la tabla.
# ---------------------------------------------------------------------------------------------
juce_add_console_app(OvniPresetsTests PRODUCT_NAME "OVNI Presets Tests")

target_sources(OvniPresetsTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/presets/tests/TestMain.cpp
    ${CMAKE_SOURCE_DIR}/shared/presets/tests/PresetsTest.cpp)       # [s3] + su factoryPresets()

target_include_directories(OvniPresetsTests PRIVATE ${CMAKE_SOURCE_DIR}/shared)
target_compile_features(OvniPresetsTests PRIVATE cxx_std_20)

target_compile_definitions(OvniPresetsTests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(OvniPresetsTests PRIVATE
    Catch2::Catch2
    ovni_presets
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)

set_target_properties(OvniPresetsTests PROPERTIES OSX_ARCHITECTURES "arm64")

# ---------------------------------------------------------------------------------------------
# Exe 3 — [gain][pulsar].  El PulsarProcessor REAL ensamblado (processor + editor + motor BINAURAL
# HRIR + SMEAR + su factoryPresets), para que el gate anti-clip mida PULSAR de verdad y NO el _probe.
# Exe SEPARADO porque _probe y PULSAR definen ambos createPluginFilter()/factoryPresets() (símbolo
# duplicado si conviven). PRODUCT_NAME sin espacios → el find del gate lo localiza fácil ("OvniPulsarTests").
# Espeja OvniTests (mismo TestMain de S3 + mismas compile-defs y libs) pero linkea PULSAR, no el _probe.
# ---------------------------------------------------------------------------------------------
juce_add_console_app(OvniPulsarTests PRODUCT_NAME "OvniPulsarTests")

target_sources(OvniPulsarTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/presets/tests/TestMain.cpp          # main + ScopedJuceInitialiser_GUI
    ${CMAKE_SOURCE_DIR}/plugins/pulsar/tests/GainTest.cpp          # [gain][pulsar]
    ${CMAKE_SOURCE_DIR}/plugins/pulsar/tests/QualityTest.cpp         # [quality][pulsar] piso/DC/null mix0/linealidad
    ${CMAKE_SOURCE_DIR}/plugins/pulsar/tests/StereoMeasure.cpp     # [measure][pulsar] imagen/fase (ruido rosa)
    ${CMAKE_SOURCE_DIR}/plugins/pulsar/tests/ResizeTest.cpp        # [resize][pulsar] 3 tamaños + snapshots
    ${CMAKE_SOURCE_DIR}/plugins/pulsar/tests/StellarPadTest.cpp    # [stellarpad][pulsar] fósforo acumula la forma
    ${CMAKE_SOURCE_DIR}/plugins/pulsar/tests/DiccionarioTest.cpp   # [diccionario][pulsar]
    ${CMAKE_SOURCE_DIR}/plugins/pulsar/tests/AliasTest.cpp         # [alias][pulsar] piso de espurias (Manifiesto #3)
    ${CMAKE_SOURCE_DIR}/plugins/pulsar/tests/IrRenderTest.cpp      # [irrender][pulsar] senos estatico/motion -> WAV
    ${CMAKE_SOURCE_DIR}/plugins/pulsar/tests/LatencyTest.cpp       # [latency][pulsar] reportada == real
    ${CMAKE_SOURCE_DIR}/plugins/pulsar/tests/BenchTest.cpp         # [bench][pulsar] CPU%
    # PULSAR ensamblado (processor/editor + UI + su factoryPresets + bases del template):
    ${_pulsar_src}/PluginProcessor.cpp
    ${_pulsar_src}/PluginEditor.cpp
    ${_pulsar_src}/ui/StellarPad.cpp
    ${_pulsar_src}/presets/FactoryPresets.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp
    # Motor BINAURAL HRIR (lo que linkea PULSAR como sources; ver plugins/pulsar/CMakeLists.txt):
    ${CMAKE_SOURCE_DIR}/shared/engines/binaural/SpatialEngine.cpp
    ${CMAKE_SOURCE_DIR}/shared/engines/binaural/Reflections.cpp)

target_include_directories(OvniPulsarTests PRIVATE ${CMAKE_SOURCE_DIR}/shared ${_pulsar_src})
target_compile_features(OvniPulsarTests PRIVATE cxx_std_20)

target_compile_definitions(OvniPulsarTests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(OvniPulsarTests PRIVATE
    Catch2::Catch2
    ovni_movement ovni_dsp ovni_data ovni_uikit ovni_presets
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)

set_target_properties(OvniPulsarTests PROPERTIES OSX_ARCHITECTURES "arm64")

# ---------------------------------------------------------------------------------------------
# Exe 4 — [fdn] motor REVERB FDN + [gain][nebula] NÉBULA ensamblado. FdnTests prueba el motor solo
# (lossless Householder + T60(ω) + breath/freeze); GainTest prueba el NÉBULA REAL (processor + editor +
# motor FDN + su factoryPresets) en el peor caso de clip, para que el gate anti-clip mida NÉBULA de verdad.
# Exe SEPARADO de OvniTests/OvniPulsarTests porque _probe/PULSAR/NÉBULA definen cada uno
# createPluginFilter()/factoryPresets() (símbolo duplicado si conviven). Espeja OvniPulsarTests: mismo
# TestMain de S3 (MessageManager) + Catch2; linkea ovni_fdn (+ ovni_uikit/ovni_presets para el editor +
# presets). FdnTests incluye plano (#include "FdnReverb.h") → shared/engines/fdn en los include dirs;
# GainTest incluye "PluginProcessor.h" → plugins/nebula/source en los include dirs.
# ---------------------------------------------------------------------------------------------
set(_nebula_src ${CMAKE_SOURCE_DIR}/plugins/nebula/source)

juce_add_console_app(OvniNebulaTests PRODUCT_NAME "OvniNebulaTests")

target_sources(OvniNebulaTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/presets/tests/TestMain.cpp           # main + ScopedJuceInitialiser_GUI
    ${CMAKE_SOURCE_DIR}/shared/engines/fdn/tests/FdnTests.cpp       # [fdn] motor FDN lossless Householder
    ${CMAKE_SOURCE_DIR}/plugins/nebula/tests/GainTest.cpp           # [gain][nebula]
    ${CMAKE_SOURCE_DIR}/plugins/nebula/tests/QualityTest.cpp         # [quality][nebula] piso/DC/null mix0/linealidad
    ${CMAKE_SOURCE_DIR}/plugins/nebula/tests/TransientTest.cpp      # [transient][nebula] true-peak/maxJump (crackle)
    ${CMAKE_SOURCE_DIR}/plugins/nebula/tests/ClipScan.cpp           # [clipscan][nebula] mapa de la cadena etapa x etapa
    ${CMAKE_SOURCE_DIR}/plugins/nebula/tests/ReverbMeasure.cpp      # [measure][nebula] T60/CORR/freeze/monosum
    ${CMAKE_SOURCE_DIR}/plugins/nebula/tests/IrRenderTest.cpp       # [irrender][nebula] IRs crudas -> WAV (analisis externo)
    ${CMAKE_SOURCE_DIR}/plugins/nebula/tests/SnapshotTest.cpp       # [snapshot][nebula] → /tmp/ovni_nebula_m.png
    ${CMAKE_SOURCE_DIR}/plugins/nebula/tests/ResizeTest.cpp         # [resize][nebula] S/M/L exactos + snapshots
    ${CMAKE_SOURCE_DIR}/plugins/nebula/tests/DiccionarioTest.cpp   # [diccionario][nebula]
    ${CMAKE_SOURCE_DIR}/plugins/nebula/tests/AliasTest.cpp          # [alias][nebula] piso de espurias (Manifiesto #3)
    ${CMAKE_SOURCE_DIR}/plugins/nebula/tests/LatencyTest.cpp        # [latency][nebula] reportada == real (lookahead)
    ${CMAKE_SOURCE_DIR}/plugins/nebula/tests/BenchTest.cpp          # [bench][nebula] CPU%
    # NÉBULA ensamblado (processor/editor + UI + su factoryPresets + bases del template):
    ${_nebula_src}/PluginProcessor.cpp
    ${_nebula_src}/PluginEditor.cpp
    ${_nebula_src}/ui/NebulaCloud.cpp
    ${_nebula_src}/presets/FactoryPresets.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

target_include_directories(OvniNebulaTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/shared/engines/fdn
    ${_nebula_src})
target_compile_features(OvniNebulaTests PRIVATE cxx_std_20)

target_compile_definitions(OvniNebulaTests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(OvniNebulaTests PRIVATE
    Catch2::Catch2
    ovni_fdn ovni_dsp ovni_uikit ovni_presets
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)

set_target_properties(OvniNebulaTests PROPERTIES OSX_ARCHITECTURES "arm64")

# ---------------------------------------------------------------------------------------------
# Exe 5 — [pitch][scale] ScaleQuantizer (header-only puro) + [pitch] PitchShifter (granular splice-
# overlap, polifónico, multi-voz). MÓDULO COMPARTIDO de pitch (shared/engines/pitch), testeable SOLO:
# no ensambla ningún plugin (no necesita createPluginFilter/factoryPresets) → sólo linkea ovni_pitch
# (+ ovni_dsp por el StereoLimiter). Espeja los otros exes en el main (TestMain de S3: MessageManager
# + sesión Catch2). ScaleTests/PitchTests son TEST_CASEs puros. Lo estrena HALO (SPC·02).
# ---------------------------------------------------------------------------------------------
juce_add_console_app(OvniPitchTests PRODUCT_NAME "OvniPitchTests")

target_sources(OvniPitchTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/presets/tests/TestMain.cpp                # main + ScopedJuceInitialiser_GUI
    ${CMAKE_SOURCE_DIR}/shared/engines/pitch/tests/ScaleTests.cpp        # [pitch][scale] ScaleQuantizer puro
    ${CMAKE_SOURCE_DIR}/shared/engines/pitch/tests/PitchTests.cpp)       # [pitch] PitchShifter (transposición + anti-clip + multi-voz)

target_include_directories(OvniPitchTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/shared/engines/pitch)
target_compile_features(OvniPitchTests PRIVATE cxx_std_20)

target_compile_definitions(OvniPitchTests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(OvniPitchTests PRIVATE
    Catch2::Catch2
    ovni_pitch ovni_dsp
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)

set_target_properties(OvniPitchTests PROPERTIES OSX_ARCHITECTURES "arm64")

# ---------------------------------------------------------------------------------------------
# Exe 6 — HALO (SPC·02): shimmer reverb scale-aware. Reusa el motor FDN de NÉBULA + estrena el módulo
# de pitch (ScaleQuantizer + PitchShifter) en el lazo de feedback. HaloEngine se compila acá (su .cpp),
# como FdnTests compila contra ovni_fdn. Tests:
#   · [stability][halo] + [gain][halo]  → StabilityTest (el lazo NO diverge + anti-clip del motor)
#   · [gain][halo]                       → GainTest (el HaloProcessor REAL en el peor caso, gate anti-clip)
#   · [diccionario][halo]                → DiccionarioTest (tira SCALE/VOICING NO pisa IN PHASE, bounds)
#   · [snapshot][halo]                   → SnapshotTest (editor magenta → /tmp)
# Exe SEPARADO (createPluginFilter/factoryPresets propios). Linkea ovni_fdn + ovni_pitch (+ uikit/presets
# para el editor). HaloEngine.cpp incluye "engines/..." plano → shared en los include dirs.
# ---------------------------------------------------------------------------------------------
set(_halo_src ${CMAKE_SOURCE_DIR}/plugins/halo/source)

juce_add_console_app(OvniHaloTests PRODUCT_NAME "OvniHaloTests")

target_sources(OvniHaloTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/presets/tests/TestMain.cpp           # main + ScopedJuceInitialiser_GUI
    ${CMAKE_SOURCE_DIR}/plugins/halo/tests/StabilityTest.cpp        # [stability][halo] + [gain][halo] motor (octava+quinta, ORBIT=1, FREEZE, 44.1/48/96k)
    ${CMAKE_SOURCE_DIR}/plugins/halo/tests/GainTest.cpp             # [gain][halo] HaloProcessor REAL
    ${CMAKE_SOURCE_DIR}/plugins/halo/tests/QualityTest.cpp         # [quality][halo] piso/DC/null mix0/linealidad
    ${CMAKE_SOURCE_DIR}/plugins/halo/tests/AliasTest.cpp            # [alias][halo] piso de alias del pitch (gate <-96 dBFS)
    ${CMAKE_SOURCE_DIR}/plugins/halo/tests/BenchTest.cpp            # [bench][halo] CPU% (budget FDN ≤5%)
    ${CMAKE_SOURCE_DIR}/plugins/halo/tests/LatencyTest.cpp          # [latency][halo] reportada == real (dry nulo)
    ${CMAKE_SOURCE_DIR}/plugins/halo/tests/MeasureTest.cpp          # [measure][halo] imagen/IACC del wet orbital
    ${CMAKE_SOURCE_DIR}/plugins/halo/tests/IrRenderTest.cpp        # [irrender][halo] IRs/escalera de octavas -> WAV
    ${CMAKE_SOURCE_DIR}/plugins/halo/tests/DiccionarioTest.cpp      # [diccionario][halo] SYNC orbital + FREEZE no pisa IN PHASE
    ${CMAKE_SOURCE_DIR}/plugins/halo/tests/HonestyTest.cpp          # [honestidad][halo] barrido fino 5 posiciones x 6 macros
    ${CMAKE_SOURCE_DIR}/plugins/halo/tests/SnapshotTest.cpp         # [snapshot][halo] → /tmp/ovni_halo_m.png
    ${CMAKE_SOURCE_DIR}/plugins/halo/tests/ResizeTest.cpp           # [resize][halo] S/M/L exactos + snapshots
    # HALO ensamblado (motor + processor/editor + UI + su factoryPresets + bases del template):
    ${_halo_src}/engine/HaloEngine.cpp
    ${_halo_src}/PluginProcessor.cpp
    ${_halo_src}/PluginEditor.cpp
    ${_halo_src}/ui/HaloRings.cpp
    ${_halo_src}/presets/FactoryPresets.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)
    # NOTA: HALO NO usa SpatialEngine/Reflections (HRTF/HRIR). La órbita es un pan binaural ITD/ILD en
    # HaloEngine.cpp (sin convolución ni datos HRIR) → ni el plugin ni este test los compilan/linkean.

target_include_directories(OvniHaloTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${_halo_src})
target_compile_features(OvniHaloTests PRIVATE cxx_std_20)

target_compile_definitions(OvniHaloTests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(OvniHaloTests PRIVATE
    Catch2::Catch2
    ovni_fdn ovni_pitch ovni_movement ovni_dsp ovni_uikit ovni_presets
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)

set_target_properties(OvniHaloTests PROPERTIES OSX_ARCHITECTURES "arm64")

# ---------------------------------------------------------------------------------------------
# Exe 7 — AURORA (SPL·01): paneo espectral mono-safe. Estrena el motor STFT compartido (ovni_stft,
# sembrado en la etapa 1 y reusado por HORIZON). AuroraEngine se compila acá (su .cpp).
# Exe SEPARADO (createPluginFilter/factoryPresets propios). Linkea ovni_stft (+ dsp/uikit/presets).
# ---------------------------------------------------------------------------------------------
set(_aurora_src ${CMAKE_SOURCE_DIR}/plugins/aurora/source)

juce_add_console_app(OvniAuroraTests PRODUCT_NAME "OvniAuroraTests")

target_sources(OvniAuroraTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/presets/tests/TestMain.cpp             # main + ScopedJuceInitialiser_GUI
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/GainTest.cpp             # [gain][aurora] AuroraProcessor REAL
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/QualityTest.cpp         # [quality][aurora] piso/DC/null mix0/linealidad
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/MeasureTest.cpp          # [measure][aurora] imagen del despliegue
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/LatencyTest.cpp          # [latency][aurora] reportada == real (OLA N)
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/AliasTest.cpp            # [alias][aurora] piso lineal + sidebands MOTION
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/BenchTest.cpp            # [bench][aurora] CPU% (budget STFT <=6%)
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/StabilityTest.cpp        # [stability][aurora] tortura de automatizacion
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/RealWorldTest.cpp        # [realworld][aurora] condiciones reales
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/DiccionarioTest.cpp      # [diccionario][aurora] SYNC + bounds + campo
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/SnapshotTest.cpp         # [snapshot][aurora] -> /tmp/ovni_aurora_m.png
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/ResizeTest.cpp           # [resize][aurora] S/M/L exactos + snapshots
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/StubsTest.cpp            # [buffersize]/[staterecall][aurora] stubs del chasis
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/BypassTest.cpp           # [null][aurora] bypass COMPENSADO: dry retrasado N + sin click
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/SyncPhaseTest.cpp        # [diccionario][aurora] SYNC: fase del MOTION cae EN el beat tras PDC
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/ConsistencyTest.cpp      # [consistency][aurora] SR 44.1/48/96 + blocks 32->2048
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/HonestyTest.cpp          # [honestidad][aurora] barrido fino 5 posiciones x 6 macros
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/RenderTest.cpp           # [render][aurora] WAV dry/wet para el A/B de oido
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/AudibleTest.cpp          # [audible][aurora] REGRESION: el despliegue se OYE (banda ancha, dry alineado)
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/MonoAudibleTest.cpp      # [monoaudible][aurora] REGRESION: se OYE sumado a MONO (movimiento espectral, no sólo side)
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/DiagTest.cpp             # [diag][aurora] TEMPORAL: aislar inaudibilidad
    ${CMAKE_SOURCE_DIR}/plugins/aurora/tests/RealBeatTest.cpp         # [realbeat][aurora] DIAGNOSTICO: beat 80s 90bpm REAL -> CORR/WIDTH/espectro
    # AURORA ensamblado (motor + processor/editor + UI + su factoryPresets + bases del template):
    ${_aurora_src}/engine/AuroraEngine.cpp
    ${_aurora_src}/PluginProcessor.cpp
    ${_aurora_src}/PluginEditor.cpp
    ${_aurora_src}/ui/AuroraField.cpp
    ${_aurora_src}/presets/FactoryPresets.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

target_include_directories(OvniAuroraTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${_aurora_src})
target_compile_features(OvniAuroraTests PRIVATE cxx_std_20)

target_compile_definitions(OvniAuroraTests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    OVNI_AURORA_REALBEAT_WAV="${CMAKE_SOURCE_DIR}/tests/assets/realbeat_90.wav")

target_link_libraries(OvniAuroraTests PRIVATE
    Catch2::Catch2
    ovni_stft ovni_dsp ovni_uikit ovni_presets
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)

set_target_properties(OvniAuroraTests PROPERTIES OSX_ARCHITECTURES "arm64")

# ---------------------------------------------------------------------------------------------
# Exe 8 — DUST (MOV.03): ecos binaurales tipo burbujas. Multi-tap delay con feedback estable
# (plugins/dust/source/engine) + banco de 16 direcciones HRIR fijas del anillo SADIE II
# (shared/engines/binaural/HrirRing.h, header-only: DUST no usa SpatialEngine/Reflections; su
# espacializador propio BubbleField convoluciona UNA vez por bus, costo fijo).
# Exe SEPARADO (createPluginFilter/factoryPresets propios).
# ---------------------------------------------------------------------------------------------
set(_dust_src ${CMAKE_SOURCE_DIR}/plugins/dust/source)

juce_add_console_app(OvniDustTests PRODUCT_NAME "OvniDustTests")

target_sources(OvniDustTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/presets/tests/TestMain.cpp           # main + ScopedJuceInitialiser_GUI
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/DustEngineTests.cpp      # [dustengine][dust] estabilidad/espaciado/spread/anti-click/denormals
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/GainTest.cpp             # [gain][dust] DustProcessor REAL, peor caso (PEAK=)
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/QualityTest.cpp         # [quality][dust] piso/DC/null mix0/linealidad
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/MeasureTest.cpp          # [measure][dust] SPREAD abre el campo / IN PHASE mono-safe (IACC=)
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/RealWorldTest.cpp        # [real][dust] grilla SYNC + DUCK + eco audible a MIX realista
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/StabilityTest.cpp        # [stability][dust] automatizacion brutal + DENSIDAD 100 60 s
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/StubsTest.cpp            # [alias]/[latency]/[bench]/[null]/[staterecall]/[transient][dust]
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/ConsistencyTest.cpp      # [consistency][dust] SR 44.1/48/96 + blocks 32/512/2048
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/HonestidadTest.cpp       # [honestidad][dust] barrido 5 posiciones por macro
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/DiccionarioTest.cpp      # [diccionario][dust] bounds disjuntos + drag ORIGIN 1:1 + RATE->campo + signo L/R (BAL)
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/SnapshotTest.cpp         # [snapshot][dust] -> /tmp/ovni_dust_m.png
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/ResizeTest.cpp           # [resize][dust] S/M/L exactos + snapshots
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/RenderTest.cpp           # [render][dust] WAV dry/wet para el A/B de oido
    ${CMAKE_SOURCE_DIR}/plugins/dust/tests/RealBeatTest.cpp         # [realbeat][dust] CORR/WIDTH/espectro sobre el BEAT REAL (no ruido rosa)
    # DUST ensamblado (motor + processor/editor + UI + presets + bases del template). El binaural va
    # como HEADER (HrirRing.h) -- DUST no usa SpatialEngine/Reflections (ver plugins/dust/CMakeLists.txt):
    ${_dust_src}/engine/MultiTapEcho.cpp
    ${_dust_src}/engine/BubbleField.cpp
    ${_dust_src}/engine/DustEngine.cpp
    ${_dust_src}/PluginProcessor.cpp
    ${_dust_src}/PluginEditor.cpp
    ${_dust_src}/ui/DustField.cpp
    ${_dust_src}/presets/FactoryPresets.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

target_include_directories(OvniDustTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${_dust_src})
target_compile_features(OvniDustTests PRIVATE cxx_std_20)

target_compile_definitions(OvniDustTests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    OVNI_DUST_ASSET_DIR="${CMAKE_SOURCE_DIR}/tests/assets")

target_link_libraries(OvniDustTests PRIVATE
    Catch2::Catch2
    ovni_movement ovni_dsp ovni_data ovni_uikit ovni_presets
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)

set_target_properties(OvniDustTests PROPERTIES OSX_ARCHITECTURES "arm64")

# ---------------------------------------------------------------------------------------------
# Exe 9 — HORIZON (SPL·02): freeze ESPECTRAL RÍTMICO. REUSA el motor STFT compartido (ovni_stft,
# sembrado por AURORA). HorizonEngine se compila acá (su .cpp). Exe SEPARADO (createPluginFilter/
# factoryPresets propios). Linkea ovni_stft (+ dsp/uikit/presets), igual que OvniAuroraTests.
# ---------------------------------------------------------------------------------------------
set(_horizon_src ${CMAKE_SOURCE_DIR}/plugins/horizon/source)

juce_add_console_app(OvniHorizonTests PRODUCT_NAME "OvniHorizonTests")

target_sources(OvniHorizonTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/presets/tests/TestMain.cpp             # main + ScopedJuceInitialiser_GUI
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/GainTest.cpp            # [gain][horizon] HorizonProcessor REAL, freeze ON (PEAK=)
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/QualityTest.cpp         # [quality][horizon] piso/DC/null mix0/linealidad
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/MeasureTest.cpp         # [measure][horizon] SPREAD abre el freeze / IN PHASE mono-safe (IACC=)
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/LatencyTest.cpp         # [latency][horizon] reportada == real (OLA N)
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/AliasTest.cpp           # [alias][horizon] piso identity STFT + peaje WHISPER
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/BenchTest.cpp           # [bench][horizon] CPU% (budget STFT <=6%)
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/StabilityTest.cpp       # [stability][horizon] automatizacion brutal + toggle freeze
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/RealWorldTest.cpp       # [real][horizon] grilla SYNC 1/8 + DUCK + freeze audible a MIX 40
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/SyncPhaseTest.cpp       # [diccionario][horizon] SYNC: el latido cae EN el beat tras PDC
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/DiccionarioTest.cpp     # [diccionario][horizon] SYNC + bounds + campo + cortes de curaduria
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/FieldMacrosTest.cpp     # [diccionario][horizon] TODAS las macros mueven el campo + reduced-motion
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/SnapshotTest.cpp        # [snapshot][horizon] -> /tmp/ovni_horizon_m.png
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/ResizeTest.cpp          # [resize][horizon] S/M/L exactos + snapshots
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/BypassTest.cpp          # [bypass][horizon] bypass COMPENSADO: dry retrasado N + sin click
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/ConsistencyTest.cpp     # [consistency][horizon] SR 44.1/48/96 (gate en SEGUNDOS) + blocks 32->2048
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/HonestyTest.cpp         # [honestidad][horizon] barrido fino 5 posiciones x 5 macros (FREEZE on)
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/StubsTest.cpp           # [buffersize]/[staterecall][horizon] stubs del chasis
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/RenderTest.cpp          # [render][horizon] WAV dry/wet para el A/B de oido
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/IrRenderTest.cpp        # [irrender][horizon] freeze de seno 440 -> WAV
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/AudibleTest.cpp         # [audible][horizon] GATE wet-vs-dry alineado: nivel pleno + freeze quieto + spread abre
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/MonoAudibleTest.cpp     # [monoaudible][horizon] REGRESION: el vivo se OYE en MONO (shimmer difuso + gate rítmico)
    ${CMAKE_SOURCE_DIR}/plugins/horizon/tests/RealBeatTest.cpp        # [realbeat][horizon] regresión con el BEAT REAL (no ruido rosa): FREEZE OFF ensancha vivo + freeze + IN PHASE
    # HORIZON ensamblado (motor + processor/editor + UI + su factoryPresets + bases del template):
    ${_horizon_src}/engine/HorizonEngine.cpp
    ${_horizon_src}/PluginProcessor.cpp
    ${_horizon_src}/PluginEditor.cpp
    ${_horizon_src}/ui/HorizonField.cpp
    ${_horizon_src}/presets/FactoryPresets.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

target_include_directories(OvniHorizonTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${_horizon_src})
target_compile_features(OvniHorizonTests PRIVATE cxx_std_20)

target_compile_definitions(OvniHorizonTests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # [realbeat] el beat real para el diagnóstico de imagen sobre MÚSICA (no ruido rosa).
    HORIZON_REALBEAT_WAV="${CMAKE_SOURCE_DIR}/tests/assets/realbeat_90.wav")

target_link_libraries(OvniHorizonTests PRIVATE
    Catch2::Catch2
    ovni_stft ovni_dsp ovni_uikit ovni_presets
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)

set_target_properties(OvniHorizonTests PROPERTIES OSX_ARCHITECTURES "arm64")

# ---------------------------------------------------------------------------------------------
# SUPERNOVA — herramienta de render offscreen → PNG (semilla de --render-frames M4 + capturas QA).
# NO es un test Catch: rinde partículas a una MTLTexture y lee de vuelta a RGBA8 (sin ventana ni permisos).
# Solo APPLE (backend Metal). Compila MetalRenderer.mm como Obj-C++ (mismo patrón que el plugin).
# ---------------------------------------------------------------------------------------------
if(APPLE)
    juce_add_console_app(OvniSupernovaRender PRODUCT_NAME "OvniSupernovaRender")
    target_sources(OvniSupernovaRender PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/supernova/render_tool/RenderMain.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/image/ImageLoader.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/image/ImageField.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/presets/FactoryPresets.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/render/metal/MetalRenderer.mm
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/render/metal/MetalSyphonServer.mm
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/image/VisionField.mm)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/render/metal/MetalRenderer.mm
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/render/metal/MetalSyphonServer.mm
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/image/VisionField.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
        TARGET_DIRECTORY OvniSupernovaRender)
    target_include_directories(OvniSupernovaRender PRIVATE
        ${CMAKE_SOURCE_DIR}/shared
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source)
    target_compile_features(OvniSupernovaRender PRIVATE cxx_std_20)
    target_compile_definitions(OvniSupernovaRender PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(OvniSupernovaRender PRIVATE
        ovni_syphon
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        "-framework Metal" "-framework QuartzCore" "-framework Cocoa" "-framework CoreVideo" "-framework IOSurface" "-framework Vision" "-framework ImageIO")
    set_target_properties(OvniSupernovaRender PROPERTIES OSX_ARCHITECTURES "arm64")
endif()

# ---------------------------------------------------------------------------------------------
# SUPERNOVA — exe de tests ENSAMBLADO (processor+editor reales). Separado porque createPluginFilter()/
# factoryPresets() son per-plugin (duplicate-symbol si conviven con otro plugin). M0: smoke de instanciación
# GPU-opcional + teardown del editor (R1). En M1 se suman [supernova][null] (bit-exacto) y análisis.
# ---------------------------------------------------------------------------------------------
if(APPLE)
    file(GLOB_RECURSE _snv_src CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/*.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/*.mm)

    juce_add_console_app(OvniSupernovaTests PRODUCT_NAME "OvniSupernovaTests")
    target_sources(OvniSupernovaTests PRIVATE
        ${CMAKE_SOURCE_DIR}/shared/presets/tests/TestMain.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/InstantiationTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/AudioAnalyzerTests.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/AnalysisChainTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/FullscreenLiveSmoke.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/PassThroughTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/GainStagingTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/VisGainTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/ImageLoaderTests.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/MidiMapperTests.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/PresetRoundTripTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/GoldenFrameTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/CutoutTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/BoundsContainmentTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/ColorGradeTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/PlexusLinksTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/Depth3DTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/LookRampsTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/UiSnapshotTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/ImageFieldTests.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/StillCanvasTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/CanvasStateTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/PhotoSequenceTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/CanvasFormatTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/MediaThumbsTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/DecodedImageCacheTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/MediaStripTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/MediaSessionEditorTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/HeicDecodeTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/ImageOrientationTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/FitAspectTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/VideoSourceTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/AppAudioTests.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/BeatClockTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/MidiCcMapTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/LfoBankTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/LfoModulationTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/SceneBankTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/PersistenceLocaleTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/UndoStackTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/ExportPresetTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/ExportSmokeTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/MidiCcQueueTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/MidiCueQueueTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/ThumbnailSmokeTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/WorldCardsTest.cpp
        ${CMAKE_SOURCE_DIR}/plugins/supernova/tests/ProcessorIntegrationTest.cpp
        ${_snv_src}
        ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
        ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/render/metal/MetalRenderer.mm
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/render/metal/SupernovaMTKView.mm
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/render/metal/MetalViewComponent.mm
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/render/metal/MetalSyphonServer.mm
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/image/VisionField.mm
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/video/VideoSource.mm
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source/video/VideoExporter.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc" TARGET_DIRECTORY OvniSupernovaTests)
    target_include_directories(OvniSupernovaTests PRIVATE
        ${CMAKE_SOURCE_DIR}/shared
        ${CMAKE_SOURCE_DIR}/plugins/supernova/source
        ${CMAKE_SOURCE_DIR}/plugins/supernova)   # #include "app/AudioSourceModel.h"
    target_compile_features(OvniSupernovaTests PRIVATE cxx_std_20)
    target_compile_definitions(OvniSupernovaTests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(OvniSupernovaTests PRIVATE
        Catch2::Catch2
        ovni_uikit ovni_presets ovni_syphon
        juce::juce_audio_utils juce::juce_gui_basics juce::juce_gui_extra juce::juce_dsp
        juce::juce_recommended_config_flags
        "-framework Metal" "-framework QuartzCore" "-framework Cocoa" "-framework CoreVideo" "-framework IOSurface" "-framework Vision"
        "-framework AVFoundation" "-framework CoreMedia" "-framework ImageIO")
    set_target_properties(OvniSupernovaTests PROPERTIES OSX_ARCHITECTURES "arm64")
    catch_discover_tests(OvniSupernovaTests TEST_PREFIX "[supernova] " DISCOVERY_MODE PRE_TEST)
endif()

# ---------------------------------------------------------------------------------------------
# [listen] — renders de ESCUCHA para la curaduría (tests/ListenTest.cpp, UN archivo compartido
# que usa el createPluginFilter()/factoryPresets() propios de cada exe). WAVs → /tmp.
# ---------------------------------------------------------------------------------------------
foreach(_t OvniPulsarTests OvniNebulaTests OvniDustTests OvniHaloTests OvniHorizonTests OvniAuroraTests)
    target_sources(${_t} PRIVATE ${CMAKE_SOURCE_DIR}/tests/ListenTest.cpp)
    target_compile_definitions(${_t} PRIVATE
        OVNI_LISTEN_BEAT_WAV="${CMAKE_SOURCE_DIR}/tests/assets/realbeat_90.wav")
endforeach()

# ---------------------------------------------------------------------------------------------
# Registro en CTest (descubrimiento diferido a test-time: NO corre los exes durante el build).
# ---------------------------------------------------------------------------------------------
catch_discover_tests(OvniTests        DISCOVERY_MODE PRE_TEST)
catch_discover_tests(OvniPulsarTests  DISCOVERY_MODE PRE_TEST)
catch_discover_tests(OvniPresetsTests DISCOVERY_MODE PRE_TEST)
catch_discover_tests(OvniNebulaTests  DISCOVERY_MODE PRE_TEST)
catch_discover_tests(OvniPitchTests   DISCOVERY_MODE PRE_TEST)
catch_discover_tests(OvniHaloTests    DISCOVERY_MODE PRE_TEST)
# AURORA con prefijo "[aurora] " en el NOMBRE ctest -> los filtros -R '(aurora)' agarran TODO el exe.
catch_discover_tests(OvniAuroraTests  TEST_PREFIX "[aurora] " DISCOVERY_MODE PRE_TEST)
catch_discover_tests(OvniDustTests    DISCOVERY_MODE PRE_TEST)
# HORIZON con prefijo "[horizon] " en el NOMBRE ctest -> los filtros -R '(horizon)' agarran TODO el exe.
catch_discover_tests(OvniHorizonTests TEST_PREFIX "[horizon] " DISCOVERY_MODE PRE_TEST)
