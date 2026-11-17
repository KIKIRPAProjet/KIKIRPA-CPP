# Vote 2D (`VoteRejectorClass`)

```
include/backend/algorithms/rejection/
├── outlier_rejector.h              Interface de base OutlierRejectorClass
└── vote_2d/
    └── vote_rejector.h             Déclaration de VoteRejectorClass
src/backend/algorithms/rejection/
└── vote_2d/
    └── vote_rejector.cpp           Implémentation de l'algorithme
```

---

## 1. Principe général

Soit un ensemble de $N$ paires de points homologues $(\mathbf{p}_{A,i}, \mathbf{p}_{B,i})$ détectées entre deux images $A$ et $B$, contenant à la fois des correspondances exactes (*inliers*) et de fausses correspondances (*outliers*).

L'algorithme **Vote 2D** estime la **translation 2D** $(t_x, t_y)$ entre $B$ et $A$ et identifie les *inliers* en deux phases déterministes en temps linéaire $\mathcal{O}(N)$ :

1. **Vote de Hough 2D sur grille** : chaque paire vote pour son déplacement $\Delta_i = \mathbf{p}_{A,i} - \mathbf{p}_{B,i}$ dans une grille 2D discrète (avec un voisinage $3 \times 3$ autour de sa cellule). La cellule cumulant le plus de votes fournit une estimation initiale $(t_x^{(0)}, t_y^{(0)})$ insensible aux *outliers* dispersés.
2. **Raffinement par médiane itérative** : 3 passes de médiane sur les déplacements situés à une distance euclidienne $\le \text{seuil}$ affinent la translation en continu au sous-pixel et déterminent le masque final des *inliers*.

---

## 2. Déroulement de l'algorithme

### Étape 1 — Filtrage des coordonnées finies et calcul des déplacements (`extractFiniteDeltas`)

L'algorithme vérifie d'abord que `matches.pointsInA` et `matches.pointsInB` ont la même taille $N$.
Pour chaque indice $i \in \{0, \dots, N-1\}$ :
- si l'une des coordonnées $(x_{A,i}, y_{A,i}, x_{B,i}, y_{B,i})$ est `NaN` ou infinie (`!std::isfinite`), la paire est ignorée ;
- sinon, le déplacement 2D est calculé en double précision et l'indice d'origine $i$ est conservé dans `validIndices` :

$$\Delta x_i = x_{A,i} - x_{B,i}, \quad \Delta y_i = y_{A,i} - y_{B,i}$$

Si le nombre de déplacements valides est strictement inférieur à `minMatches`, l'algorithme s'arrête et renvoie `false`.

---

### Étape 2 — Vote 2D avec voisinage $3 \times 3$ (`accumulateNeighborhoodVotes`)

L'espace des déplacements $(\Delta x, \Delta y)$ est découpé en cellules carrées de côté $c = \text{case}$ (en pixels). Pour chaque déplacement valide $(\Delta x_i, \Delta y_i)$, les indices entiers de sa cellule centrale sont :

$$c_{x,i} = \left\lfloor \frac{\Delta x_i}{c} \right\rfloor, \quad c_{y,i} = \left\lfloor \frac{\Delta y_i}{c} \right\rfloor$$

Afin d'éviter qu'un groupe de vrais déplacements situé sur la frontière entre deux cases ne voie ses votes partagés, chaque correspondance incrémente sa cellule centrale ainsi que ses **8 cellules voisines directes** (fenêtre $3 \times 3$, rayon `NEIGHBORHOOD_WINDOW_RADIUS = 1`) :

$$\forall (\delta_y, \delta_x) \in \{-1, 0, 1\}^2, \quad \text{votes}[(c_{y,i} + \delta_y, c_{x,i} + \delta_x)] \mathrel{+}= 1$$

Les votes sont stockés dans une table associative creuse `std::map<std::pair<int, int>, int>` indexée par `(cellY, cellX)`. Seules les cellules effectivement votées sont allouées en mémoire.

---

### Étape 3 — Extraction du mode (`computeVoteMode`)

La table `votes` est parcourue dans l'ordre lexicographique de ses clés `(cellY, cellX)` pour retenir la première cellule atteignant le maximum de votes :

$$(c_{y,\text{mode}}, c_{x,\text{mode}}) = \arg\max_{(c_y, c_x)} \text{votes}[(c_y, c_x)]$$

L'estimation initiale de la translation est le centre géométrique de cette cellule (`GRID_CELL_CENTER_OFFSET = 0.5`) :

