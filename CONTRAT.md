# Contrats et guide de lecture du pipeline

Ce document explique les contrats de donnees, la configuration, le role des
fichiers et le fonctionnement du code actuellement present dans le depot. Il
decrit l'implementation, pas une approbation metier : les conventions restent
a faire confirmer par les responsables concernes.

## 1. Vue d'ensemble

Le backend execute une liste ordonnee de steps. Chaque step lit des donnees
typees dans le `DataStore`, effectue une operation et y publie ses sorties.
Les chemins principaux sont :

```text
pipeline.json
    -> src/main.cpp
    -> buildPipeline() et registre de steps
    -> Pipeline::run()
    -> import
    -> stitching
    -> absolutePositioning
    -> intensityCorrection
    -> seamline
    -> fusion
    -> export
```

Le JSON ne transporte pas les images ni les resultats intermediaires : il
decrit les noms de steps et leurs parametres. Les objets C++ restent en memoire
dans le `DataStore` pendant une execution. L'ordre du tableau `steps` est donc
l'ordre reel de traitement.

## 2. Contrats de donnees partages

Les types sont declares dans `include/Backend/core/Types.h`. Les noms ci-dessous
sont les cles communes definies dans `stitching::keys` ; producteurs et
consommateurs doivent employer la meme cle et le meme type C++.

| Cle | Type C++ | Produit par | Lu par |
|---|---|---|---|
| `tiles` | `Tiles` = `vector<TileImage>` | `import` | `stitching`, `absolutePositioning`, `intensityCorrection`, `seamline`, `fusion` |
| `matches` | `Matches` = `vector<PairMatches>` | `stitching` | actuellement aucun step |
| `alignments` | `Alignments` = `vector<PairAlignment>` | `stitching` | `absolutePositioning` |
| `positions` | `Positions` = `vector<TilePosition>` | `absolutePositioning` | `seamline`, `fusion` |
| `corrections` | `LightCorrections` = `vector<LightCorrection>` | `intensityCorrection` | `fusion` |
| `territories` | `Territories` = `vector<TileTerritory>` | `seamline` | `fusion` |
| `mosaic` | `Mosaic` | `fusion` | `export` |

### Signification des types

- **Tuile — `TileImage`** : `path` est le fichier source ; `col` et `row`
  identifient la case de grille ; `image` est la matrice OpenCV chargee sans
  conversion (`IMREAD_UNCHANGED`). Une tuile peut donc etre en 8 ou 16 bits.
- **Paire — `TilePair`** : `first` et `second` sont des indices dans `Tiles`.
  `direction` vaut `Right` ou `Below` et indique le voisinage dans la grille.
- **Correspondances — `PairMatches`** : `firstPoints[i]` et
  `secondPoints[i]` sont les deux points associes pour une paire. Le vecteur
  `Matches` garde ensemble les points et l'identite de leur paire.
- **Transformation — `PairAlignment`** : `secondToFirst` est une homographie
  3×3 en double qui transforme les pixels de la seconde tuile vers ceux de la
  premiere. `inliers[i]` vaut vrai/non-zero si la correspondance d'indice `i`
  a ete retenue par RANSAC.
- **Position — `TilePosition`** : `tileToWorld` est une homographie 3×3 en
  double qui transforme les pixels de la tuile vers le repere mosaïque.
- **Correction lumineuse — `LightCorrection`** : `gain` et `bias` sont des
  valeurs par canal. Fusion applique `pixel * gain + bias`. La convention
  produit actuellement un gain ; le biais reste nul.
- **Territoire — `TileTerritory`** : `mask` est un masque `CV_8U` de meme taille
  que la tuile, avec 0 pour les pixels exclus et 255 pour les pixels admis.
- **Bloc — `MosaicBlock`** : `destination` decrit le rectangle de destination
  et `pixels` conserve une copie de la contribution warpée de cette tuile.
  Ce sont des informations de contribution, pas des blocs tuiles d'un format
  de stockage en blocs.
- **Mosaique — `Mosaic`** : `image` est l'image fusionnee flottante,
  `blocks` conserve les contributions et `sourceDepth` rappelle la profondeur
  initiale utilisee pour convertir l'image a l'export.

### Coordonnees et ordre des tuiles

