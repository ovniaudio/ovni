#!/usr/bin/env python3
"""make-installer-art.py — Genera el arte de fondo de los instaladores .pkg de macOS.

Installer.app dibuja `<background>` en el PANEL IZQUIERDO de la ventana, DEBAJO de la lista de
pasos (Introducción · Licencia · Destino · …). Por eso:

  · el canvas es VERTICAL (la proporción del panel), no apaisado — con `scaling="proportional"`
    una imagen apaisada se achicaría hasta ser un sello ilegible;
  · el contenido vive en el TERCIO INFERIOR y se ancla `bottomleft` — arriba va la lista de pasos
    del instalador y cualquier cosa que pongamos ahí queda tapada;
  · el fondo es TRANSPARENTE — un PNG opaco recorta un rectángulo sucio sobre el panel.

Emite un par light/dark por módulo. La variante dark NO es opcional: el instalador respeta la
apariencia del sistema y un texto oscuro sobre el panel oscuro desaparece.

Uso:
    make-installer-art.py [--outdir packaging/installer-resources]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# --- Marca (sello/brand/README.md — no re-colorear fuera de esto). ---
CYAN = (0x5E, 0xE7, 0xF0)
VIOLET = (0xC0, 0x84, 0xFC)
BRAND_ROOT = Path.home() / "OVNIAUDIO" / "sello" / "brand"
ICON_SRC = BRAND_ROOT / "ovni-icon-1024.png"

# El squircle trae su propio fondo profundo, así que el mismo activo sirve en claro y en oscuro:
# el núcleo blanco nunca se queda sin contraste. Lo único que cambia por modo es el texto.
THEMES = {
    "light": {"eyebrow": (0x6B, 0x72, 0x80), "name": (0x0A, 0x0C, 0x14)},
    "dark": {"eyebrow": (0x8B, 0x93, 0xA7), "name": (0xF2, 0xF4, 0xF8)},
}

# Canvas @2x con la proporción del panel izquierdo del Installer.
SCALE = 2
W, H = 200 * SCALE, 380 * SCALE
PAD_X, PAD_BOTTOM = 22 * SCALE, 24 * SCALE
ICON_SIZE = 76 * SCALE
GAP_ICON = 18 * SCALE
GAP_EYEBROW = 9 * SCALE
GAP_RULE = 14 * SCALE
RULE_W, RULE_H = 54 * SCALE, 3 * SCALE
EYEBROW_PT, NAME_PT = 9 * SCALE, 21 * SCALE
EYEBROW_TRACK = 2.2 * SCALE

MENLO = "/System/Library/Fonts/Menlo.ttc"
MENLO_REGULAR, MENLO_BOLD = 0, 1

# Todo el catálogo, para que agregar un plugin no deje su instalador sin arte.
MODULES = ["ORBIT", "PULSAR", "NEBULA", "DUST", "HALO", "HORIZON", "AURORA", "SUPERNOVA"]
EYEBROW = "OVNI AUDIO"


def load_font(index: int, size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(MENLO, size=size, index=index)


def draw_tracked(draw: ImageDraw.ImageDraw, xy, text, font, fill, tracking):
    """Dibuja texto con letterspacing — Pillow no lo soporta, se compone glifo a glifo."""
    x, y = xy
    for ch in text:
        draw.text((x, y), ch, font=font, fill=fill)
        x += draw.textlength(ch, font=font) + tracking


def draw_gradient_rule(img: Image.Image, x: int, y: int, w: int, h: int):
    """Hairline cian→violeta. Elemento gráfico, no texto: legible en ambos modos."""
    px = img.load()
    for i in range(w):
        t = i / max(w - 1, 1)
        col = tuple(round(CYAN[c] + (VIOLET[c] - CYAN[c]) * t) for c in range(3)) + (255,)
        for j in range(h):
            px[x + i, y + j] = col


def render(name: str, theme: str, icon: Image.Image) -> Image.Image:
    colors = THEMES[theme]
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    f_eyebrow = load_font(MENLO_REGULAR, EYEBROW_PT)
    f_name = load_font(MENLO_BOLD, NAME_PT)

    # Se apila de abajo hacia arriba: el ancla es el borde inferior del panel.
    y = H - PAD_BOTTOM - RULE_H
    draw_gradient_rule(img, PAD_X, y, RULE_W, RULE_H)

    name_box = draw.textbbox((0, 0), name, font=f_name)
    y -= GAP_RULE + (name_box[3] - name_box[1])
    draw.text((PAD_X, y - name_box[1]), name, font=f_name, fill=colors["name"])

    eyebrow_box = draw.textbbox((0, 0), EYEBROW, font=f_eyebrow)
    y -= GAP_EYEBROW + (eyebrow_box[3] - eyebrow_box[1])
    draw_tracked(draw, (PAD_X, y - eyebrow_box[1]), EYEBROW, f_eyebrow,
                 colors["eyebrow"], EYEBROW_TRACK)

    y -= GAP_ICON + ICON_SIZE
    img.alpha_composite(icon, (PAD_X, y))
    return img


def main() -> int:
    ap = argparse.ArgumentParser()
    default_out = Path(__file__).resolve().parent / "installer-resources"
    ap.add_argument("--outdir", type=Path, default=default_out)
    args = ap.parse_args()

    if not ICON_SRC.is_file():
        print(f"ERROR: no encuentro el ícono de marca en {ICON_SRC}", file=sys.stderr)
        return 1

    args.outdir.mkdir(parents=True, exist_ok=True)
    icon = Image.open(ICON_SRC).convert("RGBA").resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS)

    # "CATALOG" es el arte del instalador completo; make-per-plugin.sh cae a bg-<theme>.png
    # cuando un módulo no tiene el suyo.
    targets = [(n, n.lower()) for n in MODULES] + [("CATALOG", "all"), ("CATALOG", "")]
    for name, slug in targets:
        for theme in THEMES:
            suffix = f"-{slug}" if slug else ""
            out = args.outdir / f"bg{suffix}-{theme}.png"
            render(name, theme, icon).save(out, "PNG", optimize=True)
            print(f"  ✓ {out.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
