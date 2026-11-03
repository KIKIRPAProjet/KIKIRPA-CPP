# Egalisation — égalisation de luminosité entre tuiles

Port C++ du notebook `egalisation_gain_champ_final.ipynb` (méthodes A et B), organisé comme la corrélation de phase en trois modules :

```
include/backend/algorithms/egalisation/
├── Egalisation.h      header global : le seul que le pipeline inclut
├── Types.h            structures échangées entre les modules
├── Preparation.h      1. luminances réduites
├── Algorithme.h       2. échantillons, moindres carrés, bilan
└── Sortie.h           3. correction des tuiles, rapport
src/backend/algorithms/egalisation/
├── Egalisation.cpp    enchaîne les 3 modules
├── Preparation.cpp
├── Algorithme.cpp
└── Sortie.cpp
tests/egalisation/test_egalisation.cpp
```

Seule dépendance : **OpenCV** (`core`, `imgproc`).

---

## 1. À quoi ça sert

Deux tuiles voisines ne sont pas exposées exactement de la même façon (puissance des lampes, exposition, vignetage). Dans le recouvrement, la fonction de coût de la seamline voit alors un écart partout, et la couture devient visible en une marche de luminosité.

L'égalisation corrige **uniquement la différence entre les tuiles**, jamais l'œuvre elle-même :

- **méthode A, gain global** : un facteur par tuile, résolu pour toute la grille à la fois (Brown & Lowe, 2007) ;
- **méthode B, gain + champ d'éclairage** : la méthode A, plus un assombrissement vers les bords `exp(c · r²)`, commun à toutes les tuiles.

La correction est une **multiplication pure par un champ lisse** : aucun filtre, le grain et les craquelures ne changent pas.

---

## 2. Interface

```cpp
namespace stitching::egalisation {
class Egalisation {
public:
    explicit Egalisation(const Parametres& p = {});
    TuileReduite reduire(const cv::Mat& tuile) const;                         // 1. une tuile à la fois
    TuileReduite reduire(const cv::Mat& reduite, cv::Size pleine) const;      //    ou depuis un niveau de pyramide
    std::optional<Resultat> estimer(const std::vector<TuileReduite>& tuiles,  // 2. gains A, gains + champ B, bilan
                                    const std::vector<PaireVoisine>& paires);
    const std::string& raisonEchec() const;
    static cv::Mat corriger(const cv::Mat& tuile, const Resultat& r, int indice, Methode m);   // 3.
    static std::vector<PaireVoisine> pairesDepuisPositions(const std::vector<cv::Point2d>& positions,
                                                           const std::vector<std::pair<int, int>>& voisins);
};
}
```

Utilisation type, **une tuile en mémoire à la fois** :

```cpp
Egalisation eg;
std::vector<TuileReduite> reduites;
for (const auto& chemin : chemins) reduites.push_back(eg.reduire(cv::imread(chemin, cv::IMREAD_UNCHANGED)));

// Translations du stitching : coin haut-gauche de B dans le repère de A (Matches::dx, dy de la corrélation de phase),
// ou positions globales (TilePositionData) converties par pairesDepuisPositions
const auto r = eg.estimer(reduites, paires);
if (!r) { std::cerr << eg.raisonEchec(); return; }
std::cout << Sortie::rapport(*r);

for (int i = 0; i < n; ++i) {
    const cv::Mat corrigee = Egalisation::corriger(cv::imread(chemins[i], cv::IMREAD_UNCHANGED), *r, i, Methode::GainEtChamp);
    cv::imwrite(sortie[i], corrigee);
}
```