Les coordonnees de grille proviennent des deux groupes de capture de la regex
de `import` : groupe 1 = colonne, groupe 2 = ligne. Les tuiles sont triees par
ligne, puis colonne. Les coordonnees doivent etre non negatives, uniques et
former un rectangle complet, mais elles ne sont pas normalisees vers `(0, 0)`.
Par consequent, `referenceTile` designe un indice dans le vecteur trie, et non
une coordonnee `col,row`.

Dans une image OpenCV, le pixel est exprime en `(x,y)` : origine en haut a
gauche, `x` vers la droite, `y` vers le bas. Les homographies s'appliquent aux
coordonnees pixel. Dans `StitchingStep`, les points detectes a un niveau reduit
sont multiplies par `2^level` avant de calculer et stocker la transformation :
les transformations finales sont ainsi en coordonnees de l'image d'origine.

### Graphe et circulation des valeurs

`DataStore::set<T>` remplace la valeur associee a une cle par un objet de type
`T`. `DataStore::get<T>` renvoie une reference constante : une cle absente
declenche `std::out_of_range`, un type incorrect `std::invalid_argument` avec
la cle et les types demandes/stockes. `has()` verifie seulement la presence
d'une cle. Les types ne sont pas serialises dans le JSON du pipeline.

## 3. Contrat du JSON et registre

Le format de configuration est :

```json
{
  "pipeline": {
    "steps": [
      {
        "name": "nomDuStep",
        "options": [
          { "type": "string", "key": "option", "value": "valeur" }
        ]
      }
    ]
  }
}
```

Le parseur JSON est `nlohmann::json` (`Json`). `Config.h` declare `Option`,
`PipelineOption`, `Signature`, `StepConfig` et `PipelineConfig`, puis leurs
convertisseurs JSON. `options` absentes et champs de signature absents prennent
les valeurs par defaut des structures (vecteurs vides).

Les types d'option permis sont `string`, `int`, `float` et `bool`. `float`
accepte tout nombre JSON, y compris un entier ; les valeurs flottantes non
finies sont rejetees. Une option absente conserve sa valeur par defaut. Une
option fournie doit avoir un nom connu, ne pas etre dupliquee, avoir le type
declare par le step et respecter les regles semantiques ci-dessous.

Le registre local de `PipelineBuilder.cpp` cree les sept noms suivants :
`import`, `stitching`, `absolutePositioning`, `intensityCorrection`, `seamline`,
`fusion`, `export`. Le builder inspecte la signature de chaque step, garde les
cles/types deja produits et rejette les entrees qui n'ont pas de producteur
precedent ou dont le type ne correspond pas. Cela valide une dependance
ordonne, pas l'execution des algorithmes ni la presence d'un export final.

`Step::importConfig` fusionne les options fournies sur les valeurs par defaut.
Il refuse les champs de step inconnus, les options mal formees, inconnues ou
dupliquees, les types errones, et les entrees/signatures JSON declarees qui ne
correspondent pas au contrat C++. `validateStepOptions` verifie aussi les
valeurs semantiques au chargement du pipeline :

| Step | Option | Regle actuelle |
|---|---|---|
| `import` | `path` | non vide |
| `import` | `format` | regex valide avec au moins deux groupes |
| `stitching` | `matcher` | `sift` ou `orb` |
| `stitching` | `rejector` | `ransac` |
| `stitching` | `level` | entier de 0 a 5 |
| `stitching` | `ratio` | dans l'intervalle ouvert `(0, 1)` |
| `stitching` | `threshold` | strictement positif |
| `absolutePositioning` | `referenceTile` | entier non negatif ; le maximum depend des tuiles chargees |
| `seamline` | `seamFinder` | `none` seulement |
| `fusion` | `blending` | `average` seulement |
| `export` | `path` | non vide |
| `export` | `bits` | 8 ou 16 |

Les valeurs par defaut des constructeurs C++ sont :

| Step | Valeurs par defaut |
|---|---|
| `import` | `path = images/`, `format = img_C(\\d+)_R(\\d+)\\.tif` |
| `stitching` | `matcher = sift`, `rejector = ransac`, `level = 2`, `ratio = 0.75`, `threshold = 3.0` |
| `absolutePositioning` | `referenceTile = 0` |
| `intensityCorrection` | `enabled = true` |
| `seamline` | `seamFinder = none` |
| `fusion` | `blending = average` |
| `export` | `path = output/mosaic.tif`, `bits = 16` |

