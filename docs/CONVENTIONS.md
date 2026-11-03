# Conventions de code

Règles à suivre pour tout nouveau code et toute modification. Elles sont appliquées dans `core/`, `steps/` et `frontend/` depuis la réorganisation (PR #50, voir [MIGRATION_REORGANISATION.md](MIGRATION_REORGANISATION.md)). Les algorithmes et les tests ne les suivent pas encore entièrement (voir [§9](#9-ce-qui-nest-pas-encore-migré)).

En cas de doute, prendre pour modèle `include/backend/core/step/step.h` et `src/backend/steps/phase_correlation_step.cpp`.

## 1. Langue

- **Le code est en anglais** : fichiers, namespaces, classes, fonctions, variables, champs, constantes, clés JSON. Pas d'abréviation (`tileCount`, pas `nbTiles` ni `n`).
- **Les commentaires et la documentation sont en français.**
- Les messages des exceptions et de `result["error"]` sont en anglais, comme dans `core/` et `steps/`.

## 2. Fichiers et dossiers

| Élément | Règle | Exemple |
|---|---|---|
| Dossiers | minuscules, `snake_case` | `include/backend/core/checkpoint/` |
| Fichiers | `snake_case`, `.h` et `.cpp` | `pipeline_builder.h`, `pipeline_builder.cpp` |
| Steps | `<nom>_step.h` / `.cpp` dans `steps/` | `phase_correlation_step.h` |
| Arborescence | `include/` et `src/` identiques | `include/backend/X/y.h` ⟷ `src/backend/X/y.cpp` |

- **Première ligne** de chaque fichier : un commentaire avec son chemin depuis la racine du dépôt (`// include/backend/core/step/step.h`).
- Puis `#pragma once` dans un header.
- Un `.cpp` posé ailleurs que dans `src/backend/` n'est pas compilé (`GLOB_RECURSE` de `cmake/Backend.cmake`).

## 3. Noms

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

- Fonctions internes à un `.cpp` : dans un `namespace { }` anonyme, à l'intérieur du namespace du fichier.

## 5. Includes

- Toujours depuis `include/backend/` (`#include "core/data/algorithm_types.h"`), **jamais** de `"../"`.
- Par groupes séparés d'une ligne vide, triés par ordre alphabétique dans chaque groupe :
  1. dans un `.cpp`, son propre header ;
  2. les headers du projet ;
  3. les bibliothèques externes (`nlohmann`, `opencv2`…) ;
  4. la bibliothèque standard.

## 6. Commentaires

- **Dans le header**, au-dessus de chaque classe, fonction ou champ non évident : *ce que* ça fait, ce que ça renvoie, quand ça lève une exception.
- **Dans le `.cpp`** : *pourquoi* le code est écrit ainsi (choix, cas limites, unités, conventions de repère).
- Pas de commentaire qui répète le code.

## 7. Erreurs

- Un **step** renvoie `false` avec la raison dans `result["error"]`.
- Une fonction qui peut échouer renvoie `bool` ou `std::optional`.
- Les **exceptions** (`std::invalid_argument`) sont réservées aux erreurs de configuration : JSON mal formé, step ou paramètre inconnu, valeur invalide.
- Pas de sortie console (`std::cout`, `std::cerr`) dans les steps ni les algorithmes : les messages passent par `result` (et le logger du pipeline les affiche). Seul `PipelineClass` écrit sur `std::cerr`, quand aucun logger n'est fourni.

## 8. Structure du code

- **Steps** : une classe `<Nom>StepClass` dans `StepsNs`, qui hérite de `StepNs::StepClass`, avec une struct `<Nom>Input` (paramètres) et `<Nom>Output` (sorties). Marche à suivre : [core/Pipeline.md](core/Pipeline.md#ajouter-un-step).
- **Algorithmes** : un dossier par algorithme dans `algorithms/<étape>/<nom>/`. Un algorithme conséquent se découpe comme la corrélation de phase : un header façade (le seul inclus par les steps), un header de types, puis un module par phase (préparation des images, calcul, mise en forme de la sortie). Voir la [carte](CARTE_ARCHITECTURE.md#6-lire-un-algorithme--lexemple-de-la-corrélation-de-phase).
- **Types partagés** entre steps et algorithmes : dans `core/data/algorithm_types.h` (`DataNs`). Un type par clé du `DataStoreClass`.
- **Tests** : GoogleTest, un dossier `tests/<module>/` avec son `CMakeLists.txt`, label `unit`, sans images externes.

**Images**
- Ne jamais convertir 16 bits → 8 bits dans le pipeline. Lire avec `cv::imread(path, cv::IMREAD_UNCHANGED)`.
- Les tuiles reçues par un algorithme sont `const` : on lit des régions, on ne modifie pas les pixels.
- Une seule image pleine résolution en mémoire à la fois quand c'est possible.

## 9. Ce qui n'est pas encore migré

- `algorithms/` : noms français (`Parametres`, `raisonEchec`, `Algorithme`, `Preparation`, `Sortie`…), namespaces d'origine (`stitching::…`, `seamlib`, `Seamline`), et `algorithms/legacy_type_aliases.h`, qui leur évite de préfixer les types de `DataNs`. Ce fichier est **temporaire** : un algorithme migré qualifie lui-même ses types (`DataNs::Matches`) et ne l'inclut plus.
- `tests/` : noms français (`verifier`, `LARGEUR_TUILE`…).
- `graph_cut_step.cpp` écrit ses erreurs sur `std::cerr`, en français, en plus de `result` ; LightGlue écrit des traces `[DEBUG]` sur `std::cout`.
- Les noms des paramètres des steps existants (`niveau`, `lissage`, `marge`…) sont gardés tels quels pour ne pas casser les scripts.

Tout nouveau code suit les règles ci-dessus, même dans un dossier pas encore migré.

## 10. Documentation

- Une page par algorithme dans `docs/algorithms/<étape>/<nom>/`, une page par step dans `docs/steps/`.
- **La [carte de l'architecture](CARTE_ARCHITECTURE.md) reste à jour** : toute PR qui ajoute, déplace, renomme ou supprime un fichier, un step, un type partagé ou un test met à jour la section concernée de la carte dans la même PR.
