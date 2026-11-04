# Conventions de code

Règles à suivre pour tout nouveau code et toute modification. Elles sont appliquées dans `core/`, `steps/` et `frontend/` depuis la réorganisation (PR #50), et dans `algorithms/` depuis leur migration (voir [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md) §12). Les tests ne les suivent pas encore entièrement (voir [§9](#9-ce-qui-nest-pas-encore-migré)).

En cas de doute, prendre pour modèle `include/backend/core/step/step.h`, `src/backend/steps/phase_correlation_step.cpp` et, pour un algorithme, `algorithms/stitching/phase_correlation/`.

**Sommaire** : [1 Langue](#1-langue) · [2 Fichiers](#2-fichiers-et-dossiers) · [3 Noms](#3-noms) · [4 Mise en forme](#4-mise-en-forme) · [5 Includes](#5-includes) · [6 Commentaires](#6-commentaires) · [7 Erreurs](#7-erreurs) · [8 Structure](#8-structure-du-code) · [9 Non migré](#9-ce-qui-nest-pas-encore-migré) · [10 Documentation](#10-documentation) · [11 Classes et fonctions](#11-classes-et-fonctions) · [12 Tests](#12-tests) · [13 Principes de code propre](#13-principes-de-code-propre) · [14 Git et pull requests](#14-git-et-pull-requests)

## 1. Langue

- **Le code est en anglais** : fichiers, namespaces, classes, fonctions, variables, champs, constantes, clés JSON. Pas d'abréviation (`tileCount`, pas `nbTiles` ni `n`).
- **Les commentaires et la documentation sont en français.**
- Les messages des exceptions et de `result["error"]` sont en anglais, comme dans `core/` et `steps/`.
- Jamais de mélange dans un même identifiant : pas de `nbTuiles`, `raisonEchec`, `verifier`. Les bibliothèques utilisées (OpenCV, bibliothèque standard) sont en anglais : le code se lit alors comme une seule phrase.

## 2. Fichiers et dossiers

| Élément | Règle | Exemple |
|---|---|---|
| Dossiers | minuscules, `snake_case` | `include/backend/core/checkpoint/` |
| Fichiers | `snake_case`, `.h` et `.cpp` | `pipeline_builder.h`, `pipeline_builder.cpp` |
| Steps | `<nom>_step.h` / `.cpp` dans `steps/` | `phase_correlation_step.h` |
| Arborescence | `include/` et `src/` identiques | `include/backend/X/y.h` ⟷ `src/backend/X/y.cpp` |

- **Première ligne** de chaque fichier : un commentaire avec son chemin depuis la racine du dépôt (`// include/backend/core/step/step.h`).
- Puis `#pragma once` dans un header.
- Un `.h` et un `.cpp` par classe, avec le même nom : `PhaseCorrelationStepClass` est dans `phase_correlation_step.h` / `.cpp`.
- Le header contient l'**interface** (déclarations), le `.cpp` l'**implémentation**.
- Un `.cpp` posé ailleurs que dans `src/backend/` n'est pas compilé (`GLOB_RECURSE` de `cmake/backend.cmake`).

## 3. Noms

### 3.1 Styles de casse

La casse donne déjà une information : `TileClass` est un type, `tileCount` une valeur, `BLOCK_SIZE` une constante.

| Style | Principe | Exemple | Usage dans le projet |
|---|---|---|---|
| `snake_case` | minuscules, mots séparés par `_` | `phase_correlation_step.h` | fichiers et dossiers |
| `camelCase` | 1er mot en minuscule, puis une majuscule à chaque mot | `tileCount`, `buildPipeline` | variables, fonctions, clés JSON |
| `PascalCase` | une majuscule à chaque mot, y compris le premier | `PipelineClass`, `RunState` | classes, structs, enums, namespaces |
| `UPPER_SNAKE_CASE` | tout en majuscules, séparés par `_` | `BLOCK_SIZE` | constantes |
| `kebab-case` | minuscules, mots séparés par `-` | `phase-correlation` | nom d'un step dans le JSON |

### 3.2 Règles du projet

| Élément | Règle | Exemple |
|---|---|---|
| Namespace | un par dossier, `PascalCase` + suffixe `Ns` | `DataNs`, `PipelineNs`, `StepsNs` |
| Classe (avec un comportement) | `PascalCase` + suffixe `Class` | `PipelineClass`, `TileClass`, `ImportStepClass` |
| Structure de données (`struct`) | `PascalCase`, sans suffixe | `Matches`, `RunState`, `StepConfig`, `ImportInput` |
| Énumération | `enum class` en `PascalCase`, valeurs en `PascalCase` | `RunStatus::Completed` |
| Fonction, méthode | `camelCase`, un verbe | `buildPipeline`, `getParameter`, `setDataStore` |
| Variable, paramètre, champ | `camelCase`, sans préfixe ni lettre seule | `configJson`, `tileIdA`, `levelIndex` |
| Constante | `UPPER_SNAKE_CASE` | `BLOCK_SIZE`, `LEVEL_COUNT` |
| Alias de type | `PascalCase` | `TileList` |
| Clé JSON | `camelCase` | `skipCompletedSteps`, `pointsInA` |
| Nom d'un step dans le JSON | minuscules, mots séparés par `-` | `"phase-correlation"` |
| Paramètre d'un step | `camelCase` | `nbPoints`, `overlap` |

Accesseurs : `getX()` / `setX()`. Un nom dit ce que la chose contient ou fait, avec son unité si elle est utile (`rotationRadians`, `overlapFraction`).

### 3.3 Bien nommer

- **Il dit ce que c'est**, sans commentaire : `elapsedDays`, pas `d`.
- **Pas d'abréviation ni de lettre seule** : `tileCount`, pas `nbTiles` ni `n`.
- **L'unité** est dans le nom si elle compte : `rotationRadians`, `overlapFraction`.
- **Un mot par concept** : toujours `get`, jamais un mélange de `get` / `fetch` / `retrieve`.
- **Prononçable et facile à chercher** : on doit pouvoir le dire à voix haute et le retrouver par recherche.
- **Pas de mensonge** : ne pas appeler `tileList` ce qui n'est pas une liste.
- Plus la **portée** est grande, plus le nom est précis.

| À éviter | À écrire | Pourquoi |
|---|---|---|
| `cfg`, `p` | `config`, `parameter` | abréviation, lettre seule |
| `nbTiles` | `tileCount` | pas d'abréviation, pas de préfixe « nb » |
| `dx` | `translationX` | dit ce que c'est, pas seulement l'axe |
| `rot` | `rotationRadians` | l'unité manque |
| `overlap` (vaut 0.3) | `overlapFraction` | fraction ou pourcentage ? le nom tranche |
| `flag`, `ok` | `skipCompletedSteps` | un booléen décrit ce qu'il commande |
| `process()` | `buildPipeline()` | verbe vague, on ne sait pas ce qui sort |
| `data2`, `tmp` | `overlapEstimate` | un nom numéroté ne décrit rien |

| Élément | Convention | Exemples |
|---|---|---|
| Fonction | un **verbe** : une action | `computeOverlap`, `loadImage` |
| Test (booléen) | préfixe `is`, `has`, `can`, `should` | `isEmpty()`, `hasParentPath()` |
| Négation | éviter `isNotX` : écrire `isX` et utiliser `!` | `isValid`, pas `isNotValid` |
| Collection | nom au **pluriel** | `tiles`, `stepNames`, `completedSteps` |
| Table associative | `valeurParClé` : le nom se lit comme une phrase | `producedDataTypeByKey` |
| Index et nombre | `xIndex` pour une position, `xCount` pour un total | `stepIndex`, `stepCount` |

## 4. Mise en forme

- Indentation de **4 espaces**, pas de tabulation.
- Le contenu d'un namespace est indenté. Dans une classe, `public:` / `protected:` / `private:` sont indentés d'un niveau, et les membres d'un niveau de plus :

```cpp
namespace StepNs {

    class StepClass {

        public:
            virtual bool execute() = 0;

        protected:
            Json configJson;

    };

}
```

- Une ligne vide après l'accolade d'ouverture d'un namespace ou d'une classe.
- Une ligne trop longue se coupe **après un opérateur ou une virgule**, la suite alignée sous le début de l'expression.
- Fonctions internes à un `.cpp` : dans un `namespace { }` anonyme, à l'intérieur du namespace du fichier, avant les fonctions publiques. Jamais dans le header.

## 5. Includes

- Toujours depuis `include/backend/` (`#include "core/data/algorithm_types.h"`), **jamais** de `"../"`.
- Par groupes séparés d'une ligne vide, triés par ordre alphabétique dans chaque groupe :
  1. dans un `.cpp`, son propre header ;
  2. les headers du projet ;
  3. les bibliothèques externes (`nlohmann`, `opencv2`…) ;
  4. la bibliothèque standard.

```cpp
#include "core/pipeline/pipeline_builder.h"

#include "core/config/pipeline_config.h"
#include "steps/import_step.h"

#include <opencv2/imgproc.hpp>

#include <filesystem>
#include <string>
```

- **Inclure ce qu'on utilise** : ne pas compter sur ce qu'un autre header inclut.
- **Header minimal** : dans un `.h`, seulement ce que ses déclarations exigent ; le reste va dans le `.cpp`. Une déclaration anticipée (`class TileClass;`) suffit quand on n'utilise qu'un pointeur ou une référence.
- `"..."` pour les fichiers du projet, `<...>` pour les bibliothèques et la bibliothèque standard.
- Jamais de `using namespace` dans un header (ni `using namespace std;`) : cela pollue tous les fichiers qui l'incluent. Qualifier à l'usage : `DataNs::Matches`.
- Supprimer un include devenu inutile.

## 6. Commentaires

- **Dans le header**, au-dessus de chaque classe, fonction ou champ non évident : *ce que* ça fait, ce que ça renvoie, quand ça lève une exception, les unités. Quelqu'un qui lit seulement le header doit pouvoir utiliser la fonction sans ouvrir le `.cpp`.
- **Dans le `.cpp`** : *pourquoi* le code est écrit ainsi (choix, cas limites, unités, conventions de repère).
- Pas de commentaire qui répète le code.
- Un commentaire qui explique du code confus : d'abord renommer ou découper, ensuite commenter.

À éviter :

- un commentaire **périmé** : il se met à jour en même temps que le code ;
- du **code commenté** : Git garde l'historique, on le supprime ;
- des **séparateurs décoratifs** (`// ------`) ;
- l'auteur et la date : Git les connaît.

```cpp
// Mauvais : répète la ligne
// incrémente i
++i;

// Bon : explique un choix
// Fichier temporaire puis renommage : le script n'est jamais écrit à moitié.
const fs::path temporaryPath = scriptPath.string() + ".tmp";
```

## 7. Erreurs

- Un **step** renvoie `false` avec la raison dans `result["error"]`.
- Une fonction qui peut échouer renvoie `bool` ou `std::optional`.
- Les **exceptions** (`std::invalid_argument`) sont réservées aux erreurs de configuration : JSON mal formé, step ou paramètre inconnu, valeur invalide. Elles se détectent avant de lancer un long calcul.
- Pas de sortie console (`std::cout`, `std::cerr`) dans les steps ni les algorithmes : les messages passent par `result` (et le logger du pipeline les affiche). Seul `PipelineClass` écrit sur `std::cerr`, quand aucun logger n'est fourni.
- Si un step lance quand même une exception, `PipelineClass` l'attrape : le step est marqué en échec et le run peut être repris.

**Un bon message d'erreur** dit :

- **où** : le chemin dans le script (`pipeline.steps[2] ('graphcut')`) ;
- **quoi** : ce qui ne va pas, en anglais ;
- **quelle valeur** : la valeur reçue ou le nom concerné.

```cpp
throw std::invalid_argument(stepPath + " ('" + stepName + "'): input '" + inputSlot.key +
                            "' is not produced by a previous step");
```

Jamais de `catch (...) { }` vide : une erreur avalée est introuvable. Quand on rattrape une exception pour la relancer, on ajoute du contexte sans perdre le message d'origine (`stepPath + ": " + exception.what()`).

## 8. Structure du code

- **Steps** : une classe `<Nom>StepClass` dans `StepsNs`, qui hérite de `StepNs::StepClass`, avec une struct `<Nom>Input` (paramètres) et `<Nom>Output` (sorties). Marche à suivre : [core/Pipeline.md](core/Pipeline.md#ajouter-un-step).
- **Algorithmes** : un dossier par algorithme dans `algorithms/<étape>/<nom>/`. Un algorithme conséquent se découpe comme la corrélation de phase : un header façade (le seul inclus par les steps), un header de types, puis un module par phase (préparation des images, calcul, mise en forme de la sortie). Voir la [carte](CARTE_ARCHITECTURE.md#6-lire-un-algorithme--lexemple-de-la-corrélation-de-phase).
- **Types partagés** entre steps et algorithmes : dans `core/data/algorithm_types.h` (`DataNs`). Un type par clé du `DataStoreClass`.
- **Tests** : voir [§12](#12-tests).
- **Dépendances** : elles vont dans un seul sens, `frontend` → `steps` → `algorithms` → `core`. Le `core` ne connaît pas le contenu des steps ; seule exception, le registre des steps de `pipeline_builder.cpp`, endroit unique où on les déclare.

**Images**
- Ne jamais convertir 16 bits → 8 bits dans le pipeline : la perte est irréversible. Lire avec `cv::imread(path, cv::IMREAD_UNCHANGED)`.
- Les tuiles reçues par un algorithme sont `const` : on lit des régions, on ne modifie pas les pixels.
- Une seule image pleine résolution en mémoire à la fois quand c'est possible.

## 9. Ce qui n'est pas encore migré

Les algorithmes ont été migrés (noms anglais, un namespace par dossier, types partagés qualifiés `DataNs::…`, fonctions internes dans un `namespace { }` anonyme) ; `algorithms/legacy_type_aliases.h` a été supprimé. Il reste :

- `tests/` : noms internes en français (`verifier`, `LARGEUR_TUILE`, noms des `TEST(…, QuatreDirections)`…), et en `snake_case` dans `tests/seamline/` (`random_pair`, `maze_grid`, namespace `testmaps`). Les appels aux algorithmes, eux, sont à jour.
- Sorties console : `graph_cut_step.cpp` écrit ses erreurs sur `std::cerr`, en français, en plus de `result` ; LightGlue écrit des traces `[DEBUG]` sur `std::cout` ; GraphCut (`GraphCutSeamFinderClass`, `ImagePretreatmentClass`) écrit ses erreurs sur `std::cout` et renvoie un `int` (±1) au lieu d'un `bool`.
- Noms gardés pour ne pas casser les scripts et les configurations :
  - les paramètres des steps existants (`niveau`, `lissage`, `marge`…), alors que les champs C++ correspondants sont en anglais (`PhaseCorrelationInput::level`, `PhaseCorrelationNs::Parameters::smoothingSigma`…) ;
  - les clés JSON des algorithmes qui n'ont pas encore de step : SIFT (`max_features`, `lowe_ratio`, `contrast_threshold`, `clahe_clip_limit`, en `snake_case`), RANSAC et vote (`ransacReprojThreshold`, `maxIters`, `refineIters`, `seuil`, `case`…), LightGlue (`model_path`) ;
  - les valeurs texte : `"correlation_phase"` dans `PositioningNs::PositioningSettings::matcherName`, les options de `CostMapParameters` (`"noise"`, `"euclidian"`, `"caped"`…) et les noms du notebook (`"color_gradient"`, `"DP"`, `"Dijkstra-8"`).

  À renommer quand un step les exposera, avec l'accord de l'équipe.
- Structure : `VoteRejectorClass` est désormais isolé dans son propre sous-dossier et fichier (`algorithms/rejection/vote_2d/vote_rejector.h` / `.cpp`) ; SIFT, LightGlue, `DynamicProgrammingDijkstraNs` et GraphCut ne suivent pas encore le découpage façade / types / modules (§8).

Tout nouveau code suit les règles ci-dessus.

## 10. Documentation

- Une page par algorithme dans `docs/algorithms/<étape>/<nom>/`, une page par step dans `docs/steps/`.
- **La [carte de l'architecture](CARTE_ARCHITECTURE.md) reste à jour** : elle est mise à jour, avec [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md), **après le merge** de chaque PR qui ajoute, déplace, renomme ou supprime un fichier, un step, un type partagé ou un test, dans une PR séparée. L'auteur d'une PR n'a pas à modifier ces deux docs.

## 11. Classes et fonctions

### 11.1 Quel type choisir

| Outil | Quand l'utiliser | Nom | Exemple |
|---|---|---|---|
| `class` | un objet avec un **comportement** et des données qu'il protège | `PascalCase` + `Class` | `PipelineClass` |
| `struct` | des **données seules**, champs publics, pas de règle entre eux | `PascalCase` | `Matches`, `StepConfig` |
| `enum class` | un **ensemble fermé** de valeurs | `PascalCase`, valeurs en `PascalCase` | `RunStatus::Completed` |
| `using` | un nom plus court ou plus parlant pour un type | `PascalCase` | `TileList` |

`enum class` plutôt que `enum` : pas de conversion implicite en entier, et les valeurs sont dans leur portée (`RunStatus::Failed`, pas `Failed`).

### 11.2 Structure d'une classe

- Ordre : `public` d'abord (c'est ce que le lecteur cherche), puis `protected`, puis `private` ; les **champs en dernier**.
- `const` sur toute méthode qui ne modifie pas l'objet.
- Destructeur `virtual` dans une classe de base.
- Accesseurs `getX()` / `setX()`.

### 11.3 Écrire une fonction

Une bonne fonction **fait une chose**, tient sur un écran et porte un **nom-verbe**. Repères (pas des limites absolues) :

| Signal d'alerte | Réaction |
|---|---|
| plus de **30 lignes** | découper en fonctions plus petites, chacune bien nommée |
| plus de **4 paramètres** | les regrouper dans une `struct` (comme `<Nom>Input` pour un step) |
| un **booléen** qui change le comportement | écrire deux fonctions |
| plus de **3 niveaux** d'imbrication | retours anticipés |
| un **commentaire** par bloc de code | chaque bloc devient une fonction dont le nom remplace le commentaire |

**Retours anticipés** : traiter d'abord les cas d'erreur et sortir tout de suite ; le chemin normal reste au premier niveau.

```cpp
// Mauvais : pyramide d'imbrication
bool run(const Json& script) {
    if (script.is_object()) {
        if (script.contains("pipeline")) {
            return true;
        }
    }
    return false;
}

// Bon : gardes, puis le travail
bool run(const Json& script) {
    if (!script.is_object()) return false;
    if (!script.contains("pipeline")) return false;
    return true;
}
```

### 11.4 `const`, références, `auto`, conversions

| Sujet | Règle | Exemple |
|---|---|---|
| `const` | partout où la valeur ne change pas : variables, paramètres, méthodes | `const std::string stepName` |
| Paramètres | gros objet : `const&` ; petit type (`int`, `double`, `size_t`) : par valeur | `const Json& scriptJson` |
| Retour | par **valeur** : le compilateur évite la copie | `PipelineClass buildPipeline(...)` |
| `auto` | quand le type est **évident** à droite ; sinon écrire le type | `const auto registryEntry = ...find(name);` |
| Boucle | `const auto&` pour lire sans copier | `for (const auto& registryEntry : stepRegistry())` |
| Conversion | `static_cast` explicite, jamais de cast à la C `(int)x` | `static_cast<uint32_t>(levelImage.cols)` |
| Tailles, index | `size_t` ; ne pas comparer signé et non signé sans `static_cast` | `for (size_t stepIndex = 0; ...)` |

### 11.5 Constantes et nombres magiques

- Toute valeur littérale dont on ne comprend pas l'origine reçoit un nom : `BLOCK_SIZE`, pas `512`.
- `constexpr` plutôt que `#define` : typé, et il respecte les namespaces.
- **Portée minimale** : namespace anonyme du `.cpp` si un seul fichier l'utilise, header si plusieurs. Une valeur, **une seule définition** (voir DRY, [§13](#13-principes-de-code-propre)) : le writer TIFF réutilise `DataNs::BLOCK_SIZE` au lieu de réécrire `512`.
- Commentaire pour l'unité ou la raison.

```cpp
namespace {
    // Mêmes valeurs que la pyramide décrite par TileClass.
    constexpr uint32_t TILE_SIZE = static_cast<uint32_t>(DataNs::BLOCK_SIZE);
    constexpr int PYRAMID_LEVELS = DataNs::LEVEL_COUNT;
}
```

### 11.6 Mémoire

- **RAII** : une ressource est liée à la durée de vie d'un objet ; il disparaît, elle est libérée.
- **Pas de `new` / `delete`** écrits à la main : `std::make_unique`.
- `std::unique_ptr` par défaut (propriété exclusive) ; `std::shared_ptr` seulement si la propriété est vraiment partagée.
- **Règle du zéro** : si la classe n'a pas de besoin particulier, ni destructeur, ni copie, ni déplacement déclarés. Déclarer `~X() = default;` **supprime le déplacement automatique** : une classe qui contient un `std::unique_ptr` ne peut alors plus être renvoyée par valeur.

## 12. Tests

- **GoogleTest**, un dossier `tests/<module>/` avec son `CMakeLists.txt`, label `unit`, sans images externes : on construit des images synthétiques dans le test (rapide et reproductible).
- L'exécutable de test se lie à `kikirpa_backend` et `GTest::gtest_main` ; le dossier est déclaré par un `add_subdirectory` dans `cmake/tests_runner.cmake`.
- **Un comportement par test**, avec un nom qui le dit (`RejectsUnknownStatus`).
- Structure **préparer, agir, vérifier**.
- Tests **indépendants** et **déterministes**.
- Tester surtout les **cas limites** : entrée vide, bords, erreurs.
- Avant chaque commit : compiler sans avertissement et lancer `ctest` ; tout doit être vert.

```cpp
TEST(RunStateTest, RejectsUnknownStatus) {
    const Json runJson = {{"status", "unknown"}};                 // préparer
    EXPECT_THROW(PipelineNs::RunState::fromJson(runJson),         // agir et vérifier
                 std::invalid_argument);
}
```

## 13. Principes de code propre

Ces principes valent pour tout projet, pas seulement celui-ci.

| Principe | En clair | Question à se poser |
|---|---|---|
| **KISS** | la solution la plus simple qui fonctionne ; la bibliothèque standard connaît déjà beaucoup de choses (`std::max`, `std::find_if`) | quelqu'un d'autre comprend-il ça en 30 secondes ? |
| **DRY** | une information, un seul endroit : une valeur répétée devient une constante, un bloc répété une fonction | si je change cette valeur, combien de fichiers dois-je modifier ? |
| **YAGNI** | on ne code pas « au cas où » : pas de paramètre, de classe de base ou d'option dont on n'a pas besoin maintenant | en ai-je besoin maintenant, ou seulement peut-être un jour ? |
| **SRP** | une fonction ou une classe a une seule raison de changer | puis-je la décrire sans écrire « et » ? |
| **Boy-scout** | laisser le code plus propre qu'on ne l'a trouvé | ai-je corrigé un nom ou un commentaire douteux en passant ? |

DRY concerne la **connaissance**, pas le texte : deux codes semblables qui évoluent pour des raisons différentes restent séparés.

Autres réflexes :

- **Moindre surprise** : une fonction fait ce que son nom annonce, rien de plus (`getResult()` ne modifie rien).
- **Échouer tôt** : vérifier les entrées au début, avec un message clair (`validateScriptStructure` passe en premier).
- **Portée minimale** : déclarer une variable au plus près de son premier usage.

Pièges fréquents :

| Piège | Règle qui l'évite |
|---|---|
| fonction de 200 lignes | une fonction = une chose |
| code copié-collé | extraire une fonction (DRY) |
| noms `data2`, `tmp`, `x` | un nom qui dit ce que c'est, avec son unité |
| nombre magique | une constante nommée |
| `catch (...) { }` vide | traiter l'erreur ou la relancer avec du contexte |
| `using namespace` dans un header | qualifier : `DataNs::Matches` |
| `std::cout` de debug oublié | passer par `result` et le logger du pipeline |
| commentaire qui répète ou qui ment | expliquer le pourquoi, mettre à jour avec le code |

## 14. Git et pull requests

### 14.1 Branches et commits

- **Une branche par sujet**, nommée `type/sujet` : `fe/pipeline-json-parser`, `docs/carte-architecture`.
- **Jamais de travail direct sur `main`** : il reste dans un état qui compile et qui passe les tests.
- **Des commits petits et ciblés**, avec un message clair (par exemple `docs: carte de l'architecture`).
- **Une PR = un sujet**, relue avant fusion.
- Ne pas committer ce qui se génère : `build/`, dépendances, fichiers temporaires.

### 14.2 Mettre sa branche à jour par rapport à `main`

Faire régulièrement, pour que les conflits se règlent petit à petit. On utilise `merge`, pas `rebase` : l'historique n'est pas réécrit, rien ne casse pour les autres.

| Commande | Ce qu'elle fait |
|---|---|
| `git status` | vérifie qu'il ne reste rien à commiter (sinon `git add` puis `git commit` d'abord) |
| `git fetch origin` | télécharge les nouveautés du dépôt distant, sans modifier les fichiers |
| `git log --oneline HEAD..origin/main` | liste les commits de `main` que la branche n'a pas encore : le retard |
| `git switch ma-branche` | se placer sur sa branche |
| `git merge origin/main` | fusionne `main` dans la branche |
| recompiler puis `ctest` | vérifie que tout compile et que les tests passent après la fusion |
| `git push` | publie la branche mise à jour |

Filet de sécurité : `git branch sauvegarde-ma-branche` avant de commencer crée une copie de secours ; `git merge --abort` annule une fusion en cours et revient à l'état d'avant.

Si la branche touche à des fichiers renommés ou déplacés par la réorganisation, voir [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md).

### 14.3 Résoudre un conflit

Un conflit apparaît quand la branche et `main` ont modifié les mêmes lignes. Git ne sait pas choisir et demande.

```text
<<<<<<< HEAD
int tileCount = 4;   // version de la branche
=======
int tileCount = 8;   // version de main
>>>>>>> origin/main
```

1. `git status` liste les fichiers en conflit (`both modified`).
2. Ouvrir chaque fichier, garder une version ou combiner, puis **supprimer les trois lignes de marqueurs**.
3. `git add <fichier>`.
4. `git commit` conclut la fusion. Recompiler et lancer `ctest`.

### 14.4 Avant d'ouvrir une pull request

- [ ] Le projet **compile sans avertissement**.
- [ ] `ctest` est vert.
- [ ] Noms, commentaires et includes suivent ces conventions.
- [ ] Pas de code mort, de `std::cout` de debug ni de fichier temporaire.
- [ ] Tout nouveau `.cpp` est sous `src/backend/`.

En revue de code : le code est-il **lisible sans explication** ? Les noms disent-ils ce que c'est ? Les cas limites sont-ils testés ? Chaque fonction fait-elle une seule chose ? On commente le code, jamais la personne, et une remarque est une proposition d'amélioration.