Les erreurs de configuration declenchent une exception avec le nom du step ou
de l'option. Les erreurs metier detectees pendant un step sont placees dans
`exportData()["error"]`, le step renvoie `false`, et `Pipeline::run` s'arrete.
Une exception inattendue provenant d'un step ou d'OpenCV n'est pas transformee
par `Pipeline::run` en erreur locale ; elle remonte au `catch` du lanceur.

## 4. Explication des fichiers du coeur

### `include/Backend/core/Types.h`

Point de definition des objets C++ qui transitent entre steps. `keys` regroupe
les chaines du DataStore pour reduire les erreurs de frappe. Les alias de
vecteurs rendent explicites les collections : plusieurs paires de
correspondances, plusieurs alignements et plusieurs positions.

### `include/Backend/core/Config.h`

Decrit les structures JSON. `Option` contient `type`, `key`, `value`.
`PipelineOption` decrit une donnee attendue/produite par son nom de type et sa
cle DataStore. `StepConfig` contient le nom du step, ses options, ses entrees
(`pipelineOptions`) et sa sortie (`signature`). `PipelineConfig` est la liste
ordonnee des steps. Les macros nlohmann assurent conversion JSON/C++.

### `include/Backend/core/DataStore.h`

Stocke les valeurs dans une map `string -> any`. Le template permet de stocker
les objets C++ sans les convertir en JSON. L'acces exige le type exact utilise
au stockage ; une erreur de cle ou de type produit une exception explicite.
La duree de vie du store est celle de `Pipeline`.

### `include/Backend/core/Step.h` et `src/Backend/core/Step.cpp`

`Step` est la classe abstraite partagee : elle detient sa config, le JSON de
sortie de diagnostic et un pointeur vers le DataStore connecte par le pipeline.
Chaque classe concrete implemente `execute()`.

`option(key)` cherche une entree de `config.options` et renvoie son champ
`value`; une option manquante produit une exception identifiant le step et la
cle. `importConfig` parse la configuration du step, verifie son nom et son
contrat de donnees, remplace uniquement les valeurs des options reconnues, puis
execute les validations de types et de valeurs. `exportData()` sert au
journal d'execution et aux messages d'erreur, pas au transfert des images.

### `src/Backend/core/PipelineBuilder.cpp`

Construit le registre des noms de steps et leurs fabriques. `buildPipeline`
verifie la forme racine du JSON, lit chaque nom, cree le step, lui applique sa
configuration et controle ses entrees/sorties par rapport aux steps precedents.
Il refuse les scripts vides. Les fabriques et signatures sont codees en C++ :
le JSON configure les steps, mais ne peut pas declarer arbitrairement un
nouveau type de step ou redefinir son contrat.

### `include/Backend/core/Pipeline.h` et `src/Backend/core/Pipeline.cpp`

`Pipeline` possede les steps (`unique_ptr`) et le DataStore. `run()` passe le
store a chacun des steps puis appelle `execute()` dans l'ordre. Une valeur
`false` stoppe l'execution, ecrit un message contenant les diagnostics du step
sur `stderr` et le transmet au logger s'il existe. Une valeur `true` passe au
step suivant.

`configure()` est une API distincte : elle applique un objet de configuration a
un pipeline deja construit et exige le meme nombre de steps. Le chemin normal
du lanceur utilise `buildPipeline()` qui construit et configure les steps
directement. `exportConfig()` reconstitue un objet JSON de reprise depuis les
configs des steps.

### Interfaces et implementations de steps

Chaque en-tete de `include/Backend/steps/` declare les options par defaut dans
un `*Input`, puis une classe derivee de `Step` avec son constructeur et
`execute()`. Les commentaires de signature indiquent les entrees/sorties
attendues. Le fichier du meme nom dans `src/Backend/steps/` construit le
`StepConfig`, lit les options JSON avec `option()`, recupere les donnees
DataStore, traite les erreurs connues et publie le resultat. Le detail de
l'algorithme de chacune de ces implementations est donne a la section 5.

