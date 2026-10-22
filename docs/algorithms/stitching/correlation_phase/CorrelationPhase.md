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

Deux tuiles voisines A et B se recouvrent sur une bande (environ 20 % de leur taille). L'algorithme mesure **où se trouve B par rapport à A**, avec une précision inférieure au pixel, en deux passes : une première au pixel près sur les bandes de recouvrement nominal, une deuxième au sous-pixel sur la zone commune réelle (voir l'étape 1). Il renvoie :

1. **une translation globale** `(dx, dy)` : la position du coin haut-gauche de B dans le repère de A ;
2. **des paires de points** `pointsA[i] ↔ pointsB[i]` avec un **score de confiance** `scores[i]`. C'est le même format que les autres méthodes (SIFT, LightGlue…), ce qui permet de les passer à RANSAC / MAGSAC++.

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
class CorrelationPhase : public FeatureMatcher {
public:
    using Parametres = correlation_phase::Parametres;
    explicit CorrelationPhase(const Parametres& p = {});
    bool match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) override;
private:
    Parametres parametres;   // vérifiés une seule fois, dans le constructeur
};
```

- La classe hérite de `FeatureMatcher`, comme `SiftMatcher` et `LightGlueMatcher`. Le pipeline (`StitchingStep`) peut donc l'utiliser sans savoir de quelle méthode il s'agit.
- Elle est enregistrée dans `Factory.cpp` sous le nom **`"phase-correlation"`**, et c'est ce nom qui apparaît dans la liste de la GUI. La Factory la crée avec les valeurs par défaut : le JSON qu'elle reçoit est ignoré.
- `p` : les réglages (voir §5), sous forme de structure typée **déjà remplie par l'appelant**. Il n'y a pas d'entrée JSON. Sans argument, toutes les valeurs par défaut s'appliquent. **Un paramètre invalide lève `std::invalid_argument` à la construction**, comme un nom d'algorithme inconnu dans la Factory.

### Entrées de `match()`

| Argument | Type | Rôle |
|---|---|---|
| `a`, `b` | `cv::Mat` | Les deux tuiles déjà chargées en mémoire. Elles ne sont pas copiées : on n'en lit que des sous-zones (ROI). Gris ou couleur, 8 ou 16 bits. Elles doivent avoir la même taille. **Aucune conversion en 8 bits** : les bandes sont lues directement en float64, avec leur dynamique complète. |
| `ctx.dir` | `Direction` | Position de **B par rapport à A** : `Top`, `Bottom`, `Left` ou `Right`. |
| `ctx.overlap` | `float` | Recouvrement nominal, en fraction de la tuile (ex. `0.2`). Il doit être compris entre 0 (exclu) et 1. |
| `ctx.translation` | `std::optional<cv::Point2d>` | Coin de B dans A au pixel près, déjà mesuré par la première passe du pipeline (`EstimationRecouvrement`). Absent : `match` fait la première passe lui-même. Présent : elle est sautée, et la deuxième passe part de cette position. |

### Sortie : `Matches` (dans `core/Types.h`)

```cpp
struct Matches {
    std::vector<cv::Point2f> pointsA, pointsB;  // coordonnées dans la tuile A / la tuile B entière
    std::vector<float> scores;                  // confiance de chaque paire, entre 0 et 1
    float dx = 0.f, dy = 0.f;                   // coin haut-gauche de B dans le repère de A (px)
};
```

C'est le struct unifié avec SIFT et LightGlue (`pointsA`, `pointsB`, `scores`, `dx`, `dy`).

**Convention** : pour toute paire, `pointsA[i] − pointsB[i] ≈ (dx, dy)`. Un pixel `(X, Y)` de A correspond au pixel `(X − dx, Y − dy)` de B.

### Valeur de retour

- `true` : la translation `(dx, dy)` est valide. La liste de points peut être vide si la zone commune est trop étroite pour y placer des patchs.
- `false` : la mesure est impossible ou peu fiable. C'est le cas si les tuiles sont vides ou de tailles différentes, si les bandes n'ont pas de contenu commun (zone noire ou fond uni), si la translation réelle est hors de la marge du rail, si le pic de corrélation est ambigu, ou si `ctx.translation` est fausse de plus de 4 px (deuxième passe). `match` ne lève **aucune exception**.
- **`raisonEchec()`** : après un `false`, message qui explique pourquoi, avec les chiffres et la correction à apporter quand elle est connue. Exemple sur Philadelphia 35 → 36 (recouvrement réel 26 %, annoncé 20 %) : *« translation hors de la marge du rail : le meilleur pic est à 520 px du recouvrement nominal dans le sens du déplacement et 82 px en travers, au-delà de la marge du rail (±435 px) (rapport 14.46). Augmenter la marge (Parametres::marge >= 530 px) ou donner le recouvrement réel (environ 26 % au lieu de 20 %). »* Vide après un `true`.

---

## 3. Organisation du code et déroulement

### Les trois modules

Chaque module est une classe qui ne contient que des fonctions `static`. Son header liste **toutes** ses fonctions :
- `public` : ce que l'orchestrateur ou un autre module appelle ;
- `private` : les fonctions internes au module, plus ses constantes nommées (seuils, rayon du pic…).

Le compilateur interdit d'appeler une fonction `private` depuis un autre module. Toutes les structures sont regroupées dans `correlation_phase/Types.h` (namespace `stitching::matching::correlation_phase`).

| Module | Rôle | Public | Private |
|---|---|---|---|
| **`Preparation`** | Des tuiles brutes aux images de travail (gris float64, NaN = vide) | `verifierParametres`, `orienter`, `orienterTranslation`, `bandes`, `zoneCommune`, `recouvrement`, `valides`, `remplirVides` | `oppose`, `preparerBandes`, `bande`, `margeRail`, `zoneExterieure`, `enGris`, `valeurMax`, `zoneAvecContenu` |
| **`Algorithme`** | Uniquement du calcul sur des images préparées | `mesurerGlobal`, `affinerGlobal`, `diagnostiquerGlobal`, `raffiner` | corrélation : `correlationDePhase`, `spectreCroise`, `surfaceLissee`, `spectre`, `spectreCroiseNormalise`, `filtreGaussien`, `partieReelleInverse`, `zoneAutorisee`, `localiserPic`, `raffinerPic`, `noyauDft`, `decalageSigne`, `frequence`, `rapportPics` — local : `candidats`, `mesurerPoint`, `ncc` |
| **`Sortie`** | Mise en forme au format commun `Matches`, ou message d'échec | `construire`, `echecPaire`, `echecBandes`, `echecGlobal`, `echecZoneCommune`, `echecAffinage` | `nombre`, `trierEtLimiter`, `versTuiles`, `annulerOrientation` |

`Algorithme` utilise deux outils publics de `Preparation` (`valides`, `remplirVides`) pour traiter les pixels vides de ses patchs.

La première passe est dans une classe à part, **`EstimationRecouvrement`** (`algorithms/stitching/recouvrement/`, voir [sa page](../recouvrement/EstimationRecouvrement.md)), parce qu'elle sert à toutes les méthodes d'appariement. Elle enchaîne `Preparation::orienter`, `Preparation::bandes` et `Algorithme::mesurerGlobal` au pixel près. `CorrelationPhase` en garde une instance (`premierePasse`), utilisée quand `ctx.translation` est absente. En retour, `Preparation` utilise `EstimationRecouvrement::zonesCommunes` pour découper la zone commune réelle.

### Les structures (`Types.h`)

| Structure | Produite par | Contenu |
|---|---|---|
| `Parametres` | l'appelant (vérifiés par `Preparation::verifierParametres`) | Réglages (§5) |
| `PaireOrientee` | `Preparation::orienter` | A et B ramenées au cas « B en bas / à droite », masques extérieurs, indicateur `inversee` |
| `Bandes` | `Preparation::bandes`, `Preparation::zoneCommune` | Bandes de recouvrement nominal (première passe) ou zone commune réelle (deuxième passe), en gris + position de B si le résidu est nul + marge de recherche (marge du rail, ou ±4 px) |
| `Correlation` | `Algorithme` (interne) | Décalage `(dy, dx)`, hauteur du pic, surface lissée, zone où le pic est cherché |
| `Diagnostic` | `Algorithme::diagnostiquerGlobal` | Après un refus de `mesurerGlobal` : rapport dans la marge, meilleur pic sur toute la surface, son rapport, s'il est hors marge |
| `Mesure` | `Algorithme::mesurerGlobal`, `Algorithme::affinerGlobal` | Translation globale `(tx, ty)` + rapport des pics (première passe ; NaN après la deuxième) |
| `Recouvrement` | `Preparation::recouvrement` | Zone commune réelle vue dans A et dans B, son origine dans A, la translation arrondie, la marge |
| `Correspondance` | `Algorithme::raffiner` | Point dans la zone commune + translation locale + confiance |

### Enchaînement dans `match()` (`CorrelationPhase.cpp`)

```
match(a, b, ctx)
 │
 ├─ 1. Première passe, si ctx.translation est absente : EstimationRecouvrement::estimer
 │   ├─ Preparation::orienter ── vérifie la paire ; si B est en haut / à gauche, échange A et B
 │   │      └─ zoneExterieure ×2   pixels noirs reliés au bord de la tuile → « vides »
 │   ├─ Preparation::bandes ──── bande du bas/de droite de A, du haut/de gauche de B (recouvrement nominal)
 │   │      ├─ bande, enGris       découpe, gris float64, extérieur et saturés → NaN, érosion 4 px
 │   │      ├─ fraction valide     ≥ 25 % de pixels exploitables communs    (sinon → false)
 │   │      ├─ zoneAvecContenu     retire les bords vides communs           (sinon → false)
 │   │      └─ margeRail           décalage résiduel maximal permis par le rail
 │   ├─ Algorithme::mesurerGlobal  position de B au pixel près
 │   │      ├─ correlationDePhase  FFT → spectre croisé → pic dans la marge (pas de sous-pixel)
 │   │      └─ rapportPics         le pic est-il franc (dans la marge) ?    (sinon → false)
 │   └─ en cas d'échec seulement : Sortie::echec… → raisonEchec()
 │          └─ diagnostiquerGlobal meilleur pic sans marge : franc et hors marge → « augmenter la marge »
 │
 ├─ 2. Deuxième passe
 │   ├─ Preparation::orienter
 │   ├─ Preparation::zoneCommune   zone commune réelle à la position de la passe 1  (sinon → false)
 │   │      └─ EstimationRecouvrement::zonesCommunes, enGris, fraction valide, zoneAvecContenu
 │   └─ Algorithme::affinerGlobal  translation au 1/100 px, pic cherché à ±4 px   (pic au bord → false)
 │          └─ correlationDePhase  FFT → spectre croisé → pic dans ±4 px → sous-pixel DFT
 │
 ├─ Preparation::recouvrement ── zone commune réelle avec (x, y) arrondi   (trop étroite → pas de points)
 │
 ├─ Algorithme::raffiner
 │      ├─ candidats               coins texturés (goodFeaturesToTrack), espacés d'au moins 16 px
 │      └─ mesurerPoint ×N         patch 64×64 : correlationDePhase locale, filtres, ncc
 │
 └─ Sortie::construire
        ├─ trierEtLimiter          confiance décroissante, nbPoints meilleurs
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

