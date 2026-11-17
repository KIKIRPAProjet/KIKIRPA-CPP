# Vote2DStepClass

Code : `steps/vote_2d_step.h` / `.cpp` (namespace `StepsNs`). Algorithme : `algorithms/rejection/vote_2d/vote_rejector` ([Vote2D.md](../algorithms/rejection/vote_2d/Vote2D.md)).

Step de rejet des correspondances aberrantes (*outliers*), enregistré sous `vote-2d`. Placé après un step d'appariement (`phase-correlation`, `sift` ou `lightglue`), il filtre chaque paire de `matches` par vote de Hough 2D et médiane itérative (`RejectionNs::VoteRejectorClass`), ne garde que les correspondances cohérentes (*inliers*), recalcule la translation `(translationX, translationY)` et retire les paires qui n'ont pas assez d'inliers. Il ne relit aucune image.

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` | `phase-correlation`, `sift` ou `lightglue` |
| Sortie | `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` | `absolute-pos`, `intensity-correction` |

- **Lit** dans chaque entrée de `matches` les identifiants `tileIdA`, `tileIdB`, la direction `direction` et les correspondances `featureMatches` (`pointsInA`, `pointsInB`, `confidenceScores`). Aucune image n'est relue.
- **Écrit** dans `matches` (en remplaçant l'entrée) uniquement les paires validées par le vote 2D : `pointsInA`, `pointsInB` et `confidenceScores` sont réduits aux seuls *inliers*, et `(translationX, translationY)` est mise à jour avec la translation 2D raffinée par médiane itérative (en pixels de pleine résolution, dans le repère de la tuile A).

`getResult()` : `pairs` (nombre de paires conservées), `kept` (identique à `pairs`), `removed` (nombre de paires écartées), `failures` (`{idA, idB, reason}` pour chaque paire écartée) et `error` (vide en cas de succès, message d'erreur en cas d'échec).

## Paramètres

| Nom | Type | Défaut | Rôle |
|---|---|---|---|
| `seuil` | `float` | `1.5` | Rayon de tolérance euclidienne (en px) autour de la translation estimée pour qualifier un point d'inlier |
| `case` | `float` | `1.0` | Taille des cellules (en px) de la grille d'accumulation 2D dans l'espace des déplacements $(\Delta x, \Delta y)$ |
| `minMatches` | `int` | `3` | Nombre minimal de correspondances à coordonnées finies requis pour tenter l'estimation d'une paire ($\ge 2$) |
| `minInliers` | `int` | `3` | Nombre minimal d'inliers requis pour valider et conserver une paire ($\ge 2$) |

Dans la structure C++ `StepsNs::Vote2DInput`, le paramètre JSON `"case"` (mot-clé réservé en C++) correspond au champ `cellSize`.

## Fonctionnement

Pour chaque paire de tuiles présente dans `matches`, le step appelle `RejectionNs::VoteRejectorClass::filterPairMatches` (détaillé dans [Vote2D.md](../algorithms/rejection/vote_2d/Vote2D.md)) :

1. Filtrage des coordonnées non finies (`NaN`, `Inf`) et calcul des déplacements $\Delta_i = \mathbf{p}_{A,i} - \mathbf{p}_{B,i}$. Si moins de `minMatches` déplacements sont valides, la paire est rejetée.
2. Discrétisation des déplacements sur une grille de pas `case` et vote de chaque correspondance dans un voisinage $3 \times 3$ autour de sa cellule.
3. Extraction déterministe du mode de vote (centre de la cellule la plus votée).
4. Raffinement sous-pixel de la translation $(t_x, t_y)$ par 3 passes de médiane $L_1$ sur les correspondances situées à une distance euclidienne $\le \text{seuil}$.
5. Sélection finale des *inliers* à distance $\le \text{seuil}$ de la translation raffinée : si leur nombre est supérieur ou égal à `minInliers`, seuls ces points et leurs scores sont conservés dans `featureMatches`, et `(translationX, translationY)` reçoit $(t_x, t_y)$. Sinon, la paire est écartée et ajoutée à `result["failures"]`.

## Exemple JSON

```json
{
  "pipeline": {
    "steps": [
      { "name": "import", "parameters": [ { "valueType": "string", "name": "path", "value": "images/" } ] },
      { "name": "sift" },
      { "name": "vote-2d",
        "parameters": [
          { "valueType": "float", "name": "seuil", "value": 1.5 },
          { "valueType": "float", "name": "case", "value": 1.0 },
          { "valueType": "int", "name": "minMatches", "value": 3 },
          { "valueType": "int", "name": "minInliers", "value": 3 }
        ] },
      { "name": "absolute-pos" }
    ]
  }
}
```

## Erreurs

`execute()` renvoie `false`, place la cause dans `result["error"]` et **ne modifie pas** l'entrée `matches` présente dans le DataStore si :

- aucun `DataStoreClass` n'est attaché au step ;
- la clé `matches` est absente du DataStore ou d'un autre type ;
- `seuil` ou `case` n'est pas fini ou est $\le 0$ ;
- `minMatches` ou `minInliers` est $< 2$ ou hors de la plage `int` ;
- aucune paire ne survit au filtrage (`output.value.empty()`).

## Tests

Fichier : `tests/steps/test_vote_2d_step.cpp` (cible `test_vote_2d_step`, label `unit`).

Couvre :
- le filtrage nominal des *outliers*, la mise à jour de `(translationX, translationY)` et le rejet d'une paire insuffisante dans `failures` ;
- l'échec quand aucune paire ne survit et la préservation de l'entrée `matches` dans le DataStore ;
- la lecture des paramètres `seuil`, `case`, `minMatches` et `minInliers` depuis un script JSON (`importConfig`) ;
- le rejet des paramètres hors bornes et des entrées manquantes ;
- le filtrage des coordonnées non finies (`NaN`, `Inf`) et le rejet des paires dont `confidenceScores` a une taille incohérente avec `pointsInA` ;
- la vérification de l'enchaînement par `PipelineNs::buildPipeline` (refusé sans step d'appariement en amont, accepté entre `phase-correlation` et `absolute-pos`) ;
- la sauvegarde et la restauration de la sortie filtrée via `CheckpointNs::CheckpointClass`.
