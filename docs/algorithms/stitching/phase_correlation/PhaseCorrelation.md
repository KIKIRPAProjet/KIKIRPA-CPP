# PhaseCorrelationMatcherClass — corrélation de phase entre deux tuiles

Port C++ de `KIKIRPA-Research/FeaturesExtraction/Correlation-de-phase/correlation_phase.py`, organisé en trois modules (voir §3) :

```
include/backend/algorithms/stitching/phase_correlation/      namespace PhaseCorrelationNs
├── phase_correlation_matcher.h   header façade : le seul que le pipeline inclut
├── phase_correlation_types.h     toutes les structures échangées entre les modules
├── preparation.h                 1. préparation des images
├── algorithm.h                   2. algorithme pur
└── output.h                      3. préparation de la sortie
src/backend/algorithms/stitching/phase_correlation/
├── phase_correlation_matcher.cpp constructeur + match() : enchaîne les 3 modules
├── preparation.cpp
├── algorithm.cpp
└── output.cpp
```

Seule dépendance : **OpenCV** (déjà utilisée par le projet). Pas besoin d'Eigen, de FFTW ni de libtiff : `cv::dft` remplace `np.fft`, et la lecture des TIFF est faite en amont par `ImportStepClass`.

---

## 1. À quoi ça sert

Deux tuiles voisines A et B se recouvrent sur une bande (environ 20 % de leur taille). L'algorithme mesure **où se trouve B par rapport à A**, avec une précision inférieure au pixel, en deux passes : une première au pixel près sur les bandes de recouvrement nominal, une deuxième au sous-pixel sur la zone commune réelle (voir l'étape 1). Il renvoie :

1. **une translation globale** `(translationX, translationY)` : la position du coin haut-gauche de B dans le repère de A ;
2. **des paires de points** `pointsInA[i] ↔ pointsInB[i]` avec un **score de confiance** `confidenceScores[i]`. C'est le même format que les autres méthodes (SIFT, LightGlue…), ce qui permet de les passer à RANSAC / MAGSAC++.

```
          A                              repère de A
  ┌───────────────────┐                  (0,0) ──► x
  │                   │                    │
  │                   │                    ▼ y
  │          ┌────────┼──────────┐
  │          │ zone   │          │
  └──────────┼commune─┘          │   ◄── B posée en (dx, dy)
             │                   │
             │         B         │
             └───────────────────┘
```

---

## 2. Le header global : l'interface pour le pipeline

```cpp
class PhaseCorrelationMatcherClass : public StitchingNs::FeatureMatcherClass {
    public:
        explicit PhaseCorrelationMatcherClass(const Parameters& initialParameters = {});
        bool match(const cv::Mat& imageA, const cv::Mat& imageB, const DataNs::PairContext& context,
                   DataNs::Matches& matches) override;
        const std::string& getFailureReason() const;
    private:
        Parameters parameters;   // vérifiés une seule fois, dans le constructeur
        OverlapEstimationNs::OverlapEstimatorClass overlapEstimator;
        std::string failureReason;
};
```

- La classe hérite de `StitchingNs::FeatureMatcherClass`, comme `SiftNs::SiftMatcherClass` et `LightGlueNs::LightGlueMatcherClass`. Un step peut donc l'utiliser sans savoir de quelle méthode il s'agit.
- Elle est appelée par le step **`"phase-correlation"`** (`StepsNs::PhaseCorrelationStepClass`, voir [sa page](../../../steps/PhaseCorrelationStep.md)), qui remplit `Parameters` à partir des paramètres du script JSON.
- `initialParameters` : les réglages (voir §5), sous forme de structure typée **déjà remplie par l'appelant**. Il n'y a pas d'entrée JSON. Sans argument, toutes les valeurs par défaut s'appliquent. **Un paramètre invalide lève `std::invalid_argument` à la construction**.

### Entrées de `match()`

| Argument | Type | Rôle |
|---|---|---|
| `imageA`, `imageB` | `cv::Mat` | Les deux tuiles déjà chargées en mémoire. Elles ne sont pas copiées : on n'en lit que des sous-zones (ROI). Gris ou couleur, 8 ou 16 bits. Elles doivent avoir la même taille. **Aucune conversion en 8 bits** : les bandes sont lues directement en float64, avec leur dynamique complète. |
| `context.direction` | `DataNs::Direction` | Position de **B par rapport à A** : `Top`, `Bottom`, `Left` ou `Right`. |
| `context.overlapFraction` | `float` | Recouvrement nominal, en fraction de la tuile (ex. `0.2`). Il doit être compris entre 0 (exclu) et 1. |
| `context.estimatedTranslation` | `std::optional<cv::Point2d>` | Coin de B dans A au pixel près, déjà mesuré par la première passe du pipeline (`OverlapEstimatorClass`). Absent : `match` fait la première passe lui-même. Présent : elle est sautée, et la deuxième passe part de cette position. |

### Sortie : `DataNs::Matches` (dans `core/data/algorithm_types.h`)

```cpp
struct Matches {
    std::vector<cv::Point2f> pointsInA, pointsInB;   // coordonnées dans la tuile A / la tuile B entière
    std::vector<float> confidenceScores;             // confiance de chaque paire, entre 0 et 1
    float translationX = 0.f, translationY = 0.f;    // coin haut-gauche de B dans le repère de A (px)
};
```

