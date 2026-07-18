#!/usr/bin/env python3
# supernova-world-impact — GATE DE CRAFT de los 36 mundos (§9.7, Phase H). Mide OBJETIVAMENTE el impacto de
# cada mundo bajo un kick real: kick-response (cuánto cambia la imagen con el golpe), motion (movimiento
# sostenido) y coverage (cuánto llena/brilla). Ranking + marca los TÍMIDOS (candidatos a reordenar/retocar).
# NO reemplaza el ojo de Joaquín — es el PUNTO DE PARTIDA data-driven para su gate final.
#
# Uso: python3 tools/supernova-world-impact.py   (requiere OvniSupernovaRender + numpy + PIL)
# Rinde en /tmp; imprime la tabla y escribe docs/qa-sessions/supernova-world-impact.md
import re, subprocess, pathlib, sys, tempfile
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "build/tests/OvniSupernovaRender_artefacts/Release/OvniSupernovaRender"
W = H = "384"
KICK = 45                       # frame del kick (scenario 'kick')
PRE, DEV, DEV1 = KICK - 5, KICK + 12, KICK + 13   # pre-kick, kick desarrollado, +1 (motion)
OUT_MD = ROOT / "docs/qa-sessions/supernova-world-impact.md"

def world_names():
    src = (ROOT / "plugins/supernova/source/presets/FactoryPresets.cpp").read_text()
    return re.findall(r'\{\s*"([^"]+)",\s*C::', src)

def render(idx, frame, tmp):
    png = tmp / f"w{idx}_f{frame}.png"
    cmd = [str(TOOL), "--width", W, "--height", H, "--frames", str(frame + 1),
           "--scenario", "kick", "--kick-at", str(KICK), "--preset", str(idx), "--out", str(png)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 or not png.exists():
        print(f"  ! render failed idx={idx} frame={frame}: {r.stderr.strip()[:120]}", file=sys.stderr)
        return None
    return np.asarray(Image.open(png).convert("RGB"), dtype=np.float32)

def luma(a): return 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2]

def main():
    if not TOOL.exists():
        sys.exit("build the render tool first: ninja -C build OvniSupernovaRender")
    names = world_names()
    print(f"Measuring {len(names)} worlds under a real kick...\n")
    rows = []
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        for idx, name in enumerate(names):
            pre, dev, dev1 = render(idx, PRE, tmp), render(idx, DEV, tmp), render(idx, DEV1, tmp)
            if pre is None or dev is None or dev1 is None:
                rows.append((idx, name, 0.0, 0.0, 0.0, 0.0)); continue
            kick_response = float(np.mean(np.abs(luma(dev) - luma(pre))))     # cuánto cambia con el kick
            motion        = float(np.mean(np.abs(luma(dev1) - luma(dev))))    # movimiento sostenido
            ld = luma(dev)
            coverage      = float(np.mean(ld) * 0.5 + np.std(ld) * 0.5)       # cuánto llena/brilla + contraste
            rows.append((idx, name, kick_response, motion, coverage, 0.0))
            print(f"  [{idx:2d}] {name:<14} kick={kick_response:6.2f} motion={motion:5.2f} cover={coverage:6.2f}")

    # Score normalizado (cada eje 0..1 sobre el máximo) → impacto compuesto.
    def col(i): return np.array([r[i] for r in rows], dtype=np.float32)
    kr, mo, co = col(2), col(3), col(4)
    def norm(v): m = v.max(); return v / m if m > 1e-6 else v
    score = 0.45*norm(kr) + 0.25*norm(mo) + 0.30*norm(co)
    rows = [(r[0], r[1], r[2], r[3], r[4], float(score[k])) for k, r in enumerate(rows)]
    ranked = sorted(rows, key=lambda r: -r[5])
    n_timid = max(3, len(rows)//5)
    timid = set(r[0] for r in ranked[-n_timid:])

    md = ["# SUPERNOVA — Craft gate: 36-world impact ranking (Phase H)\n",
          "> Data-driven starting point for Joaquin's final eye. Score = 0.45·kick-response + 0.25·motion + "
          "0.30·coverage, each normalized to the strongest world. Rendered under a real kick (scenario `kick`, "
          "kick@45, measured at the developed gesture kick+12). **This is not a verdict — judge the flagged "
          "ones in VIDEO with music.**\n",
          "| Rank | Idx | World | Score | Kick-resp | Motion | Coverage | Flag |",
          "|-----:|----:|-------|------:|----------:|-------:|---------:|------|"]
    for rank, (idx, name, krv, mov, cov, sc) in enumerate(ranked, 1):
        flag = "⚠ TIMID — review" if idx in timid else ("★ lead" if rank <= 6 else "")
        md.append(f"| {rank} | {idx} | {name} | {sc:.3f} | {krv:.2f} | {mov:.2f} | {cov:.2f} | {flag} |")
    md += ["\n## Proposed reorder (strongest-first defines the first impression)\n",
           "First-6 (leads): " + ", ".join(f"{r[1]}" for r in ranked[:6]),
           "\nTimid (judge in video, retouch or move back): " + ", ".join(sorted(names[i] for i in timid)),
           "\n_The QA already flagged Collapse/Big Bang; cross-check against this ranking._"]
    OUT_MD.parent.mkdir(parents=True, exist_ok=True)
    OUT_MD.write_text("\n".join(md) + "\n")
    print(f"\nLeads: {', '.join(r[1] for r in ranked[:6])}")
    print(f"Timid (flagged): {', '.join(sorted(names[i] for i in timid))}")
    print(f"\nReport → {OUT_MD.relative_to(ROOT)}")

if __name__ == "__main__":
    main()
