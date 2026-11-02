# Étape LightGlue

## Rôle

`LightGlueStep` consomme les tuiles chargées par `ImportStep` et produit les correspondances entre les voisines immédiates. Il utilise le modèle existant `LightGlueMatcher`, sans RANSAC, positionnement global, fusion ni export de mosaïque.

Une paire est traitée une seule fois : vers la droite puis vers le bas, dans l'ordre des tuiles reçues. Les images complètes sont transmises au matcher. Aucun découpage de bande n'est réalisé ; les coordonnées de sortie restent dans les repères des images originales.

## Contrat

- Nom de l'étape : `lightglue`.
- Entrée `DataStore` : `tiles`, de type `stitching::Tiles` (`vector<shared_ptr<Tuile>>`).
- Sortie `DataStore` : `matches`, de type `vector<stitching::PairMatches>`.
- Chaque `PairMatches` contient les identifiants `idA` et `idB`, une direction et un `Matches` (points A/B, scores, dx/dy). Les voisins sont lus via `Tuile::getRight()` et `Tuile::getBottom()`.
- `dx/dy` estime la translation par médiane des différences A - B. Ce n'est pas une transformation géométrique validée par RANSAC.
- `LightGlueOutput` représente la sortie du step avec trois champs : `key` ("matches"), `value` (la liste `PairMatches`) et `type` ("PairMatches").
- `getOutput()` renvoie un `const LightGlueOutput&` ; les résultats se lisent via `getOutput().value`. Comme pour `ImportOutput.tiles`, le DataStore conserve directement la liste sous sa clé (`matches` de type `PairMatches`).
- Après succès, `exportData()` renvoie l'enveloppe JSON `key/value/type`, avec les identifiants des tuiles, la direction, les points, les scores et dx/dy. Sur échec, il renvoie un diagnostic `error`, accompagné de la paire si elle est connue.

