# CorrelationPhase — corrélation de phase entre deux tuiles

Fichiers :

- `include/algorithms/matching/CorrelationPhase.h` : la déclaration (l'interface que voit le reste du pipeline)
- `src/algorithms/matching/CorrelationPhase.cpp` : l'implémentation (port de `KIKIRPA-Research/FeaturesExtraction/Correlation-de-phase/correlation_phase.py`)

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

## 2. Le fichier `.h` : l'interface

```cpp
class CorrelationPhase : public FeatureMatcher {
    Json params;
public:
    explicit CorrelationPhase(const Json& params = {});
    bool match(const cv::Mat& a, const cv::Mat& b, const PairContext& ctx, Matches& out) override;
};
```

- La classe hérite de `FeatureMatcher`, comme `SiftMatcher` et `LightGlueMatcher`. Le pipeline (`StitchingStep`) peut donc l'utiliser sans savoir de quelle méthode il s'agit.
- Elle est enregistrée dans `Factory.cpp` sous le nom **`"phase-correlation"`**, et c'est ce nom qui apparaît dans la liste de la GUI.
- `params` : un objet JSON facultatif pour régler l'algorithme (voir §5). Si on ne le donne pas, les valeurs par défaut s'appliquent.

### Entrées de `match()`

| Argument | Type | Rôle |
|---|---|---|
| `a`, `b` | `cv::Mat` | Les deux tuiles déjà chargées en mémoire. Elles ne sont pas copiées : on n'en lit que des sous-zones (ROI). Gris ou couleur, 8 ou 16 bits. Elles doivent avoir la même taille. |
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
- `false` : la mesure est impossible ou peu fiable. C'est le cas si les tuiles sont vides ou de tailles différentes, si les bandes n'ont pas de contenu commun (zone noire ou fond uni), ou si le pic de corrélation est ambigu. **Aucune exception n'est levée.**

---

## 3. Le fichier `.cpp` : le déroulement

Tout le travail se fait dans un namespace anonyme : les fonctions qu'il contient sont internes au fichier. Seuls le constructeur et `match()` sont publics.

### Vue d'ensemble

```
match(a, b, ctx)
 │
 ├─ B en haut / à gauche ? ──► on échange A et B, on calcule « en bas / à droite »,
 │                             puis on inverse le signe de (x, y) et on échange pointsA/pointsB
 │
 └─ apparier(A, B, Bottom|Right)
     │
     ├─ 1. mesurerDecalage ─────────── translation globale (x, y) sur les bandes de recouvrement
     │      ├─ bande()                 découpe la bande du bas/de droite de A, du haut/de gauche de B
     │      ├─ enGris()                gris, float64, pixels noirs → NaN
     │      ├─ zoneAvecContenu()       retire les bords vides communs aux deux bandes
     │      ├─ correlationDePhase()    FFT → spectre croisé → pic
     │      └─ rapportPics()           le pic est-il franc ? (sinon → false)
     │
     ├─ 2. zone commune réelle         rectangle où A et B se superposent avec (x, y) arrondi
     │
     ├─ 3. goodFeaturesToTrack         candidats : coins texturés, espacés d'au moins 16 px
     │
     ├─ 4. pour chaque candidat        patch 64×64 dans A et dans B
     │      ├─ correlationDePhase()    translation locale
     │      ├─ filtres                 écart à (x, y) ≤ 3 px, pic local franc
     │      └─ matchTemplate (NCC)     confiance entre 0 et 1
     │
     └─ 5. tri par confiance           on garde les nb_points meilleurs
```

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

| Python (`correlation_phase.py`) | C++ (`CorrelationPhase.cpp`) | Remarque |
|---|---|---|
| `charger` | `enGris` | Lecture de fichier retirée (rôle d'`ImportStep`). Pas de normalisation par percentiles (voir §6). |
| `bande` | `bande` | Renvoie un `cv::Rect` : la bande n'est pas copiée. |
| `_zone_avec_contenu` | `zoneAvecContenu` | `PasDeContenu` devient `std::nullopt`. Mêmes seuils (0.1 / 0.5 / 32 px). |
| `_sous_pixel` | `sousPixel` | Identique. |
| `correlation_de_phase` | `correlationDePhase` | Même calcul, en double précision comme numpy. La surface lissée n'est pas recentrée (`fftshift`) : ce n'est pas nécessaire. |
| `rapport_pics` | `rapportPics` | Indices modulo au lieu de `np.roll`. Le voisinage masqué est le même. |
| `mesurer_decalage` + `decalage_entre_bandes` | `mesurerDecalage` + `match` | L'échange A↔B pour haut/gauche se fait une seule fois, dans `match`. |
| — | `apparier` | **Nouveau** : génération des points de correspondance. |
| `profil_couture`, `variation_profil` | non porté | Diagnostics de déformation ; les points locaux + RANSAC jouent ce rôle. |
| `assembler`, `coller`, `zone_commune` | non porté | Rôle de `FusionStep`. |
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

- **Pixels à 0 = vide.** Comme en Python, un pixel exactement noir est considéré comme hors de l'œuvre (NaN). Une ombre bouchée à 0 sera donc ignorée.
- **Pas de normalisation par percentiles.** La corrélation de phase ne garde que la phase et la NCC est normalisée : ni l'une ni l'autre ne change si l'on applique une transformation affine à l'intensité (`a·I + b`). L'étape n'apportait donc rien.
- **Test « zone uniforme » fait sur la bande** et non sur la tuile entière. C'est sans conséquence pratique : une bande uniforme ne peut de toute façon pas être mesurée.
- **Translation uniquement.** Le modèle global est une translation. Une légère rotation ou une déformation du support apparaît dans les points locaux, que RANSAC traitera ensuite.
- **Tuiles de même taille** obligatoires, comme dans le Python.
- **`Direction`** : la syntaxe `{ Top, Bottom, Left, Right }` est encore à confirmer avec l'équipe.

---

## 7. Comment vérifier

> ⚠ Le code **n'a pas encore été compilé** : OpenCV n'était pas installé sur la machine de développement (`OpenCV_DIR-NOTFOUND` dans CLion). Installer OpenCV (par ex. `vcpkg install opencv4`) puis configurer `OpenCV_DIR`.

1. **Parité avec le Python** : sur deux vraies tuiles, comparer `(out.y, out.x)` à `mesurer_decalage(A, B, "bas")` (ou `"droite"`, `"haut"`, `"gauche"`). Écart attendu : moins de 0,05 px.
2. **Test synthétique** : construire B = A décalée d'une valeur connue non entière (`cv::warpAffine`), puis vérifier `(x, y)` et que `pointsA[i] − pointsB[i] ≈ (x, y)` à moins de 0,5 px pour tous les points.
3. **Cas limites** :
   - bande noire → `match` renvoie `false`, sans exception ;
   - `Top`/`Left` → résultat de signe opposé à celui de `Bottom`/`Right` sur la paire inversée.
