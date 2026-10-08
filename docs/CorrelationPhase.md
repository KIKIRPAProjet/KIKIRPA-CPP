# CorrelationPhase — corrélation de phase entre deux tuiles

Port C++ de `KIKIRPA-Research/FeaturesExtraction/Correlation-de-phase/correlation_phase.py`, organisé en trois modules (voir §3) :

```
include/algorithms/matching/
├── CorrelationPhase.h            header global : le seul que le pipeline inclut
└── correlation_phase/
    ├── Types.h                   toutes les structures échangées entre les modules
    ├── Preparation.h             1. préparation des images
    ├── Algorithme.h              2. algorithme pur
    └── Sortie.h                  3. préparation de la sortie
src/algorithms/matching/
├── CorrelationPhase.cpp          constructeur + match() : enchaîne les 3 modules
└── correlation_phase/
    ├── Preparation.cpp
    ├── Algorithme.cpp
    └── Sortie.cpp
```

Seule dépendance : **OpenCV** (déjà utilisée par le projet). Pas besoin d'Eigen, de FFTW ni de libtiff : `cv::dft` remplace `np.fft`, et la lecture des TIFF est faite en amont par `ImportStep`.

---

## 1. À quoi ça sert

Deux tuiles voisines A et B se recouvrent sur une bande (environ 20 % de leur taille). L'algorithme mesure **où se trouve B par rapport à A**, avec une précision inférieure au pixel. Il renvoie :

1. **une translation globale** `(x, y)` : la position du coin haut-gauche de B dans le repère de A ;
2. **des paires de points** `pointsA[i] ↔ pointsB[i]` avec une **confiance** `confidence[i]`. C'est le même format que les autres méthodes (SIFT, LightGlue…), ce qui permet de les passer à RANSAC / MAGSAC++.

```
          A                              repère de A
  ┌───────────────────┐                  (0,0) ──► x
  │                   │                    │
  │                   │                    ▼ y
  │          ┌────────┼──────────┐
  │          │ zone   │          │
  └──────────┼commune─┘          │   ◄── B posée en (x, y)
             │                   │
             │         B         │
             └───────────────────┘
```

---

## 2. Le header global : l'interface pour le pipeline

```cpp
class CorrelationPhase : public FeatureMatcher {
public:
    explicit CorrelationPhase(const Json& params = {});
    bool match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) override;
private:
    correlation_phase::Parametres parametres;   // lus une seule fois, dans le constructeur
};
```

- La classe hérite de `FeatureMatcher`, comme `SiftMatcher` et `LightGlueMatcher`. Le pipeline (`StitchingStep`) peut donc l'utiliser sans savoir de quelle méthode il s'agit.
- Elle est enregistrée dans `Factory.cpp` sous le nom **`"phase-correlation"`**, et c'est ce nom qui apparaît dans la liste de la GUI.
- `params` : un objet JSON facultatif pour régler l'algorithme (voir §5). Il est lu une seule fois, dans le constructeur ; sans lui, les valeurs par défaut s'appliquent. **Un paramètre invalide lève `std::invalid_argument` à la construction**, comme un nom d'algorithme inconnu dans la Factory.

### Entrées de `match()`

| Argument | Type | Rôle |
|---|---|---|
| `a`, `b` | `cv::Mat` | Les deux tuiles déjà chargées en mémoire. Elles ne sont pas copiées : on n'en lit que des sous-zones (ROI). Gris ou couleur, 8 ou 16 bits. Elles doivent avoir la même taille. **Aucune conversion en 8 bits** : les bandes sont lues directement en float64, avec leur dynamique complète. |
| `ctx.dir` | `Direction` | Position de **B par rapport à A** : `Top`, `Bottom`, `Left` ou `Right`. |
| `ctx.overlap` | `float` | Recouvrement nominal, en fraction de la tuile (ex. `0.2`). Il doit être compris entre 0 (exclu) et 1. |

### Sortie : `Matches` (dans `core/Types.h`)

```cpp
struct Matches {
    float x = 0.f, y = 0.f;                     // coin haut-gauche de B dans le repère de A (px)
    std::vector<cv::Point2f> pointsA, pointsB;  // coordonnées dans la tuile A / la tuile B entière
    std::vector<float> confidence;              // confiance de chaque paire, entre 0 et 1
};
```

C'est le struct proposé par l'équipe (`x`, `y`, `coord_A`, `coord_B`, `confiance`) : `pointsA` correspond à `coord_A` et `pointsB` à `coord_B`.