$$t_x^{(0)} = (c_{x,\text{mode}} + 0{,}5) \cdot c, \quad t_y^{(0)} = (c_{y,\text{mode}} + 0{,}5) \cdot c$$

---

### Étape 4 — Raffinement par médiane itérative (`refineTranslation`, `computeMedian`)

Le centre de cellule $(t_x^{(0)}, t_y^{(0)})$ a une résolution limitée au pas de grille $c$. Pour obtenir une estimation sous-pixel continue, l'algorithme effectue **3 passes** (`MEDIAN_REFINEMENT_ITERATIONS = 3`).

À chaque itération $k \in \{0, 1, 2\}$ :

1. **Collecte des inliers courants (`collectInliersForCenter`)** — on sélectionne les déplacements valides situés dans le disque euclidien de rayon $R = \text{seuil}$ centré en $(t_x^{(k)}, t_y^{(k)})$ :
   $$\mathcal{I}^{(k)} = \left\{ i \in \text{validIndices} \;\middle|\; \sqrt{(\Delta x_i - t_x^{(k)})^2 + (\Delta y_i - t_y^{(k)})^2} \le R \right\}$$
2. Si $\mathcal{I}^{(k)}$ est vide, la boucle s'interrompt.
3. **Mise à jour par la médiane (`computeMedian`)** — les coordonnées sont mises à jour par la médiane sur chaque axe :
   $$t_x^{(k+1)} = \operatorname{médiane}_{i \in \mathcal{I}^{(k)}}(\Delta x_i), \quad t_y^{(k+1)} = \operatorname{médiane}_{i \in \mathcal{I}^{(k)}}(\Delta y_i)$$