Exemple de forme JSON (une seule correspondance illustrée pour alléger l'exemple ; quatre sont requises par défaut) :

```json
{
  "key": "matches",
  "type": "PairMatches",
  "value": [
    {
      "idA": 0,
      "idB": 1,
      "direction": "right",
      "matches": {
        "pointsA": [[900, 250]],
        "pointsB": [[100, 252]],
        "scores": [0.98],
        "dx": 800,
        "dy": -2
      }
    }
  ]
}
```

La sérialisation convertit les points OpenCV en tableaux `[x, y]`. Le parser Python du projet n'étant pas présent dans cette branche, son intégration doit encore être vérifiée avec son schéma effectif.

## Configuration

| Option | Type déclaré | Valeur par défaut | Règle |
|---|---|---|---|
| model_path | string | chaîne vide | Chemin obligatoire d'un modèle ONNX compatible. |
| min_matches | int | 4 | Entier strictement positif ; correspondances minimum par paire. |
| overlap | float | 0.2 | Nombre dans (0, 1] ; transmis dans PairContext, actuellement ignoré par LightGlueMatcher. |

Le seuil de confiance reste fixé à 0.9 dans le matcher. Les sorties sont également contrôlées par le step : tailles cohérentes, coordonnées finies dans les images, scores finis entre 0.9 et 1, translation finie.

Le constructeur prépare les options. `Step::importConfig()` remplace toute la configuration : une configuration importée doit donc contenir les trois options. Les options inconnues, dupliquées, de type incorrect ou manquantes sont refusées.

Exemple d'objet décrivant uniquement cette étape :

```json
{
  "name": "lightglue",
  "options": [
    {"type": "string", "key": "model_path", "value": "/chemin/modele.onnx"},
    {"type": "int", "key": "min_matches", "value": 4},
    {"type": "float", "key": "overlap", "value": 0.2}
  ],
  "pipelineOptions": [{"type": "Tiles", "key": "tiles"}],
  "signature": {"pipelineOptions": [{"type": "PairMatches", "key": "matches"}]}
}
```

Cet objet peut configurer un `LightGlueStep` déjà construit via `importConfig()`.

## Utilisation C++

```cpp
#include "core/Pipeline.h"
#include "steps/ImportStep.h"
#include "steps/LightGlueStep.h"

stitching::Pipeline pipeline;
pipeline.add(std::make_unique<stitching::steps::ImportStep>(
    stitching::steps::ImportInput{"images/"}));
pipeline.add(std::make_unique<stitching::steps::LightGlueStep>(
    stitching::steps::LightGlueInput{"/chemin/modele.onnx", 4}));
if (!pipeline.run()) {
    // L'appelant présente l'erreur journalisée et interrompt le traitement.
} else {
    const auto& pairs = pipeline.getData()
        .get<std::vector<stitching::PairMatches>>("matches");
    // Consommer pairs dans l'étape suivante.
}
```

Le parcours de validation import -> LightGlue est défini dans `tests/steps/LightGlueTestPipeline.h` et utilisé par les tests automatisés. Il ne fait pas partie de l'API du builder applicatif `buildPipeline(settings, a, b)`, dont l'implémentation initiale reste à compléter séparément.

## Gestion des erreurs

Le step renvoie `false` avec `outputData.error` pour les erreurs attendues : configuration, données absentes/mal typées, identifiants négatifs ou dupliqués, voisins invalides, images vides, format de pixels non pris en charge, absence de voisins, dépendances ou modèle manquants, erreur d'inférence et correspondances insuffisantes.

Les images acceptées sont des matrices 2D en 8 ou 16 bits non signés, avec 1, 3 ou 4 canaux. Chaque paire doit réussir : il n'y a pas de mode qui ignore silencieusement une paire en échec. Les tuiles sans voisin sont sans paire ; la connexité de toute la grille n'est pas garantie par cette étape.

La sortie précédente est retirée au début de `execute()`. Aucune sortie partielle n'est publiée si une paire échoue. Le pipeline réinitialise aussi son DataStore à chaque lancement, y compris si l'import échoue avant LightGlue.

Le modèle est chargé au premier traitement valide, puis réutilisé pour les autres paires et relances. Un changement de `model_path` recrée le matcher. Ne pas exécuter simultanément le même objet Step sur plusieurs threads.

## Construction et tests

Prérequis : C++17, CMake, OpenCV (core/imgproc/imgcodecs et dnn pour LightGlue), nlohmann_json, ONNX Runtime et un modèle compatible.

```bash
cmake -S . -B build/lightglue-step -DBUILD_TESTING=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build/lightglue-step --target test_lightglue_step --parallel
ctest --test-dir build/lightglue-step -R '^lightglue_step$' --output-on-failure
```

Le mode déconnecté évite un téléchargement implicite de nlohmann_json : sa configuration CMake doit déjà être disponible. Si les dépendances sont installées hors des chemins usuels, fournir leurs chemins effectifs :

```bash
cmake -S . -B build/lightglue-step \
  -DBUILD_TESTING=ON \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DOpenCV_DIR="/chemin/lib/cmake/opencv4" \
  -Dnlohmann_json_DIR="/chemin/share/cmake/nlohmann_json" \
  -DONNXRUNTIME_ROOT="/chemin/onnxruntime" \
  -DLIGHTGLUE_TEST_MODEL="/chemin/modele.onnx"
cmake --build build/lightglue-step --parallel
ctest --test-dir build/lightglue-step --output-on-failure
```

CMake garde le step compilable sans ONNX, mais son exécution réelle échoue alors explicitement. Le test d'orchestration injecte un matcher contrôlé et ne nécessite pas ONNX. Il nécessite néanmoins OpenCV réel pour sa compilation et son intégration TIFF.

`lightglue_model` n'est enregistré dans CTest que si LightGlue est disponible et `LIGHTGLUE_TEST_MODEL` est fourni. Il vérifie une translation synthétique de 128 px en 8 et 16 bits, avec des dimensions non multiples de 32 et au moins 80 % des correspondances géométriquement cohérentes. Sans modèle, ce test n'a pas été exécuté ; il ne doit pas être présenté comme réussi.

## Limites

Le modèle doit respecter l'un des deux formats déjà décodés par le matcher : une entrée batch avec keypoints/matches/mscores, ou deux entrées image0/image1 avec six sorties. Le mode batch exige des dimensions identiques après préparation. La détection automatique de canaux suppose un contrat NCHW ; les modèles à canaux dynamiques doivent être vérifiés séparément.

Les images sources sont lues à partir des chemins portés par les objets `Tuile`. Les checkpoints et la validation JSON globale restent hors du périmètre de cette étape.
