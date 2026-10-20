from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


PROJECT_DIR = Path(__file__).resolve().parent
QT_DIR = Path(r"C:\Qt\6.12.0")
MINGW_DIR = Path(r"C:\Qt\Tools\mingw1310_64")
CMAKE_DIR = Path(r"C:\Qt\Tools\CMake_64")
NINJA_DIR = Path(r"C:\Qt\Tools\Ninja")


def main() -> int:
    cmake = CMAKE_DIR / "bin" / "cmake.exe"
    ninja = NINJA_DIR / "ninja.exe"
    qt_prefix = QT_DIR / "mingw_64"

    required_files = (cmake, ninja, qt_prefix)
    missing = [str(path) for path in required_files if not path.exists()]
    if missing:
        print("Outil ou dossier Qt introuvable :")
        print("\n".join(f"  - {path}" for path in missing))
        return 1

    environment = os.environ.copy()
    environment["PATH"] = os.pathsep.join(
        [
            str(MINGW_DIR / "bin"),
            str(qt_prefix / "bin"),
            str(CMAKE_DIR / "bin"),
            str(NINJA_DIR),
            environment.get("PATH", ""),
        ]
    )

    temporary_dir = Path(tempfile.mkdtemp(prefix="Frontend-"))
    print(f"Compilation temporaire dans : {temporary_dir}")

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
            print(f"Executable introuvable : {executable}")
            return 1

        print("Lancement de Frontend.exe...")
        subprocess.run([str(executable)], cwd=temporary_dir, check=True)
        return 0
    except subprocess.CalledProcessError as error:
        print(f"La commande a échoué avec le code {error.returncode}.")
        return error.returncode or 1
    finally:
        shutil.rmtree(temporary_dir, ignore_errors=True)
        print("Fichiers temporaires supprimés.")


if __name__ == "__main__":
    sys.exit(main())
