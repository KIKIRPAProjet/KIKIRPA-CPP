# Pourquoi RANSAC a été envisagé puis remplacé par Vote 2D

Ce document retrace l'architecture initialement envisagée autour de **RANSAC**, les limites mises en évidence par l'analyse théorique et les bancs d'essai du projet, et les raisons factuelles de son remplacement par **Vote 2D** (`VoteRejectorClass`, PR #63).

---

## 1. Ce qui avait été envisagé et implémenté avec RANSAC

Au début du développement, le rejet des correspondances aberrantes (*outliers*) reposait sur le module `algorithms/rejection/ransac/` (`RansacAlgorithm`, `RansacPreprocessor`, `RansacRejector`, `RansacTypes`, puis leurs équivalents `ransac_*.{h,cpp}`), supprimé ensuite lors de l'isolation de Vote 2D (PR #63).

Son cœur algorithmique appelait `cv::estimateAffinePartial2D` avec la méthode `cv::RANSAC` :

```cpp
cv::Mat affine = cv::estimateAffinePartial2D(
    input.sourcePoints,
    input.destinationPoints,
    masqueInliers,
    cv::RANSAC,
    params.reprojectionThreshold,
    params.maxIters,
    params.confidence,
    params.refineIters
);
```

Cet estimateur ajuste une **similitude affine partielle à 4 degrés de liberté (4-DDL)** : une translation $(t_x, t_y)$, une rotation $\theta$ et un facteur d'échelle isotrope $s$ :

$$\begin{pmatrix} x_A \\ y_A \end{pmatrix} = \begin{pmatrix} s\cos\theta & -s\sin\theta \\ s\sin\theta & s\cos\theta \end{pmatrix} \begin{pmatrix} x_B \\ y_B \end{pmatrix} + \begin{pmatrix} t_x \\ t_y \end{pmatrix}$$

---

## 2. Pourquoi RANSAC a été remplacé par Vote 2D

### 2.1. Modèle 4-DDL inadapté au banc rigide et effet de bras de levier

Sur le portique motorisé cartésien X-Y du KIK-IRPA, la caméra se déplace selon deux axes orthogonaux sans rotation ni changement d'échelle ($\theta = 0$, $s = 1$) : la transformation physique entre deux tuiles voisines est une **pure translation 2D (2-DDL)**.

Or, les correspondances détectées par SIFT ou LightGlue sont confinées dans une **bande de recouvrement étroite** ($\approx 15\text{ à }25\,\%$ de la tuile) et comportent un bruit naturel de localisation sous-pixel ($\approx 0{,}1\text{ à }0{,}3\text{ px}$ pour les extrema DoG de SIFT). Avec 4 degrés de liberté, `cv::estimateAffinePartial2D` **sur-apprend (*overfit*) ce bruit sous-pixel** en produisant une micro-rotation parasite $\theta \ne 0$ :

- **Notebooks de recherche (`02_sift.ipynb` et `03_lightglue.ipynb`, section 7 « Faut-il RANSAC ? »)** : la rotation fantôme estimée ($\theta \approx 0{,}05^\circ\text{ à }0{,}15^\circ$) dégrade le décalage estimé, faisant monter l'erreur moyenne de **$0{,}183\text{ px}$** (en pure translation 2D) à **$0{,}709\text{ px}$** (en similitude 4-DDL).
- **Tuiles réelles haute résolution 100 Mpx (`test_x128491l`)** : sur 4 tuiles réelles Hasselblad H6D-100c du KIK-IRPA (`x128488l.tif` à `x128491l.tif`, $11\,600 \times 8\,700\text{ px}$, RGB 16 bits, 3 paires verticales), par **effet de bras de levier** sur la largeur $W = 11\,600\text{ px}$ ($\Delta_{\text{bord}} \approx W \cdot \sin\theta$), les micro-rotations parasites mesurées ($0{,}03^\circ\text{ à }0{,}08^\circ$) provoquent une **dérive de $7\text{ à }15{,}8\text{ px}$** aux extrémités des tuiles :
  - Paire 1, SIFT à $1{,}5\text{ px}$ : $\theta = 0{,}0413^\circ \implies \mathbf{8{,}36\text{ px}}$ de dérive ;
  - Paire 2, SIFT à $3{,}0\text{ px}$ : $\theta = 0{,}0782^\circ \implies \mathbf{15{,}83\text{ px}}$ de dérive ;
  - Paire 3, SIFT à $1{,}5\text{ px}$ : $\theta = -0{,}0503^\circ \implies \mathbf{10{,}19\text{ px}}$ de dérive ;
  - Paire 3, SIFT à $0{,}1\text{ px}$ : $\theta = -0{,}0781^\circ \implies \mathbf{15{,}82\text{ px}}$ de dérive ;
  - Paire 3, LightGlue à $1{,}0\text{ px}$ : $\theta = -0{,}0654^\circ \implies \mathbf{13{,}24\text{ px}}$ de dérive.

