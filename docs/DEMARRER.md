# Démarrer le projet sur son poste

Ce guide mène d'un poste vide à l'application qui s'ouvre, puis aux tests qui passent. Comptez environ une heure la première fois, dont l'essentiel en téléchargements. Ensuite, une seule commande compile et lance l'application (§ 4).

La partie Windows suit ce qui a été fait sur un poste de l'équipe le 09/11/2026. Linux et macOS sont plus courts (§ 8). La liste complète des dépendances et les options du build sont dans [BUILD.md](BUILD.md).

## Ce que vous aurez à la fin

| Élément | Où | Taille |
|---|---|---|
| Visual Studio 2022 Build Tools (compilateur MSVC) | `C:\Program Files (x86)\Microsoft Visual Studio` | plusieurs Go, avec le SDK Windows |
| OpenCV 4 | `C:\opencv` | environ 0,9 Go |
| vcpkg, avec libtiff et GoogleTest | `C:\vcpkg` | environ 1 Go |
| Qt 6.10.3 (module de base) | `C:\Qt` | environ 300 Mo |
| Le build (exécutables, tests, ONNX Runtime, modèle LightGlue) | `build\windows-aqt` dans le dépôt | environ 700 Mo |

Toutes les commandes de ce guide se tapent dans un terminal ordinaire : PowerShell, ou le terminal de PyCharm ou de CLion. Le script `scripts/demarrer.py` prépare lui-même l'environnement du compilateur : le terminal « x64 Native Tools » n'est pas nécessaire.

## 1. Installer les outils (Windows, une seule fois)