La médiane d'un vecteur de taille $M$ est calculée en $\mathcal{O}(M)$ via `std::nth_element` :
- si $M$ est impair, on prend l'élément d'indice $\lfloor M/2 \rfloor$ ;
- si $M$ est pair, on prend la demi-somme de l'élément d'indice $M/2$ et du maximum de la moitié inférieure $[0, M/2[$.

---

### Étape 5 — Marquage final des inliers (`markInliers`)

Le masque binaire `out.inlierMask` (taille $N$, initialisé à `0`) est calculé autour de la translation raffinée $(t_x, t_y) = (t_x^{(3)}, t_y^{(3)})$ :

$$\text{inlierMask}[i] = \begin{cases} 1 & \text{si } i \in \text{validIndices} \text{ et } \sqrt{(\Delta x_i - t_x)^2 + (\Delta y_i - t_y)^2} \le R \\ 0 & \text{sinon} \end{cases}$$

Si le nombre d'inliers retenus est strictement inférieur à `minInliers`, `out.inlierMask` est intégralement remis à `0` et `estimate()` renvoie `false`.

---

### Étape 6 — Écriture du résultat (`estimate`, `filterMatches`)

Lorsque `inlierCount >= minInliers` :
- **`estimate(matches, out)`** remplit `out.transformBToA` avec la matrice homogène $3 \times 3$ (`CV_64F`) de translation pure :
  $$\mathbf{T}_{B \to A} = \begin{pmatrix} 1 & 0 & t_x \\ 0 & 1 & t_y \\ 0 & 0 & 1 \end{pmatrix}$$
- **`filterMatches(matches, out)`** (ainsi que **`filterPairMatches(pairMatches, out)`**) appelle `estimate(matches, out)`, filtre sur place `matches.pointsInA`, `matches.pointsInB` et `matches.confidenceScores` pour ne conserver que les éléments où `inlierMask[i] != 0` (`retainInlierPoints`), et copie $(t_x, t_y)$ dans `(matches.translationX, matches.translationY)`.

---

## 3. Paramètres

Les paramètres sont passés sous forme d'objet JSON (`nlohmann::json`) au constructeur `VoteRejectorClass(const Json& parameters)`. Toute clé absente prend sa valeur par défaut :

| Clé JSON | Type | Défaut | Plancher de sécurité | Description |
|---|---|---|---|---|
| `"seuil"` (ou `"ransacReprojThreshold"`) | `double` | `1.5` | `1e-6` | Rayon maximal $R$ (en px) autour de $(t_x, t_y)$ pour accepter un déplacement comme *inlier*. |
| `"case"` | `double` | `1.0` | `1e-6` | Côté $c$ (en px) d'une cellule de la grille de vote 2D. |
| `"minMatches"` | `int` | `3` | `2` | Nombre minimal de paires de points à coordonnées finies requis avant le vote. |
| `"minInliers"` | `int` | `3` | `2` | Nombre minimal d'*inliers* requis après raffinement pour valider la translation. |

---

## 4. Interface C++

```cpp
namespace RejectionNs {

class VoteRejectorClass : public OutlierRejectorClass {
public:
    VoteRejectorClass() = default;
    explicit VoteRejectorClass(const Json& parameters);

    // Calcule out.transformBToA (3x3 CV_64F) et out.inlierMask sans modifier matches.
    bool estimate(const DataNs::Matches& matches, DataNs::Alignment& out) override;

    // Appelle estimate(), conserve uniquement les inliers dans matches et met à jour (translationX, translationY).
    bool filterMatches(DataNs::Matches& matches, DataNs::Alignment& out);

    // Appelle filterMatches() sur pairMatches.featureMatches.
    bool filterPairMatches(DataNs::PairMatches& pairMatches, DataNs::Alignment& out);

    // Formate la translation de alignment.transformBToA en objet JSON.
    static Json toEqualizationJson(int tileIdA, int tileIdB, const DataNs::Alignment& alignment);
};

using VoteRejector = VoteRejectorClass;

} // namespace RejectionNs
```


---

## 5. Emplacements du code implémenté (Algorithme et Step)

### 5.1 Algorithme Vote 2D (`algorithms/rejection/vote_2d/`)

| Fichier | Rôle général |
|---|---|
| [`include/backend/algorithms/rejection/outlier_rejector.h`](../../../../include/backend/algorithms/rejection/outlier_rejector.h) | Définit l'interface abstraite `RejectionNs::OutlierRejectorClass` (`estimate(matches, out)`) commune aux méthodes de rejet des aberrants. |
| [`include/backend/algorithms/rejection/vote_2d/vote_rejector.h`](../../../../include/backend/algorithms/rejection/vote_2d/vote_rejector.h) | Déclare la classe `VoteRejectorClass` (dans `stitching::rejection` et exposée sous `RejectionNs::VoteRejectorClass`), ses constructeurs et ses méthodes publiques (`estimate`, `filterMatches`, `filterPairMatches`, `toEqualizationJson`). |
| [`src/backend/algorithms/rejection/vote_2d/vote_rejector.cpp`](../../../../src/backend/algorithms/rejection/vote_2d/vote_rejector.cpp) | Contient toute l'implémentation du calcul de Vote 2D et du filtrage des correspondances. |

#### Détail des fonctions dans `src/backend/algorithms/rejection/vote_2d/vote_rejector.cpp` :
- **`extractFiniteDeltas(matches)`** *(lignes 51–68)* : Parcourt les paires de points `(pointsInA[i], pointsInB[i])`, écarte toute coordonnée `NaN` ou `Inf`, calcule les déplacements `deltaX = xA - xB` et `deltaY = yA - yB`, et conserve les indices valides dans `validIndices`.
- **`accumulateNeighborhoodVotes(centerCell, votes)`** *(lignes 71–78)* : Ajoute un vote dans les 9 cellules du voisinage $3 \times 3$ autour de la cellule `(cellY, cellX)` dans la `std::map` des votes.
- **`computeVoteMode(deltas, cellSizePx)`** *(lignes 81–100)* : Discrétise chaque déplacement valide au pas `cellSizePx`, appelle `accumulateNeighborhoodVotes`, recherche la cellule cumulant le maximum de votes et retourne les coordonnées $(t_x^{(0)}, t_y^{(0)})$ de son centre.
- **`computeMedian(values)`** *(lignes 103–115)* : Calcule en $\mathcal{O}(M)$ la médiane d'un tableau de `double` à l'aide de `std::nth_element` (avec moyenne des deux valeurs centrales lorsque la taille est paire).
- **`collectInliersForCenter(deltas, center, thresholdPx)`** *(lignes 118–129)* : Sélectionne tous les déplacements `(deltaX[i], deltaY[i])` situés à une distance euclidienne (`std::hypot`) inférieure ou égale à `thresholdPx` du centre courant.
- **`refineTranslation(deltas, initialMode, thresholdPx)`** *(lignes 132–146)* : Enchaîne 3 passes d'affinement en alternant `collectInliersForCenter` et `computeMedian` sur $X$ et sur $Y$.
- **`markInliers(deltas, translation, thresholdPx, inlierMask)`** *(lignes 149–161)* : Met à `1` les entrées de `inlierMask` correspondant aux points situés à distance $\le \text{thresholdPx}$ de la translation finale et renvoie le nombre total d'*inliers*.
- **`retainInlierPoints(matches, inlierMask)`** *(lignes 164–189)* : Supprime sur place dans `matches.pointsInA`, `matches.pointsInB` et `matches.confidenceScores` tous les points où `inlierMask[i] == 0`.
- **`VoteRejectorClass::estimate(matches, out)`** *(lignes 196–223)* : Point d'entrée principal de l'estimation ; lit les paramètres JSON (`minMatches`, `minInliers`, `seuil`, `case`), exécute les étapes 1 à 5, vérifie `inlierCount >= minInliers` et construit la matrice homogène $3 \times 3$ dans `out.transformBToA`.
- **`VoteRejectorClass::filterMatches(matches, out)`** et **`filterPairMatches(pairMatches, out)`** *(lignes 225–237)* : Exécutent `estimate()`, filtrent les vecteurs de points via `retainInlierPoints` et mettent à jour `(translationX, translationY)` avec la translation estimée.

---

### 5.2 Step du pipeline (`steps/vote_2d_step.*`)

| Fichier | Rôle général |
|---|---|
| [`include/backend/steps/vote_2d_step.h`](../../../../include/backend/steps/vote_2d_step.h) | Déclare la structure de paramètres `StepsNs::Vote2DInput` (`seuil = 1.5f`, `cellSize = 1.0f`, `minMatches = 3`, `minInliers = 3`), la structure de sortie `StepsNs::Vote2DOutput` (clé `"matches"`, type `"PairMatches"`) et la classe `StepsNs::Vote2DStepClass`. |
| [`src/backend/steps/vote_2d_step.cpp`](../../../../src/backend/steps/vote_2d_step.cpp) | Implémente l'adaptateur de step `"vote-2d"` qui lit `"matches"` dans le `DataStoreClass`, applique `VoteRejectorClass` à chaque paire de tuiles et réécrit les paires filtrées dans `"matches"`. |
| [`src/backend/core/pipeline/pipeline_builder.cpp`](../../../../src/backend/core/pipeline/pipeline_builder.cpp) | Enregistre la clé `"vote-2d"` dans `stepRegistry()` *(ligne 58)* pour permettre son instanciation depuis un script JSON de pipeline. |
| [`tests/steps/test_vote_2d_step.cpp`](../../../../tests/steps/test_vote_2d_step.cpp) | Suite de 7 tests unitaires GoogleTest vérifiant le filtrage des *outliers*, la mise à jour de la translation, le rejet des paramètres invalides ou des `NaN`/`Inf`, et la sauvegarde/reprise par checkpoint. |

#### Détail des fonctions dans `src/backend/steps/vote_2d_step.cpp` :
- **`Vote2DStepClass::Vote2DStepClass(input)`** *(lignes 52–62)* : Initialise la configuration `StepConfig` du step avec le nom `"vote-2d"`, les 4 paramètres (`"seuil"`, `"case"`, `"minMatches"`, `"minInliers"`), l'entrée `"matches"` (`PairMatches`) et la sortie `"matches"` (`PairMatches`).
- **`Vote2DStepClass::readInput()`** *(lignes 64–81)* : Extrait les paramètres du script JSON et vérifie leur validité (`seuil > 0` fini, `case > 0` fini, `minMatches >= 2`, `minInliers >= 2`), levant `std::invalid_argument` en cas de valeur hors bornes.
- **`filterPairs(pairs, rejector, keptPairs, failures)`** *(lignes 28–47)* : Parcourt toutes les paires de tuiles de `"matches"`, vérifie la cohérence de taille de `confidenceScores` et appelle `rejector.filterPairMatches(filteredPair, alignment)`. Les paires validées sont ajoutées à `keptPairs` ; les paires rejetées sont journalisées dans le tableau JSON `failures` avec leur motif.
- **`Vote2DStepClass::execute()`** *(lignes 83–108)* : Récupère `"matches"` depuis le `DataStoreClass`, instancie `RejectionNs::VoteRejectorClass` avec les paramètres lus, appelle `filterPairs`, renseigne le compte rendu `result` (`pairs`, `kept`, `removed`, `failures`, `error`), et remplace `"matches"` dans le `DataStoreClass` uniquement si au moins une paire a été validée (`!output.value.empty()`).