---

### 2.2. Preuve par RANSAC 100 % exhaustif que le défaut vient du modèle 4-DDL et non du tirage aléatoire

Pour vérifier si ces rotations parasites venaient d'un échantillonnage aléatoire insuffisant de RANSAC, le banc `test_x128491l_exhaustif` a remplacé `cv::estimateAffinePartial2D` par une énumération **100 % exhaustive** de toutes les paires de points possibles $\binom{N}{2} = \frac{N(N-1)}{2}$, testées chacune sur l'ensemble des $N$ points (jusqu'à $\binom{2257}{2} = 2\,545\,896$ paires et $5{,}75\text{ milliards}$ de projections par seuil sur la Paire 3), suivie du même raffinement par moindres carrés :

| Paire & Extracteur | Seuil | Inliers (Aléatoire $\to$ Exhaustif) | Rotation parasite (Aléatoire $\to$ Exhaustif) | Dérive bord $11\,600\text{ px}$ | Temps (Aléatoire $\to$ Exhaustif) |
|---|---|---:|---:|---:|---:|
| **Paire 1 — SIFT** ($N = 1051$) | `1.5 px` | $374 \to \mathbf{398}$ | $0{,}0413^\circ \to \mathbf{0{,}0527^\circ}$ | $8{,}36\text{ px} \to \mathbf{10{,}67\text{ px}}$ | $0{,}46\text{ ms} \to 1\,249{,}72\text{ ms}$ |
| **Paire 2 — LightGlue** ($N = 1659$) | `1.5 px` | $648 \to \mathbf{682}$ | $0{,}0031^\circ \to \mathbf{0{,}0090^\circ}$ | $0{,}63\text{ px} \to \mathbf{1{,}83\text{ px}}$ | $0{,}53\text{ ms} \to 5\,372{,}48\text{ ms}$ |
| **Paire 3 — SIFT** ($N = 2257$) | `1.5 px` | $921 \to \mathbf{957}$ | $-0{,}0503^\circ \to \mathbf{-0{,}0468^\circ}$ | $10{,}19\text{ px} \to \mathbf{9{,}48\text{ px}}$ | $1{,}33\text{ ms} \to 13\,229{,}89\text{ ms}$ |
| **Paire 3 — SIFT** ($N = 2257$) | `0.1 px` | $29 \to \mathbf{30}$ | $-0{,}0781^\circ \to \mathbf{-0{,}0784^\circ}$ | $15{,}82\text{ px} \to \mathbf{15{,}88\text{ px}}$ | $261{,}86\text{ ms} \to 7\,973{,}93\text{ ms}$ |

Même en trouvant le maximum mathématique absolu d'*inliers*, les micro-rotations parasites et la dérive aux bords persistent au même niveau (et augmentent parfois, comme sur la Paire 1 où la dérive passe de $8{,}36\text{ px}$ à $10{,}67\text{ px}$), tandis que le coût explose jusqu'à **$\times 9\,947$** ($13{,}2\text{ s}$ par paire). Ce résultat confirme que la dérive provient intrinsèquement des **4 degrés de liberté** du modèle, et non du caractère aléatoire de RANSAC.

---

### 2.3. Inefficacité et instabilité du resserrement de seuil (`3.0 px` $\to$ `0.1 px`)

Des campagnes multi-seuils (`3.0`, `1.5`, `1.0`, `0.5`, `0.2`, `0.1 px`) avec `maxIters = 100 000`, `confidence = 0.999999` et `refineIters = 50` ont montré qu'abaisser le seuil de reprojection ne corrige pas ces limites :

- **Sur la grille synthétique 16 bits `NOM_0001` (10 paires, grille $3 \times 3$)** :
  - Descendre de `1.5 px` à `0.1 px` fait chuter le ratio d'*inliers* de **$99{,}5\,\%$** ($274{,}9$ points) à **$26{,}2\,\%$** ($72{,}8$ points), éliminant jusqu'à **$73{,}8\,\%$** de correspondances SIFT légitimes en raison de l'incertitude intrinsèque de localisation des extrema DoG ($0{,}1\text{ à }0{,}3\text{ px}$).
  - La précision par rapport à la vérité terrain ne s'améliore pas : l'erreur moyenne sur les 10 paires reste à **$1{,}044\text{ px}$** (à `3.0 px` et `1.5 px`) contre **$1{,}042\text{ px}$** (à `0.1 px`), et l'écart global sur la grille $3 \times 3$ passe de **$0{,}897\text{ px}$** (`1.5 px`) à **$0{,}905\text{ px}$** (`0.1 px`).
- **Sur les tuiles réelles 100 Mpx (`test_x128491l`)** :
  - À `0.1 px`, le taux d'*inliers* $w$ s'effondre à **$1{,}3\text{–}1{,}6\,\%$** (ex. $29/2257$ en SIFT et $13/1020$ en LightGlue sur la Paire 3).
  - Le nombre théorique d'itérations $k \ge \frac{\ln(1-p)}{\ln(1-w^2)}$ explose : le temps d'exécution RANSAC passe de $0{,}46\text{–}1{,}33\text{ ms}$ (à `1.5 px`) à **$101{,}95\text{–}261{,}86\text{ ms}$** (à `0.1 px`), tout en conservant jusqu'à $15{,}82\text{ px}$ de dérive aux bords.

---

### 2.4. Non-déterminisme stochastique et comparaison avec les autres méthodes

1. **Non-déterminisme** : `cv::estimateAffinePartial2D` repose sur un tirage aléatoire interne (dépendant de l'état du générateur RNG d'OpenCV), ce qui peut produire des ensembles d'*inliers* et des translations légèrement différents d'une exécution à l'autre si la graine n'est pas figée, à l'encontre de l'exigence de reproductibilité du Cahier des Charges (§ 4.4).
2. **Coût de `MAGSAC++`** : l'étude comparative des 9 méthodes de rejet dans les notebooks de recherche (`aucun`, `mediane`, `mediane_2passes`, `ransac`, `ransac_opencv`, `consensus`, `msac`, `magsac++`, `vote`) a également écarté `MAGSAC++`, jugé inutilement lourd ($70\text{ à }90\text{ ms}$ par paire, jusqu'à $300\text{ ms}$) sans gain de précision ($0{,}24\text{ px}$).