**Convention** : pour toute paire, `pointsA[i] − pointsB[i] ≈ (x, y)`. Un pixel `(X, Y)` de A correspond au pixel `(X − x, Y − y)` de B.

### Valeur de retour

- `true` : la translation `(x, y)` est valide. La liste de points peut être vide si la zone commune est trop étroite pour y placer des patchs.
- `false` : la mesure est impossible ou peu fiable. C'est le cas si les tuiles sont vides ou de tailles différentes, si les bandes n'ont pas de contenu commun (zone noire ou fond uni), ou si le pic de corrélation est ambigu. `match` ne lève **aucune exception**.

---

## 3. Organisation du code et déroulement

### Les trois modules

Chaque module est une classe qui ne contient que des fonctions `static`. Son header liste **toutes** ses fonctions :
- `public` : ce que l'orchestrateur ou un autre module appelle ;
- `private` : les fonctions internes au module, plus ses constantes nommées (seuils, rayon du pic…).

Le compilateur interdit d'appeler une fonction `private` depuis un autre module. Toutes les structures sont regroupées dans `correlation_phase/Types.h` (namespace `stitching::matching::correlation_phase`).

| Module | Rôle | Public | Private |
|---|---|---|---|
| **`Preparation`** | Des tuiles brutes aux images de travail (gris float64, NaN = vide) | `lireParametres`, `orienter`, `bandes`, `recouvrement`, `valides`, `remplirVides` | `oppose`, `bande`, `zoneExterieure`, `enGris`, `zoneAvecContenu` |
| **`Algorithme`** | Uniquement du calcul sur des images préparées | `mesurerGlobal`, `raffiner` | corrélation : `correlationDePhase`, `spectre`, `spectreCroiseNormalise`, `filtreGaussien`, `partieReelleInverse`, `localiserPic`, `sousPixel`, `frequence`, `rapportPics` — local : `candidats`, `mesurerPoint`, `ncc` |
| **`Sortie`** | Mise en forme au format commun `Matches` | `construire` | `trierEtLimiter`, `versTuiles`, `annulerOrientation` |

`Algorithme` utilise deux outils publics de `Preparation` (`valides`, `remplirVides`) pour traiter les pixels vides de ses patchs.

### Les structures (`Types.h`)

| Structure | Produite par | Contenu |
|---|---|---|
| `Parametres` | `Preparation::lireParametres` | Réglages (§5) |
| `PaireOrientee` | `Preparation::orienter` | A et B ramenées au cas « B en bas / à droite », masques extérieurs, indicateur `inversee` |
| `Bandes` | `Preparation::bandes` | Bandes de recouvrement en gris + position nominale `(tx0, ty0)` |
| `Correlation` | `Algorithme` (interne) | Décalage `(dy, dx)`, hauteur du pic, surface lissée |
| `Mesure` | `Algorithme::mesurerGlobal` | Translation globale `(tx, ty)` + rapport des pics |
| `Recouvrement` | `Preparation::recouvrement` | Zone commune réelle vue dans A et dans B, son origine dans A, la translation arrondie, la marge |
| `Correspondance` | `Algorithme::raffiner` | Point dans la zone commune + translation locale + confiance |

### Enchaînement dans `match()` (`CorrelationPhase.cpp`)

```
match(a, b, ctx)
 │
 ├─ Preparation::orienter ────── vérifie la paire ; si B est en haut / à gauche, échange A et B
 │      └─ zoneExterieure ×2       pixels noirs reliés au bord de la tuile → « vides »
 │
 ├─ Preparation::bandes ──────── bande du bas/de droite de A, du haut/de gauche de B
 │      ├─ bande, enGris           découpe, gris float64, zone extérieure → NaN
 │      └─ zoneAvecContenu         retire les bords vides communs           (sinon → false)
 │
 ├─ Algorithme::mesurerGlobal ── translation globale (x, y)
 │      ├─ correlationDePhase      FFT → spectre croisé → pic sous-pixel
 │      └─ rapportPics             le pic est-il franc ?                    (sinon → false)
 │
 ├─ Preparation::recouvrement ── zone commune réelle avec (x, y) arrondi   (trop étroite → pas de points)
 │
 ├─ Algorithme::raffiner
 │      ├─ candidats               coins texturés (goodFeaturesToTrack), espacés d'au moins 16 px
 │      └─ mesurerPoint ×N         patch 64×64 : correlationDePhase locale, filtres, ncc
 │
 └─ Sortie::construire
        ├─ trierEtLimiter          confiance décroissante, nb_points meilleurs
        ├─ versTuiles              coordonnées dans les tuiles entières
        └─ annulerOrientation      si A et B avaient été échangées : −(x, y), pointsA ↔ pointsB
```

