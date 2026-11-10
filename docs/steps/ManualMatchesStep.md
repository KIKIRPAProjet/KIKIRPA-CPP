# ManualMatchesStep

`StepsNs::ManualMatchesStepClass`, enregistré sous `manual-matches`, applique les corrections faites à la main sur les points de contrôle d'une paire de tuiles (tâche 7). Il remplace les points des paires corrigées, recalcule leur translation et laisse les autres paires inchangées. Il se place entre le step d'appariement et `absolute-pos`.

Ce step est la partie backend de la tâche. L'écran de saisie des points viendra quand l'interface sera reliée au backend (R2) : il n'aura qu'à écrire les corrections dans le paramètre `corrections` du script, puis à relancer le run.

## Entrées et sorties

| Sens | Clé | Type JSON | Type C++ | Vient de / va vers |
|---|---|---|---|---|
| Entrée | `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` | `phase-correlation`, `sift` ou `lightglue` |
| Sortie | `matches` | `PairMatches` | `std::vector<DataNs::PairMatches>` | `absolute-pos`, `intensity-correction` |

- **Lit** dans `matches` toutes les paires.
- **Écrit** dans `matches` les mêmes paires, dans le même ordre. Pour chaque paire corrigée : `pointsInA` et `pointsInB` sont remplacés par les points saisis, `confidenceScores` vaut 1 pour chaque point (points vérifiés par l'utilisateur), et `translationX`, `translationY` sont recalculés. Les autres paires ne changent pas.

`getResult()` contient `pairs` (nombre de paires en sortie), `correctedPairs` (`{idA, idB}` des paires corrigées) et `error`.

## Paramètre

`corrections` est de type `json` : un tableau, vide par défaut. Avec un tableau vide, `matches` est transmis sans changement. Chaque correction désigne une paire par `tileIdA` et `tileIdB`, dans l'ordre où elle figure dans `matches`, et donne les points appariés : `pointsInA[i]` correspond à `pointsInB[i]`, en pixels de pleine résolution, chacun dans le repère de sa tuile.

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

- désigne une paire absente de `matches` ;
- corrige une paire déjà corrigée ;
- n'a pas d'entiers `tileIdA` et `tileIdB` ;
- a une liste de points vide, mal formée (`[x, y]` attendu) ou non finie ;
- a des listes `pointsInA` et `pointsInB` de tailles différentes.

En cas d'erreur, `matches` n'est pas modifié.

## Exemple JSON

À insérer après un step produisant `matches` :

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
