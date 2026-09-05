# Instalador de Windows (Inno Setup) — gratis, pocos clicks

**Qué es:** la experiencia del pkg de Mac pero en Windows — `OVNI-ORBIT-v<VER>-Setup.exe`:
doble clic → **Install** → listo. Copia `ORBIT.vst3` a `C:\Program Files\Common Files\VST3`
y deja desinstalador en "Agregar o quitar programas". Reemplaza el flujo manual del ZIP
(desbloquear → extraer → copiar a mano).

**Costo: $0.** Inno Setup es gratis/open source; GitHub Actions es gratis en repos
públicos (los runners `windows-latest` ya traen `iscc` preinstalado). Lo único pago del
mundo Windows sería un certificado Authenticode para silenciar SmartScreen — **NO hace
falta**: sin firma, SmartScreen avisa una vez ("Más información → Ejecutar de todos
modos"), mismo esquema gratis que veníamos usando en macOS sin notarizar.

## Archivos

- `OVNI-ORBIT.iss` — script del instalador (parametrizado: `/DAppVersion` y `/DBundleDir`).
  `AppId` fijo `{8E7A2F60-…}` para que los upgrades pisen la misma entrada de desinstalación.
- `ci-job.yml` — job listo para pegar en el `release.yml` del repo público en la ola 2:
  extrae el bundle del zip de Windows, compila el Setup, hace **smoke test real**
  (instalación `/VERYSILENT` en el runner + verificación de que el .vst3 aterrizó) y sube
  el artefacto.

## Compilar a mano (si hay una Windows a mano)

```bat
iscc /DAppVersion=0.2.0 /DBundleDir=stage packaging\windows\OVNI-ORBIT.iss
:: con stage\ORBIT.vst3\ = bundle extraído del OVNI-ORBIT-v0.2.0-Windows.zip
```

## Pendientes al integrarlo (ola 2)

1. Pegar el job de `ci-job.yml` en `release.yml` (ajustar `needs`/nombre del artefacto del zip).
2. Subir el Setup.exe como asset del release y sumar el botón "ORBIT — Windows ↓ (installer)"
   en la web (`build-plugins.py` `win_btn` + copy `dl.install` de Windows: cambiar
   "desbloqueá el ZIP…" por la nota de SmartScreen). El ZIP puede quedar como alternativa.
3. Cuando salgan los otros 6 plugins de audio en Windows: generalizar el .iss (loop de
   bundles o un .iss por plugin — mismo patrón).
4. (Opcional, si SmartScreen molesta a escala) firma Authenticode: Azure Trusted Signing
   (~US$10/mes) o cert clásico — decisión de negocio, nunca requisito.