C'est le struct unifié avec SIFT et LightGlue (`pointsInA`, `pointsInB`, `confidenceScores`, `translationX`, `translationY`).

**Convention** : pour toute paire, `pointsInA[i] − pointsInB[i] ≈ (translationX, translationY)`. Un pixel `(X, Y)` de A correspond au pixel `(X − translationX, Y − translationY)` de B.

### Valeur de retour

- `true` : la translation `(translationX, translationY)` est valide. La liste de points peut être vide si la zone commune est trop étroite pour y placer des patchs.
- `false` : la mesure est impossible ou peu fiable. C'est le cas si les tuiles sont vides ou de tailles différentes, si les bandes n'ont pas de contenu commun (zone noire ou fond uni), si la translation réelle est hors de la marge du rail, si le pic de corrélation est ambigu, ou si `context.estimatedTranslation` est fausse de plus de 4 px (deuxième passe). `match` ne lève **aucune exception**.
- **`getFailureReason()`** : après un `false`, message qui explique pourquoi, avec les chiffres et la correction à apporter quand elle est connue. Exemple sur Philadelphia 35 → 36 (recouvrement réel 26 %, annoncé 20 %) : *« translation outside the rail margin: the best peak is 520 px from the nominal overlap along the motion and 82 px across, beyond the rail margin (±435 px) (ratio 14.46). Increase the rail margin (Parameters::railMargin, 'marge' in the step, >= 530 px) or give the actual overlap (about 26 % instead of 20 %). »* (messages en anglais, comme ceux des steps). Vide après un `true`.

---

## 3. Organisation du code et déroulement

### Les trois modules

Chaque module est une classe qui ne contient que des fonctions `static` :
- son header ne liste que les fonctions **publiques** : ce que l'orchestrateur ou un autre module appelle ;
- les fonctions internes au module, et ses constantes nommées (seuils, rayon du pic…), sont dans un `namespace { }` anonyme de son `.cpp` (voir `docs/CONVENTIONS.md` §4) : elles ne sont pas visibles depuis un autre module.

Toutes les structures sont regroupées dans `phase_correlation/phase_correlation_types.h`. Tout le module est dans le namespace `PhaseCorrelationNs`.

| Module | Rôle | Public | Interne au `.cpp` |
|---|---|---|---|
| **`PreparationClass`** | Des tuiles brutes aux images de travail (gris float64, NaN = vide) | `validateParameters`, `orientPair`, `orientTranslation`, `extractNominalStrips`, `extractCommonArea`, `extractOverlap`, `computeValidMask`, `fillEmptyPixels` | `oppositeDirection`, `prepareStrips`, `stripRectangle`, `computeRailMargin`, `computeOutsideMask`, `convertToGrey`, `maximumValueOfDepth`, `findContentRectangle` |
| **`AlgorithmClass`** | Uniquement du calcul sur des images préparées | `measureGlobalTranslation`, `refineGlobalTranslation`, `diagnoseGlobalTranslation`, `measureLocalPoints` | corrélation : `correlatePhase`, `computeCrossSpectrum`, `computeSmoothedSurface`, `computeSpectrum`, `normalizeCrossSpectrum`, `buildGaussianFilter`, `inverseRealPart`, `buildAllowedMask`, `locatePeak`, `refinePeak`, `buildDftKernel`, `signedShift`, `frequencyOf`, `computePeakRatio` — local : `findCandidates`, `measurePoint`, `computeNormalizedCrossCorrelation` |
| **`OutputClass`** | Mise en forme au format commun `Matches`, ou message d'échec | `buildMatches`, `describePairFailure`, `describeStripsFailure`, `describeGlobalFailure`, `describeCommonAreaFailure`, `describeRefinementFailure` | `formatNumber`, `sortAndLimit`, `toTileCoordinates`, `undoOrientation` |

`AlgorithmClass` utilise deux outils publics de `PreparationClass` (`computeValidMask`, `fillEmptyPixels`) pour traiter les pixels vides de ses patchs.

La première passe est dans une classe à part, **`OverlapEstimationNs::OverlapEstimatorClass`** (`algorithms/stitching/overlap_estimation/`, voir [sa page](../overlap_estimation/OverlapEstimation.md)), parce qu'elle sert à toutes les méthodes d'appariement. Elle enchaîne `PreparationClass::orientPair`, `PreparationClass::extractNominalStrips` et `AlgorithmClass::measureGlobalTranslation` au pixel près. `PhaseCorrelationMatcherClass` en garde une instance (`overlapEstimator`), utilisée quand `context.estimatedTranslation` est absente. En retour, `PreparationClass` utilise `OverlapEstimatorClass::computeCommonAreas` pour découper la zone commune réelle.

### Les structures (`phase_correlation_types.h`)

