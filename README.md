# Validation générique de pipelines JSON en C++17

Bibliothèque de validation **statique**, indépendante de toute exécution métier. Elle lit un fichier JSON, vérifie sa structure, les configurations et les liaisons entre étapes, puis produit un rapport JSON et un ordre déterministe. Aucun dataset, chemin d'image ou traitement n'est ouvert pendant la validation.

Le dossier initial était vide : il ne contenait ni classes `Pipeline`/`Step`, ni conventions, ni build ou tests. Cette livraison propose donc une bibliothèque autonome et un exemple d'intégration ; elle ne prétend pas avoir connecté un pipeline de stitching préexistant.

## Construire, tester et utiliser

Prérequis : CMake ≥ 3.16 et compilateur C++17 (Clang, GCC ou MSVC).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/pipeline-validator validate examples/valid.json
./build/pipeline-validator validate examples/missing-dependency.json
./build/integration-example
```

Avec un générateur multiconfiguration, ajouter `--config Debug` au build et `-C Debug` à CTest ; les exécutables sont dans `build/Debug`.

La commande imprime le rapport sur stdout. Codes de sortie : **0** valide, **1** invalide ou fichier inaccessible, **2** mauvais arguments. La CLI utilise le registre du stitching. Pour d'autres domaines, créer un registre C++ puis utiliser la bibliothèque (voir l'exemple ETL compilé `examples/integration.cpp`). Aucun plugin, fonction ou code arbitraire n'est chargé depuis le JSON.

## Organisation et décisions

- `include/pipeline/validator.hpp` : en-tête commun conservé pour la compatibilité.
- `include/pipeline/` : un en-tête par composant (rapport, étape, configuration, registre, graphe et validateur).
- `src/validation_report.cpp` : sérialisation des diagnostics et rapports.
- `src/step_config_validator.cpp` : paramètres de configuration.
- `src/step_registry.cpp` : enregistrement et recherche des types.
- `src/dependency_resolver.cpp` : graphe, cycles et ordre topologique.
- `src/pipeline_schema.cpp` : structure JSON et lecture des étapes.
- `src/pipeline_rules.cpp` : dépendances obligatoires et contrats de sortie.
- `src/pipeline_validator.cpp` : orchestration et garde avant exécution.
- `src/pipeline_io.cpp` : lecture du texte JSON et des fichiers.
- `src/ordered_json.cpp` et `src/ordered_options.cpp` : adaptation du format métier.
- `src/ordered_stitching.cpp` : règles des noms et options du format métier.
- `src/diagnostic_utils.hpp` : création des diagnostics, outil interne.
- `tests/CMakeLists.txt` : configuration des tests et découverte des scénarios JSON.
- `src/stitching.cpp` : règles métier modifiables, séparées des algorithmes.
- `src/main.cpp` : petite CLI.
- `tests/validator_tests.cpp` : tests autonomes sans framework ni images.
- `examples/` : configurations valides/invalides, intégration ETL et rapport.
- `docs/architecture.md` : règles détaillées, UML et extension.
- `third_party/nlohmann/json.hpp` : nlohmann/json **3.11.3**, sous licence MIT fournie dans `third_party/nlohmann/LICENSE.MIT`, [source officielle](https://github.com/nlohmann/json/tree/v3.11.3).

C++17 permet des valeurs possédées, `std::optional`, des callbacks typés et une API simple. La bibliothèque JSON est distribuée avec le source : le build ne télécharge rien. Elle évite d'écrire un parseur JSON maison. Aucun framework d'exécution n'est nécessaire.

Les règles imposent des **liaisons directes explicites** ; une étape présente dans le fichier ne satisfait pas à elle seule une dépendance. Les instances sont sélectionnées par les IDs du JSON et, au besoin, par un ID imposé dans le registre. Aucun choix implicite n'est effectué.

## Format métier : name/options

Le format fourni par votre projet est directement pris en charge :

```json
{
  "pipeline": {
    "steps": [
      {
        "name": "import",
        "options": [
          {"type": "string", "key": "path", "value": "images/"},
          {"type": "string", "key": "format", "value": ".*\\.tif$"}
        ]
      }
    ]
  }
}
```

L’exemple complet avec les sept étapes et vos paramètres est dans `examples/valid.json` et `tests/json/02_pipeline_complet.json`.

- Chaque étape exige un `name` non vide et une liste `options`.
- Chaque option exige `type`, `key` et `value`. Les types acceptés sont `string`, `int`, `float`, `bool`. Une valeur entière convient aussi à `float` ; un booléen n’est pas un nombre.
- Les noms d’étapes et clés d’options sont uniques. Les noms sont sensibles à la casse.
- L’ordre de la liste définit l’ordre d’exécution. Un prérequis placé après l’étape utilisatrice entraîne `MISSING_DEPENDENCY`, comme un prérequis absent. Il peut être séparé de son utilisateur par d’autres étapes.
- Les noms reconnus sont `import`, `stitching`, `absolutePositioning`, `intensityCorrection`, `seamline`, `fusion`, `export`.
- `pipeline.name` et `pipeline.version` sont facultatifs dans ce format. Les pipelines vides restent refusés par défaut.

Les options sont transformées en configuration interne. Les règles métier sont dans `src/ordered_stitching.cpp` ; les valeurs autorisées et limites sont des règles d’exemple configurables. La lecture est séparée entre `src/ordered_json.cpp` et `src/ordered_options.cpp`.

Les exemples invalides couvrent une importation absente, un prérequis placé après son utilisateur et des options incorrectes : voir `tests/README.md`.

### Compatibilité avec le format explicite

Le format précédent (`pipeline.name`, `pipeline.version`, étapes avec `id`, `type`, `depends_on`, `config`) reste pris en charge. Il permet un graphe non séquentiel, plusieurs instances de même type et les contrôles de cycles. Ne pas mélanger les deux formats dans un même pipeline. `examples/cycle.json` et l’intégration ETL illustrent cette API explicite.

## Rapport et erreurs

Le rapport contient `valid`, `errors`, `warnings`, `validatedSteps` et `executionOrder`. Chaque diagnostic possède `code`, `message`, `details` et `stepId` lorsqu'il est connu. `executionOrder` est `null` si une seule erreur subsiste. Les étapes localement correctes et dont les ancêtres sont corrects apparaissent dans `validatedSteps` ; elles ne constituent jamais une autorisation d'exécuter un pipeline globalement invalide. Une erreur globale de métadonnée peut laisser des étapes localement validées.

| Code | Signification |
| --- | --- |
| `INVALID_JSON` | Syntaxe JSON invalide, nombre non représentable ou clé d'objet répétée |
| `INVALID_SCHEMA` | Structure, champ ou type JSON incorrect ; pipeline vide interdit |
| `UNKNOWN_STEP_TYPE` | Type absent du registre |
| `DUPLICATE_STEP_ID` | Identifiant déclaré plusieurs fois |
| `MISSING_DEPENDENCY` | Liaison directe obligatoire ou alternative non satisfaite |
| `UNKNOWN_DEPENDENCY` | Référence à un ID inexistant ou non interprétable |
| `CYCLIC_DEPENDENCY` | Cycle ; `details.cycle` contient un chemin fermé |
| `SELF_DEPENDENCY` | Étape dépendant d'elle-même |
| `INVALID_CONFIGURATION` | Paramètre invalide ou échec du callback de configuration |
| `INCOMPATIBLE_STEP_OUTPUT` | Contrats statiques de sortie/entrée incompatibles |
| `FILE_READ_ERROR` | Fichier impossible à ouvrir ou lire |
| `DUPLICATE_DEPENDENCY` | Avertissement : référence répétée, traitée comme une seule liaison |

Les vérifications continuent après les erreurs récupérables. Une syntaxe JSON invalide empêche l'analyse structurelle. Une étape mal structurée est écartée du graphe, avec diagnostic ; les autres étapes restent analysées. En cas d'ID dupliqué, la première définition structurellement correcte sert uniquement à poursuivre les diagnostics : le pipeline reste invalide et aucun ordre exécutable n'est publié.

## Intégration rapide

```cpp
#include <pipeline/validator.hpp>
#include <iostream>

