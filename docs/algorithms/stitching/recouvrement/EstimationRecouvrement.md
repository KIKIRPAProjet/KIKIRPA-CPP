# EstimationRecouvrement — première passe commune à toutes les méthodes d'appariement

```
include/backend/algorithms/stitching/recouvrement/EstimationRecouvrement.h
src/backend/algorithms/stitching/recouvrement/EstimationRecouvrement.cpp
```

## 1. À quoi ça sert

Le rail d'acquisition annonce un recouvrement nominal (environ 20 %), mais le recouvrement réel s'en écarte (26 % sur Philadelphia 35 → 36, par exemple). Découper les bandes à 20 % fait alors perdre une partie du vrai recouvrement, ou ajoute du contenu qui n'a pas de correspondance dans l'autre tuile.

`EstimationRecouvrement` mesure, pour une paire de tuiles voisines, **la position de B par rapport à A au pixel près**, et en déduit le **recouvrement réel**. Chaque méthode d'appariement peut ensuite travailler directement sur la zone commune réelle :

- la corrélation de phase y mesure la translation au 1/100 px (deuxième passe, voir [CorrelationPhase.md](../correlation_phase/CorrelationPhase.md), étape 1) ;
- SIFT et LightGlue, qui attendent des bandes déjà découpées, y cherchent leurs points.

La mesure est une corrélation de phase sur les bandes de recouvrement nominal, avec les mêmes modules que `CorrelationPhase` (`Preparation::orienter`, `Preparation::bandes`, `Algorithme::mesurerGlobal`) mais **sans sous-pixel** : le pixel suffit pour découper la zone, et c'est le sous-pixel qui coûte le plus.

## 2. Interface

```cpp
class EstimationRecouvrement {
public:
    explicit EstimationRecouvrement(const correlation_phase::Parametres& p = {});
    std::optional<RecouvrementReel> estimer(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx);
    const std::string& raisonEchec() const;
    static std::pair<cv::Rect, cv::Rect> zonesCommunes(cv::Size a, cv::Size b, cv::Point2d translation);
};
```

- **Paramètres** : ceux de la corrélation de phase. Seuls `lissage`, `seuilRapport`, `marge` (marge du rail) et `fractionValideMin` servent ; `facteurDft` est forcé à 1. Un paramètre invalide lève `std::invalid_argument` à la construction.
- **`estimer(a, b, ctx)`** : `ctx.dir` et `ctx.overlap` (recouvrement nominal) comme pour `match()`. Vide si la mesure est impossible ou peu fiable, dans les mêmes cas que `CorrelationPhase::match` : tuiles vides ou de tailles différentes, pas de contenu commun, pic hors de la marge du rail ou ambigu. Dans ce cas, `raisonEchec()` donne la raison, avec la marge ou le recouvrement à essayer quand ils sont connus.
- **`zonesCommunes(tailleA, tailleB, translation)`** : rectangles de la zone commune dans A et dans B (même taille), B placée à la translation arrondie. Rectangles vides si les tuiles ne se superposent pas.

Sortie (`core/Types.h`) :

```cpp
struct RecouvrementReel {
    cv::Point2d translation;   // coin haut-gauche de B dans le repère de A, au pixel près
    float overlap;             // recouvrement réel : fraction de la tuile dans le sens du déplacement
    double rapport;            // rapport pic / second pic de la mesure (confiance)
};
```

Le recouvrement réel est `1 − |ty| / hauteur` pour une paire haut/bas, `1 − |tx| / largeur` pour une paire gauche/droite. Le décalage en travers (`tx` pour une paire haut/bas) ne change pas le recouvrement, mais il réduit la zone commune sur l'autre axe : `zonesCommunes` en tient compte.

## 3. Utilisation dans le pipeline

La position mesurée se transmet aux méthodes d'appariement par `PairContext` :

```cpp
struct PairContext {
    Direction dir;
    float overlap;                              // nominal, ou réel une fois estimé
    std::optional<cv::Point2d> translation;     // position de B dans A (première passe) ; absente = non estimée
};
```

Enchaînement prévu pour un step de la première passe, placé avant le step `stitching` :

```cpp
EstimationRecouvrement estimation;
const auto reel = estimation.estimer(A, B, {dir, 0.2f});          // recouvrement nominal du rail
if (!reel) { /* outputData["error"] = estimation.raisonEchec() */ }
const PairContext ctx{dir, reel->overlap, reel->translation};

// Corrélation de phase : tuiles entières, la première passe est sautée
correlationPhase.match(A, B, ctx, matches);

// SIFT, LightGlue : bandes déjà découpées sur la zone commune réelle
const auto [za, zb] = EstimationRecouvrement::zonesCommunes(A.size(), B.size(), *ctx.translation);
sift.match(A(za), B(zb), ctx, matches);   // points dans le repère des zones : ajouter za.tl() / zb.tl()
```

Sans `ctx.translation`, `CorrelationPhase::match` fait la première passe lui-même : le comportement est le même, appel par appel.

## 4. Limites

- **Le résidu doit rester dans la marge du rail** (5 % de la tuile par défaut). Au-delà, `estimer` échoue, et `raisonEchec()` donne une estimation du recouvrement réel à essayer.
- **Un faux pic franc dans la marge** (motif répétitif) donne une fausse position, sans que rien ne le signale. La deuxième passe de la corrélation de phase la refuse si elle est fausse de plus de 4 px, mais SIFT et LightGlue travailleraient sur une mauvaise zone.
- **Une seule valeur par paire.** En cas de rotation ou de déformation, le recouvrement varie le long de la couture ; ce sont les points locaux qui le mesurent.
- **Pas encore de step.** Les steps du pipeline et le type des paires (`Alignments`…) ne sont pas encore fixés (branche `pipeline_pr`). La classe est prête à être appelée par un step ; l'idée d'une médiane des recouvrements réels par rangée, pour corriger les paires qui échouent, n'est pas implémentée.

## 5. Comment vérifier

Testé dans `test_correlation_phase` (section « Première et deuxième passe ») : recouvrements réels de 23 % et 17 % annoncés à 20 %, sur une texture générée à vérité exacte (position non entière).

| Vérification | Résultat (20/10/2026, Debug) |
|---|---|
| Position de la première passe | entière, à moins de 0,5 px de la vérité sur chaque axe |
| Recouvrement réel | 23,00 % et 17,00 % (vérité 22,97 % et 16,97 %) |
| `Top(B, A)` | `= −Bottom(A, B)`, même recouvrement |
| Zone commune | 994 × 230 px et 994 × 170 px (bande nominale : 200 px de haut) |
| Deuxième passe avec `ctx.translation` | même translation et mêmes points que `match()` seul ; rattrapée si fausse de 2 px ; `false` si fausse de 10 px |