- **Entrées** : tuiles 8 ou 16 bits, gris ou couleur (BGR / BGRA, comme `cv::imread`), de tailles éventuellement différentes. Les paires sont les voisins directs (droite, dessous) ; leur translation vient du stitching (aucun recalage n'est refait ici).
- **`estimer`** : vide si les entrées sont invalides (moins de 2 tuiles, tuile vide, indice hors limites) ou si aucune paire n'a d'échantillon ; `raisonEchec()` donne la raison.
- **`corriger`** : tuile entière 8 ou 16 bits → **16 bits** (8 bits × 257, comme le notebook) ; flottant → même type. Même facteur sur les 3 canaux couleur (teinte conservée), canal alpha recopié.

Sortie (`Types.h`) :

```cpp
struct Resultat {
    std::vector<double> gainsA, gainsB;   // gain de chaque tuile, méthode A et méthode B
    double c;                              // champ B : V(x, y) = exp(c · r²)
    std::vector<int> tuilesIsolees;        // tuiles sans paire exploitable : gain 1, non égalisées
    std::vector<int> pairesVides;          // paires sans échantillon (pas de recouvrement exploitable)
    std::size_t nbEchantillons;
    Bilan avant, apresA, apresB;           // qualité dans les recouvrements, en %
};
```

---

## 3. Paramètres

| Champ | Défaut | Rôle |
|---|---|---|
| `reduction` | 8 | Réduction de la tuile pour l'estimation (1/8). Le modèle n'a que n + 1 paramètres : quelques dizaines de milliers de pixels suffisent. |
| `flou` | 1,5 | Sigma (px de l'image réduite) du flou gaussien appliqué avant l'estimation. |
| `marge` | 40 | Pixels (pleine résolution) ignorés au bord de chaque tuile. |
| `seuilBas`, `seuilHaut` | 0,03 ; 0,97 | Pixels trop sombres ou saturés (reflets) exclus, en fraction de la dynamique. |
| `iterations` | 6 | Itérations des moindres carrés repondérés (IRLS). |
| `huber` | 1,5 | Seuil des poids de Huber, en écarts-types robustes (MAD × 1,4826). |
| `poidsJauge` | 1000 | Poids de la contrainte « moyenne des log g = 0 ». |

Une valeur hors domaine lève `std::invalid_argument` à la construction. Les valeurs par défaut sont celles du notebook.

---

## 4. Méthode

### 4.1 Modèle

La luminosité observée d'un point du tableau dans la tuile i vaut `I_i(x, y) = g_i · V(x, y) · R`, avec `R` la réflectance (inconnue), `g_i` le gain de la tuile et `V(x, y) = exp(c · r²)` le champ d'éclairage, avec `r² = u² + v²` et `u, v ∈ [−1, 1]` les coordonnées normalisées dans la tuile. Un même point vu par deux tuiles voisines donne, en logarithme, une équation **linéaire** où `R` disparaît :

```
log I_A − log I_B = (log g_A − log g_B) + c · (r²_A − r²_B)
```

Méthode A : même équation sans le terme `c`.

### 4.2 Étapes

1. **Préparation** (`Preparation::reduire`) : la tuile est réduite au 1/8 (`INTER_AREA`) **avant** d'être convertie en luminance (BGR → gris pondéré, ramenée dans [0, 1]), puis floutée (σ = 1,5). Une tuile 8 bits passe d'abord en 16 bits pour que la moyenne ne soit pas arrondie au 1/255. Rien n'est jamais copié en flottant à pleine résolution.
2. **Échantillons** (`Algorithme::echantillons`) : pour chaque pixel réduit de A, le point correspondant dans B (B placée à la translation de la paire) est lu par interpolation bilinéaire. Il est gardé s'il est dans les deux tuiles hors de la marge, et ni trop sombre ni saturé dans les deux. On note `r²_A`, `r²_B` et `d = log I_A − log I_B`.
3. **Moindres carrés robustes** (`Algorithme::resoudre`) : toutes les tuiles à la fois, contrainte de jauge (moyenne des log g nulle, ce qui conserve la luminosité globale de l'œuvre), puis 6 itérations IRLS avec des poids de Huber. Les **équations normales** (taille (n + 1)²) sont accumulées directement, au lieu du système complet (plusieurs millions de lignes) : chaque échantillon n'a que 2 ou 3 coefficients non nuls. Résolution par SVD.
4. **Bilan** (`Algorithme::residus`, `Algorithme::mesurer`) : avant, après A et après B (voir §5).
5. **Correction** (`Sortie::corriger`) : `tuile / (g · exp(c · r²))`, ligne par ligne et en parallèle.

### 4.3 Différences avec le notebook

| Point | Notebook | C++ |
|---|---|---|
| Recalage des paires | SIFT (version égalisation seule) ou corrélation de phase (pipeline complet) | translations fournies par l'appelant (stitching), aucun recalage refait |
| Système | `lstsq` sur toutes les équations | équations normales accumulées, SVD : même solution, mémoire constante |
| Échelle de réduction | 1/8 nominal | échelle réelle de chaque tuile (taille pleine / taille réduite), un peu plus précise |
| Tuile sans paire | gain issu de la solution de norme minimale | `log g = 0` imposé : gain exactement 1, signalée dans `tuilesIsolees` |
| Ordre réduction / luminance | luminance puis réduction | réduction puis luminance (identique au calcul près, sans copie flottante pleine résolution) |

---

## 5. Bilan de qualité

Mesuré sur les luminances réduites, en % (plus petit = mieux) :

- **`ecartMoyen`** : différence absolue moyenne entre les deux tuiles dans les recouvrements, en % de la dynamique ;
- **`variationBF`** : le rapport de luminosité est lissé sur une grande échelle (noyau 51 px, σ = 12), puis on mesure l'écart entre ses 5e et 95e centiles ; c'est ce qui se verrait en une marche sur la couture ;
- **`rmsResidu`**, **`residuMedian`** : écart de luminosité restant entre pixels correspondants, sur les échantillons.

`Sortie::rapport(r)` écrit les gains, le champ et ces quatre mesures avant / A / B.

---

## 6. Vérification

`tests/egalisation/test_egalisation.cpp` (GoogleTest, lancé par `ctest`) découpe 6 tuiles (grille 2 × 3, recouvrement de 17 %, décalages perpendiculaires aléatoires, une tuile plus haute de 50 px) dans une texture générée, applique des gains et un champ connus (c = −0,12), en 8 et 16 bits mélangés, en gris puis en couleur. Il vérifie :

- les gains B à 1 % de la vérité, les gains A à 3 %, et c à 0,005 près ;
- l'écart moyen dans les recouvrements : avant > A > B, et B < 0,5 % ;
- la tuile corrigée : 16 bits, même taille et mêmes canaux, même luminosité que sa voisine dans le recouvrement, teinte conservée en couleur ;
- les cas limites : paramètre invalide, une seule tuile, indice hors limites, aucun recouvrement, tuile sans paire (gain 1), paire sans recouvrement ignorée, réduction depuis un niveau 1/8.

Résultat : gains B à 0,01 % de la vérité, c = −0,1200 (vrai −0,1200), écart moyen dans les recouvrements 8,10 % → 1,42 % (A) → 0,12 % (B) en gris.

### Comparaison avec le notebook sur le dataset 05

Mêmes 6 tuiles réelles (archive « modifié », 8 bits BGRA et 16 bits gris mélangés) et mêmes translations que le notebook `Projet_KIK_IRPA_stitching_egalisation_seamline.ipynb` (positions retenues par la corrélation de phase) :

| Mesure | Notebook Python | C++ |
|---|---|---|
| Échantillons | 444 723 | 444 723 |
| Champ c | −0,1153 | −0,1153 |
| Écart moyen avant / A / B (%) | 11,08 / 1,58 / 1,21 | 11,08 / 1,58 / 1,21 |
| Variation BF avant / A / B (%) | 13,59 / 14,06 / 8,73 | 13,60 / 14,07 / 8,74 |
| RMS du résidu avant / A / B (%) | 42,37 / 9,81 / 9,02 | 42,36 / 9,80 / 9,01 |
| Résidu médian avant / A / B (%) | 27,36 / 3,37 / 1,76 | 27,36 / 3,37 / 1,76 |

Les écarts (0,01 au plus) viennent de l'échelle de réduction réelle et de l'ordre réduction / luminance (§4.3). Durée : 7,6 s pour lire les 6 tuiles, estimer et faire le bilan (sans écrire les tuiles corrigées).