Dans `enGris`, deux autres sortes de pixels sont retirées (NaN) :

- **les pixels saturés** : gris ≥ 99,8 % du maximum du type (65 535 en 16 bits, 255 en 8 bits, 1 pour une tuile flottante). Un reflet écrêté n'a plus de texture, et la tache claire n'est pas au même endroit dans les deux tuiles : elle attirerait le pic vers un faux décalage ;
- **une bordure de 4 px** autour de toute zone retirée (extérieur ou saturation), par érosion du masque des pixels valides. La transition entre le fond et la peinture (quelques pixels sombres mais non nuls) est au même endroit dans les deux tuiles : sans érosion, elle formerait un faux détail commun, de décalage nul. Le bord de l'image n'est pas érodé.

Enfin, `bandes` refuse la mesure si moins de 25 % des pixels sont exploitables dans les deux bandes à la fois (`fractionValideMin`).

### Étape 1 : la translation globale, en deux passes

**Pourquoi on ne corrèle que les bandes.** Hors du recouvrement, le contenu de A et de B est différent et brouillerait le pic. Pour B en bas de A, on corrèle donc la bande du bas de A avec la bande du haut de B. Le décalage mesuré est un **résidu** autour de la position nominale `ty0 = hauteur − hauteur_bande`.

**Pourquoi deux passes.** La bande nominale (20 %) ne coïncide pas avec le vrai recouvrement quand le rail s'écarte du nominal. Avec un recouvrement réel o_r et un nominal o_n, seule une part `1 − |o_r − o_n| / o_n` de chaque bande a une correspondance dans l'autre (70 % pour 26 % réels annoncés à 20 %). Le reste abaisse le pic sans le déplacer, ce qui suffit pour une mesure au pixel. Mais quand o_r > o_n, une partie du vrai recouvrement n'est pas utilisée.