1. **Git pour Windows** : [git-scm.com](https://git-scm.com/download/win). Il installe aussi **Git Bash**, qui sert à lancer les scripts `.sh` du projet.
2. **Visual Studio 2022 ou 2026 Build Tools** (ou la version Community) ([visualstudio.microsoft.com](https://visualstudio.microsoft.com/fr/downloads/), section « Outils pour Visual Studio »). Dans l'installeur, cochez la charge de travail **« Développement Desktop en C++ »**. Dans le détail à droite, laissez coché **« Outils CMake C++ pour Windows »** : cette option fournit `cmake` et `ninja`. Si vous utilisez CLion, ses propres `cmake` et `ninja` suffisent, et le script les trouve.

   Le compilateur MSVC est indispensable, même pour ne travailler que sur l'interface : les binaires d'OpenCV pour Windows ne se lient qu'avec lui, pas avec le MinGW fourni avec Qt Creator.
3. **Python 3.8 ou plus** ([python.org](https://www.python.org/downloads/)) : il sert à installer Qt (§ 2) et à lancer le script (§ 4).

## 2. Installer les bibliothèques (Windows, une seule fois)

1. **OpenCV 4** : téléchargez l'installeur Windows sur [opencv.org/releases](https://opencv.org/releases/) (`opencv-4.14.0-windows.exe` ou plus récent) et extrayez-le dans `C:\`. Vous devez obtenir `C:\opencv\build\OpenCVConfig.cmake`. **Avec Visual Studio 2026, prenez au moins la 4.14** : les versions plus anciennes, comme la 4.10 qu'utilise la CI, ne reconnaissent pas son compilateur (MSVC 19.5x) et n'y trouvent aucun binaire. Le script le vérifie.
2. **vcpkg, libtiff et GoogleTest** :
   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   ```
   ```powershell
   C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   ```
   ```powershell
   C:\vcpkg\vcpkg install tiff gtest --triplet x64-windows
   ```
   Si vcpkg est déjà installé, ne lancez que la dernière commande. Elle doit finir par `All requested installations completed successfully`.
3. **Qt 6**, avec l'outil `aqt`, sans Qt Creator ni compte Qt :
   ```powershell
   python -m pip install --user aqtinstall
   ```
   ```powershell
   python -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -O C:\Qt --archives qtbase
   ```
   Vous devez obtenir `C:\Qt\6.10.3\msvc2022_64`. Un Qt déjà installé avec Qt Creator ne convient que s'il contient le dossier `msvc2022_64` ; un Qt `mingw_64` seul ne suffit pas. La commande laisse un fichier `aqtinstall.log` dans le dossier courant : supprimez-le, et ne le commitez pas.
4. **Les trois variables du build**, nécessaires pour CLion et pour compiler à la main (§ 5). Le script, lui, s'en passe si vous avez gardé les emplacements de ce guide.
   ```powershell
   setx KIKIRPA_VCPKG_ROOT C:\vcpkg
   ```
   ```powershell
   setx OpenCV_DIR C:\opencv\build
   ```
   ```powershell
   setx QT_ROOT C:\Qt\6.10.3\msvc2022_64
   ```
   `setx` ne s'applique qu'aux **terminaux et IDE ouverts après**. Rouvrez CLion et PyCharm s'ils étaient ouverts.

## 3. Récupérer le dépôt

```powershell
git clone https://github.com/KIKIRPAProjet/KIKIRPA-CPP
```
```powershell
cd KIKIRPA-CPP
```

## 4. Compiler et lancer : le script

Depuis la racine du dépôt :

```powershell
python scripts/demarrer.py
```

Le script :
1. trouve MSVC, CMake, Ninja, vcpkg, OpenCV et Qt. S'il manque quelque chose, il dit quoi installer, avec la commande à taper ;
2. configure le projet la première fois. Cette étape télécharge ONNX Runtime et le modèle de test LightGlue (environ 63 Mo), donc il faut une connexion internet ;
3. compile ;
4. lance l'application, avec les DLL de Qt et d'OpenCV.

Les autres usages :

| Commande | Ce qu'elle fait |
|---|---|
| `python scripts/demarrer.py --verifier` | vérifie seulement l'installation et affiche ce qui a été trouvé |
| `python scripts/demarrer.py --tests` | compile, puis lance les tests ; résultat attendu : `100% tests passed`, environ 140 tests |
| `python scripts/demarrer.py --sans-lancer` | compile sans lancer l'application |

Pendant la configuration, les messages suivants ne sont **pas** des erreurs : `pthread … not found`, `Could NOT find WrapVulkanHeaders`, et l'avertissement `CMake Warning (dev) … DOWNLOAD_EXTRACT_TIMESTAMP`.

Une fois l'application ouverte, vous pouvez créer un projet, régler la grille, parcourir les onglets, lancer la simulation et enregistrer le projet. **Aucune mosaïque n'est encore calculée** : l'interface simule les étapes et n'appelle pas encore le backend (voir [COMPRENDRE_LE_PROJET.md](COMPRENDRE_LE_PROJET.md) § 9).

## 5. Sans le script

Ce sont les mêmes étapes, à la main, dans le terminal **« x64 Native Tools Command Prompt for VS 2022 »** (menu Démarrer), qui contient le compilateur MSVC. Il faut aussi les variables du § 2.4.

```bat
cd /d <dossier du dépôt>
```
```bat
chcp 65001
```
```bat
cmake --preset windows-aqt
```

`chcp 65001` passe la console en UTF-8. Il faut le faire dans chaque nouveau terminal, avant `cmake`. Sans lui, sur un Windows en français, le build affiche des milliers de lignes « Remarque : inclusion du fichier », et un `.cpp` n'est plus recompilé quand un `.h` qu'il inclut change (§ 9). Le script le fait de lui-même.

```bat
cmake --build --preset windows-aqt
```
```bat
ctest --preset windows-aqt
```
```bat
set PATH=%QT_ROOT%\bin;%OpenCV_DIR%\x64\vc16\bin;%PATH%
```
```bat
build\windows-aqt\stitching.exe
```

Pour lancer `stitching.exe` d'un double-clic, copiez une fois les DLL de Qt à côté de lui avec `"%QT_ROOT%\bin\windeployqt.exe" build\windows-aqt\stitching.exe`. Copiez aussi `opencv_world4*.dll`, qui se trouve dans `%OpenCV_DIR%\x64\vc16\bin`.

## 6. Travailler dans CLion (recommandé pour modifier le C++)

CLion lit les presets du projet. Il fournit CMake et Ninja, et trouve MSVC tout seul. PyCharm, lui, ne compile pas le C++ : gardez-le pour la doc, Git et les scripts Python.

1. Faites les § 1 et 2, variables comprises, puis **ouvrez CLion après le `setx`**.
2. Ouvrez le dossier du dépôt.
3. *Settings → Build, Execution, Deployment → Toolchains* : choisissez **Visual Studio**, pas MinGW.
4. *CMake → Profiles* : activez le preset **windows-aqt**, et désactivez le profil `Debug` que CLion crée de lui-même.
5. En haut à droite, choisissez la cible **stitching**, puis Run (▶). Si Windows signale une DLL de Qt ou d'OpenCV introuvable : *Run → Edit Configurations → stitching → Environment variables*, et ajoutez
   `PATH=C:\Qt\6.10.3\msvc2022_64\bin;C:\opencv\build\x64\vc16\bin;$PATH$`.
6. Les tests se lancent depuis la cible **All CTest**, ou un par un depuis l'éditeur.

## 7. Au quotidien

```powershell
git pull
```
```powershell
python scripts/demarrer.py
```

Le script ne reconfigure que si c'est nécessaire, et la compilation ne refait que ce qui a changé. Le dossier `build\windows-aqt` peut être supprimé à tout moment : il est recréé, et Git ne le suit pas.

Pour écrire du code et ouvrir une pull request, voir [CONVENTIONS.md](CONVENTIONS.md). Pour comprendre ce que fait le code, voir [COMPRENDRE_LE_PROJET.md](COMPRENDRE_LE_PROJET.md).

## 8. Linux et macOS

Le script est pour Windows. Sur ces systèmes, trois commandes suffisent, et il n'y a pas de DLL à copier.

**Ubuntu 24.04**, la configuration de la CI :
```bash
sudo apt-get install build-essential cmake ninja-build libopencv-dev libtiff-dev nlohmann-json3-dev qt6-base-dev libgtest-dev
```
```bash
cmake --preset linux && cmake --build --preset linux && ctest --preset linux
```
```bash
./build/linux/stitching
```

**macOS** (Homebrew) :
```bash
brew install cmake opencv libtiff qtbase googletest
```
```bash
cmake --preset macos && cmake --build --preset macos && ctest --preset macos
```
```bash
./build/macos/stitching
```

Détails : [BUILD.md](BUILD.md).

## 9. Problèmes rencontrés et solutions

| Message | Cause | Solution |
|---|---|---|
| `ERREUR : Compilateur MSVC introuvable` (script) | Visual Studio Build Tools absent, ou sans la charge de travail C++ | § 1.2, puis relancer le script |
| `ERREUR : cmake introuvable` (script), ou `cmake : Le terme «cmake» n'est pas reconnu` | ni « Outils CMake C++ pour Windows », ni CLion | ajouter cette option dans Visual Studio Installer (§ 1.2) |
| `gtest n'est pas installé` (script), ou `Could NOT find GTest` | GoogleTest manque dans vcpkg | `C:\vcpkg\vcpkg install gtest --triplet x64-windows` |
| `Qt 6 pour MSVC introuvable` (script), ou `Could NOT find Qt6` | Qt absent, ou seulement en version MinGW | § 2.3 ; pour CLion et le § 5, vérifier aussi `QT_ROOT` et rouvrir l'IDE |
| `python` ouvre le Microsoft Store, ou « n'est pas reconnu » | Python absent, ou pas dans le `PATH` | installer Python (§ 1.3) en cochant « Add python.exe to PATH » |
| `bash ../script.sh` affiche « Sous-système Windows pour Linux n'a aucune distribution installée » | dans PowerShell, `bash` lance celui de WSL, pas celui de Git | `& "C:\Program Files\Git\bin\bash.exe" ../script.sh`, ou ouvrir Git Bash |
| `grep : Le terme «grep» n'est pas reconnu` | `grep` n'existe pas dans PowerShell | `git grep -n "texte" -- <dossier>`, ou Git Bash |
| « Qt6Widgets.dll est introuvable » ou « opencv_world… est introuvable » en lançant `stitching.exe` à la main | les DLL ne sont pas dans le `PATH` | passer par le script, ou suivre le § 5 |
| la compilation affiche des milliers de lignes `Remarque : inclusion du fichier : …` | MSVC écrit ces notes dans l'encodage de la console, et l'étape AUTOMOC de Qt le change en plein build : Ninja ne les reconnaît plus, et ne recompile plus un `.cpp` quand un `.h` qu'il inclut change | passer par le script, qui le règle ; à la main, `chcp 65001` puis `cmake --preset windows-aqt` (§ 5) |
| `Found OpenCV Windows Pack but it has no binaries compatible with your configuration`, `OpenCV RUNTIME:` vide, puis `OpenCV_FOUND to FALSE` ; ou `Votre OpenCV … est trop ancien` (script) | OpenCV est plus ancien que le compilateur : la 4.10 ne connaît pas celui de Visual Studio 2026 (MSVC 19.5x) | installer OpenCV 4.14 ou plus récent dans `C:\opencv` (§ 2.1), supprimer `build\windows-aqt`, relancer |
| `ninja: error: loading 'build.ninja': GetLastError()=2` (ou « The system cannot find the file specified ») | une configuration précédente a échoué, par exemple faute d'une bibliothèque, et n'a pas créé `build.ninja` | corriger ce qui manquait (le script le signale), puis relancer le script, qui refait la configuration ; à la main : `cmake --preset windows-aqt` avant `cmake --build` |
| CMake refuse le dossier de build après un déplacement du dépôt | le build garde l'ancien chemin en mémoire | le script refait la configuration tout seul ; à la main, supprimer `build\windows-aqt` |
| `warning: in the working copy of '…', LF will be replaced by CRLF` | Git signale qu'il convertira les fins de ligne | rien à faire, ce n'est pas une erreur |