pipeline::PipelineValidator validator(pipeline::stitchingRegistry());
auto report = validator.validateFile("pipeline.json");
std::cout << report.toJson().dump(2) << '\n';
```

Lier votre application à la cible CMake `pipeline_validation`. Pour garantir la validation avant exécution, appeler `requireValid(validator, snapshot)` au début de votre `Pipeline::run()`, puis utiliser `report.executionOrder` et **le même snapshot immuable** pour l'exécution. En cas d'erreur, la garde lève `std::runtime_error` contenant le rapport JSON. Elle ne connaît aucun exécuteur métier.

Voir `examples/integration.cpp` pour un registre ETL complet. La validation de fichiers ne constitue pas un contrôle des sorties réelles : les tags de contrats offrent un point d'extension pour une future vérification dynamique dans le moteur hôte.

## Scénarios JSON prêts à tester

Voir [le tableau des 24 scénarios et leurs rapports](tests/README.md). Les fichiers sont dans `tests/json/`, les rapports conservés dans `tests/reports/`. Relancer seulement ces cas avec `ctest --test-dir build -R "^json-" --output-on-failure`.

## Responsabilité de chaque fichier

Voir [le rapport détaillé des fichiers](docs/responsabilites-fichiers.md), avec le rôle des sources, en-têtes, scénarios, rapports et fichiers de construction.