1. **Première passe** (`EstimationRecouvrement`) : corrélation sur les bandes nominales, pic cherché dans la marge du rail, **au pixel près** (pas de sous-pixel). Elle donne la position de B, donc le recouvrement réel : `o_r = 1 − |ty| / hauteur` (ou `1 − |tx| / largeur`). C'est elle qui contrôle l'ambiguïté (`rapportPics`) et qui explique les échecs (`diagnostiquerGlobal`).
2. **Deuxième passe** (`Preparation::zoneCommune` + `Algorithme::affinerGlobal`) : B est placée à cette position arrondie, et on corrèle l'**intersection exacte** des deux tuiles. Les deux images montrent la même partie de la peinture, à un pixel près : rien n'est perdu quand o_r > o_n, et aucun contenu sans correspondance n'affaiblit le pic quand o_r < o_n. Le pic est cherché à ±4 px (`MARGE_DEUXIEME_PASSE`), puis raffiné au 1/100 px. Si le pic entier tombe sur le bord de cette marge, la position de la première passe était fausse de plus de 4 px : `false` (`echecAffinage`), plutôt qu'une translation tronquée au bord. Le test porte sur le pic entier, car le raffinement peut ramener un pic de bord jusqu'à 0,75 px vers l'intérieur. Le second pic n'est pas contrôlé : la marge est plus petite que le voisinage exclu (±10 px), le test passerait toujours.

