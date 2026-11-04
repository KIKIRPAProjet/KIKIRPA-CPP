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


def pause_before_exit() -> None:
    try:
        input("Appuyez sur Entrée pour fermer cette fenêtre...")
    except EOFError:
        pass


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
        print(error, flush=True)
        pause_before_exit()
        return 1

    cmake = cmake_dir / "bin" / "cmake.exe"
    ninja = ninja_dir / "ninja.exe"

    required_files = (cmake, ninja, qt_prefix)
    missing = [str(path) for path in required_files if not path.exists()]
    if missing:
        print("Outil ou dossier Qt introuvable :", flush=True)
        print("\n".join(f"  - {path}" for path in missing), flush=True)
        print(f"Qt détecté : {qt_dir}", flush=True)
        pause_before_exit()
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

    temporary_dir = Path(tempfile.mkdtemp(prefix="Frontend-"))
    succeeded = False
    print(f"Qt détecté : {qt_dir}", flush=True)
    print(f"Compilation temporaire dans : {temporary_dir}", flush=True)

    try:
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
            print(f"Exécutable introuvable : {executable}", flush=True)
            print(f"Les fichiers de compilation sont conservés dans : {temporary_dir}", flush=True)
            pause_before_exit()
            return 1

        print("Lancement de Frontend.exe...", flush=True)
        subprocess.run([str(executable)], cwd=temporary_dir, check=True)
        succeeded = True
        return 0
    except subprocess.CalledProcessError as error:
        print(f"La commande a échoué avec le code {error.returncode}.", flush=True)
        if isinstance(error.cmd, (list, tuple)):
            command = subprocess.list2cmdline([str(part) for part in error.cmd])
        else:
            command = str(error.cmd)
        print(f"Commande : {command}", flush=True)
        print(f"Les fichiers de compilation sont conservés dans : {temporary_dir}", flush=True)
        pause_before_exit()
        return error.returncode or 1
    except OSError as error:
        print(f"Impossible d’exécuter une commande : {error}", flush=True)
        print(f"Les fichiers de compilation sont conservés dans : {temporary_dir}", flush=True)
        pause_before_exit()
        return 1
    finally:
        if succeeded:
            shutil.rmtree(temporary_dir, ignore_errors=True)
            print("Fichiers temporaires supprimés.", flush=True)


if __name__ == "__main__":
    sys.exit(main())