| Interface | Implementation | Responsabilite |
|---|---|---|
| `include/Backend/steps/ImportStep.h` | `src/Backend/steps/ImportStep.cpp` | Lire, ordonner et verifier les tuiles |
| `include/Backend/steps/StitchingStep.h` | `src/Backend/steps/StitchingStep.cpp` | Detecter les features, apparier les voisins, estimer les homographies |
| `include/Backend/steps/AbsolutePositioningStep.h` | `src/Backend/steps/AbsolutePositioningStep.cpp` | Propager les homographies depuis la reference |
| `include/Backend/steps/IntensityCorrectionStep.h` | `src/Backend/steps/IntensityCorrectionStep.cpp` | Calculer gains et biais par tuile |
| `include/Backend/steps/SeamlineStep.h` | `src/Backend/steps/SeamlineStep.cpp` | Produire les masques de validite des tuiles |
| `include/Backend/steps/FusionStep.h` | `src/Backend/steps/FusionStep.cpp` | Projeter, accumuler et moyenner les images |
| `include/Backend/steps/ExportStep.h` | `src/Backend/steps/ExportStep.cpp` | Convertir et encoder la mosaique |

## 5. Explication detaillee des steps

### 5.1 Import — `ImportStep`

**Fichiers :** `include/Backend/steps/ImportStep.h`,
`src/Backend/steps/ImportStep.cpp`.

