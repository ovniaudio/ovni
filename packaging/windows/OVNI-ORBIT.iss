; OVNI · ORBIT — instalador de Windows (Inno Setup, gratis y open source).
; Objetivo: la experiencia "pocos clicks" del pkg de Mac — doble clic → Install → listo.
; Instala ORBIT.vst3 (bundle-carpeta VST3) en C:\Program Files\Common Files\VST3 y deja
; desinstalador en "Agregar o quitar programas". SIN firma Authenticode (SmartScreen avisa
; una vez: "Más información → Ejecutar de todos modos" — mismo esquema gratis que macOS
; sin notarizar; la web ya explica el patrón).
;
; Compilar (en Windows o en CI windows-latest, Inno viene preinstalado en los runners):
;   iscc /DAppVersion=0.2.0 /DBundleDir=stage packaging\windows\OVNI-ORBIT.iss
; donde %BundleDir%\ORBIT.vst3\ es el bundle extraído del zip de Windows del release.
; Ver ci-job.yml (job listo para pegar en release.yml) y README.md de esta carpeta.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BundleDir
  #define BundleDir "stage"
#endif

[Setup]
; AppId FIJO para siempre: así los upgrades pisan la misma entrada de desinstalación.
AppId={{8E7A2F60-5B1C-4B7E-9A43-0C41B2D3E4F5}
AppName=OVNI ORBIT
AppVersion={#AppVersion}
AppVerName=OVNI ORBIT v{#AppVersion}
AppPublisher=OVNI Audio
AppPublisherURL=https://ovniaudio.com
AppSupportURL=https://ovniaudio.com/orbit
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=OVNI-ORBIT-v{#AppVersion}-Setup
OutputDir=out
Compression=lzma2/max
SolidCompression=yes
Uninstallable=yes
UninstallDisplayName=OVNI ORBIT (VST3)

[Files]
; El bundle VST3 es una CARPETA (VST3 3.6.10+): se copia recursiva tal cual.
Source: "{#BundleDir}\ORBIT.vst3\*"; DestDir: "{commoncf64}\VST3\ORBIT.vst3"; \
  Flags: recursesubdirs createallsubdirs ignoreversion

[Run]
; Nada que lanzar: es un plugin. El DAW lo escanea en el próximo arranque.

[Messages]
; Un toque de voz OVNI en la pantalla final.
FinishedLabelNoIcons=ORBIT aterrizó en tu carpeta VST3.%nAbrí tu DAW y re-escaneá plugins — listo para orbitar.
