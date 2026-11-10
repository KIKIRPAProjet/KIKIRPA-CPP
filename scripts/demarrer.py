# scripts/demarrer.py
"""Compile et lance KIKIRPA-CPP sous Windows, depuis n'importe quel terminal.

Le script remplace le terminal « x64 Native Tools » : il trouve MSVC, CMake, Ninja, Qt, OpenCV et vcpkg,
prépare l'environnement, configure le projet si besoin, le compile, puis lance l'application.

    python scripts/demarrer.py               vérifie, compile, lance l'application
    python scripts/demarrer.py --tests       vérifie, compile, lance les tests
    python scripts/demarrer.py --verifier    vérifie seulement l'installation
    python scripts/demarrer.py --sans-lancer vérifie et compile

Installation des outils et des bibliothèques : docs/DEMARRER.md.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

REPO_DIR = Path(__file__).resolve().parent.parent
PRESET = "windows-aqt"
BUILD_DIR = REPO_DIR / "build" / PRESET
MINIMUM_CMAKE = (3, 21)


class SetupError(Exception):
    """Un élément de l'installation manque : le message dit quoi faire."""


def info(message: str) -> None:
    print(message, flush=True)


def latest(paths: List[str]) -> Optional[Path]:
    """Le chemin de la version la plus récente : les nombres comparés comme des nombres (6.10.3 après 6.8.3)."""
    def version_key(path: str) -> List[Tuple[int, int, str]]:
        return [(0, int(token), "") if token.isdigit() else (1, 0, token) for token in re.findall(r"\d+|\D+", path.lower())]
    existing = [path for path in paths if Path(path).exists()]
    return Path(sorted(existing, key=version_key)[-1]) if existing else None


def user_variable(name: str) -> Optional[str]:
    """Variable du processus, sinon variable utilisateur enregistrée par setx (terminal ouvert avant le setx)."""
    if os.environ.get(name):
        return os.environ[name]
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as key:
            return str(winreg.QueryValueEx(key, name)[0])
    except OSError:
        return None


