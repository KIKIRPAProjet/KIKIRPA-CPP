"""Détecte Qt, compile le projet Frontend, puis lance l'exécutable(pour ne pas trainer des versions différentes de exe)."""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent


# Retourne le dossier Qt portant la version numerique la plus récente, ou None
# si le dossier de base n'existe pas ou ne contient aucune version valide
def findLatestQtRoot(baseDir: Path) -> Path | None:
    if not baseDir.exists():
        return None

    versions = []
    for child in baseDir.iterdir():
        if not child.is_dir():
            continue
        try:
            version = tuple(int(part) for part in child.name.split("."))
        except ValueError:
            continue
        versions.append((version, child))

    if not versions:
        return None

    return max(versions, key=lambda item: item[0])[1]


# Retourne le dossier MinGW dont le nom contient la version numérique la plus
# élevée, ou None si le dossier de base n'existe pas ou ne contient aucun candidat
def findMingwDir(baseDir: Path) -> Path | None:
    if not baseDir.exists():
        return None

    candidates = [path for path in baseDir.iterdir() if path.is_dir() and "mingw" in path.name.lower()]
    if not candidates:
        return None

    # Retourne une clé numérique permettant de comparer les versions MinGW
    def versionKey(path: Path):
        numbers = [int(part) for part in re.findall(r"\d+", path.name)]
        while len(numbers) < 2:
            numbers.append(0)
        return tuple(numbers)

    return max(candidates, key=versionKey)


# Résout les chemins nécessaires à Qt, MinGW, CMake et Ninja
# Lève FileNotFoundError si aucun dossier Qt utilisable n'est trouvé
def resolvePaths() -> tuple[Path, Path, Path, Path, Path]:
    qtDirStr = os.environ.get("QT_DIR") or os.environ.get("QTDIR")
    qtDir = Path(qtDirStr) if qtDirStr else None
    if qtDir is None or not qtDir.exists():
        qtDir = findLatestQtRoot(Path(r"C:\Qt"))

    if qtDir is None or not qtDir.exists():
        raise FileNotFoundError("Aucun dossier Qt n’a été trouvé sous C:\\Qt. Installez Qt 6 et réessayez.")

    mingwDirStr = os.environ.get("MINGW_DIR")
    mingwDir = Path(mingwDirStr) if mingwDirStr else None
    if mingwDir is None or not mingwDir.exists():
        mingwDir = findMingwDir(Path(r"C:\Qt\Tools")) or (qtDir.parent / "Tools" / "mingw1310_64")

    cmakeDir = Path(os.environ.get("CMAKE_DIR") or r"C:\Qt\Tools\CMake_64")
    ninjaDir = Path(os.environ.get("NINJA_DIR") or r"C:\Qt\Tools\Ninja")

    qtPrefix = Path(os.environ.get("QT_PREFIX") or (qtDir / "mingw_64"))
    return qtDir, mingwDir, cmakeDir, ninjaDir, qtPrefix


# Configure l'environnement, compile temporairement le projet Frontend et
# lance l'exécutable
# Retourne 0 pour succès et 1 pour echec
def main() -> int:
    try:
        qtDir, mingwDir, cmakeDir, ninjaDir, qtPrefix = resolvePaths()
    except FileNotFoundError as error:
        print(error)
        return 1

    cmake = cmakeDir / "bin" / "cmake.exe"
    ninja = ninjaDir / "ninja.exe"

    requiredFiles = (cmake, ninja, qtPrefix)
    missing = [str(path) for path in requiredFiles if not path.exists()]
    if missing:
        print("Outil ou dossier Qt introuvable :")
        print("\n".join(f"  - {path}" for path in missing))
        print(f"Qt détecté : {qtDir}")
        return 1

    environment = os.environ.copy()
    environment["PATH"] = os.pathsep.join(
        [
            str(mingwDir / "bin"),
            str(qtPrefix / "bin"),
            str(cmakeDir / "bin"),
            str(ninjaDir),
            environment.get("PATH", ""),
        ]
    )

    temporaryDir: Path | None = None
    try:
        temporaryDir = Path(tempfile.mkdtemp(prefix="Frontend-"))
        print(f"Qt détecté : {qtDir}")
        print(f"Compilation temporaire dans : {temporaryDir}")

        subprocess.run(
            [
                str(cmake),
                "-S",
                str(PROJECT_DIR),
                "-B",
                str(temporaryDir),
                "-G",
                "Ninja",
                f"-DCMAKE_PREFIX_PATH={qtPrefix}",
            ],
            env=environment,
            check=True,
        )

        subprocess.run(
            [str(cmake), "--build", str(temporaryDir), "--parallel", "4"],
            env=environment,
            check=True,
        )

        executable = temporaryDir / "Frontend.exe"
        if not executable.exists():
            print(f"Executable introuvable : {executable}")
            return 1

        print("Lancement de Frontend.exe...")
        subprocess.run([str(executable)], cwd=temporaryDir, check=True)
        return 0
    except subprocess.CalledProcessError as error:
        print(f"La commande a échoué avec le code {error.returncode}.")
        return error.returncode or 1
    except OSError as error:
        print(f"Impossible d'accéder à un fichier ou d'exécuter un outil : {error}")
        return 1
    finally:
        if temporaryDir is not None:
            try:
                shutil.rmtree(temporaryDir)
                print("Fichiers temporaires supprimés.")
            except OSError as error:
                print(f"Impossible de supprimer les fichiers temporaires : {error}")


if __name__ == "__main__":
    sys.exit(main())