### Étape 0 : la zone extérieure (`zoneExterieure`)

Il faut savoir quels pixels noirs sont « hors de l'œuvre » (sans valeur) et lesquels sont du vrai noir dans la peinture (ombre bouchée à 0). La règle : **un pixel à 0 n'est vide que s'il est relié au bord de la tuile par d'autres pixels à 0**.

1. `cv::inRange` marque les pixels dont tous les canaux valent 0.
2. Depuis chaque pixel à 0 du bord de la tuile, on lance un remplissage par diffusion (`cv::floodFill`, connexité 4). Tous les pixels noirs reliés au bord sont marqués.
3. Seuls ces pixels deviennent NaN dans `enGris`. Une tache noire isolée au milieu de la peinture reste une donnée normale.

```
  bord de la tuile
  ▼
  ████████░░░░░░░░░░       ████ = 0 relié au bord  → vide (NaN)
  ██████░░░░░░▓▓░░░░       ▓▓   = 0 isolé (ombre)  → donnée conservée
  ████░░░░░░░░▓▓░░░░       ░░   = peinture
  ██░░░░░░░░░░░░░░░░
```

Le calcul se fait sur **la tuile entière** et non sur la seule bande. Une zone extérieure peut en effet entrer dans la bande par l'intérieur de la tuile, sans toucher le bord de la bande. On n'en extrait ensuite que les sous-zones utiles. Le coût est faible : un masque de 1 octet par pixel et un remplissage proportionnel à la surface noire.

### Étape 1 : la translation globale

**Pourquoi on ne corrèle que les bandes.** Hors du recouvrement, le contenu de A et de B est différent et brouillerait le pic. Pour B en bas de A, on corrèle donc la bande du bas de A avec la bande du haut de B. Le décalage mesuré est un **résidu** autour de la position nominale `ty0 = hauteur − hauteur_bande`.

**Principe de la corrélation de phase** (`correlationDePhase`) :

1. Les pixels NaN (vides) sont remplacés par la moyenne de l'image, puis on soustrait la moyenne.
2. On multiplie l'image par une **fenêtre de Hann**. Sans elle, les bords de l'image créent un faux pic à décalage nul.
3. On calcule les FFT `Fa` et `Fb`, puis le **spectre croisé** `Fa · conj(Fb)`.
4. On le **normalise** (division par son module) : seule la phase est conservée, c'est-à-dire seulement l'information de position. C'est ce qui rend la méthode insensible aux différences d'éclairage.
5. On multiplie par une **gaussienne** dans le domaine fréquentiel, ce qui revient à flouter la surface de corrélation (σ = `lissage`). Cela atténue les faux pics fins dus au grain du capteur.
6. La FFT inverse donne une **surface de corrélation** : son maximum est situé au décalage cherché.
7. On affine au **sous-pixel** avec une parabole passant par le pic et ses deux voisins (`sousPixel`).
8. La FFT est périodique : un pic situé au-delà de la moitié de l'image correspond à un décalage négatif.

**Contrôle de fiabilité** (`rapportPics`) : on divise la hauteur du meilleur pic par celle du second, cherché hors d'un carré de ±10 px autour du premier. Un rapport proche de 1 signifie que deux positions se valent : la mesure est rejetée (seuil 1.5).

### Étapes 2 à 5 : les points de correspondance

La translation globale ne suffit pas pour le format « points d'intérêt ». Il faut aussi des paires de points **mesurées indépendamment**, pour que RANSAC puisse détecter une rotation ou une déformation et écarter les mauvais points.