Le coût total reste proche d'une seule passe : le sous-pixel DFT, le plus cher, n'est calculé qu'une fois, sur la zone commune. La première passe ne coûte qu'une FFT.

Le calcul de la corrélation est le même dans les deux passes.

**Principe de la corrélation de phase** (`correlationDePhase`) :

1. Les pixels NaN (vides) sont remplacés par la moyenne de l'image, puis on soustrait la moyenne.
2. On multiplie l'image par une **fenêtre de Hann**. Sans elle, les bords de l'image créent un faux pic à décalage nul.
3. On calcule les FFT `Fa` et `Fb`, puis le **spectre croisé** `Fa · conj(Fb)`.
4. On le **normalise** (division par son module) : seule la phase est conservée, c'est-à-dire seulement l'information de position. C'est ce qui rend la méthode insensible aux différences d'éclairage.
5. On multiplie par une **gaussienne** dans le domaine fréquentiel, ce qui revient à flouter la surface de corrélation (σ = `lissage`). Cela atténue les faux pics fins dus au grain du capteur.
6. La FFT inverse donne une **surface de corrélation** : son maximum est situé au décalage cherché. La FFT est périodique : un indice au-delà de la moitié de l'image correspond à un décalage négatif (`decalageSigne`).
7. Le pic entier est cherché sur la surface lissée, **seulement dans la marge** (marge du rail en première passe, ±4 px en deuxième passe) (`zoneAutorisee`, `localiserPic`) : les décalages résiduels `|dx|, |dy| ≤ marge`. Les bandes étant prises au recouvrement nominal, le décalage résiduel attendu est nul. Un pic plus haut au-delà de la marge ne peut pas être le bon : il vient d'un motif répétitif ou d'un fond uni, et le rail ne peut pas s'être trompé d'autant. Marge par défaut : 5 % de la tuile dans le sens du déplacement, au moins 48 px (même règle que la chaîne Python de référence).
8. En deuxième passe, la fraction de pixel est calculée par **DFT suréchantillonnée** (Guizar-Sicairos, 2008 ; `raffinerPic`) : la surface **brute** est évaluée exactement sur une grille de 150 × 150 points au pas de 1/100 px, sur ±0,75 px autour du pic entier. Il suffit de deux produits matriciels `Ky · croise · Kxᵀ` (`noyauDft`), sans suréchantillonner toute l'image. On raffine sur la surface brute, et non sur la surface lissée, parce que le lissage déplace légèrement le sommet du pic. L'ancienne parabole sur trois valeurs entières était, elle, tirée vers l'entier le plus proche.

