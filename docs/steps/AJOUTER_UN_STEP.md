# Ajouter un step

Un step est une classe C++ qui lit des données dans le DataStore, fait un calcul, puis y range ses sorties. Le pipeline enchaîne les steps dans l'ordre du script JSON. Le fonctionnement du moteur est décrit dans [core/Pipeline.md](../core/Pipeline.md), et les règles de code dans [CONVENTIONS.md](../CONVENTIONS.md).

Cette page suit un exemple complet : **`match-filter`**, un step qui retire de `matches` les paires qui ont trop peu de points. **Ce step n'existe pas dans le code**, il sert seulement d'exemple. Son code a été compilé avec MSVC contre `main` le 08/11/2026, avec le vrai `step.cpp`. Ses trois premiers tests ont été exécutés et passent. Le quatrième, qui passe par `buildPipeline`, a été compilé mais pas exécuté.

## Sommaire

0. [Avant d'écrire du code](#0-avant-décrire-du-code)
1. [Les fichiers à toucher](#1-les-fichiers-à-toucher)
2. [L'en-tête](#2-len-tête)
3. [Le constructeur : la configuration par défaut](#3-le-constructeur--la-configuration-par-défaut)
4. [`execute()`](#4-execute)
5. [Enregistrer le step](#5-enregistrer-le-step)
6. [Si la sortie est d'un nouveau type](#6-si-la-sortie-est-dun-nouveau-type)
7. [Tester](#7-tester)
8. [L'utiliser dans un script](#8-lutiliser-dans-un-script)
9. [Documenter](#9-documenter)
10. [Liste de contrôle](#10-liste-de-contrôle)

## 0. Avant d'écrire du code

Il faut décider de cinq choses.

**Le nom**, en minuscules, avec les mots séparés par `-` : `match-filter`. C'est le nom à écrire dans le script, dans le registre et dans la configuration du step.

**Les entrées** : les clés que le step lit dans le DataStore. Chacune doit être produite par un step placé avant lui, sinon `buildPipeline` refuse le script. Les clés existantes :

| Clé | Type JSON | Type C++ | Produite par |
|---|---|---|---|
| `tiles` | `Tiles` | `DataNs::TileList` | `import` |
| `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` | `phase-correlation`, `sift` ou `lightglue` |
| `positions` | `Positions` | `std::vector<DataNs::TilePositionData>` | `absolute-pos` |
| `intensityCorrection` | `IntensityCorrection` | `DataNs::IntensityCorrection` | `intensity-correction` |
| `territories` | `Territories` | `std::vector<DataNs::TileTerritory>` | `graphcut` |

Ce que contient chaque donnée : [COMPRENDRE_LE_PROJET.md §4](../COMPRENDRE_LE_PROJET.md#4-ce-que-chaque-step-reçoit-et-produit).

⚠️ **Le type C++ d'une clé est le même pour tous les steps.** Le DataStore range les données dans un `std::any` : lire `matches` avec un autre type que `std::vector<DataNs::PairMatches>` lève `std::bad_any_cast`, et cela n'arrive qu'à l'exécution.

**Les sorties** : il y a deux cas.

- Le step produit **une nouvelle donnée** : il lui faut une nouvelle clé, et souvent un nouveau type (§6).
- Le step **modifie une donnée existante** : il réécrit la même clé. C'est le cas de `match-filter`, parce que les steps suivants lisent des clés fixées dans leur code : `absolute-pos` lit toujours `matches`. `buildPipeline` l'accepte. À la reprise, c'est le checkpoint du dernier step qui a écrit la clé qui est rechargé.

**Les paramètres** : un nom en `camelCase`, un type (`string`, `int`, `float` ou `bool`) et une valeur par défaut. Pour `match-filter`, il y en a un seul : `minPoints` (`int`, 10).

**Où mettre le calcul.** Le step lit ses entrées, appelle le calcul et écrit ses sorties. Au-delà de quelques lignes, le calcul va dans un algorithme, dans `algorithms/<étape>/<nom>/` ([CONVENTIONS.md §8](../CONVENTIONS.md#8-structure-du-code)). L'algorithme ne connaît ni le DataStore ni le JSON du pipeline, et se teste seul. Pour `match-filter`, le calcul tient en deux lignes et reste dans le step.

## 1. Les fichiers à toucher

| Fichier | Quoi |
|---|---|
| `include/backend/steps/match_filter_step.h` | créer : la classe (§2) |
| `src/backend/steps/match_filter_step.cpp` | créer : constructeur et `execute()` (§3, §4) |
| `src/backend/core/pipeline/pipeline_builder.cpp` | un `#include` et une ligne dans `stepRegistry()` (§5) |
| `tests/core/test_pipeline.cpp` | `REGISTERED_STEP_COUNT` + 1 (§5) |
| `tests/steps/test_match_filter_step.cpp` | créer : les tests (§7) |
| `tests/steps/CMakeLists.txt` | quatre lignes pour le test (§7) |
| `docs/steps/MatchFilterStep.md` | créer : la fiche (§9) |
| `docs/steps/README.md`, `docs/COMPRENDRE_LE_PROJET.md` §4 | une ligne chacun (§9) |
| `include/backend/core/data/…`, `core/checkpoint/…` | seulement pour un nouveau type de donnée (§6) |

**Aucun `CMakeLists.txt` à modifier pour le code du step** : `cmake/backend.cmake` prend tous les `.cpp` de `src/backend/` (`file(GLOB_RECURSE … CONFIGURE_DEPENDS)`). Si l'IDE ne voit pas le nouveau fichier, relancer `cmake --preset <preset>`.

## 2. L'en-tête

```cpp
// include/backend/steps/match_filter_step.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <string>
#include <vector>

namespace StepsNs {

    // Paramètres du step, avec leurs valeurs par défaut.
    struct MatchFilterInput {
        int minPoints = 10;   // nombre minimal de points pour garder une paire
    };

    // Sortie du step : la donnée, et la clé et le type sous lesquels elle est rangée dans le DataStore.
    struct MatchFilterOutput {
        std::string key = "matches";
        std::vector<DataNs::PairMatches> value;
        std::string type = "PairMatches";
    };

    // Retire de `matches` les paires qui ont moins de minPoints points. Ne relit aucune image.
    class MatchFilterStepClass : public StepNs::StepClass {

        public:
            explicit MatchFilterStepClass(const MatchFilterInput& input = {});
            bool execute() override;
            const MatchFilterOutput& getOutput() const { return output; }

        private:
            MatchFilterOutput output;

    };

}
```

- La classe s'appelle `<Nom>StepClass`, dans le namespace `StepsNs`, et hérite de `StepNs::StepClass`.
- `<Nom>Input` regroupe les paramètres et leurs valeurs par défaut. On peut ainsi construire le step en C++ sans JSON, par exemple dans un test : `MatchFilterStepClass step(MatchFilterInput{10});`.
- `<Nom>Output` regroupe la sortie, sa clé et son type. `getOutput()` sert aux tests et au débogage.
- **Le constructeur par défaut est obligatoire** : le registre crée le step sans argument (`= {}`).

## 3. Le constructeur : la configuration par défaut

```cpp
// Configuration par défaut : nom du step, paramètres avec leurs valeurs par défaut, entrées et sorties.
MatchFilterStepClass::MatchFilterStepClass(const MatchFilterInput& input) {
    ConfigNs::StepConfig stepConfig;
    stepConfig.name = "match-filter";
    stepConfig.parameters = {{"int", "minPoints", input.minPoints}};
    stepConfig.inputs = {{"PairMatches", "matches"}};
    stepConfig.outputs = {{output.type, output.key}};
    configJson = stepConfig;
}
```

La configuration (`ConfigNs::StepConfig`, [pipeline_config.h](../../include/backend/core/config/pipeline_config.h)) dit au moteur tout ce qu'il doit savoir du step :

| Champ | Forme | Sert à |
|---|---|---|
| `name` | le nom du registre | `importConfig` refuse la configuration d'un autre step |
| `parameters` | `{valueType, name, valeur par défaut}` | `importConfig` n'accepte que ces paramètres, avec ce type ; un paramètre absent du script garde sa valeur par défaut |
| `inputs` | `{dataType, key}` | `buildPipeline` vérifie qu'un step précédent produit chaque clé, avec ce type |
| `outputs` | `{dataType, key}` | `buildPipeline` en tient compte pour les steps suivants ; le checkpoint enregistre ces clés |

Le script ne peut changer que **les valeurs** des paramètres. Les entrées et les sorties sont fixées par le step : si le script en contient, elles sont ignorées.

## 4. `execute()`

```cpp
bool MatchFilterStepClass::execute() {
    output.value.clear();
    result = {{"kept", 0}, {"removed", 0}, {"error", ""}};
    try {
        if (!dataStore) throw std::invalid_argument("match-filter: DataStore is not attached");
        const int minPoints = getParameter("minPoints").get<int>();
        if (minPoints < 1) throw std::invalid_argument("match-filter: minPoints must be at least 1");

        const auto& pairs = dataStore->get<std::vector<DataNs::PairMatches>>("matches");
        for (const DataNs::PairMatches& pair : pairs)
            if (static_cast<int>(pair.featureMatches.pointsInA.size()) >= minPoints) output.value.push_back(pair);
        result["kept"] = output.value.size();
        result["removed"] = pairs.size() - output.value.size();
        if (output.value.empty()) throw std::runtime_error("match-filter: no pair has enough points");

        // En dernier : `pairs` désigne l'ancienne donnée, qui n'existe plus après replace.
        dataStore->replace(output.key, output.value);
        return true;
    } catch (const std::exception& exception) {
        result["error"] = exception.what();
        return false;
    }
}
```

Les includes du `.cpp` sont `steps/match_filter_step.h`, `core/config/pipeline_config.h`, `core/data/algorithm_types.h`, `<nlohmann/json.hpp>`, `<exception>`, `<stdexcept>` et `<vector>`.

Les règles que suivent tous les steps :

1. **Tout remettre à zéro au début** (`output`, `result`). Un même objet step peut être exécuté plusieurs fois, par exemple dans un test ou après une reprise.
2. **Ne jamais laisser sortir d'exception.** En cas d'échec, `execute()` renvoie `false` et met la cause dans `result["error"]`. Le message est en anglais et commence par le nom du step. Le pipeline s'arrête et affiche ce message.
3. **Lire les paramètres avec `getParameter("nom")`**, puis contrôler leurs bornes. `importConfig` ne vérifie que le type : `minPoints = -3` lui convient.
4. **Lire les entrées avec `dataStore->get<T>("clé")`.** C'est une référence, pas une copie, et elle ne vaut plus rien dès que la clé est remplacée. D'où l'ordre ci-dessus : tout lire, calculer, puis écrire.
5. **Écrire les sorties avec `dataStore->replace("clé", valeur)`**, comme tous les steps. `add` lève une exception si la clé existe déjà.
6. **Ne pas publier un résultat faux.** Quand la sortie est une nouvelle clé, les autres steps la vident au début : `absolute-pos` commence par `dataStore->replace(output.key, std::vector<DataNs::TilePositionData>{})`. Après un échec, l'ancien résultat n'est donc plus publié. Ici, la sortie est aussi l'entrée, donc on ne la touche qu'en cas de succès.
7. **Pas de `std::cout` ni de `std::cerr`.** Ce qui doit être lu par un humain va dans `result` : des compteurs, `failures` (une entrée `{idA, idB, reason}` par paire ignorée) et `error`. `getResult()` le renvoie, et le pipeline l'affiche.
8. **Les images.** Un step qui lit des pixels les relit depuis `tile->getSource()`, **une ou deux images à la fois**, et les libère aussitôt. Il ne modifie jamais les fichiers source. Les ordres de grandeur sont dans [COMPRENDRE_LE_PROJET.md §6](../COMPRENDRE_LE_PROJET.md#6-la-mémoire--pourquoi-on-en-parle-autant).
9. **Ce que le step lit hors du pipeline.** Si le step lit un fichier qui n'est pas une entrée du DataStore (un modèle ONNX, par exemple), redéfinir `externalInputsFingerprint()`. Elle renvoie par exemple le nom, la taille et la date du fichier. Sinon, un changement de ce fichier ne périme pas le checkpoint. Modèle : `ImportStepClass`. `lightglue` ne le fait pas encore pour son modèle.

## 5. Enregistrer le step

Dans `src/backend/core/pipeline/pipeline_builder.cpp` :

```cpp
#include "steps/match_filter_step.h"
```

```cpp
const std::map<std::string, StepCreator>& stepRegistry() {
    static const std::map<std::string, StepCreator> registry = {
        // … les steps existants …
        {"match-filter", [] { return std::make_unique<StepsNs::MatchFilterStepClass>(); }},
    };
    return registry;
}
```

C'est le seul endroit où le moteur connaît la liste des steps. Le nom doit être exactement celui de `stepConfig.name`. `tests/core/test_pipeline.cpp` vérifie que chaque nom du registre crée un step de ce nom.

Dans `tests/core/test_pipeline.cpp`, augmenter `REGISTERED_STEP_COUNT` (8 → 9) et le message à côté (`"8 steps enregistrés"`). Sinon, le test du registre échoue.

## 6. Si la sortie est d'un nouveau type

`match-filter` réutilise `PairMatches`. Un step qui produit une donnée d'un genre nouveau, par exemple la taille de la mosaïque, doit faire trois choses de plus.

**1. Déclarer le type** dans `include/backend/core/data/`, namespace `DataNs`. Les types partagés par plusieurs steps vont dans `algorithm_types.h`, et un type plus gros dans son propre fichier, comme `intensity_correction.h`. Le `dataType` des `inputs` et `outputs` est son nom en `PascalCase` : `"MosaicBounds"`.

**2. Lui donner une sérialisation pour les checkpoints.** Sans elle, le pipeline marche, mais le step est **refait à chaque reprise**, avec le message `checkpoint incomplete` dans le journal. Il y a deux façons de faire :

- une ligne dans `pipelineTypes()` (`src/backend/core/checkpoint/checkpoint_types.cpp`), pour un type du cœur du pipeline ;
- un appel à `CheckpointClass::registerType` depuis le constructeur du step. C'est ce que fait `intensity-correction`, avec `core/checkpoint/intensity_correction_checkpoint.cpp`.

Esquisse de la seconde façon (compilée, non testée) :

```cpp
// src/backend/core/checkpoint/mosaic_bounds_checkpoint.cpp
#include "core/checkpoint/checkpoint.h"
#include "core/step/data_store.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>

namespace DataNs {
    struct MosaicBounds { int width = 0; int height = 0; };
}

namespace CheckpointNs {

    namespace {

        using Json = nlohmann::json;

        // Écrit <clé>.json dans le dossier du checkpoint du step.
        void saveBounds(const StepNs::DataStoreClass& store, const std::string& key, const std::filesystem::path& directory) {
            const auto& bounds = store.get<DataNs::MosaicBounds>(key);
            std::ofstream output(directory / (key + ".json"), std::ios::binary | std::ios::trunc);
            output << Json{{"width", bounds.width}, {"height", bounds.height}}.dump(2);
            output.close();
            if (!output) throw std::runtime_error("mosaic bounds checkpoint: cannot write " + key);
        }

        // Relit <clé>.json et range la donnée dans le DataStore, avec le même type C++ que le step.
        void loadBounds(StepNs::DataStoreClass& store, const std::string& key, const std::filesystem::path& directory) {
            std::ifstream input(directory / (key + ".json"), std::ios::binary);
            if (!input) throw std::runtime_error("mosaic bounds checkpoint: cannot read " + key);
            const Json data = Json::parse(input);
            store.replace(key, DataNs::MosaicBounds{data.at("width").get<int>(), data.at("height").get<int>()});
        }

    }

    // Appelée par le constructeur du step qui produit ce type.
    void registerMosaicBoundsCheckpoint() {
        if (!CheckpointClass::hasType("MosaicBounds"))
            CheckpointClass::registerType("MosaicBounds", saveBounds, loadBounds);
    }

}
```

Dans un vrai fichier, `MosaicBounds` est déclaré dans `core/data/`, et `registerMosaicBoundsCheckpoint` dans un en-tête de `include/backend/core/checkpoint/`. Le constructeur du step commence par l'appeler, comme celui de `IntensityCorrectionStepClass`.

**3. Le documenter** : une ligne dans le tableau « Sérialisation des sorties » de [core/Pipeline.md](../core/Pipeline.md#reprise-pause-checkpoints), et son JSON dans [COMPRENDRE_LE_PROJET.md §4](../COMPRENDRE_LE_PROJET.md#4-ce-que-chaque-step-reçoit-et-produit).

## 7. Tester

Les tests unitaires utilisent GoogleTest, un fichier par step, dans `tests/steps/`. Un test n'a pas besoin du pipeline : on remplit un DataStore à la main, on y attache le step, on l'exécute.

```cpp
// tests/steps/test_match_filter_step.cpp
#include "steps/match_filter_step.h"

#include "core/data/algorithm_types.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core/types.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

    DataNs::PairMatches makePair(int tileIdA, int tileIdB, int pointCount) {
        DataNs::PairMatches pair;
        pair.tileIdA = tileIdA;
        pair.tileIdB = tileIdB;
        pair.featureMatches.pointsInA.assign(pointCount, cv::Point2f(0.f, 0.f));
        pair.featureMatches.pointsInB.assign(pointCount, cv::Point2f(0.f, 0.f));
        pair.featureMatches.confidenceScores.assign(pointCount, 1.f);
        return pair;
    }

    // Une paire de 12 points, une de 3.
    std::vector<DataNs::PairMatches> twoPairs() { return {makePair(0, 1, 12), makePair(1, 2, 3)}; }

}

// Le step lit `matches` dans le DataStore et y range le résultat sous la même clé.
TEST(MatchFilterStep, KeepsPairsWithEnoughPoints) {
    StepNs::DataStoreClass store;
    store.replace("matches", twoPairs());
    StepsNs::MatchFilterStepClass step(StepsNs::MatchFilterInput{10});
    step.setDataStore(store);

    ASSERT_TRUE(step.execute()) << step.getResult().dump();
    const auto& kept = store.get<std::vector<DataNs::PairMatches>>("matches");
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(kept[0].tileIdB, 1);
    EXPECT_EQ(step.getResult().at("removed").get<int>(), 1);
}

// Échec : execute() renvoie false, la cause est dans getResult()["error"], l'entrée n'est pas modifiée.
TEST(MatchFilterStep, FailsWhenNoPairIsLeft) {
    StepNs::DataStoreClass store;
    store.replace("matches", twoPairs());
    StepsNs::MatchFilterStepClass step(StepsNs::MatchFilterInput{100});
    step.setDataStore(store);

    EXPECT_FALSE(step.execute());
    EXPECT_FALSE(step.getResult().at("error").get<std::string>().empty());
    EXPECT_EQ(store.get<std::vector<DataNs::PairMatches>>("matches").size(), 2u);
}

// La valeur donnée dans le script remplace la valeur par défaut.
TEST(MatchFilterStep, ReadsItsParameterFromTheScript) {
    StepsNs::MatchFilterStepClass step;
    step.importConfig({{"name", "match-filter"},
                       {"parameters", {{{"valueType", "int"}, {"name", "minPoints"}, {"value", 3}}}}});
    StepNs::DataStoreClass store;
    store.replace("matches", twoPairs());
    step.setDataStore(store);

    ASSERT_TRUE(step.execute());
    EXPECT_EQ(store.get<std::vector<DataNs::PairMatches>>("matches").size(), 2u);
}

// buildPipeline refuse le step si aucun step précédent ne produit `matches`.
TEST(MatchFilterStep, IsRefusedWithoutAMatchingStep) {
    const nlohmann::json script = {{"pipeline", {{"steps", {{{"name", "import"}}, {{"name", "match-filter"}}}}}}};
    EXPECT_THROW(PipelineNs::buildPipeline(script), std::invalid_argument);
}
```

À ajouter à `tests/steps/CMakeLists.txt` :

```cmake
add_executable(test_match_filter_step test_match_filter_step.cpp)
kikirpa_copy_onnxruntime_dlls(test_match_filter_step)
target_link_libraries(test_match_filter_step PRIVATE kikirpa_backend GTest::gtest_main)
gtest_discover_tests(test_match_filter_step PROPERTIES LABELS "unit")
```

`kikirpa_copy_onnxruntime_dlls` est nécessaire même si le step n'utilise pas ONNX. Le backend en dépend, et sous Windows l'exécutable du test ne démarre pas sans ces DLL à côté de lui. Le label `unit` fait tourner le test dans la CI.

Ce qu'un test de step devrait couvrir :

| Cas | Dans l'exemple |
|---|---|
| le cas nominal, sur des données fabriquées à la main | `KeepsPairsWithEnoughPoints` |
| l'échec : `false`, `error` rempli, pas de résultat faux publié | `FailsWhenNoPairIsLeft` |
| un paramètre lu depuis le JSON | `ReadsItsParameterFromTheScript` |
| le chaînage : `buildPipeline` refuse le step sans ses entrées | `IsRefusedWithoutAMatchingStep` |
| la reprise : la sortie est relue depuis le checkpoint | voir `tests/steps/test_absolute_pos_step.cpp` |
| un step qui lit des images : images synthétiques dans un dossier temporaire | voir `tests/steps/test_sift_step.cpp` |

Lancer seulement ce test :

```sh
cmake --build --preset windows-aqt
ctest --preset windows-aqt -R MatchFilterStep
```

Remplacer `windows-aqt` par `linux` ou `macos` selon la machine.

## 8. L'utiliser dans un script

```json
{
  "pipeline": {
    "steps": [
      { "name": "import", "parameters": [ … voir ImportStep.md … ] },
      { "name": "phase-correlation" },
      { "name": "match-filter",
        "parameters": [ { "valueType": "int", "name": "minPoints", "value": 4 } ] },
      { "name": "absolute-pos" }
    ]
  }
}
```

Ici, `minPoints` vaut 4, et non 10, la valeur par défaut. En effet, `phase-correlation` renvoie au plus `nbPoints` points par paire, 10 par défaut. Avec `minPoints = 10`, seules les paires qui ont tous leurs points seraient gardées.

Le paramètre `format` de `import` est à régler pour que `absolute-pos` fonctionne : voir [ImportStep.md](ImportStep.md), section « Le format par défaut ne convient pas à la suite du pipeline ». Pour lancer un script depuis le code : [COMPRENDRE_LE_PROJET.md §3](../COMPRENDRE_LE_PROJET.md#3-faire-tourner-le-pipeline-aujourdhui).

## 9. Documenter

**La fiche du step**, `docs/steps/<Nom>Step.md`, avec ces sections, dans cet ordre :

1. un titre (`# MatchFilterStepClass`), les fichiers du code, puis une ou deux phrases sur ce que fait le step ;
2. **Entrées et sorties** :

   | Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
   |---|---|---|---|---|
   | Entrée | `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` | `phase-correlation`, `sift` ou `lightglue` |
   | Sortie | `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` | `absolute-pos`, `intensity-correction` |

   Puis ce que le step **lit** de chaque entrée (quels champs, s'il relit des images), ce qu'il **écrit** dans chaque sortie (une entrée par quoi, quelles unités, quel repère), et le contenu de `getResult()` ;
3. **Paramètres** : un tableau avec le nom, le type, le défaut et le rôle ;
4. **Fonctionnement** : les étapes du calcul, avec un lien vers la page de l'algorithme ;
5. **Erreurs** : les cas où `execute()` renvoie `false` ;
6. **Checkpoint** : un exemple du JSON écrit, si la sortie est d'un nouveau type ;
7. **Mémoire**, si le step lit des images ;
8. **Tests** : le fichier, et ce qu'il couvre ou ne couvre pas.

[GraphCutStep.md](GraphCutStep.md) et [ImportStep.md](ImportStep.md) suivent ce plan.

**Ailleurs** :

- une ligne dans le tableau de [docs/steps/README.md](README.md) ;
- une ligne dans le tableau de [COMPRENDRE_LE_PROJET.md §4](../COMPRENDRE_LE_PROJET.md#4-ce-que-chaque-step-reçoit-et-produit), et le step dans le schéma ;
- la [carte de l'architecture](../CARTE_ARCHITECTURE.md) et [MIGRATION_REORGANISATION.md](../MIGRATION_REORGANISATION.md) sont mises à jour **après le merge**, dans une PR séparée : l'auteur du step n'a pas à le faire ([CONVENTIONS.md §10](../CONVENTIONS.md#10-documentation)).

## 10. Liste de contrôle

- [ ] Le nom est en `kebab-case`, identique dans `stepConfig.name` et dans `stepRegistry()`.
- [ ] Le constructeur par défaut remplit `name`, `parameters`, `inputs` et `outputs`.
- [ ] Chaque clé lue ou écrite a le même type C++ que partout ailleurs.
- [ ] `execute()` remet `output` et `result` à zéro, ne laisse sortir aucune exception, et renvoie `false` avec `result["error"]` en cas d'échec.
- [ ] Les bornes des paramètres sont contrôlées dans `execute()`.
- [ ] Les entrées sont toutes lues avant que les sorties soient écrites.
- [ ] Pas de `std::cout` ni de `std::cerr`.
- [ ] Les images sont lues une ou deux à la fois, et les fichiers source ne sont jamais modifiés.
- [ ] Un nouveau type de donnée a sa sérialisation de checkpoint.
- [ ] `REGISTERED_STEP_COUNT` est augmenté dans `tests/core/test_pipeline.cpp`.
- [ ] Le test GoogleTest est dans `tests/steps/`, avec le label `unit`, et `ctest` passe.
- [ ] La fiche est écrite dans `docs/steps/`, et une ligne est ajoutée dans `docs/steps/README.md` et dans `COMPRENDRE_LE_PROJET.md` §4.