---

## 3. Bilan comparatif ayant motivé le choix de Vote 2D

| Critère | RANSAC 4-DDL (`cv::estimateAffinePartial2D`) | Vote 2D (`VoteRejectorClass`) |
|---|---|---|
| **Modèle géométrique** | Similitude affine 4-DDL ($t_x, t_y, s, \theta$) | Pure translation 2-DDL ($t_x, t_y$), $\theta = 0$ |
| **Dérive par bras de levier (tuiles $11\,600\text{ px}$)** | $7\text{ à }15{,}8\text{ px}$ (micro-rotations de $0{,}03^\circ\text{ à }0{,}08^\circ$) | **$0{,}00\text{ px}$** (rotation strictement nulle) |
| **Déterminisme** | Stochastique (tirage aléatoire de paires) | **100 % déterministe** (grille + ordre lexicographique) |
| **Complexité** | Itérative, sensible à la chute du ratio $w$ ($k \propto 1/w^2$) | **Linéaire $\mathcal{O}(N)$** (vote sur grille + 3 médianes) |
| **Robustesse aux *outliers*** | Dégradation forte du temps si $w$ est faible | Robuste jusqu'à **80–90 % d'*outliers*** dispersés |
| **Temps mesuré** | $0{,}2\text{ à }262\text{ ms}$ (jusqu'à $19\text{ s}$ en exhaustif) | **$\approx 0{,}1\text{ à }13\text{ ms}$** ($\approx 1\text{ ms}$ en moyenne) |