```
  surface lissée               spectre brut, autour du pic entier
  ┌───────────────────┐        ┌─────────────┐
  │      ┌─────┐      │        │ · · · · · · │  grille 1/100 px
  │      │  ●  │ marge│  ───►  │ · · ✚ · · · │  sur ±0,75 px
  │      └─────┘      │        │ · · · · · · │
  │ ●  pic hors marge │        └─────────────┘
  └───────────────────┘
```

**Contrôle de fiabilité** (`rapportPics`) : on divise la hauteur du meilleur pic par celle du second, cherché hors d'un carré de ±10 px autour du premier. Les deux sont cherchés **dans la marge** : un pic hors marge ne rend pas la mesure ambiguë, puisqu'il ne peut pas être le bon. Un rapport proche de 1 signifie que deux positions se valent : la mesure est rejetée (seuil 1.5).

### Étapes 2 à 5 : les points de correspondance

La translation globale ne suffit pas pour le format « points d'intérêt ». Il faut aussi des paires de points **mesurées indépendamment**, pour que RANSAC puisse détecter une rotation ou une déformation et écarter les mauvais points.

- **Candidats** : `cv::goodFeaturesToTrack` (Shi-Tomasi) choisit des coins bien texturés dans la zone commune. On en demande 3 × `nbPoints`, espacés d'au moins `espacementMin` px, et assez loin des bords pour que les patchs restent dans l'image.
- **Mesure locale** : pour chaque candidat, on prend un patch de `fenetre`×`fenetre` px dans A et le patch correspondant dans B, selon la translation arrondie. On relance `correlationDePhase` sur ces deux patchs, ce qui donne un petit résidu local, au 1/100 px lui aussi. Ici le pic est cherché **sans marge** : limiter la recherche à ±`ecartMax` masquerait tout le voisinage du pic et viderait le test du second pic. L'écart à la translation globale est contrôlé juste après.
- **Filtres** : le point est rejeté si le patch contient plus de 50 % de pixels vides, si la translation locale s'écarte de plus de `ecartMax` px de la globale, ou si le pic local est ambigu.
- **Confiance** : NCC (corrélation normalisée, `cv::matchTemplate` en mode `TM_CCOEFF_NORMED`) entre les deux patchs une fois recalés. 1 signifie des patchs identiques ; la valeur est ramenée à 0 si elle est négative.
- **Sélection** : tri par confiance décroissante, puis on garde au plus `nbPoints` points.

Coordonnées : `pointsA[i]` est entier (centre du patch dans A) ; `pointsB[i]` est sous-pixel (`pointsA[i] − translation locale`).

---

## 4. Correspondance avec le Python