Options : `path` (repertoire d'images) et `format` (regex du nom). Le format
par defaut `img_C(\\d+)_R(\\d+)\\.tif` capture colonne puis ligne. Le step :

1. Verifie que le chemin est un repertoire et compile la regex sans tenir
   compte de la casse.
2. Parcourt les fichiers reguliers et ne conserve que ceux dont le nom complet
   correspond a la regex.
3. Convertit les deux captures en entiers, rejette les coordonnees negatives
   et les doublons.
4. Trie par ligne puis colonne et verifie que le rectangle englobant est
   complet ; une grille avec un trou ou un decalage de coordonnees demeure
   valide si son rectangle est complet.
5. Charge chaque fichier avec `cv::imread(..., IMREAD_UNCHANGED)`, donc sans
   reduire volontairement la profondeur 16 bits.
6. Stocke `Tiles` dans `keys::Tiles` et expose le nombre de tuiles dans ses
   donnees de diagnostic.

Les erreurs de repertoire, regex, coordonnees, grille vide et lecture d'image
font retourner `false` avec une cause. Les erreurs de parcours du systeme de
fichiers hors de ces cas peuvent remonter comme exceptions.

### 5.2 Correspondances et transformations — `StitchingStep`

**Fichiers :** `include/Backend/steps/StitchingStep.h`,
`src/Backend/steps/StitchingStep.cpp`.

Entree : `tiles`. Sorties : `matches` et `alignments`. Parametres : matcher SIFT
ou ORB, rejet RANSAC, niveau pyramidal `level`, seuil de ratio et seuil RANSAC.
Le step :

1. Convertit chaque image en niveaux de gris. Les images 16 bits sont
   converties en 8 bits pour la detection ; les pixels originaux des tuiles
   restent inchanges pour la fusion.
2. Reduit temporairement la resolution par `2^level` avec `INTER_AREA`.
3. Detecte les points et descripteurs sur chaque tuile (SIFT ou ORB avec
   jusqu'a 5000 points).
4. Ne compare que des tuiles voisines a droite ou en dessous dans la grille.
5. Compare les descripteurs de la seconde tuile avec ceux de la premiere par
   force brute et conserve les candidats passant le test de ratio de Lowe.
6. Reconvertit les points a la resolution d'origine, exige au moins quatre
   paires et calcule une homographie `secondToFirst` par `findHomography` avec
   RANSAC.
7. Stocke points correspondants et masque des inliers dans `Matches` et
   homographies dans `Alignments`.

Un manque de descripteurs, moins de quatre correspondances, une transformation
impossible ou l'absence de paire adjacente est signale. Le seuil `threshold`
est la distance RANSAC dans les coordonnees image originales.

### 5.3 Positionnement absolu — `AbsolutePositioningStep`

**Fichiers :** `include/Backend/steps/AbsolutePositioningStep.h`,
`src/Backend/steps/AbsolutePositioningStep.cpp`.

Entrees : `tiles`, `alignments`. Option : indice `referenceTile`, par defaut 0.
Le step traite les alignements comme un graphe : chaque paire donne une arete
dans le sens `second -> first` et une arete inverse obtenue par inversion de la
matrice. Une recherche en largeur part de la tuile de reference et compose les
homographies rencontrees pour produire `tileToWorld` de chaque tuile. Il
refuse un indice de reference hors limites, des indices d'alignement invalides
ou toute tuile deconnectee. La reference recoit la matrice identite.

Le code ne fait pas d'ajustement global du graphe, de fermeture de boucle ni
d'optimisation photogrammetrique : la position depend du premier chemin visite
par le parcours en largeur.

### 5.4 Corrections lumineuses — `IntensityCorrectionStep`

**Fichiers :** `include/Backend/steps/IntensityCorrectionStep.h`,
`src/Backend/steps/IntensityCorrectionStep.cpp`.

Entree : `tiles`. Option `enabled`. Pour chaque tuile, le step calcule la
moyenne globale des pixels par canal avec `cv::mean`, calcule ensuite la
moyenne de ces moyennes entre tuiles et genere un gain qui rapproche chaque
moyenne de la cible. Les gains sont limites a `[0.5, 2.0]`; si la moyenne du
canal est quasi nulle, le gain reste 1. Le canal alpha, s'il existe, n'est pas
corrige. Les biais restent a zero. Si `enabled` est faux, les corrections
neutres sont publiees.

Il s'agit d'une approximation simple fondee sur l'image entiere, pas d'une
estimation sur les zones de recouvrement. Le step produit `corrections` mais ne
modifie pas directement les images ; `fusion` applique ces parametres.

### 5.5 Territoires — `SeamlineStep`

**Fichiers :** `include/Backend/steps/SeamlineStep.h`,
`src/Backend/steps/SeamlineStep.cpp`.

Entrees : `tiles`, `positions`. Seule option admise : `seamFinder = "none"`.
Pour chaque image, le step cree un masque tout blanc ; pour une image a quatre
canaux, les pixels dont alpha vaut zero sont exclus et les autres inclus. Les
masques sont publies en `territories`.

Malgre son nom, ce step ne cherche pas actuellement une ligne de couture et
n'utilise pas les positions pour calculer un partage entre tuiles. Il prepare
un masque de validite simple pour la fusion. Toute autre valeur de
`seamFinder` est rejetee au chargement de configuration.

### 5.6 Fusion — `FusionStep`

**Fichiers :** `include/Backend/steps/FusionStep.h`,
`src/Backend/steps/FusionStep.cpp`.

Entrees : `tiles`, `positions`, `corrections`, `territories`. Seule option :
`blending = "average"`. Le step :

1. Verifie que les collections ont la meme taille, que les images ont meme
   nombre de canaux et profondeur, que les positions/masques correspondent aux
   indices des tuiles, et que les masques ont les dimensions attendues.
2. Transforme les quatre coins de chaque tuile par son homographie pour
   calculer l'emprise globale de la mosaique.
3. Ajoute une translation si necessaire pour que l'emprise commence a `(0,0)`,
   alloue une image accumulatrice et une carte de poids en `CV_32F`.
4. Convertit chaque tuile en flottant, applique gain et biais par canal, puis
   projette image et masque dans l'emprise.
5. Additionne les pixels masques et les poids. Dans les recouvrements, la
   sortie est la moyenne des contributions admises.
6. Divise la somme par les poids, met les pixels sans poids a zero, conserve
   les contributions par tuile dans `MosaicBlock`, et publie le `Mosaic`.

La fusion ne met pas en oeuvre multibande, plume (feathering), correction de
couleur locale ni seam finder. Les blocs gardent les donnees warpées, pas le
resultat divise par le poids. Les masques sont interpoles au plus proche voisin.

### 5.7 Export — `ExportStep`

**Fichiers :** `include/Backend/steps/ExportStep.h`,
`src/Backend/steps/ExportStep.cpp`.

Entree : `mosaic`. Options : `path` et profondeur de sortie `bits` (8 ou 16).
Le step cree les repertoires parents, convertit l'image flottante en 8 ou
16 bits, en ajustant l'echelle entre sources 8 et 16 bits, puis appelle
`cv::imwrite`. Le format du fichier est choisi par OpenCV d'apres l'extension
du chemin ; il n'y a pas d'option BigTIFF, pyramide, tuilage de fichier ou
conservation ICC dans le code actuel. `imwrite == false` et les exceptions
d'ecriture sont rapportees dans `outputData["error"]`.

## 6. Lanceur et construction

### `src/main.cpp`

Le lanceur choisit le JSON passe en premier argument, ou
`PROJECT_DIR/pipeline.json` par defaut. Il le parse, appelle `buildPipeline`,
puis change le repertoire courant vers celui du JSON : les chemins relatifs
`images/` et `output/...` sont ainsi interpretes depuis le dossier du script.
Il affiche le nom et les options de chaque step, lance le pipeline, puis renvoie
un code de sortie 0 en cas de succes et 1 en cas d'erreur. Les exceptions sont
affichees sur `stderr`.

### `src/main_test.cpp`

Cette source contient egalement un executable de ligne de commande qui lit un
JSON, appelle `buildPipeline`, affiche les steps et execute le pipeline. Dans
`CMakeLists.txt`, elle est liee a la cible `stitching_test`; malgre son nom,
elle ne contient actuellement pas de suite d'assertions, de jeu de test ni de
mode simulation. Son nom de cible ne constitue donc pas une preuve d'integration
ou de test automatique.

### `CMakeLists.txt`

Construit le backend statique a partir des sources `src/Backend/*.cpp`, lie
OpenCV et nlohmann-json, puis construit `stitching` (lanceur) et
`stitching_test` (cible definie depuis `src/main_test.cpp`). Qt6 est optionnel :
sans Qt le frontend n'est pas construit. Les sources frontend sont absentes ou
stub dans l'etat actuel ; elles ne participent pas au pipeline backend.
`src/Frontend/ToDelete.cpp` est explicitement exclu de la liste de sources
frontend. `include/Frontend/stub.cpp` ne contient pas d'implementation du
pipeline. Ces fichiers ne fournissent pas de step backend supplementaire.

### `Tuile` et `Pyramide`

`include/Backend/core/Tuile.h` et `src/Backend/core/Tuile.cpp` definissent une
structure historique de tuile/grille et des niveaux de pyramide. `Tuile::image`
renvoie la premiere image de `pyramide.original` ou une matrice vide. Ces
classes ne sont pas les types transportes par le pipeline actuel : celui-ci
emploie `TileImage`/`Tiles` dans `Types.h`.

## 7. Configuration de reference et execution

`pipeline.json` configure les sept steps dans l'ordre. Il choisit SIFT, RANSAC,
un niveau de reduction 2, le ratio 0,75, le seuil 3 pixels, la tuile d'indice 0
comme reference, la correction lumineuse active, `seamFinder: none`, `average`
et un export TIFF 16 bits via `output/mosaic.tif`.

Exemple de noms de fichiers correspondant a la regex : `img_C00_R00.tif`,
`img_C01_R00.tif`, `img_C00_R01.tif`, `img_C01_R01.tif`. Ces quatre fichiers
forment une grille 2×2 ; pour que l'alignement reussisse, les voisins doivent
en plus avoir suffisamment de texture distinctive et des zones communes
reconnues par le matcher.

Le depot ne contient pas `images/`. La validite syntaxique de la configuration
ne prouve donc ni la qualite des images, ni le succes du RANSAC, ni l'execution
de bout en bout. Une simulation qui valide uniquement l'ordre et les options
ne vaut pas execution reelle jusqu'a l'export. Le projet doit etre compile,
puis `stitching` lance avec ce JSON et quatre images 2×2 raccordables ; il faut
verifier le code de sortie et l'existence/lisibilite de l'image exportee avant
de declarer l'integration complete.

## 8. Limites et points a faire confirmer

- Confirmer avec les responsables les types, les noms de cles, les coordonnees,
  le sens des homographies et les parametres avant de declarer les contrats
  approuves.
- `Matches` est produit, mais aucun consommateur ne l'utilise actuellement.
- Le graphe d'alignement est compose par parcours, sans optimisation globale.
- La correction lumineuse utilise les moyennes de tuiles entieres, pas les
  recouvrements.
- `seamline` ne calcule pas de couture ; `fusion` ne fait qu'une moyenne
  ponderee par masque.
- L'export est celui des encodeurs OpenCV disponibles, pas un export BigTIFF
  avec pyramides/ICC garanti.
- La validation de configuration garantit le schema et les contrats declares,
  pas que les fichiers existent, que l'extension de sortie est encodeable ou
  que les donnees produiront un alignement.