| Structure | Produite par | Contenu |
|---|---|---|
| `Parameters` | l'appelant (vérifiés par `PreparationClass::validateParameters`) | Réglages (§5) |
| `OrientedPair` | `PreparationClass::orientPair` | A et B ramenées au cas « B en bas / à droite », masques extérieurs, indicateur `isSwapped` |
| `Strips` | `PreparationClass::extractNominalStrips`, `PreparationClass::extractCommonArea` | Bandes de recouvrement nominal (première passe) ou zone commune réelle (deuxième passe), en gris + position de B si le résidu est nul + marge de recherche (marge du rail, ou ±4 px) |
| `Correlation` | `AlgorithmClass` (interne) | Décalage `(shiftY, shiftX)`, hauteur du pic, surface lissée, zone où le pic est cherché |
| `Diagnostic` | `AlgorithmClass::diagnoseGlobalTranslation` | Après un refus de `measureGlobalTranslation` : rapport dans la marge, meilleur pic sur toute la surface, son rapport, s'il est hors marge |
| `Measurement` | `AlgorithmClass::measureGlobalTranslation`, `AlgorithmClass::refineGlobalTranslation` | Translation globale `(translationX, translationY)` + rapport des pics (première passe ; NaN après la deuxième) |
| `Overlap` | `PreparationClass::extractOverlap` | Zone commune réelle vue dans A et dans B, son origine dans A, la translation arrondie, la marge |
| `Correspondence` | `AlgorithmClass::measureLocalPoints` | Point dans la zone commune + translation locale + confiance |

### Enchaînement dans `match()` (`phase_correlation_matcher.cpp`)

```
match(imageA, imageB, context)
 │
 ├─ 1. Première passe, si context.estimatedTranslation est absente : OverlapEstimatorClass::estimate
 │   ├─ PreparationClass::orientPair ── vérifie la paire ; si B est en haut / à gauche, échange A et B
 │   │      └─ computeOutsideMask ×2   pixels noirs reliés au bord de la tuile → « vides »
 │   ├─ PreparationClass::extractNominalStrips ── bande du bas/de droite de A, du haut/de gauche de B
 │   │      │                          (recouvrement nominal)
 │   │      ├─ stripRectangle, convertToGrey   découpe, gris float64, extérieur et saturés → NaN, érosion 4 px
 │   │      ├─ fraction valide         ≥ 25 % de pixels exploitables communs    (sinon → false)
 │   │      ├─ findContentRectangle    retire les bords vides communs           (sinon → false)
 │   │      └─ computeRailMargin       décalage résiduel maximal permis par le rail
 │   ├─ AlgorithmClass::measureGlobalTranslation   position de B au pixel près
 │   │      ├─ correlatePhase          FFT → spectre croisé → pic dans la marge (pas de sous-pixel)
 │   │      └─ computePeakRatio        le pic est-il franc (dans la marge) ?    (sinon → false)
 │   └─ en cas d'échec seulement : OutputClass::describe…Failure → getFailureReason()
 │          └─ diagnoseGlobalTranslation   meilleur pic sans marge : franc et hors marge → « increase the rail margin »
 │
 ├─ 2. Deuxième passe
 │   ├─ PreparationClass::orientPair
 │   ├─ PreparationClass::extractCommonArea   zone commune réelle à la position de la passe 1  (sinon → false)
 │   │      └─ OverlapEstimatorClass::computeCommonAreas, convertToGrey, fraction valide, findContentRectangle
 │   └─ AlgorithmClass::refineGlobalTranslation   translation au 1/100 px, pic cherché à ±4 px   (pic au bord → false)
 │          └─ correlatePhase          FFT → spectre croisé → pic dans ±4 px → sous-pixel DFT
 │
 ├─ PreparationClass::extractOverlap ── zone commune réelle avec (x, y) arrondi   (trop étroite → pas de points)
 │
 ├─ AlgorithmClass::measureLocalPoints
 │      ├─ findCandidates          coins texturés (goodFeaturesToTrack), espacés d'au moins 16 px
 │      └─ measurePoint ×N         patch 64×64 : correlatePhase locale, filtres, computeNormalizedCrossCorrelation
 │
 └─ OutputClass::buildMatches
        ├─ sortAndLimit            confiance décroissante, maximumPointCount meilleurs
        ├─ toTileCoordinates       coordonnées dans les tuiles entières
        └─ undoOrientation         si A et B avaient été échangées : −(x, y), pointsInA ↔ pointsInB
```

### Étape 0 : la zone extérieure (`computeOutsideMask`)

Il faut savoir quels pixels noirs sont « hors de l'œuvre » (sans valeur) et lesquels sont du vrai noir dans la peinture (ombre bouchée à 0). La règle : **un pixel à 0 n'est vide que s'il est relié au bord de la tuile par d'autres pixels à 0**.

1. `cv::inRange` marque les pixels dont tous les canaux valent 0.
2. Depuis chaque pixel à 0 du bord de la tuile, on lance un remplissage par diffusion (`cv::floodFill`, connexité 4). Tous les pixels noirs reliés au bord sont marqués.
3. Seuls ces pixels deviennent NaN dans `convertToGrey`. Une tache noire isolée au milieu de la peinture reste une donnée normale.

```
  bord de la tuile
  ▼
  ████████░░░░░░░░░░       ████ = 0 relié au bord  → vide (NaN)
  ██████░░░░░░▓▓░░░░       ▓▓   = 0 isolé (ombre)  → donnée conservée
  ████░░░░░░░░▓▓░░░░       ░░   = peinture
  ██░░░░░░░░░░░░░░░░
```

Le calcul se fait sur **la tuile entière** et non sur la seule bande. Une zone extérieure peut en effet entrer dans la bande par l'intérieur de la tuile, sans toucher le bord de la bande. On n'en extrait ensuite que les sous-zones utiles. Le coût est faible : un masque de 1 octet par pixel et un remplissage proportionnel à la surface noire.