- **Candidats** : `cv::goodFeaturesToTrack` (Shi-Tomasi) choisit des coins bien texturés dans la zone commune. On en demande 3 × `nb_points`, espacés d'au moins `espacement_min` px, et assez loin des bords pour que les patchs restent dans l'image.
- **Mesure locale** : pour chaque candidat, on prend un patch de `fenetre`×`fenetre` px dans A et le patch correspondant dans B, selon la translation arrondie. On relance `correlationDePhase` sur ces deux patchs, ce qui donne un petit résidu local.
- **Filtres** : le point est rejeté si le patch contient plus de 50 % de pixels vides, si la translation locale s'écarte de plus de `ecart_max` px de la globale, ou si le pic local est ambigu.
- **Confiance** : NCC (corrélation normalisée, `cv::matchTemplate` en mode `TM_CCOEFF_NORMED`) entre les deux patchs une fois recalés. 1 signifie des patchs identiques ; la valeur est ramenée à 0 si elle est négative.
- **Sélection** : tri par confiance décroissante, puis on garde au plus `nb_points` points.

Coordonnées : `pointsA[i]` est entier (centre du patch dans A) ; `pointsB[i]` est sous-pixel (`pointsA[i] − translation locale`).

---

## 4. Correspondance avec le Python

| Python (`correlation_phase.py`) | C++ | Remarque |
|---|---|---|
| `charger` | `Preparation::zoneExterieure` + `enGris` | Lecture de fichier retirée (rôle d'`ImportStep`). Pas de normalisation par percentiles (voir §6). **Amélioration** : en Python, tous les pixels à 0 deviennent NaN ; en C++, seulement ceux qui sont reliés au bord de la tuile. |
| `bande` | `Preparation::bande` | Renvoie un `cv::Rect` : la bande n'est pas copiée. |
| `_zone_avec_contenu` | `Preparation::zoneAvecContenu` | `PasDeContenu` devient `std::nullopt`. Mêmes seuils (0.1 / 0.5 / 32 px), nommés en constantes. |
| `_sous_pixel` | `Algorithme::sousPixel` | Identique. |
| `correlation_de_phase` | `Algorithme::correlationDePhase` | Même calcul, en double précision comme numpy, découpé en `spectre`, `spectreCroiseNormalise`, `filtreGaussien`, `partieReelleInverse` et `localiserPic`. La surface lissée n'est pas recentrée (`fftshift`) : ce n'est pas nécessaire. |
| `rapport_pics` | `Algorithme::rapportPics` | Indices modulo au lieu de `np.roll`. Le voisinage masqué est le même. |
| `mesurer_decalage` + `decalage_entre_bandes` | `Preparation::orienter` + `Preparation::bandes` + `Algorithme::mesurerGlobal` | L'échange A↔B pour haut/gauche se fait une seule fois (`orienter`) et s'annule dans `Sortie::annulerOrientation`. |
| `zone_commune` (partie calcul) | `Preparation::recouvrement` | Sert de zone de recherche des points. |
| — | `Algorithme::raffiner` + `Sortie` | **Nouveau** : génération et mise en forme des points de correspondance. |
| `profil_couture`, `variation_profil` | non porté | Diagnostics de déformation ; les points locaux + RANSAC jouent ce rôle. |
| `assembler`, `coller` | non porté | Rôle de `FusionStep`. |
| `metadonnees_tiff` | non porté | Rôle d'`ExportStep`. |

---

## 5. Paramètres

On les passe en JSON au moment de la création, par exemple `makeFeatureMatcher("phase-correlation", {{"nb_points", 1000}})`.

| Clé | Défaut | Effet |
|---|---|---|
| `lissage` | 3.0 | σ (px) du flou de la surface globale. Plus il est grand, moins le bruit haute fréquence pèse, mais le pic est plus large. |
| `seuil_rapport` | 1.5 | Rapport minimal entre le meilleur pic et le second, pour la mesure globale comme pour les mesures locales. |
| `nb_points` | 500 | Nombre maximal de points renvoyés (0 = translation seule, sans points). |
| `fenetre` | 64 | Côté (px) des patchs locaux. Plus grand : mesure plus stable mais plus lente et moins locale. |
| `lissage_local` | 1.5 | σ (px) du flou des surfaces locales. |
| `ecart_max` | 3.0 | Écart maximal (px) toléré entre la translation locale et la translation globale. |
| `espacement_min` | 16 | Distance minimale (px) entre deux points candidats. |

---

## 6. Choix et limites à connaître

- **Pleine dynamique, aucune perte.** Les tuiles 16 bits ne passent jamais en 8 bits. `enGris` fait un `convertTo(CV_64F)` sans mise à l'échelle, donc une valeur 0–65535 reste 0–65535. Les patchs passent ensuite en float32 pour `goodFeaturesToTrack` et `matchTemplate`, ce qui est sans perte pour du 16 bits (24 bits de mantisse). Les seules matrices 8 bits sont des masques (pixel valide ou non). Les tuiles d'origine ne sont jamais modifiées.
- **Pixels à 0 = vide seulement s'ils sont reliés au bord** (voir l'étape 0). Il reste une limite : on ne sait pas quels bords de la tuile sont l'extrémité du tableau. Une ombre à 0 coupée par un bord **intérieur** de la tuile (un bord partagé avec une tuile voisine) est donc encore considérée comme vide. Voir la remarque ci-dessous.
- **Exactement 0.** Seul un 0 exact est considéré comme vide. Un fond extérieur légèrement bruité (1, 2…) sera traité comme une donnée.
- **Pas de normalisation par percentiles.** La corrélation de phase ne garde que la phase et la NCC est normalisée : ni l'une ni l'autre ne change si l'on applique une transformation affine à l'intensité (`a·I + b`). L'étape n'apportait donc rien.
- **Test « zone uniforme » fait sur la bande** et non sur la tuile entière. C'est sans conséquence pratique : une bande uniforme ne peut de toute façon pas être mesurée.
- **Translation uniquement.** Le modèle global est une translation. Une légère rotation ou une déformation du support apparaît dans les points locaux, que RANSAC traitera ensuite.
- **Tuiles de même taille** obligatoires, comme dans le Python.
- **`Direction`** : la syntaxe `{ Top, Bottom, Left, Right }` est encore à confirmer avec l'équipe.