| Python (`correlation_phase.py`) | C++ | Remarque |
|---|---|---|
| `charger` | `Preparation::zoneExterieure` + `enGris` | Lecture de fichier retirée (rôle d'`ImportStep`). Pas de normalisation par percentiles (voir §6). **Amélioration** : en Python, tous les pixels à 0 deviennent NaN ; en C++, seulement ceux qui sont reliés au bord de la tuile. |
| `bande` | `Preparation::bande` | Renvoie un `cv::Rect` : la bande n'est pas copiée. |
| `_zone_avec_contenu` | `Preparation::zoneAvecContenu` | `PasDeContenu` devient `std::nullopt`. Mêmes seuils (0.1 / 0.5 / 32 px), nommés en constantes. |
| `_sous_pixel` | remplacé par `Algorithme::raffinerPic` | Parabole remplacée par la DFT suréchantillonnée (voir ci-dessous). |
| `correlation_de_phase` | `Algorithme::correlationDePhase` | Même calcul, en double précision comme numpy, découpé en `spectre`, `spectreCroiseNormalise`, `filtreGaussien`, `partieReelleInverse`, `localiserPic` et `raffinerPic`. La surface lissée n'est pas recentrée (`fftshift`) : ce n'est pas nécessaire. |
| `rapport_pics` | `Algorithme::rapportPics` | Indices modulo au lieu de `np.roll`. Le voisinage masqué est le même ; les pics sont cherchés dans la marge. |
| `mesurer_decalage` + `decalage_entre_bandes` | `Preparation::orienter` + `Preparation::bandes` + `Algorithme::mesurerGlobal` | L'échange A↔B pour haut/gauche se fait une seule fois (`orienter`) et s'annule dans `Sortie::annulerOrientation`. |
| `zone_commune` (partie calcul) | `Preparation::recouvrement` | Sert de zone de recherche des points. |
| — | `Algorithme::raffiner` + `Sortie` | **Nouveau** : génération et mise en forme des points de correspondance. |
| `profil_couture`, `variation_profil` | non porté | Diagnostics de déformation ; les points locaux + RANSAC jouent ce rôle. |
| `assembler`, `coller` | non porté | Rôle de `FusionStep`. |
| `metadonnees_tiff` | non porté | Rôle d'`ExportStep`. |

Trois éléments viennent de la version révisée du chef de projet (notebook `Stitching_Python/01_correlation_de_phase.ipynb`, classe `CorrelationDePhase`) et non du prototype :

| Notebook | C++ | Remarque |
|---|---|---|
| marge du rail (`autorise`, `paire.marge`) | `Preparation::margeRail`, `Algorithme::zoneAutorisee` | Même marge par défaut. Ici le décalage attendu est nul (bandes au recouvrement nominal). |
| `raffiner_pic`, `dft_surechantillonnee` (`sous_pixel="dft"`, `raffiner_sur="brut"`, `facteur_dft=100`) | `Algorithme::raffinerPic`, `noyauDft` | Formulation vérifiée identique en numpy (mêmes valeurs à 0,01 px). Les options `parabole`/`aucun` et `raffiner_sur="lisse"` de l'étude ne sont pas portées. |
| `masque_zone` (saturation 0,998, érosion 4 px), `fraction_valide_min` | `Preparation::enGris`, `Preparation::bandes` | Le fond reste détecté par le remplissage depuis le bord (étape 0), et non par le seuil sur une moyenne 7 × 7 du notebook : un noir isolé dans la peinture reste une donnée. |

---

## 5. Paramètres

Ce sont les champs de la structure `CorrelationPhase::Parametres` (`correlation_phase/Types.h`). L'appelant la remplit et la passe au constructeur ; chaque champ non modifié garde sa valeur par défaut :

```cpp
CorrelationPhase::Parametres p;   // valeurs par défaut
p.nbPoints = 1000;
CorrelationPhase algo(p);
```

| Champ | Défaut | Effet |
|---|---|---|
| `lissage` | 3.0 | σ (px) du flou de la surface globale. Plus il est grand, moins le bruit haute fréquence pèse, mais le pic est plus large. |
| `seuilRapport` | 1.5 | Rapport minimal entre le meilleur pic et le second, pour la mesure globale comme pour les mesures locales. |
| `nbPoints` | 10 | Nombre maximal de points renvoyés (0 = translation seule, sans points). |
| `fenetre` | 64 | Côté (px) des patchs locaux. Plus grand : mesure plus stable mais plus lente et moins locale. |
| `lissageLocal` | 1.5 | σ (px) du flou des surfaces locales. |
| `ecartMax` | 3.0 | Écart maximal (px) toléré entre la translation locale et la translation globale. |
| `espacementMin` | 16 | Distance minimale (px) entre deux points candidats. |
| `marge` | 0 | Écart maximal (px), sur chaque axe, entre la translation globale et le recouvrement nominal (incertitude du rail, première passe). 0 = automatique : 5 % de la tuile dans le sens du déplacement, au moins 48 px. |
| `fractionValideMin` | 0.25 | Part minimale de pixels exploitables (ni extérieurs, ni saturés) communs aux deux bandes. |
| `facteurDft` | 100 | Précision du sous-pixel : 1/`facteurDft` px (deuxième passe et points locaux). Le coût du raffinement est proportionnel à ce facteur. 1 = au pixel, sans raffinement (c'est ce que fait la première passe). |

La marge de la deuxième passe (±4 px autour de la position de la première) est une constante, `Preparation::MARGE_DEUXIEME_PASSE`, et non un paramètre.

---

## 6. Choix et limites à connaître

- **Pleine dynamique, aucune perte.** Les tuiles 16 bits ne passent jamais en 8 bits. `enGris` fait un `convertTo(CV_64F)` sans mise à l'échelle, donc une valeur 0–65535 reste 0–65535. Les patchs passent ensuite en float32 pour `goodFeaturesToTrack` et `matchTemplate`, ce qui est sans perte pour du 16 bits (24 bits de mantisse). Les seules matrices 8 bits sont des masques (pixel valide ou non). Les tuiles d'origine ne sont jamais modifiées.
- **Pixels à 0 = vide seulement s'ils sont reliés au bord** (voir l'étape 0). Il reste une limite : on ne sait pas quels bords de la tuile sont l'extrémité du tableau. Une ombre à 0 coupée par un bord **intérieur** de la tuile (un bord partagé avec une tuile voisine) est donc encore considérée comme vide. Voir la remarque ci-dessous.
- **Exactement 0.** Seul un 0 exact est considéré comme vide. Un fond extérieur légèrement bruité (1, 2…) sera traité comme une donnée. L'érosion de 4 px en atténue l'effet au bord du fond, sans le supprimer.
- **Position fournie par le pipeline.** Si `ctx.translation` est fausse de plus de 4 px (par exemple une valeur déduite de la médiane d'une rangée, et non mesurée sur la paire), la deuxième passe échoue (`false`, « le pic touche le bord de sa marge »). Pour l'accepter, il faudrait agrandir `MARGE_DEUXIEME_PASSE`.
- **Marge du rail fixe.** Elle suppose que la translation réelle est à moins de `marge` px du recouvrement nominal donné par `ctx.overlap`. Si le rail ou le recouvrement annoncé est plus faux que cela, la mesure trouve un pic faux dans la marge, ou échoue. Augmenter `marge` dans ce cas. Quand elle échoue, `raisonEchec()` le dit explicitement : le meilleur pic est alors cherché sur toute la surface, et s'il est franc et hors marge, le message donne la marge à utiliser (écart mesuré + 10 px) et le recouvrement réel estimé. Ce diagnostic ne coûte rien quand la mesure réussit : une corrélation de plus, seulement en cas d'échec. **Limite** : si un faux pic franc existe dans la marge, la mesure réussit avec une valeur fausse, et rien ne le signale.
- **Coût du sous-pixel.** Le raffinement DFT coûte environ 150 × (nombre de pixels de la bande) multiplications complexes, soit de l'ordre de 10⁹ pour une bande de 1035 × 5176 px. **Le temps n'a pas été mesuré** (code non compilé). S'il est trop long : baisser `facteurDft` (le coût est proportionnel), ou raffiner en deux passes (1/10 px sur ±0,75 px, puis 1/100 px sur ±0,1 px), environ 5 fois moins cher.
- **Pas de normalisation par percentiles.** La corrélation de phase ne garde que la phase et la NCC est normalisée : ni l'une ni l'autre ne change si l'on applique une transformation affine à l'intensité (`a·I + b`). L'étape n'apportait donc rien.
- **Test « zone uniforme » fait sur la bande** et non sur la tuile entière. C'est sans conséquence pratique : une bande uniforme ne peut de toute façon pas être mesurée.
- **Translation uniquement.** Le modèle global est une translation. Une légère rotation ou une déformation du support apparaît dans les points locaux, que RANSAC traitera ensuite.
- **Tuiles de même taille** obligatoires, comme dans le Python.
- **`Direction`** : la syntaxe `{ Top, Bottom, Left, Right }` est encore à confirmer avec l'équipe.
- **`distorsion optique`** : La distorsion est une aberration géométrique qui se produit lorsque le grossissement d'un objectif varie en s'éloignant de l'axe optique. Comme les bords du capteur photo (plat) sont plus éloignés du centre de la lentille que le milieu du capteur, l'image subit une réduction de grossissement à sa périphérie.--> à coriger avec pré traitement ou autre ?

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

Les points 2 et 3 sont automatisés dans `tests/test_correlation_phase.cpp` (cible `test_correlation_phase`, lancée par `ctest`) ; le point 1 par `tests/comparer_python.py`. Résultats du 12/10/2026 (OpenCV 4.14, MSVC, Release) en fin de section.

1. **Parité avec le Python** : sur deux vraies tuiles, comparer `(out.y, out.x)` à `mesurer_decalage(A, B, "bas")` (ou `"droite"`, `"haut"`, `"gauche"`). Les deux versions ne font pas le même sous-pixel (parabole sur la surface lissée en Python, DFT sur la surface brute ici) : des écarts de quelques dixièmes de pixel sont normaux, pas des écarts de plusieurs pixels.
2. **Test synthétique** : découper A et B dans une même image à un décalage connu non entier, puis vérifier `(x, y)` et que `pointsA[i] − pointsB[i] ≈ (x, y)` à moins de 0,5 px pour tous les points. **B doit être décalée par la FFT** (théorème du décalage), pas par `cv::warpAffine` : l'interpolation bicubique déforme la phase et fausse la vérité de 0,1 à 0,25 px.
3. **Cas limites** :
   - bande noire → `match` renvoie `false`, sans exception ;
   - bande surtout saturée (plus de 75 % de pixels au maximum) → `false` ;
   - `Top`/`Left` → résultat de signe opposé à celui de `Bottom`/`Right` sur la paire inversée ;
   - motif périodique de période supérieure à la marge : le pic retenu doit être celui de la marge, même si une copie du motif donne un pic plus haut au-delà ;
   - recouvrement réel de 27 % annoncé à 20 % (70 px hors de la marge de 50 px) → `false`, et `raisonEchec()` dit d'augmenter la marge ; avec `marge = 100`, la translation est juste ;
   - **deux passes**, recouvrements réels de 23 % et 17 % annoncés à 20 % : la première passe donne une position entière à moins de 1 px de la vérité et le recouvrement réel au pixel près ; `Top(B, A) = −Bottom(A, B)` ; la zone commune fait toute la hauteur du recouvrement réel (230 ou 170 px, contre 200 px pour la bande nominale) ; `match` est juste au 1/100 px ; avec `ctx.translation` fournie, le résultat est identique au bit près ; fausse de (+2, −2) px, elle est rattrapée ; fausse de 10 px, ou sans recouvrement → `false`, avec la raison.
4. **Données de contrôle à vérité exacte** : les jeux `C01` à `C10` (générés par le notebook `00_donnees_controle.ipynb`, positions exactes dans `verite.json`). Sur ces jeux, le notebook `01_correlation_de_phase.ipynb` annonce une erreur médiane de 0,010 à 0,020 px. Ses 4 erreurs graves (C04 fond uni, C05 reflet, C10 cadre) y sont écartées par le contrôle NCC commun à toutes les méthodes, qui ne fait pas partie de ce module : en C++, ces paires peuvent encore être renvoyées avec `true`. **Non fait** : ces jeux ne sont pas dans `KIKIRPA-Research/data/`.

### Résultats

Les mesures ci-dessous ont été faites avec `nbPoints = 500` (défaut de l'époque ; il est maintenant de 10).

**Vérité exacte** (`test_correlation_phase`, tuiles de 1000 px, recouvrement 20 %, décalages résiduels non entiers de 3 à 31 px, 4 directions), sur trois sources : texture générée, `synthetic_v1.0/NOM_0001_full.tif`, tuile réelle Savery 03.

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

Détail du temps sur Savery 01 → 02 : 8,1 s avec `nbPoints = 500`, 2,5 s en translation seule (`nbPoints = 0`, autant que le Python), 1,6 s en translation seule au 1/10 px. **Les 500 points locaux coûtent donc environ 5,5 s**, presque entièrement dans le sous-pixel DFT au 1/100 de chaque patch : avec `facteurDft = 10`, la paire entière prend 2,5 s.

**Deux passes** (20/10/2026, build Debug, `nbPoints = 10`) : sur Savery 01 → 02, 03 → 04 et 04 → 05, la translation change d'au plus 0,06 px par rapport à la version à une passe, avec le même nombre de points (10, 10 et 2). Le temps de `match()` augmente de 15 à 25 % en Debug (par exemple 17,0 → 19,0 s sur 01 → 02). La cause n'a pas été mesurée. Piste probable : `Preparation::orienter` est appelé deux fois quand `match()` fait aussi la première passe, et `zoneExterieure` est donc recalculée sur les deux tuiles entières. **Non mesuré en Release.**
