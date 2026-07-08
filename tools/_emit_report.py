#!/usr/bin/env python3
# tools/_emit_report.py — emite el validate-report (schemas/validate-report.json) desde
# variables de entorno + archivos de error/log que setea validate.sh. Determinístico (0 tokens).
# No se llama solo; es el emisor de JSON de validate.sh (escaping correcto).
import json
import os


def envb(name):  # "1" → True
    return os.environ.get(name, "0") == "1"


def readlines(path, limit=60):
    try:
        with open(path) as f:
            lines = [ln.rstrip("\n") for ln in f if ln.strip()]
        return lines[-limit:]
    except OSError:
        return []


def readtail(path, n=4000):
    try:
        with open(path) as f:
            return f.read()[-n:]
    except OSError:
        return ""


def gate(status):
    return {"ok": status == "passed", "status": status}


def archs_list(raw):
    # "PLUGIN=[arm64 x86_64] " → set de arquitecturas vistas
    out = []
    for tok in ("arm64", "x86_64"):
        if tok in raw:
            out.append(tok)
    return out


peak = float(os.environ.get("PEAK", "-1") or "-1")
ceiling = float(os.environ.get("CEILING", "0.85") or "0.85")
failures = [f for f in os.environ.get("T_FAILURES", "").split(";") if f.strip()]


def fnum(name):
    """float del entorno o None si vacío/null."""
    v = os.environ.get(name, "")
    if v in ("", "null", "None"):
        return None
    try:
        return float(v)
    except ValueError:
        return None


def fint(name):
    v = fnum(name)
    return int(v) if v is not None else None


def gate3(status):
    # H7/H8/H9: passed/failed/warn/skip → ok True sólo si passed o warn (warn no rompe el verde).
    return {"ok": status in ("passed", "warn"), "status": status}

report = {
    "plugin": os.environ.get("PLUGIN", ""),
    "ok": envb("OK"),
    "stage": os.environ.get("STAGE", "build"),
    "build": {
        "ok": envb("BUILD_OK"),
        "warnings": int(os.environ.get("WARNINGS", "0") or "0"),
        "target": os.environ.get("TARGET", ""),
    },
    "universal": {
        "ok": envb("UNIVERSAL_OK"),
        "archs": archs_list(os.environ.get("ARCHS", "")),
    },
    "pluginval": {
        "vst3": gate(os.environ.get("PV_VST3", "failed")),
        "au": gate(os.environ.get("PV_AU", "failed")),
        "strictness": 8,
    },
    "auval": {
        "ok": envb("AUVAL_OK"),
        "code": os.environ.get("CODE", ""),
        "manu": os.environ.get("MANU", ""),
        "status": os.environ.get("AUVAL_STATUS", "failed"),
    },
    "tests": {
        "total": int(os.environ.get("T_TOTAL", "0") or "0"),
        "passed": int(os.environ.get("T_PASS", "0") or "0"),
        "failed": int(os.environ.get("T_FAIL", "0") or "0"),
        "failures": failures,
    },
    "peak": peak,
    "ceiling": ceiling,
    "clip": os.environ.get("CLIP", "false") == "true",
    # ── Mediciones del sello (house-standard §2): los 3 NÚMEROS PÚBLICOS (alias floor / latencia / CPU) +
    #    IACC, con sus puertas H7/H8/H9. Genérico por plugin (null si el plugin no provee los tests).
    "measurements": {
        "alias_floor_dbfs": fnum("M_ALIAS"),
        "latency_samples": fint("M_LAT_REAL"),
        "latency_reported": fint("M_LAT_REP"),
        "cpu_pct": fnum("M_CPU"),
        "iacc": fnum("M_IACC"),
        "gates": {
            "alias": gate3(os.environ.get("H_ALIAS", "skip")),     # H7 (duro)
            "latency": gate3(os.environ.get("H_LATENCY", "skip")), # H8 (duro)
            "cpu": gate3(os.environ.get("H_CPU", "skip")),         # H9 (warn)
        },
        "thresholds": {
            "alias_floor_dbfs": fnum("ALIAS_FLOOR"),
            "cpu_budget_pct": fnum("CPU_BUDGET"),
            "latency_tol_samples": fint("LAT_TOL"),
        },
    },
    "errors": readlines(os.environ.get("ERR_FILE", "")),
    "log": readtail(os.environ.get("LOG_FILE", "")),
}

print(json.dumps(report))
