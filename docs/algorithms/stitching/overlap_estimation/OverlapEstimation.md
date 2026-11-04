# OverlapEstimatorClass — première passe commune à toutes les méthodes d'appariement

```
include/backend/algorithms/stitching/overlap_estimation/overlap_estimator.h     namespace OverlapEstimationNs
src/backend/algorithms/stitching/overlap_estimation/overlap_estimator.cpp
```

## 1. À quoi ça sert

Le rail d'acquisition annonce un recouvrement nominal (environ 20 %), mais le recouvrement réel s'en écarte (26 % sur Philadelphia 35 → 36, par exemple). Découper les bandes à 20 % fait alors perdre une partie du vrai recouvrement, ou ajoute du contenu qui n'a pas de correspondance dans l'autre tuile.

`OverlapEstimatorClass` mesure, pour une paire de tuiles voisines, **la position de B par rapport à A au pixel près**, et en déduit le **recouvrement réel**. Chaque méthode d'appariement peut ensuite travailler directement sur la zone commune réelle :

- la corrélation de phase y mesure la translation au 1/100 px (deuxième passe, voir [PhaseCorrelation.md](../phase_correlation/PhaseCorrelation.md), étape 1) ;
- SIFT et LightGlue, qui attendent des bandes déjà découpées, y cherchent leurs points.

La mesure est une corrélation de phase sur les bandes de recouvrement nominal, avec les mêmes modules que `PhaseCorrelationMatcherClass` (`PreparationClass::orientPair`, `PreparationClass::extractNominalStrips`, `AlgorithmClass::measureGlobalTranslation`) mais **sans sous-pixel** : le pixel suffit pour découper la zone, et c'est le sous-pixel qui coûte le plus.

## 2. Interface

```cpp
class OverlapEstimatorClass {
    public:
        explicit OverlapEstimatorClass(const PhaseCorrelationNs::Parameters& initialParameters = {});
        std::optional<DataNs::OverlapEstimate> estimate(const cv::Mat& imageA, const cv::Mat& imageB,
                                                        const DataNs::PairContext& context);
        const std::string& getFailureReason() const;
        static std::pair<cv::Rect, cv::Rect> computeCommonAreas(cv::Size sizeA, cv::Size sizeB, cv::Point2d translation);
};
```

- **Paramètres** : ceux de la corrélation de phase. Seuls `smoothingSigma`, `minimumPeakRatio`, `railMargin` (marge du rail) et `minimumValidFraction` servent ; `upsamplingFactor` est forcé à 1. Un paramètre invalide lève `std::invalid_argument` à la construction.
- **`estimate(imageA, imageB, context)`** : `context.direction` et `context.overlapFraction` (recouvrement nominal) comme pour `match()`. Vide si la mesure est impossible ou peu fiable, dans les mêmes cas que `PhaseCorrelationMatcherClass::match` : tuiles vides ou de tailles différentes, pas de contenu commun, pic hors de la marge du rail ou ambigu. Dans ce cas, `getFailureReason()` donne la raison, avec la marge ou le recouvrement à essayer quand ils sont connus.
- **`computeCommonAreas(sizeA, sizeB, translation)`** : rectangles de la zone commune dans A et dans B (même taille), B placée à la translation arrondie. Rectangles vides si les tuiles ne se superposent pas.

Sortie (`core/data/algorithm_types.h`) :

```cpp
struct OverlapEstimate {
    cv::Point2d translationBInA;   // coin haut-gauche de B dans le repère de A, au pixel près
    float overlapFraction;         // recouvrement réel : fraction de la tuile dans le sens du déplacement
    double peakRatio;              // rapport pic / second pic de la mesure (confiance)
};
```

Le recouvrement réel est `1 − |ty| / hauteur` pour une paire haut/bas, `1 − |tx| / largeur` pour une paire gauche/droite. Le décalage en travers (`tx` pour une paire haut/bas) ne change pas le recouvrement, mais il réduit la zone commune sur l'autre axe : `computeCommonAreas` en tient compte.

## 3. Utilisation dans le pipeline

La position mesurée se transmet aux méthodes d'appariement par `DataNs::PairContext` :

```cpp
struct PairContext {
    Direction direction;
    float overlapFraction;                              // nominal, ou réel une fois estimé
    std::optional<cv::Point2d> estimatedTranslation;    // position de B dans A (première passe) ; absente = non estimée
};
```

Enchaînement prévu pour un step de la première passe, placé avant le step `stitching` :

```cpp
OverlapEstimationNs::OverlapEstimatorClass overlapEstimator;
const auto overlapEstimate = overlapEstimator.estimate(imageA, imageB, {direction, 0.2f});   // recouvrement nominal du rail
if (!overlapEstimate) { /* result["error"] = overlapEstimator.getFailureReason() */ }
const DataNs::PairContext context{direction, overlapEstimate->overlapFraction, overlapEstimate->translationBInA};

// Corrélation de phase : tuiles entières, la première passe est sautée
phaseCorrelationMatcher.match(imageA, imageB, context, matches);

// SIFT, LightGlue : bandes déjà découpées sur la zone commune réelle
const auto [areaA, areaB] = OverlapEstimationNs::OverlapEstimatorClass::computeCommonAreas(
    imageA.size(), imageB.size(), *context.estimatedTranslation);
siftMatcher.match(imageA(areaA), imageB(areaB), context, matches);   // points dans le repère des zones : ajouter
                                                                     // areaA.tl() / areaB.tl()
```

Sans `context.estimatedTranslation`, `PhaseCorrelationMatcherClass::match` fait la première passe lui-même : le comportement est le même, appel par appel.

## 4. Limites

- **Le résidu doit rester dans la marge du rail** (5 % de la tuile par défaut). Au-delà, `estimate` échoue, et `getFailureReason()` donne une estimation du recouvrement réel à essayer.
- **Un faux pic franc dans la marge** (motif répétitif) donne une fausse position, sans que rien ne le signale. La deuxième passe de la corrélation de phase la refuse si elle est fausse de plus de 4 px, mais SIFT et LightGlue travailleraient sur une mauvaise zone.
- **Une seule valeur par paire.** En cas de rotation ou de déformation, le recouvrement varie le long de la couture ; ce sont les points locaux qui le mesurent.
- **Pas encore de step.** La classe est prête à être appelée par un step ; l'idée d'une médiane des recouvrements réels par rangée, pour corriger les paires qui échouent, n'est pas implémentée.

## 5. Comment vérifier

Testé dans `test_phase_correlation` (section « Première et deuxième passe ») : recouvrements réels de 23 % et 17 % annoncés à 20 %, sur une texture générée à vérité exacte (position non entière).

| Vérification | Résultat (20/10/2026, Debug) |
|---|---|
| Position de la première passe | entière, à moins de 0,5 px de la vérité sur chaque axe |
| Recouvrement réel | 23,00 % et 17,00 % (vérité 22,97 % et 16,97 %) |
| `Top(B, A)` | `= −Bottom(A, B)`, même recouvrement |
| Zone commune | 994 × 230 px et 994 × 170 px (bande nominale : 200 px de haut) |
| Deuxième passe avec `context.estimatedTranslation` | même translation et mêmes points que `match()` seul ; rattrapée si fausse de 2 px ; `false` si fausse de 10 px |