Dans `convertToGrey`, deux autres sortes de pixels sont retirées (NaN) :

- **les pixels saturés** : gris ≥ 99,8 % du maximum du type (65 535 en 16 bits, 255 en 8 bits, 1 pour une tuile flottante). Un reflet écrêté n'a plus de texture, et la tache claire n'est pas au même endroit dans les deux tuiles : elle attirerait le pic vers un faux décalage ;
- **une bordure de 4 px** autour de toute zone retirée (extérieur ou saturation), par érosion du masque des pixels valides. La transition entre le fond et la peinture (quelques pixels sombres mais non nuls) est au même endroit dans les deux tuiles : sans érosion, elle formerait un faux détail commun, de décalage nul. Le bord de l'image n'est pas érodé.

Enfin, `extractNominalStrips` refuse la mesure si moins de 25 % des pixels sont exploitables dans les deux bandes à la fois (`minimumValidFraction`).

### Étape 1 : la translation globale, en deux passes

**Pourquoi on ne corrèle que les bandes.** Hors du recouvrement, le contenu de A et de B est différent et brouillerait le pic. Pour B en bas de A, on corrèle donc la bande du bas de A avec la bande du haut de B. Le décalage mesuré est un **résidu** autour de la position nominale `ty0 = hauteur − hauteur_bande`.

**Pourquoi deux passes.** La bande nominale (20 %) ne coïncide pas avec le vrai recouvrement quand le rail s'écarte du nominal. Avec un recouvrement réel o_r et un nominal o_n, seule une part `1 − |o_r − o_n| / o_n` de chaque bande a une correspondance dans l'autre (70 % pour 26 % réels annoncés à 20 %). Le reste abaisse le pic sans le déplacer, ce qui suffit pour une mesure au pixel. Mais quand o_r > o_n, une partie du vrai recouvrement n'est pas utilisée.

1. **Première passe** (`OverlapEstimatorClass`) : corrélation sur les bandes nominales, pic cherché dans la marge du rail, **au pixel près** (pas de sous-pixel). Elle donne la position de B, donc le recouvrement réel : `o_r = 1 − |ty| / hauteur` (ou `1 − |tx| / largeur`). C'est elle qui contrôle l'ambiguïté (`computePeakRatio`) et qui explique les échecs (`diagnoseGlobalTranslation`).
2. **Deuxième passe** (`PreparationClass::extractCommonArea` + `AlgorithmClass::refineGlobalTranslation`) : B est placée à cette position arrondie, et on corrèle l'**intersection exacte** des deux tuiles. Les deux images montrent la même partie de la peinture, à un pixel près : rien n'est perdu quand o_r > o_n, et aucun contenu sans correspondance n'affaiblit le pic quand o_r < o_n. Le pic est cherché à ±4 px (`SECOND_PASS_MARGIN`), puis raffiné au 1/100 px. Si le pic entier tombe sur le bord de cette marge, la position de la première passe était fausse de plus de 4 px : `false` (`describeRefinementFailure`), plutôt qu'une translation tronquée au bord. Le test porte sur le pic entier, car le raffinement peut ramener un pic de bord jusqu'à 0,75 px vers l'intérieur. Le second pic n'est pas contrôlé : la marge est plus petite que le voisinage exclu (±10 px), le test passerait toujours.

Le coût total reste proche d'une seule passe : le sous-pixel DFT, le plus cher, n'est calculé qu'une fois, sur la zone commune. La première passe ne coûte qu'une FFT.

Le calcul de la corrélation est le même dans les deux passes.

**Principe de la corrélation de phase** (`correlatePhase`) :

1. Les pixels NaN (vides) sont remplacés par la moyenne de l'image, puis on soustrait la moyenne.
2. On multiplie l'image par une **fenêtre de Hann**. Sans elle, les bords de l'image créent un faux pic à décalage nul.
3. On calcule les FFT `Fa` et `Fb`, puis le **spectre croisé** `Fa · conj(Fb)`.
4. On le **normalise** (division par son module) : seule la phase est conservée, c'est-à-dire seulement l'information de position. C'est ce qui rend la méthode insensible aux différences d'éclairage.
5. On multiplie par une **gaussienne** dans le domaine fréquentiel, ce qui revient à flouter la surface de corrélation (σ = `smoothingSigma`). Cela atténue les faux pics fins dus au grain du capteur.
6. La FFT inverse donne une **surface de corrélation** : son maximum est situé au décalage cherché. La FFT est périodique : un indice au-delà de la moitié de l'image correspond à un décalage négatif (`signedShift`).
7. Le pic entier est cherché sur la surface lissée, **seulement dans la marge** (marge du rail en première passe, ±4 px en deuxième passe) (`buildAllowedMask`, `locatePeak`) : les décalages résiduels `|dx|, |dy| ≤ railMargin`. Les bandes étant prises au recouvrement nominal, le décalage résiduel attendu est nul. Un pic plus haut au-delà de la marge ne peut pas être le bon : il vient d'un motif répétitif ou d'un fond uni, et le rail ne peut pas s'être trompé d'autant. Marge par défaut : 5 % de la tuile dans le sens du déplacement, au moins 48 px (même règle que la chaîne Python de référence).
8. En deuxième passe, la fraction de pixel est calculée par **DFT suréchantillonnée** (Guizar-Sicairos, 2008 ; `refinePeak`) : la surface **brute** est évaluée exactement sur une grille de 150 × 150 points au pas de 1/100 px, sur ±0,75 px autour du pic entier. Il suffit de deux produits matriciels `Ky · croise · Kxᵀ` (`buildDftKernel`), sans suréchantillonner toute l'image. On raffine sur la surface brute, et non sur la surface lissée, parce que le lissage déplace légèrement le sommet du pic. L'ancienne parabole sur trois valeurs entières était, elle, tirée vers l'entier le plus proche.

