# ManualMatchesStep

`StepsNs::ManualMatchesStepClass`, enregistré sous `manual-matches`, applique les corrections faites à la main sur les points de contrôle d'une paire de tuiles (tâche 7). Il remplace les points des paires corrigées, recalcule leur translation et laisse les autres paires inchangées. Une paire que l'appariement ou `vote-2d` a écartée est ajoutée : c'est ainsi qu'on relie à la main une tuile qu'aucune paire ne reliait plus, et dont `absolute-pos` refuserait sinon le placement (« some tiles are disconnected from the reference »). Il se place après le step d'appariement et après `vote-2d` s'il est présent, juste avant `absolute-pos` : appariement → `vote-2d` → `manual-matches` → `absolute-pos`. Placé avant `vote-2d`, il verrait ses points saisis à la main refiltrés, voire la paire rejetée s'il en reste trop peu.

Ce step est la partie backend de la tâche. L'écran de saisie des points viendra quand l'interface sera reliée au backend (R2) : il n'aura qu'à écrire les corrections dans le paramètre `corrections` du script, puis à relancer le run.

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | `tiles` | `Tiles` | `DataNs::TileList` | `import` |
| Entrée | `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` | `phase-correlation`, `sift`, `lightglue` ou `vote-2d` |
| Sortie | `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` | `absolute-pos`, `intensity-correction` |

- **Lit** dans `tiles` les identifiants et les voisines de chaque tuile (pas les pixels), et dans `matches` toutes les paires.
- **Écrit** dans `matches` les mêmes paires, dans le même ordre, suivies des paires ajoutées. Une paire ajoutée a la direction de B par rapport à A (`Right` ou `Bottom`), comme celles des matchers. Pour chaque paire corrigée ou ajoutée : `pointsInA` et `pointsInB` sont remplacés par les points saisis, `confidenceScores` vaut 1 pour chaque point (points vérifiés par l'utilisateur), et `translationX`, `translationY` sont recalculés. Les autres paires ne changent pas.

`getResult()` contient `pairs` (nombre de paires en sortie), `correctedPairs` (`{idA, idB, added}` des paires corrigées, dans le sens de `matches` ; `added` vaut `true` pour une paire ajoutée) et `error`.

## Paramètre

`corrections` est de type `json` : un tableau, vide par défaut. Avec un tableau vide, `matches` est transmis sans changement. Chaque correction désigne deux tuiles voisines par `tileIdA` et `tileIdB` et donne les points appariés : `pointsInA[i]` (dans la tuile `tileIdA`) correspond à `pointsInB[i]` (dans la tuile `tileIdB`), en pixels de pleine résolution, chacun dans le repère de sa tuile.

Les paires de `matches` vont toujours de A vers sa voisine de droite ou du bas. Une correction saisie dans l'autre sens, par exemple `(1, 0)` pour la paire `(0, 1)`, est retournée : les deux tuiles sont échangées avec leurs points, et c'est la paire `(0, 1)` qui est corrigée. Si la paire n'est pas dans `matches`, elle est ajoutée à la fin.

## Calcul

`absolute-pos` ne lit que `translationX` et `translationY`, pas les points. Après remplacement des points, la translation de la paire est donc recalculée, avec la même convention que les matchers (position de B dans le repère de A) :

```
translationX = médiane de (pointsInA[i].x - pointsInB[i].x)
translationY = médiane de (pointsInA[i].y - pointsInB[i].y)
```

La médiane plutôt que la moyenne : un point mal placé ne déplace pas la tuile.

## Reprise

Les corrections sont des paramètres du step, donc enregistrées dans le script. Un checkpoint n'est valide que si les paramètres du step n'ont pas changé : modifier une correction rend invalide le checkpoint de `manual-matches`. À la reprise, l'import et l'appariement sont sautés, `manual-matches` et les steps suivants sont refaits.

## Erreurs

Le step renvoie `false`, avec la raison dans `error`, si une correction :

- désigne une tuile absente de `tiles`, deux fois la même tuile, ou deux tuiles qui ne sont pas voisines (voisine de droite ou du bas de l'une ou de l'autre) ;
- corrige une paire déjà corrigée, dans un sens ou dans l'autre ;
- n'a pas d'entiers `tileIdA` et `tileIdB` ;
- a une liste de points vide, mal formée (`[x, y]` attendu) ou non finie ;
- a des listes `pointsInA` et `pointsInB` de tailles différentes.

En cas d'erreur, `matches` n'est pas modifié, même si des corrections précédentes de la liste étaient valides.

## Tests

`tests/steps/test_manual_matches_step.cpp` (label `unit`), sur une grille de 2 × 2 tuiles sans pixels :

- médiane avec un point faux, seule la paire corrigée change ;
- paire écartée par l'appariement ajoutée à la fin avec sa direction ;
- correction saisie dans l'autre sens appliquée à la paire existante ;
- liste vide transmise sans changement ;
- refus sans modification de `matches` (tuile inconnue, tuiles non voisines ou identiques, paire corrigée deux fois, listes vides, de tailles différentes ou mal formées, `corrections` qui n'est pas un tableau) ;
- tuile reliée à aucune autre : `absolute-pos` échoue, puis la place une fois la paire ajoutée ;
- checkpoint invalidé quand une correction change.

Non couvert : le paramètre lu depuis le script (`importConfig`) et le refus par `buildPipeline` sans step d'appariement.

## Exemple JSON

À insérer juste avant `absolute-pos`, après l'appariement et après `vote-2d` s'il est présent :

```json
{
  "name": "manual-matches",
  "parameters": [
    { "valueType": "json", "name": "corrections", "value": [
      { "tileIdA": 0, "tileIdB": 1,
        "pointsInA": [[812.0, 140.5], [820.0, 600.0]],
        "pointsInB": [[12.0, 141.0], [20.0, 600.5]] }
    ]}
  ]
}
```
