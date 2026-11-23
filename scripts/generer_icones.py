"""Régénère les images du logo dans resources/logo/ depuis les SVG de Logo/.

- logo.png et logo.ico : depuis logo polyptyque fond.svg (icônes, logo en haut à gauche) ;
- logo_home.png : depuis logo polyptyque.svg, sans fond et en gris, au centre de la page d'accueil.

Qt est installé sans le module Qt SVG : l'application charge donc un PNG (en-tête de l'interface,
icône de fenêtre) et l'exécutable Windows embarque un .ico (Explorateur, barre des tâches).
À relancer uniquement quand le logo change, puis commiter les fichiers générés.

Prérequis : Windows avec Microsoft Edge (rendu du SVG) et Pillow (`pip install pillow`).
Usage : python scripts/generer_icones.py
"""

import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

RACINE = Path(__file__).resolve().parent.parent
SVG_AVEC_FOND = RACINE / "Logo" / "logo polyptyque fond.svg"
SVG_SANS_FOND = RACINE / "Logo" / "logo polyptyque.svg"
SORTIE = RACINE / "resources" / "logo"
EDGE = Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe")
COTE_RENDU = 1024
RAYON_COINS = 0.18  # fraction du côté : coins arrondis façon icône d'application
TAILLES_ICO = [16, 24, 32, 48, 64, 128, 256]
COTE_ACCUEIL = 768  # affiché à 400 px logiques : reste net jusqu'à un écran à 200 %
# Gris du logo d'accueil sur le fond #303030 : le trait bleu foncé devient le gris le plus clair,
# les pointillés turquoise un gris plus sombre. Gris = GRIS_MIN + (255 - luminance) * PENTE_GRIS.
GRIS_MIN = 40
PENTE_GRIS = 0.45


def rendre_svg(svg: Path, dossier: Path) -> Image.Image:
    page = dossier / f"{svg.stem}.html"
    page.write_text(
        f'<html><body style="margin:0"><img src="{svg.as_uri()}" '
        f'style="width:{COTE_RENDU}px;height:{COTE_RENDU}px;display:block"></body></html>',
        encoding="utf-8",
    )
    capture = dossier / f"{svg.stem}.png"
    subprocess.run(
        [str(EDGE), "--headless=new", "--disable-gpu", "--hide-scrollbars",
         f"--user-data-dir={dossier / 'edge'}", "--default-background-color=00000000",
         f"--window-size={COTE_RENDU},{COTE_RENDU}", f"--screenshot={capture}", page.as_uri()],
        check=True, capture_output=True,
    )
    return Image.open(capture).convert("RGBA")


def arrondir(image: Image.Image) -> Image.Image:
    masque = Image.new("L", image.size, 0)
    rayon = int(image.width * RAYON_COINS)
    ImageDraw.Draw(masque).rounded_rectangle((0, 0, image.width - 1, image.height - 1), rayon, fill=255)
    image.putalpha(masque)
    return image


def griser(image: Image.Image) -> Image.Image:
    """Garde la transparence ; remplace chaque couleur par un gris d'autant plus clair qu'elle était foncée."""
    gris = image.convert("RGB").convert("L").point(lambda luminance: round(GRIS_MIN + (255 - luminance) * PENTE_GRIS))
    return Image.merge("RGBA", (gris, gris, gris, image.getchannel("A")))


def main() -> int:
    if not EDGE.exists():
        print(f"Edge introuvable : {EDGE}", file=sys.stderr)
        return 1
    SORTIE.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as dossier:
        logo = arrondir(rendre_svg(SVG_AVEC_FOND, Path(dossier)))
        accueil = griser(rendre_svg(SVG_SANS_FOND, Path(dossier)))
    logo.resize((256, 256), Image.LANCZOS).save(SORTIE / "logo.png", optimize=True)
    logo.save(SORTIE / "logo.ico", sizes=[(t, t) for t in TAILLES_ICO])
    accueil.resize((COTE_ACCUEIL, COTE_ACCUEIL), Image.LANCZOS).save(SORTIE / "logo_home.png", optimize=True)
    print(f"Écrit : logo.png, logo.ico et logo_home.png dans {SORTIE}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
