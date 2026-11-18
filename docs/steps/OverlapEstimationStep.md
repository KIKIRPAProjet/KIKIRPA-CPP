# OverlapEstimationStepClass

Code : [`include/backend/steps/overlap_estimation_step.h`](../../include/backend/steps/overlap_estimation_step.h), [`src/backend/steps/overlap_estimation_step.cpp`](../../src/backend/steps/overlap_estimation_step.cpp).
Algorithme : [OverlapEstimation.md](../algorithms/stitching/overlap_estimation/OverlapEstimation.md).

Mesure, pour chaque paire de tuiles voisines, la position de B par rapport à A **au pixel près** et le recouvrement réel qui s'en déduit, puis en tire la zone commune aux deux tuiles. C'est la *première passe* commune aux trois méthodes d'appariement : elle remplace le recouvrement nominal (`overlap`) par un recouvrement mesuré, sur lequel `phase-correlation`, `sift` et `lightglue` peuvent ensuite travailler.

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | `tiles` | `Tiles` | `DataNs::TileList` | `import` |
| Sortie | `overlapEstimates` | `OverlapEstimates` | `DataNs::OverlapEstimates` | `phase-correlation`, `sift`, `lightglue` (s'ils viennent après, voir [Qui lit cette sortie](#qui-lit-cette-sortie)) |

Ce que le step **lit** de `tiles` : l'identifiant de chaque tuile, ses voisines de droite et du dessous (`getRight()`, `getBottom()`), et **les pixels des deux tuiles de chaque paire**, à pleine résolution, par `PhaseCorrelationStepClass::readPixels(tile, 0)`.

Ce qu'il **écrit** dans `overlapEstimates` : une entrée par paire mesurée, avec

- `tileIdA`, `tileIdB` : les identifiants des deux tuiles, A étant celle dont B est la voisine ;
- `direction` : `right` ou `bottom`, le lien par lequel la paire a été trouvée ;
- `estimate.translationBInA` : le coin haut-gauche de B dans le repère de A, en pixels de pleine résolution, au pixel près ;
- `estimate.overlapFraction` : le recouvrement réel, en fraction de la tuile dans le sens du déplacement ;
- `estimate.peakRatio` : le rapport pic / second pic de la mesure, c'est-à-dire sa confiance ;
- `commonAreaInA`, `commonAreaInB` : la zone commune dans le repère local de chaque tuile, de même taille des deux côtés, en pixels de pleine résolution.

Les paires **non** mesurées sont absentes de la sortie : elles ne sont pas publiées avec des valeurs nulles.

`getResult()` :

| Clé | Contenu |
|---|---|
| `pairs` | nombre de paires effectivement estimées |
| `failures` | une entrée `{ idA, idB, reason }` par paire non mesurée |
| `error` | présent seulement quand `execute()` renvoie `false` |

## Paramètres

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `overlap` | `float` | `0.2` | recouvrement nominal, en fraction de la tuile : largeur des bandes sur lesquelles la mesure part. Doit être dans `]0, 1[` |
| `smoothingSigma` | `float` | `3.0` | sigma (px) du flou gaussien de la surface de corrélation |
| `minimumPeakRatio` | `float` | `1.5` | rapport pic / second pic minimal pour accepter une mesure |
| `railMargin` | `float` | `0.0` | écart maximal (px) toléré entre la translation mesurée et le recouvrement nominal. `0` : pas de contrainte |
| `minimumValidFraction` | `float` | `0.25` | part minimale de pixels exploitables, communs aux deux bandes |

Le type `"float"` d'un paramètre vaut `double` en C++ ([`core/config/pipeline_config.h`](../../include/backend/core/config/pipeline_config.h)) : les quatre derniers sont transmis tels quels à `PhaseCorrelationNs::Parameters`.

Ces quatre réglages sont ceux de la corrélation de phase, et portent ici leurs **noms anglais**, alors que le step `phase-correlation` expose les mêmes sous des noms français (`lissage`, `seuilRapport`, `marge`, `fractionValideMin`). Les deux steps ne se configurent donc pas avec les mêmes clés.

## Fonctionnement

1. Lecture de `tiles` et contrôle de `overlap`.
2. Recherche des paires voisines : pour chaque tuile, sa voisine de droite et celle du dessous, dédoublonnées par `{min(id), max(id)}` — une paire n'est donc mesurée qu'une fois.
3. Pour chaque paire : lecture des pixels des deux tuiles, puis `OverlapEstimatorClass::estimate` sur les bandes de recouvrement nominal.
4. `OverlapEstimatorClass::computeCommonAreas` convertit la translation mesurée en deux rectangles de même taille, un par repère local.
5. Publication des paires mesurées sous `overlapEstimates`.

Une paire dont la mesure échoue, ou dont la zone commune est vide, est inscrite dans `failures` et le step **continue**.

## Erreurs

`execute()` renvoie `false`, avec la cause dans `getResult()["error"]`, quand :

- aucun `DataStoreClass` n'est attaché ;
- `overlap` est hors de `]0, 1[` ;
- la liste de tuiles contient une tuile nulle ;
- les pixels d'une tuile ne peuvent pas être lus ;
- aucune paire voisine n'existe (`tiles` isolées ou vide) ;
- **aucune** paire n'a pu être estimée — les raisons sont alors dans `failures`.

## Checkpoint

Nouveau type `OverlapEstimates`, déclaré dans [`core/checkpoint/checkpoint_types.cpp`](../../src/backend/core/checkpoint/checkpoint_types.cpp). Fichier écrit : `overlapEstimates.json`.

```json
[
  {
    "tileIdA": 0,
    "tileIdB": 1,
    "direction": "right",
    "translationX": 319.875,
    "translationY": -0.03125,
    "overlapFraction": 0.1875,
    "peakRatio": 2.5,
    "commonAreaInA": [320, 0, 80, 300],
    "commonAreaInB": [0, 0, 80, 300]
  }
]
```

## Mémoire

Le step garde **deux tuiles à pleine résolution** en mémoire pendant la mesure d'une paire, puis les libère : avec des tuiles de 400 Mo, le pic est d'environ deux tuiles, comme `phase-correlation`.

En revanche, une tuile est **relue une fois par paire à laquelle elle appartient** : au centre d'une grille, la même tuile est lue quatre fois depuis le disque. Sur une grille 10×10 (180 paires voisines), cela fait 360 lectures à pleine résolution. Deux pistes, non faites : mémoriser l'image de la tuile courante le temps de ses paires, et estimer à un niveau réduit (`readPixels` prend déjà un niveau, et une mesure au pixel près n'a pas besoin de la pleine résolution).

## Tests

[`tests/steps/test_overlap_estimation_step.cpp`](../../tests/steps/test_overlap_estimation_step.cpp), label `unit` : échec sans `DataStore`, `overlap` invalide, paire horizontale, paire verticale, grille 2×2 (quatre paires), paire sans recouvrement estimable, et aller-retour `importConfig` de la configuration par défaut.

L'aller-retour sauvegarde → chargement du type `OverlapEstimates` est couvert par `Reprise.TypesDuPipeline` dans [`tests/core/test_reprise.cpp`](../../tests/core/test_reprise.cpp).

Non couvert : données réelles, mesure à un niveau réduit, tuiles de taille réelle (temps et mémoire mesurés nulle part).

## Qui lit cette sortie

Quand `overlap-estimation` est placé avant l'appariement, les trois steps d'appariement partent de la translation mesurée, paire par paire (fonctions `findOverlapEstimates` et `findEstimatedTranslation` de `steps/matching_step_utils`) :

- `phase-correlation` la ramène au niveau `niveau`, puis la donne à l'algorithme dans `DataNs::PairContext::estimatedTranslation`. Il cherche alors le décalage autour de cette translation, au lieu de partir du recouvrement nominal ;
- `sift` et `lightglue` prennent la zone commune qu'elle donne, au lieu de refaire leur propre première passe.

Une paire absente de `overlapEstimates` (non mesurée) garde l'ancien comportement : recouvrement nominal pour `phase-correlation`, première passe interne pour `sift` et `lightglue`. Sans ce step dans le script, rien ne change.

`overlapEstimates` n'est pas une entrée déclarée de ces steps : un script sans `overlap-estimation` reste valide. C'est donc l'ordre du script qui compte : `overlap-estimation` doit venir **avant** l'appariement.

## Limites
- Le step appelle `PhaseCorrelationStepClass::readPixels`, donc un step en appelle un autre, ce que [CONVENTIONS.md §8](../CONVENTIONS.md#8-structure-du-code) écarte. `intensity-correction` fait déjà la même chose : la lecture des pixels d'une tuile devrait remonter dans un utilitaire partagé.