def find_vcvars() -> Path:
    vswhere = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
    candidates: List[str] = []
    if vswhere.exists():
        found = subprocess.run([str(vswhere), "-products", "*", "-property", "installationPath"],
                               capture_output=True, text=True, check=False).stdout.split("\n")
        candidates += [str(Path(line.strip()) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat") for line in found if line.strip()]
    # vswhere ne liste pas toujours les Build Tools : on cherche aussi aux emplacements habituels.
    for root in (r"C:\Program Files (x86)\Microsoft Visual Studio", r"C:\Program Files\Microsoft Visual Studio"):
        candidates += glob.glob(root + r"\20*\*\VC\Auxiliary\Build\vcvars64.bat")
    vcvars = latest(candidates)
    if vcvars is None:
        raise SetupError("Compilateur MSVC introuvable. Installez Visual Studio 2022 Build Tools avec la charge de travail "
                         "« Développement Desktop en C++ » (docs/DEMARRER.md, § 1).")
    return vcvars


def msvc_environment(vcvars: Path) -> Dict[str, str]:
    """L'environnement que vcvars64.bat prépare, c'est-à-dire celui du terminal « x64 Native Tools »."""
    output = subprocess.run(f'cmd /d /c ""{vcvars}" >nul 2>&1 && set"', capture_output=True, text=True,
                            shell=True, check=False, encoding="mbcs", errors="replace").stdout
    environment = dict(line.split("=", 1) for line in output.splitlines() if "=" in line and not line.startswith("="))
    if "VCToolsInstallDir" not in environment:
        raise SetupError(f"{vcvars} n'a pas préparé l'environnement MSVC. Réparez Visual Studio Build Tools depuis "
                         "Visual Studio Installer.")
    return environment


def find_tool(name: str, environment: Dict[str, str], fallbacks: List[str]) -> Path:
    """L'outil du PATH de MSVC, sinon celui de CLion ou des outils de Qt."""
    found = shutil.which(name, path=environment.get("PATH", environment.get("Path", "")))
    if found:
        return Path(found)
    tool = latest([path for pattern in fallbacks for path in glob.glob(pattern)])
    if tool is None:
        raise SetupError(f"{name} introuvable. Dans Visual Studio Installer, ajoutez « Outils CMake C++ pour Windows » "
                         "(docs/DEMARRER.md, § 1), ou installez CLion.")
    return tool


def check_cmake_version(cmake: Path) -> None:
    first_line = subprocess.run([str(cmake), "--version"], capture_output=True, text=True, check=False).stdout.split("\n")[0]
    numbers = first_line.split()[-1].split(".") if first_line else ["0"]
    version = tuple(int(part) for part in numbers[:2] if part.isdigit())
    if version < MINIMUM_CMAKE:
        raise SetupError(f"{cmake} est en version {first_line.split()[-1]} ; il faut CMake 3.21 ou plus.")


def find_libraries() -> Dict[str, str]:
    """Les trois variables lues par le preset windows-aqt, avec les emplacements de docs/DEMARRER.md par défaut."""
    vcpkg = Path(user_variable("KIKIRPA_VCPKG_ROOT") or r"C:\vcpkg")
    opencv = Path(user_variable("OpenCV_DIR") or r"C:\opencv\build")
    qt_value = user_variable("QT_ROOT")
    qt = Path(qt_value) if qt_value else latest(glob.glob(r"C:\Qt\*\msvc2022_64"))

    problems = []
    if not (vcpkg / "vcpkg.exe").exists():
        problems.append(f"vcpkg introuvable dans {vcpkg} (§ 2.2).")
    else:
        for package, header in (("tiff", "tiffio.h"), ("gtest", "gtest/gtest.h")):
            if not (vcpkg / "installed" / "x64-windows" / "include" / header).exists():
                problems.append(f"{package} n'est pas installé : {vcpkg}\\vcpkg install {package} --triplet x64-windows")
    if not (opencv / "OpenCVConfig.cmake").exists():
        problems.append(f"OpenCV introuvable : {opencv}\\OpenCVConfig.cmake n'existe pas (§ 2.1).")
    if qt is None or not (qt / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake").exists():
        mingw_only = glob.glob(r"C:\Qt\*\mingw*")
        hint = " Seul un Qt MinGW est installé : le projet a besoin du Qt MSVC." if mingw_only else ""
        where = str(qt) if qt else r"C:\Qt\<version>\msvc2022_64"
        problems.append(f"Qt 6 pour MSVC introuvable ({where}).{hint} Installez-le avec aqt (§ 2.3).")
    if problems:
        raise SetupError("Installation incomplète :\n  - " + "\n  - ".join(problems))
    return {"KIKIRPA_VCPKG_ROOT": str(vcpkg), "OpenCV_DIR": str(opencv), "QT_ROOT": str(qt)}


def run(command: List[str], environment: Dict[str, str]) -> None:
    info("> " + subprocess.list2cmdline(command))
    result = subprocess.run(command, cwd=REPO_DIR, env=environment, check=False)
    if result.returncode != 0:
        raise SetupError(f"La commande a échoué (code {result.returncode}). Lisez la première erreur affichée au-dessus ; "
                         "les erreurs courantes sont dans docs/DEMARRER.md, § 9.")


def configuration_state() -> str:
    """« absente », « deplacee » (configurée depuis un autre emplacement du dépôt), « incomplete » ou « ok ».

    Une configuration qui échoue (une bibliothèque manque, par exemple) laisse un CMakeCache.txt mais pas de
    build.ninja : sans elle, Ninja s'arrête sur « loading 'build.ninja': GetLastError()=2 ».
    """
    cache = BUILD_DIR / "CMakeCache.txt"
    if not cache.exists():
        return "absente"
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="):
            if Path(line.split("=", 1)[1]).resolve() != REPO_DIR.resolve():
                return "deplacee"
            break
    if not (BUILD_DIR / "build.ninja").exists():
        return "incomplete"
    return "ok"


def check_opencv_knows_msvc(environment: Dict[str, str]) -> None:
    """OpenCVConfig.cmake choisit ses binaires d'après la version de MSVC : une version qu'il ne connaît pas
    n'en trouve aucun (« OpenCV RUNTIME: » vide, puis « OpenCV_FOUND to FALSE »). Ex. : OpenCV 4.10 et Visual Studio 2026."""
    tools = environment.get("VCToolsVersion", "").split(".")
    if len(tools) < 2 or not tools[1].isdigit():
        return
    msvc_version = f"19{tools[1]}"   # outils 14.51 → compilateur 19.51 → MSVC_VERSION 1951
    config = (Path(environment["OpenCV_DIR"]) / "OpenCVConfig.cmake").read_text(encoding="utf-8", errors="replace")
    patterns = re.findall(r'MSVC_VERSION MATCHES "([^"]+)"', config)
    exact = re.findall(r"MSVC_VERSION EQUAL (\d+)", config)
    if not patterns and not exact:
        return   # format inconnu : on laisse CMake juger
    if msvc_version in exact or any(re.match(pattern, msvc_version) for pattern in patterns):
        return
    raise SetupError(f"Votre OpenCV ({environment['OpenCV_DIR']}) est trop ancien pour votre compilateur MSVC 19.{tools[1]} "
                     f"(outils {environment['VCToolsVersion']}) : il n'y trouvera aucun binaire. Installez OpenCV 4.14 "
                     "ou plus récent dans C:\\opencv (docs/DEMARRER.md, § 2.1).")


def prepare() -> Dict[str, str]:
    if sys.platform != "win32":
        raise SetupError("Ce script est pour Windows. Sous Linux et macOS : docs/DEMARRER.md, § 8.")
    vcvars = find_vcvars()
    environment = msvc_environment(vcvars)
    environment.update(find_libraries())
    check_opencv_knows_msvc(environment)
    cmake = find_tool("cmake", environment, [r"C:\Program Files\JetBrains\CLion*\bin\cmake\win\x64\bin\cmake.exe",
                                             r"C:\Qt\Tools\CMake_64\bin\cmake.exe", r"C:\Program Files\CMake\bin\cmake.exe"])
    ninja = find_tool("ninja", environment, [r"C:\Program Files\JetBrains\CLion*\bin\ninja\win\x64\ninja.exe",
                                             r"C:\Qt\Tools\Ninja\ninja.exe"])
    check_cmake_version(cmake)
    path_key = "Path" if "Path" in environment else "PATH"
    environment[path_key] = os.pathsep.join([str(cmake.parent), str(ninja.parent), environment.get(path_key, "")])
    info(f"MSVC   : {vcvars.parents[3]}")
    info(f"CMake  : {cmake}")
    info(f"Ninja  : {ninja}")
    for name in ("KIKIRPA_VCPKG_ROOT", "OpenCV_DIR", "QT_ROOT"):
        info(f"{name} : {environment[name]}")
    environment["KIKIRPA_CMAKE"] = str(cmake)
    return environment


def launch(environment: Dict[str, str]) -> None:
    executable = BUILD_DIR / "stitching.exe"
    if not executable.exists():
        raise SetupError(f"{executable} n'existe pas : la compilation n'a pas produit l'application.")
    path_key = "Path" if "Path" in environment else "PATH"
    application_environment = dict(environment)
    application_environment[path_key] = os.pathsep.join([str(Path(environment["QT_ROOT"]) / "bin"),
                                                         str(Path(environment["OpenCV_DIR"]) / "x64" / "vc16" / "bin"),
                                                         environment.get(path_key, "")])
    info(f"Lancement de {executable} …")
    subprocess.run([str(executable)], cwd=BUILD_DIR, env=application_environment, check=False)


def use_utf8_console() -> Optional[Tuple[int, int]]:
    """Met la console en UTF-8 et renvoie ses anciennes pages de code, entrée et sortie (None sans console).

    MSVC écrit ses notes « Remarque : inclusion du fichier : » dans la page de code de la console, et CMake écrit le
    préfixe que Ninja doit reconnaître dans la page de code du moment où il génère le build. Les deux divergent
    facilement : le terminal peut être en 850, 1252 ou 65001, et l'étape AUTOMOC de Qt passe la console en UTF-8
    en plein build. Ninja ne reconnaît alors plus ces notes : il en affiche des milliers, et il ne recompile plus
    un .cpp quand un .h qu'il inclut change. Le script travaille donc toujours en UTF-8.
    """
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        previous = (int(kernel32.GetConsoleCP()), int(kernel32.GetConsoleOutputCP()))
        if previous[1] and kernel32.SetConsoleOutputCP(65001):
            kernel32.SetConsoleCP(65001)
            return previous
    except (AttributeError, OSError):
        pass
    return None


def restore_console(previous: Optional[Tuple[int, int]]) -> None:
    if previous:
        import ctypes
        ctypes.windll.kernel32.SetConsoleCP(previous[0])
        ctypes.windll.kernel32.SetConsoleOutputCP(previous[1])


def generated_with_other_code_page() -> bool:
    """Les règles Ninja ont été générées dans une autre page de code que l'UTF-8 (voir use_utf8_console).

    CMake écrit le préfixe des notes de MSVC dans rules.ninja dans la page de code de la console du moment :
    s'il n'est pas lisible en UTF-8, il faut regénérer les règles avant de compiler.
    """
    rules = BUILD_DIR / "CMakeFiles" / "rules.ninja"
    if not rules.exists():
        return False
    for line in rules.read_bytes().splitlines():
        if line.strip().startswith(b"msvc_deps_prefix"):
            try:
                line.decode("utf-8")
            except UnicodeDecodeError:
                return True
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description="Compile et lance KIKIRPA-CPP sous Windows.")
    parser.add_argument("--tests", action="store_true", help="lancer les tests après la compilation, au lieu de l'application")
    parser.add_argument("--verifier", action="store_true", help="vérifier l'installation, sans compiler")
    parser.add_argument("--sans-lancer", action="store_true", help="compiler sans lancer l'application")
    arguments = parser.parse_args()
    previous_code_page = use_utf8_console()
    try:
        environment = prepare()
        if arguments.verifier:
            info("Installation complète.")
            return 0
        # Chemins complets : sous Windows, un programme se cherche dans le PATH du script, pas dans celui passé à env.
        cmake = environment["KIKIRPA_CMAKE"]
        ctest = str(Path(cmake).with_name("ctest.exe"))
        state = configuration_state()
        if state == "deplacee":
            info("Le dossier de build vient d'un autre emplacement du dépôt : sa configuration est refaite.")
            (BUILD_DIR / "CMakeCache.txt").unlink()
            shutil.rmtree(BUILD_DIR / "CMakeFiles", ignore_errors=True)
        elif state == "incomplete":
            info("La configuration précédente n'a pas abouti : elle est relancée.")
        if state != "ok":
            run([cmake, "--preset", PRESET], environment)
        elif generated_with_other_code_page():
            info("Les règles de build ont été générées dans un autre encodage de console : elles sont regénérées.")
            run([cmake, "--preset", PRESET], environment)
        run([cmake, "--build", "--preset", PRESET], environment)
        if arguments.tests:
            run([ctest, "--preset", PRESET], environment)
        elif not arguments.sans_lancer:
            launch(environment)
        return 0
    except SetupError as error:
        info(f"\nERREUR : {error}")
        # Pause pour lire le message quand le script a été lancé d'un double-clic (sa fenêtre se fermerait aussitôt).
        try:
            input("\nAppuyez sur Entrée pour fermer…")
        except EOFError:
            pass
        return 1
    finally:
        restore_console(previous_code_page)


if __name__ == "__main__":
    sys.exit(main())