> **Remarque — correction possible, non implémentée**
>
> **Problème.** `zoneExterieure` lance le remplissage depuis les **quatre** bords de la tuile. Or un bord partagé avec une tuile voisine se trouve *à l'intérieur* du tableau. Une ombre bouchée à 0 que ce bord coupe est donc prise à tort pour de l'extérieur.
>
> **Idée.**
> 1. Le pipeline sait déjà quels côtés d'une tuile sont l'extrémité du tableau : ce sont ceux où la `Tuile` n'a pas de voisin (`getLeft()`, `getRight()`, `getTop()`, `getBottom()` valent `nullptr`).
> 2. On transmettrait cette information au matcher, par exemple avec deux champs dans `PairContext` :
>    ```cpp
>    struct Bords { bool haut = true, bas = true, gauche = true, droite = true; };  // côtés au bord du tableau
>    struct PairContext {
>        Direction dir;
>        float overlap;
>        Bords bordsA, bordsB;   // par défaut tous à true = comportement actuel
>    };
>    ```
> 3. `Preparation::orienter` les transmettrait à `zoneExterieure(tuile, bords)`, qui ne lancerait le remplissage que depuis ces côtés-là.
>
> **Pourquoi ça suffit.** Une zone extérieure qui s'étend jusqu'à un bord intérieur reste reliée à l'extrémité du tableau, donc elle serait toujours remplie entièrement. Avec les valeurs par défaut (`true` partout), le comportement serait identique à l'actuel.
>
> **Cas particulier.** Une tuile intérieure à la grille n'a aucun côté au bord du tableau. Pour un tableau non rectangulaire (cintré, par exemple), son fond noir éventuel ne serait alors pas détecté comme vide.
>
> **Pourquoi ce n'est pas fait.** La modification touche `PairContext`, l'interface commune à toutes les méthodes, dont la syntaxe n'est pas encore fixée. À décider avec l'équipe.

---

## 7. Comment vérifier

> ⚠ Le code **n'a pas encore été compilé** : OpenCV n'était pas installé sur la machine de développement (`OpenCV_DIR-NOTFOUND` dans CLion). Installer OpenCV (par ex. `vcpkg install opencv4`) puis configurer `OpenCV_DIR`.

1. **Parité avec le Python** : sur deux vraies tuiles, comparer `(out.y, out.x)` à `mesurer_decalage(A, B, "bas")` (ou `"droite"`, `"haut"`, `"gauche"`). Écart attendu : moins de 0,05 px.
2. **Test synthétique** : construire B = A décalée d'une valeur connue non entière (`cv::warpAffine`), puis vérifier `(x, y)` et que `pointsA[i] − pointsB[i] ≈ (x, y)` à moins de 0,5 px pour tous les points.
3. **Cas limites** :
   - bande noire → `match` renvoie `false`, sans exception ;
   - `Top`/`Left` → résultat de signe opposé à celui de `Bottom`/`Right` sur la paire inversée.
