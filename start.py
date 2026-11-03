import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent


def find_latest_qt_root(base_dir: Path) -> Path | None:
    if not base_dir.exists():
        return None

    versions = []
    for child in base_dir.iterdir():
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


def find_mingw_dir(base_dir: Path) -> Path | None:
    if not base_dir.exists():
        return None

    candidates = [path for path in base_dir.iterdir() if path.is_dir() and "mingw" in path.name.lower()]
    if not candidates:
        return None

    def version_key(path: Path):
        numbers = [int(part) for part in re.findall(r"\d+", path.name)]
        while len(numbers) < 2:
            numbers.append(0)
        return tuple(numbers)

    return max(candidates, key=version_key)


def resolve_paths() -> tuple[Path, Path, Path, Path, Path]:
    qt_dir_str = os.environ.get("QT_DIR") or os.environ.get("QTDIR")
    qt_dir = Path(qt_dir_str) if qt_dir_str else None
    if qt_dir is None or not qt_dir.exists():
        qt_dir = find_latest_qt_root(Path(r"C:\Qt"))

    if qt_dir is None or not qt_dir.exists():
        raise FileNotFoundError("Aucun dossier Qt n’a été trouvé sous C:\\Qt. Installez Qt 6 et réessayez.")

    mingw_dir_str = os.environ.get("MINGW_DIR")
    mingw_dir = Path(mingw_dir_str) if mingw_dir_str else None
    if mingw_dir is None or not mingw_dir.exists():
        mingw_dir = find_mingw_dir(Path(r"C:\Qt\Tools")) or (qt_dir.parent / "Tools" / "mingw1310_64")

    cmake_dir = Path(os.environ.get("CMAKE_DIR") or r"C:\Qt\Tools\CMake_64")
    ninja_dir = Path(os.environ.get("NINJA_DIR") or r"C:\Qt\Tools\Ninja")

    qt_prefix = Path(os.environ.get("QT_PREFIX") or (qt_dir / "mingw_64"))
    return qt_dir, mingw_dir, cmake_dir, ninja_dir, qt_prefix


def main() -> int:
    try:
        qt_dir, mingw_dir, cmake_dir, ninja_dir, qt_prefix = resolve_paths()
    except FileNotFoundError as error:
        print(error)
        return 1

    cmake = cmake_dir / "bin" / "cmake.exe"
    ninja = ninja_dir / "ninja.exe"

    required_files = (cmake, ninja, qt_prefix)
    missing = [str(path) for path in required_files if not path.exists()]
    if missing:
        print("Outil ou dossier Qt introuvable :")
        print("\n".join(f"  - {path}" for path in missing))
        print(f"Qt détecté : {qt_dir}")
        return 1

    environment = os.environ.copy()
    environment["PATH"] = os.pathsep.join(
        [
            str(mingw_dir / "bin"),
            str(qt_prefix / "bin"),
            str(cmake_dir / "bin"),
            str(ninja_dir),
            environment.get("PATH", ""),
        ]
    )

    temporary_dir: Path | None = None
    try:
        temporary_dir = Path(tempfile.mkdtemp(prefix="Frontend-"))
        print(f"Qt détecté : {qt_dir}")
        print(f"Compilation temporaire dans : {temporary_dir}")

        subprocess.run(
            [
                str(cmake),
                "-S",
                str(PROJECT_DIR),
                "-B",
                str(temporary_dir),
                "-G",
                "Ninja",
                f"-DCMAKE_PREFIX_PATH={qt_prefix}",
            ],
            env=environment,
            check=True,
        )

        subprocess.run(
            [str(cmake), "--build", str(temporary_dir), "--parallel", "4"],
            env=environment,
            check=True,
        )

        executable = temporary_dir / "Frontend.exe"
        if not executable.exists():
            print(f"Executable introuvable : {executable}")
            return 1

        print("Lancement de Frontend.exe...")
        subprocess.run([str(executable)], cwd=temporary_dir, check=True)
        return 0
    except subprocess.CalledProcessError as error:
        print(f"La commande a échoué avec le code {error.returncode}.")
        return error.returncode or 1
    except OSError as error:
        print(f"Impossible d'accéder à un fichier ou d'exécuter un outil : {error}")
        return 1
    finally:
        if temporary_dir is not None:
            try:
                shutil.rmtree(temporary_dir)
                print("Fichiers temporaires supprimés.")
            except OSError as error:
                print(f"Impossible de supprimer les fichiers temporaires : {error}")


if __name__ == "__main__":
    sys.exit(main())
