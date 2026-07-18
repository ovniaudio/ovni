#!/usr/bin/env python3
"""Impacto visual de cada knob de la franja SUPERNOVA — medido, no a ojo. (v2)

Metodología:
  · spatial  = MAE(default, punta) del MISMO frame — cuánto cambia la IMAGEN al girar el knob
               desde donde está (default APVTS) hasta cada punta de su rango. max sobre puntas/contextos.
  · motionΔ  = |selfDelta(punta) − selfDelta(default)| donde selfDelta = MAE(frame N, frame N+1) —
               cuánto cambia la ENERGÍA DE MOVIMIENTO (un frame quieto vs hirviendo casi no difiere en
               spatial pero sí acá). max sobre puntas/contextos.
  · score    = spatial + 2·motionΔ (el movimiento pesa doble: el ojo lo domina).
Contextos: IDLE (60 frames, rms 0.30) · KICK (frame kick+12, el gesto desarrollado).
FORM se mide con FIGURE=Sphere, AMOUNT con PALETTE=Thermal, BREATHE con MOTION=Matter
(el stepper decide SI, el knob CUÁNTO → se miden con su stepper/modo activo).
Baseline pineado a los defaults REALES del APVTS (--intensity 50 --chaos 30 --size 40).

Uso:  python3 tools/supernova-knob-impact.py     (desde la raiz del repo; corre ~5 min con cache frio;
      requiere el build del render tool — ninja -C build OvniSupernovaRender — y PIL+numpy)
Nota: el orden de la franja (ControlStrip) sale de este ranking; si un cambio de motor lo mueve,
      re-correr y re-evaluar el orden con Joaquin.
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

TOOL = "build/tests/OvniSupernovaRender_artefacts/Release/OvniSupernovaRender"
OUT = Path("/tmp/snv-impact")
OUT.mkdir(parents=True, exist_ok=True)

SIZE = ["--width", "512", "--height", "512"]
BASE = ["--intensity", "50", "--chaos", "30", "--size", "40"]   # defaults APVTS reales
CTX = {
    "idle": {"frames": 60, "extra": []},
    "kick": {"frames": 57, "extra": ["--kick-at", "45"]},   # frame final = kick+12 (gesto desarrollado)
}

# knob -> (flag, [puntas], ctx_extra, nota). Punta "FORCE" = flag booleano.
KNOBS = {
    "INTENSITY": ("--intensity", ["0", "100"], [], ""),
    "CHAOS":     ("--chaos",     ["0", "100"], [], ""),
    "SPEED":     ("--speed",     ["0", "100"], [], ""),
    "PUMP":      ("--pump",      ["0", "100"], [], ""),
    "BLAST":     ("--blast",     ["0", "100"], [], ""),
    "GRAVITY":   ("--gravity",   ["-100", "100"], [], ""),
    "BREATHE":   ("--breathe",   ["0", "100"], ["--motion-mode", "1"], "con MOTION=Matter (donde mas pega; en Contours vive desde 2026-07-12)"),
    "SIZE":      ("--size",      ["0", "100"], [], ""),
    "DENSITY":   ("--density",   ["1"], [], ""),
    "SCATTER":   ("--scatter",   ["100"], [], ""),
    "TRAILS":    ("--trails",    ["100"], [], ""),
    "LINKS":     ("--links",     ["100"], [], ""),
    "CUTOUT":    ("--cutout",    ["FORCE"], [], ""),
    "FORM":      ("--form",      ["0"], ["--figure", "1"], "con FIGURE=Sphere"),
    "DEPTH":     ("--depth",     ["100"], [], ""),
    "ROT X":     ("--rot-x",     ["-120", "120"], [], ""),
    "ROT Y":     ("--rot-y",     ["-120", "120"], [], ""),
    "ORBIT":     ("--orbit",     ["45"], [], ""),
    "ROTATE":    ("--rotate",    ["30"], [], ""),
    "AMOUNT":    ("--color-amt", ["0"], ["--palette", "1"], "con PALETTE=Thermal"),
    "SAT":       ("--sat",       ["0", "100"], [], ""),
    "HUE":       ("--hue",       ["180"], [], ""),
    "HUE CYC":   ("--hue-cycle", ["60"], [], ""),
    "GLOW":      ("--glow",      ["0", "100"], [], ""),
    "VARIATION": ("--variation", ["100"], [], ""),
}


def render(tag: str, ctx: str, nframes_extra: int, extra: list) -> Path:
    cfg = CTX[ctx]
    frames = cfg["frames"] + nframes_extra
    png = OUT / f"{tag}--{ctx}--f{frames}.png"
    if png.exists():
        return png
    cmd = [TOOL, *SIZE, "--frames", str(frames), *cfg["extra"], "--out", str(png), *BASE, *extra]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"FALLO {tag} {ctx}: {r.stderr}", file=sys.stderr)
        sys.exit(1)
    return png


def load(p: Path) -> np.ndarray:
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float32)


def mae(a: Path, b: Path) -> float:
    return float(np.abs(load(a) - load(b)).mean())


def pair(tag: str, ctx: str, extra: list) -> tuple[Path, Path]:
    """Renderiza frame N y N+1 (dos corridas: la sim es determinista)."""
    return render(tag, ctx, 0, extra), render(tag, ctx, 1, extra)


def main() -> None:
    # Defaults por contexto (y por ctx_extra distinto: figure/palette/motion-mode activos)
    default_cache: dict[tuple, tuple[Path, Path]] = {}

    def default_pair(ctx: str, ctx_extra: list) -> tuple[Path, Path]:
        key = (ctx, tuple(ctx_extra))
        if key not in default_cache:
            tag = "baseline" if not ctx_extra else "baseline-" + "_".join(ctx_extra).replace("--", "")
            default_cache[key] = pair(tag, ctx, ctx_extra)
        return default_cache[key]

    results = {}
    for name, (flag, poles, ctx_extra, note) in KNOBS.items():
        spatial, motion = 0.0, 0.0
        for ctx in CTX:
            d0, d1 = default_pair(ctx, ctx_extra)
            self_d = mae(d0, d1)
            for pole in poles:
                extra = ([flag] if pole == "FORCE" else [flag, pole]) + ctx_extra
                tag = f"{name}-{'on' if pole == 'FORCE' else pole}".replace(" ", "_")
                p0, p1 = pair(tag, ctx, extra)
                spatial = max(spatial, mae(d0, p0))
                motion = max(motion, abs(mae(p0, p1) - self_d))
        results[name] = {"spatial": round(spatial, 2), "motion": round(motion, 2),
                         "score": round(spatial + 2.0 * motion, 2), "note": note}
        print(f"{name:10s} spatial={spatial:7.2f} motionD={motion:6.2f} "
              f"score={results[name]['score']:7.2f} {note}", flush=True)

    # El hervor del default (dato para el pitch de CLEAR): cuánto se mueve el mundo default por frame
    d0, d1 = default_cache[("idle", ())]
    print(f"\nselfDelta del DEFAULT idle (el 'hervor' por frame): {mae(d0, d1):.2f} MAE/frame")

    ranked = sorted(results.items(), key=lambda kv: -kv[1]["score"])
    print("\n=== RANKING (score = spatial + 2*motionD, MAE 0-255) ===")
    for i, (name, r) in enumerate(ranked, 1):
        star = f"  [{r['note']}]" if r["note"] else ""
        print(f"{i:2d}. {name:10s} {r['score']:7.2f}  (spatial {r['spatial']:6.2f} · motionD {r['motion']:6.2f}){star}")
    (OUT / "impact.json").write_text(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