```
  surface lissée               spectre brut, autour du pic entier
  ┌───────────────────┐        ┌─────────────┐
  │      ┌─────┐      │        │ · · · · · · │  grille 1/100 px
  │      │  ●  │ marge│  ───►  │ · · ✚ · · · │  sur ±0,75 px
  │      └─────┘      │        │ · · · · · · │
  │ ●  pic hors marge │        └─────────────┘
  └───────────────────┘
```

**Contrôle de fiabilité** (`computePeakRatio`) : on divise la hauteur du meilleur pic par celle du second, cherché hors d'un carré de ±10 px autour du premier. Les deux sont cherchés **dans la marge** : un pic hors marge ne rend pas la mesure ambiguë, puisqu'il ne peut pas être le bon. Un rapport proche de 1 signifie que deux positions se valent : la mesure est rejetée (seuil 1.5).

### Étapes 2 à 5 : les points de correspondance

La translation globale ne suffit pas pour le format « points d'intérêt ». Il faut aussi des paires de points **mesurées indépendamment**, pour que RANSAC puisse détecter une rotation ou une déformation et écarter les mauvais points.

- **Candidats** : `cv::goodFeaturesToTrack` (Shi-Tomasi) choisit des coins bien texturés dans la zone commune. On en demande 3 × `maximumPointCount`, espacés d'au moins `minimumPointSpacing` px, et assez loin des bords pour que les patchs restent dans l'image.
- **Mesure locale** : pour chaque candidat, on prend un patch de `patchSize`×`patchSize` px dans A et le patch correspondant dans B, selon la translation arrondie. On relance `correlatePhase` sur ces deux patchs, ce qui donne un petit résidu local, au 1/100 px lui aussi. Ici le pic est cherché **sans marge** : limiter la recherche à ±`maximumLocalDeviation` masquerait tout le voisinage du pic et viderait le test du second pic. L'écart à la translation globale est contrôlé juste après.
- **Filtres** : le point est rejeté si le patch contient plus de 50 % de pixels vides, si la translation locale s'écarte de plus de `maximumLocalDeviation` px de la globale, ou si le pic local est ambigu.
- **Confiance** : NCC (corrélation normalisée, `cv::matchTemplate` en mode `TM_CCOEFF_NORMED`) entre les deux patchs une fois recalés. 1 signifie des patchs identiques ; la valeur est ramenée à 0 si elle est négative.
- **Sélection** : tri par confiance décroissante, puis on garde au plus `maximumPointCount` points.

Coordonnées : `pointsInA[i]` est entier (centre du patch dans A) ; `pointsInB[i]` est sous-pixel (`pointsInA[i] − translation locale`).

---

## 4. Correspondance avec le Python

