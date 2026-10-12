"""Compare CorrelationPhase (C++) à mesurer_decalage (Python de référence) sur les paires réelles du README.

Usage (avec le venv du dépôt de recherche, OpenCV C++ dans le PATH) :
    python tests/comparer_python.py [chemin/vers/test_correlation_phase.exe]

Le Python de référence est le prototype (correlation_phase.py) : sous-pixel par parabole, pas de marge de rail.
Le C++ suit la version révisée (DFT suréchantillonnée, marge du rail) : des écarts de quelques dixièmes
de pixel sont donc attendus, pas des écarts de plusieurs pixels.
"""
import json
import os
import subprocess
import sys
import time

ICI = os.path.dirname(os.path.abspath(__file__))
RECHERCHE = os.path.normpath(os.path.join(ICI, "..", "..", "..", "KIKIRPA-Research"))
DATA = os.path.join(RECHERCHE, "data", "01-Nominal")
sys.path.insert(0, os.path.join(RECHERCHE, "FeaturesExtraction", "Correlation-de-phase"))
from correlation_phase import charger, mesurer_decalage, rapport_pics  # noqa: E402


def savery(n):
    return os.path.join(DATA, "IRR-1-2-Savery", f"{n:02d}_no filter f11 v30 20cm.tif")


def vdv(n):
    return os.path.join(DATA, "IRR-1-1-VanDerVeken", f"{n}_no filter f8 v30 20cm.tif")


# (nom, A, B, position de B par rapport à A) : paires voisines du README de la piste
PAIRES = [
    ("Savery 01 -> 02", savery(1), savery(2), "haut"),
    ("Savery 02 -> 03", savery(2), savery(3), "haut"),
    ("Savery 03 -> 04", savery(3), savery(4), "droite"),
    ("Savery 04 -> 05", savery(4), savery(5), "bas"),
    ("Savery 05 -> 06", savery(5), savery(6), "bas"),
    ("Savery 01 -> 06", savery(1), savery(6), "droite"),
    ("Savery 02 -> 05", savery(2), savery(5), "droite"),
    ("VanDerVeken 1 -> 2", vdv(1), vdv(2), "haut"),
    ("VanDerVeken 2 -> 3", vdv(2), vdv(3), "droite"),
    ("VanDerVeken 3 -> 4", vdv(3), vdv(4), "bas"),
    ("VanDerVeken 1 -> 4", vdv(1), vdv(4), "droite"),
]


def mesure_python(chemin_a, chemin_b, position):
    _, A = charger(chemin_a)
    _, B = charger(chemin_b)
    debut = time.perf_counter()
    ty, tx, _, surface = mesurer_decalage(A, B, position)
    return {"x": tx, "y": ty, "rapport": rapport_pics(surface), "ms": (time.perf_counter() - debut) * 1000}


def mesure_cpp(exe, chemin_a, chemin_b, position):
    sortie = subprocess.run([exe, "--paire", chemin_a, chemin_b, position], capture_output=True, text=True, check=True)
    return json.loads(sortie.stdout)


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ICI, "..", "build-msvc", "test_correlation_phase.exe")
    print(f"{'paire':<20}{'pos':>7} | {'Python x':>9}{'y':>10}{'rapp.':>6} | {'C++ x':>9}{'y':>10}{'pts':>5}"
          f"{'pts méd':>8} | {'écart x':>8}{'y':>7} | {'ms py':>6}{'ms C++':>7}")
    ecarts = []
    for nom, a, b, position in PAIRES:
        py = mesure_python(a, b, position)
        cpp = mesure_cpp(exe, a, b, position)
        if not cpp["ok"]:
            print(f"{nom:<20}{position:>7} | {py['x']:9.2f}{py['y']:10.2f}{py['rapport']:6.1f} | C++ : mesure refusée (false)")
            continue
        ex, ey = cpp["x"] - py["x"], cpp["y"] - py["y"]
        ecarts.append(max(abs(ex), abs(ey)))
        med = cpp["ecart_points_median"]
        print(f"{nom:<20}{position:>7} | {py['x']:9.2f}{py['y']:10.2f}{py['rapport']:6.1f} | {cpp['x']:9.2f}{cpp['y']:10.2f}"
              f"{cpp['nb_points']:5d}{(med if med is not None else float('nan')):8.2f} | {ex:+8.2f}{ey:+7.2f} | "
              f"{py['ms']:6.0f}{cpp['ms']:7.0f}")
    if ecarts:
        ecarts.sort()
        print(f"\nÉcart C++ / Python (max des deux axes) : médiane {ecarts[len(ecarts) // 2]:.2f} px, max {ecarts[-1]:.2f} px"
              f" sur {len(ecarts)} / {len(PAIRES)} paires")


if __name__ == "__main__":
    main()