| Python (`correlation_phase.py`) | C++ | Remarque |
|---|---|---|
| `charger` | `computeOutsideMask` + `convertToGrey` | Lecture de fichier retirée (rôle d'`ImportStepClass`). Pas de normalisation par percentiles (voir §6). **Amélioration** : en Python, tous les pixels à 0 deviennent NaN ; en C++, seulement ceux qui sont reliés au bord de la tuile. |
| `stripRectangle` | `stripRectangle` | Renvoie un `cv::Rect` : la bande n'est pas copiée. |
| `_zone_avec_contenu` | `findContentRectangle` | `PasDeContenu` devient `std::nullopt`. Mêmes seuils (0.1 / 0.5 / 32 px), nommés en constantes. |
| `_sous_pixel` | remplacé par `refinePeak` | Parabole remplacée par la DFT suréchantillonnée (voir ci-dessous). |
| `correlation_de_phase` | `correlatePhase` | Même calcul, en double précision comme numpy, découpé en `computeSpectrum`, `normalizeCrossSpectrum`, `buildGaussianFilter`, `inverseRealPart`, `locatePeak` et `refinePeak`. La surface lissée n'est pas recentrée (`fftshift`) : ce n'est pas nécessaire. |
| `rapport_pics` | `computePeakRatio` | Indices modulo au lieu de `np.roll`. Le voisinage masqué est le même ; les pics sont cherchés dans la marge. |
| `mesurer_decalage` + `decalage_entre_bandes` | `PreparationClass::orientPair` + `PreparationClass::extractNominalStrips` + `AlgorithmClass::measureGlobalTranslation` | L'échange A↔B pour haut/gauche se fait une seule fois (`orientPair`) et s'annule dans `undoOrientation`. |
| `zone_commune` (partie calcul) | `PreparationClass::extractOverlap` | Sert de zone de recherche des points. |
| — | `AlgorithmClass::measureLocalPoints` + `OutputClass` | **Nouveau** : génération et mise en forme des points de correspondance. |
| `profil_couture`, `variation_profil` | non porté | Diagnostics de déformation ; les points locaux + RANSAC jouent ce rôle. |
| `assembler`, `coller` | non porté | Rôle de `FusionStep`. |
| `metadonnees_tiff` | non porté | Rôle d'`ExportStep`. |

Trois éléments viennent de la version révisée du chef de projet (notebook `Stitching_Python/01_correlation_de_phase.ipynb`, classe `CorrelationDePhase`) et non du prototype :

| Notebook | C++ | Remarque |
|---|---|---|
| marge du rail (`autorise`, `paire.marge`) | `computeRailMargin`, `buildAllowedMask` | Même marge par défaut. Ici le décalage attendu est nul (bandes au recouvrement nominal). |
| `raffiner_pic`, `dft_surechantillonnee` (`sous_pixel="dft"`, `raffiner_sur="brut"`, `facteur_dft=100`) | `refinePeak`, `buildDftKernel` | Formulation vérifiée identique en numpy (mêmes valeurs à 0,01 px). Les options `parabole`/`aucun` et `raffiner_sur="lisse"` de l'étude ne sont pas portées. |
| `masque_zone` (saturation 0,998, érosion 4 px), `fraction_valide_min` | `convertToGrey`, `PreparationClass::extractNominalStrips` | Le fond reste détecté par le remplissage depuis le bord (étape 0), et non par le seuil sur une moyenne 7 × 7 du notebook : un noir isolé dans la peinture reste une donnée. |

---

## 5. Paramètres

Ce sont les champs de la structure `PhaseCorrelationNs::Parameters` (`phase_correlation/phase_correlation_types.h`). L'appelant la remplit et la passe au constructeur ; chaque champ non modifié garde sa valeur par défaut :

```cpp
PhaseCorrelationNs::Parameters parameters;   // valeurs par défaut
parameters.maximumPointCount = 1000;
PhaseCorrelationNs::PhaseCorrelationMatcherClass matcher(parameters);
```

La deuxième colonne donne le nom du paramètre dans le script JSON du step `phase-correlation` : il n'a pas changé avec la migration des noms, pour ne pas casser les scripts existants.

| Champ | Paramètre du step | Défaut | Effet |
|---|---|---|---|
| `smoothingSigma` | `lissage` | 3.0 | σ (px) du flou de la surface globale. Plus il est grand, moins le bruit haute fréquence pèse, mais le pic est plus large. |
| `minimumPeakRatio` | `seuilRapport` | 1.5 | Rapport minimal entre le meilleur pic et le second, pour la mesure globale comme pour les mesures locales. |
| `maximumPointCount` | `nbPoints` | 10 | Nombre maximal de points renvoyés (0 = translation seule, sans points). |
| `patchSize` | `fenetre` | 64 | Côté (px) des patchs locaux. Plus grand : mesure plus stable mais plus lente et moins locale. |
| `localSmoothingSigma` | `lissageLocal` | 1.5 | σ (px) du flou des surfaces locales. |
| `maximumLocalDeviation` | `ecartMax` | 3.0 | Écart maximal (px) toléré entre la translation locale et la translation globale. |
| `minimumPointSpacing` | `espacementMin` | 16 | Distance minimale (px) entre deux points candidats. |
| `railMargin` | `marge` | 0 | Écart maximal (px), sur chaque axe, entre la translation globale et le recouvrement nominal (incertitude du rail, première passe). 0 = automatique : 5 % de la tuile dans le sens du déplacement, au moins 48 px. |
| `minimumValidFraction` | `fractionValideMin` | 0.25 | Part minimale de pixels exploitables (ni extérieurs, ni saturés) communs aux deux bandes. |
| `upsamplingFactor` | `facteurDft` | 100 | Précision du sous-pixel : 1/`upsamplingFactor` px (deuxième passe et points locaux). Le coût du raffinement est proportionnel à ce facteur. 1 = au pixel, sans raffinement (c'est ce que fait la première passe). |

La marge de la deuxième passe (±4 px autour de la position de la première) est une constante, `SECOND_PASS_MARGIN` (`preparation.cpp`), et non un paramètre.

---

## 6. Choix et limites à connaître

- **Pleine dynamique, aucune perte.** Les tuiles 16 bits ne passent jamais en 8 bits. `convertToGrey` fait un `convertTo(CV_64F)` sans mise à l'échelle, donc une valeur 0–65535 reste 0–65535. Les patchs passent ensuite en float32 pour `goodFeaturesToTrack` et `matchTemplate`, ce qui est sans perte pour du 16 bits (24 bits de mantisse). Les seules matrices 8 bits sont des masques (pixel valide ou non). Les tuiles d'origine ne sont jamais modifiées.
- **Pixels à 0 = vide seulement s'ils sont reliés au bord** (voir l'étape 0). Il reste une limite : on ne sait pas quels bords de la tuile sont l'extrémité du tableau. Une ombre à 0 coupée par un bord **intérieur** de la tuile (un bord partagé avec une tuile voisine) est donc encore considérée comme vide. Voir la remarque ci-dessous.
- **Exactement 0.** Seul un 0 exact est considéré comme vide. Un fond extérieur légèrement bruité (1, 2…) sera traité comme une donnée. L'érosion de 4 px en atténue l'effet au bord du fond, sans le supprimer.
- **Position fournie par le pipeline.** Si `context.estimatedTranslation` est fausse de plus de 4 px (par exemple une valeur déduite de la médiane d'une rangée, et non mesurée sur la paire), la deuxième passe échoue (`false`, « le pic touche le bord de sa marge »). Pour l'accepter, il faudrait agrandir `SECOND_PASS_MARGIN`.
- **Marge du rail fixe.** Elle suppose que la translation réelle est à moins de `railMargin` px du recouvrement nominal donné par `context.overlapFraction`. Si le rail ou le recouvrement annoncé est plus faux que cela, la mesure trouve un pic faux dans la marge, ou échoue. Augmenter `railMargin` dans ce cas. Quand elle échoue, `getFailureReason()` le dit explicitement : le meilleur pic est alors cherché sur toute la surface, et s'il est franc et hors marge, le message donne la marge à utiliser (écart mesuré + 10 px) et le recouvrement réel estimé. Ce diagnostic ne coûte rien quand la mesure réussit : une corrélation de plus, seulement en cas d'échec. **Limite** : si un faux pic franc existe dans la marge, la mesure réussit avec une valeur fausse, et rien ne le signale.
- **Coût du sous-pixel.** Le raffinement DFT coûte environ 150 × (nombre de pixels de la bande) multiplications complexes, soit de l'ordre de 10⁹ pour une bande de 1035 × 5176 px. **Le temps n'a pas été mesuré** (code non compilé). S'il est trop long : baisser `upsamplingFactor` (le coût est proportionnel), ou raffiner en deux passes (1/10 px sur ±0,75 px, puis 1/100 px sur ±0,1 px), environ 5 fois moins cher.
- **Pas de normalisation par percentiles.** La corrélation de phase ne garde que la phase et la NCC est normalisée : ni l'une ni l'autre ne change si l'on applique une transformation affine à l'intensité (`a·I + b`). L'étape n'apportait donc rien.
- **Test « zone uniforme » fait sur la bande** et non sur la tuile entière. C'est sans conséquence pratique : une bande uniforme ne peut de toute façon pas être mesurée.
- **Translation uniquement.** Le modèle global est une translation. Une légère rotation ou une déformation du support apparaît dans les points locaux, que RANSAC traitera ensuite.
- **Tuiles de même taille** obligatoires, comme dans le Python.
- **`Direction`** : la syntaxe `{ Top, Bottom, Left, Right }` est encore à confirmer avec l'équipe.
- **`distorsion optique`** : La distorsion est une aberration géométrique qui se produit lorsque le grossissement d'un objectif varie en s'éloignant de l'axe optique. Comme les bords du capteur photo (plat) sont plus éloignés du centre de la lentille que le milieu du capteur, l'image subit une réduction de grossissement à sa périphérie.--> à coriger avec pré traitement ou autre ?

> **Remarque — correction possible, non implémentée**
>
> **Problème.** `computeOutsideMask` lance le remplissage depuis les **quatre** bords de la tuile. Or un bord partagé avec une tuile voisine se trouve *à l'intérieur* du tableau. Une ombre bouchée à 0 que ce bord coupe est donc prise à tort pour de l'extérieur.
>
> **Idée.**
> 1. Le pipeline sait déjà quels côtés d'une tuile sont l'extrémité du tableau : ce sont ceux où la `TileClass` n'a pas de voisin (`getLeft()`, `getRight()`, `getTop()`, `getBottom()` valent `nullptr`).
> 2. On transmettrait cette information au matcher, par exemple avec deux champs dans `DataNs::PairContext` :
>    ```cpp
>    struct TileBorders { bool top = true, bottom = true, left = true, right = true; };  // côtés au bord du tableau
>    struct PairContext {
>        Direction direction;
>        float overlapFraction;
>        std::optional<cv::Point2d> estimatedTranslation;
>        TileBorders bordersA, bordersB;   // par défaut tous à true = comportement actuel
>    };
>    ```
> 3. `PreparationClass::orientPair` les transmettrait à `computeOutsideMask(tile, borders)`, qui ne lancerait le remplissage que depuis ces côtés-là.
>
> **Pourquoi ça suffit.** Une zone extérieure qui s'étend jusqu'à un bord intérieur reste reliée à l'extrémité du tableau, donc elle serait toujours remplie entièrement. Avec les valeurs par défaut (`true` partout), le comportement serait identique à l'actuel.
>
> **Cas particulier.** Une tuile intérieure à la grille n'a aucun côté au bord du tableau. Pour un tableau non rectangulaire (cintré, par exemple), son fond noir éventuel ne serait alors pas détecté comme vide.
>
> **Pourquoi ce n'est pas fait.** La modification touche `PairContext`, l'interface commune à toutes les méthodes, dont la syntaxe n'est pas encore fixée. À décider avec l'équipe.

---

## 7. Comment vérifier

Les points 2 et 3 sont automatisés dans `tests/phase_correlation/test_phase_correlation.cpp` (GoogleTest, lancé par `ctest`, sur une texture générée) ; le point 1 par `tests/comparer_python.py`. Résultats du 12/10/2026 (OpenCV 4.14, MSVC, Release) en fin de section.

1. **Parité avec le Python** : sur deux vraies tuiles, comparer `(matches.translationY, matches.translationX)` à `mesurer_decalage(A, B, "bas")` (ou `"droite"`, `"haut"`, `"gauche"`). Les deux versions ne font pas le même sous-pixel (parabole sur la surface lissée en Python, DFT sur la surface brute ici) : des écarts de quelques dixièmes de pixel sont normaux, pas des écarts de plusieurs pixels.
2. **Test synthétique** : découper A et B dans une même image à un décalage connu non entier, puis vérifier `(x, y)` et que `pointsInA[i] − pointsInB[i] ≈ (x, y)` à moins de 0,5 px pour tous les points. **B doit être décalée par la FFT** (théorème du décalage), pas par `cv::warpAffine` : l'interpolation bicubique déforme la phase et fausse la vérité de 0,1 à 0,25 px.
3. **Cas limites** :
   - bande noire → `match` renvoie `false`, sans exception ;
   - bande surtout saturée (plus de 75 % de pixels au maximum) → `false` ;
   - `Top`/`Left` → résultat de signe opposé à celui de `Bottom`/`Right` sur la paire inversée ;
   - motif périodique de période supérieure à la marge : le pic retenu doit être celui de la marge, même si une copie du motif donne un pic plus haut au-delà ;
   - recouvrement réel de 27 % annoncé à 20 % (70 px hors de la marge de 50 px) → `false`, et `getFailureReason()` dit d'augmenter la marge ; avec `railMargin = 100`, la translation est juste ;
   - **deux passes**, recouvrements réels de 23 % et 17 % annoncés à 20 % : la première passe donne une position entière à moins de 1 px de la vérité et le recouvrement réel au pixel près ; `Top(B, A) = −Bottom(A, B)` ; la zone commune fait toute la hauteur du recouvrement réel (230 ou 170 px, contre 200 px pour la bande nominale) ; `match` est juste au 1/100 px ; avec `context.estimatedTranslation` fournie, le résultat est identique au bit près ; fausse de (+2, −2) px, elle est rattrapée ; fausse de 10 px, ou sans recouvrement → `false`, avec la raison.
4. **Données de contrôle à vérité exacte** : les jeux `C01` à `C10` (générés par le notebook `00_donnees_controle.ipynb`, positions exactes dans `verite.json`). Sur ces jeux, le notebook `01_correlation_de_phase.ipynb` annonce une erreur médiane de 0,010 à 0,020 px. Ses 4 erreurs graves (C04 fond uni, C05 reflet, C10 cadre) y sont écartées par le contrôle NCC commun à toutes les méthodes, qui ne fait pas partie de ce module : en C++, ces paires peuvent encore être renvoyées avec `true`. **Non fait** : ces jeux ne sont pas dans `KIKIRPA-Research/data/`.

### Résultats

Les mesures ci-dessous ont été faites avec `maximumPointCount = 500` (défaut de l'époque ; il est maintenant de 10).

**Vérité exacte** (`test_phase_correlation`, tuiles de 1000 px, recouvrement 20 %, décalages résiduels non entiers de 3 à 31 px, 4 directions), sur trois sources : texture générée, `synthetic_v1.0/NOM_0001_full.tif`, tuile réelle Savery 03.

| | |
|---|---|
| Erreur de translation | ≤ 0,005 px sur les 12 paires (limite de la grille de 1/100 px) |
| Points | 67 à 362 par paire ; écart médian 0,004 à 0,014 px, max 0,027 px |
| Cas limites | tous conformes (bandes noires, saturées, uniformes, tailles différentes, tache noire isolée, Top = −Bottom, motif périodique, paramètres invalides) |

**Paires réelles** (`comparer_python.py`, 7 paires Savery et 4 VanDerVeken du README de la piste Python, tuiles 5176 × 5176) :

| | |
|---|---|
| Même pic que le Python | 11 / 11 |
| Écart C++ / Python | médiane 0,43 px, max 1,32 px (Savery 05 → 06, rapport 5,1) |
| Points | 136 à 500 par paire, à 1 à 2,7 px (médiane) de la translation globale. Sur le synthétique, cet écart est de 0,01 px : il mesure la déformation réelle le long de la couture, qu'une translation ne décrit pas (voir le README Python, « arche » de 12 px) |
| Temps de `match()` | 2 à 8 s par paire, contre 0,8 à 2,5 s pour `mesurer_decalage` (Python, translation seule) |

Détail du temps sur Savery 01 → 02 : 8,1 s avec `maximumPointCount = 500`, 2,5 s en translation seule (`maximumPointCount = 0`, autant que le Python), 1,6 s en translation seule au 1/10 px. **Les 500 points locaux coûtent donc environ 5,5 s**, presque entièrement dans le sous-pixel DFT au 1/100 de chaque patch : avec `upsamplingFactor = 10`, la paire entière prend 2,5 s.

**Deux passes** (20/10/2026, build Debug, `maximumPointCount = 10`) : sur Savery 01 → 02, 03 → 04 et 04 → 05, la translation change d'au plus 0,06 px par rapport à la version à une passe, avec le même nombre de points (10, 10 et 2). Le temps de `match()` augmente de 15 à 25 % en Debug (par exemple 17,0 → 19,0 s sur 01 → 02). La cause n'a pas été mesurée. Piste probable : `PreparationClass::orientPair` est appelé deux fois quand `match()` fait aussi la première passe, et `computeOutsideMask` est donc recalculée sur les deux tuiles entières. **Non mesuré en Release.**
