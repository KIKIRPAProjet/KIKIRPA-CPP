# Vérification finale des images — documentation détaillée

## 1. But et limites

`VerificationNs::compareImages(inputImagePath, outputImagePath)` construit un rapport JSON décrivant les différences de métadonnées entre une image de référence et une image reconstruite. La référence peut être un TIFF ou un JPEG ; la sortie reconstruite doit être un TIFF.

L'algorithme :

- lit les dimensions et un ensemble défini de métadonnées TIFF/JPEG/Exif/JFIF/ICC ;
- compare les propriétés interprétées et certains segments JPEG bruts ;
- rapporte les différences, les profils ICC et leurs tailles ;
- ne décode, ne décompresse et ne compare jamais les pixels ;
- ne produit aucun verdict de qualité, de conformité ou d'équivalence visuelle ;
- ne compare ni la résolution physique, ni les DPI/PPI.

Une différence signalée signifie seulement que les valeurs comparées diffèrent. Par exemple, deux profils ICC différents ne prouvent pas que les couleurs rendues seront différentes : aucun moteur de gestion des couleurs n'est utilisé.

L'implémentation est dans [`final_verification.cpp`](../../../src/backend/algorithms/verification/final_verification.cpp), son interface publique dans [`final_verification.h`](../../../include/backend/algorithms/verification/final_verification.h). Le step de pipeline qui publie ce rapport est décrit dans [`FinalVerificationStep.md`](../../steps/FinalVerificationStep.md).

## 2. Vue d'ensemble du traitement

```text
compareImages(input, output)
  ├─ normalise et valide l'extension de sortie : TIFF obligatoire
  ├─ lit les métadonnées de la référence
  │    ├─ .tif/.tiff  → lecteur libtiff
  │    └─ .jpg/.jpeg  → lecteur de segments JPEG jusqu'à SOS
  ├─ lit les métadonnées de la sortie TIFF avec libtiff
  └─ crée le rapport
       ├─ union des propriétés JSON (valeurs absentes → null)
       ├─ segments de métadonnées JPEG comparés par position
       ├─ comparaison binaire des profils ICC
       └─ dimensions, écarts signés et ratios sortie/référence
```

Le format est sélectionné à partir de l'extension, rendue minuscule avant le test. Le contenu n'est pas auto-détecté pour choisir le lecteur. Le lecteur JPEG vérifie néanmoins la signature `FF D8`, et les lecteurs TIFF s'appuient sur libtiff pour ouvrir effectivement le fichier.

Le chemin de sortie est contrôlé avant toute lecture : une extension différente de `.tif` ou `.tiff` déclenche une erreur. La référence accepte `.tif`, `.tiff`, `.jpg` et `.jpeg`.

## 3. Modèle de données interne

Chaque image est ramenée à une structure `ImageMetadata` :

| Champ | Contenu |
|---|---|
| `format` | `"TIFF"` ou `"JPEG"` selon le lecteur sélectionné. |
| `properties` | Objet JSON des dimensions et propriétés interprétées. |
| `iccProfile` | Octets du profil ICC, éventuellement réassemblés depuis plusieurs segments JPEG. |
| `hasIccProfile` | Indique qu'un profil non vide a été lu. |
| `metadataSegments` | Pour le JPEG, liste ordonnée de couples `(marqueur, octets)` conservés pour une comparaison brute. Toujours vide côté TIFF. |

Le lecteur ne conserve pas les pixels dans cette structure. Les métadonnées JPEG ont aussi une liste JSON `properties["metadataSegments"]` qui décrit, pour chaque segment APP rencontré, son marqueur et la taille de son payload. Cette liste descriptive n'est pas elle-même utilisée comme liste binaire de comparaison.

## 4. Parcours TIFF

### 4.1 Ouverture et durée de vie

Le fichier est ouvert en lecture par `TIFFOpen`. Le pointeur est enveloppé dans un `unique_ptr` muni de `TIFFClose` : la fermeture est donc assurée aussi bien au retour normal qu'en cas d'exception.

Un échec d'ouverture lève une `runtime_error`. L'algorithme ne parcourt que le répertoire TIFF courant pour les tags principaux ; il ne décode pas les bandes/tiles image.

### 4.2 Dimensions

Les tags requis sont `IMAGEWIDTH` et `IMAGELENGTH`. Les deux doivent être lus et non nuls. Ils sont également refusés s'ils dépassent `int` maximal, plage supportée par le rapport.

Les propriétés émises sont `width` et `height`. Toute dimension invalide fait échouer la lecture de cette image au lieu de produire des valeurs de remplacement.

### 4.3 Propriétés techniques

`TIFFGetFieldDefaulted` lit les champs suivants, avec les valeurs normalisées par libtiff si un tag n'est pas explicitement présent :

| Propriété JSON | Tag TIFF | Valeur initiale fournie au lecteur |
|---|---|---:|
| `samplesPerPixel` | `SAMPLESPERPIXEL` | 1 |
| `bitsPerSample` | `BITSPERSAMPLE` | 1 |
| `sampleFormat` | `SAMPLEFORMAT` | `SAMPLEFORMAT_UINT` |
| `photometricInterpretation` | `PHOTOMETRIC` | 0 |
| `compression` | `COMPRESSION` | `COMPRESSION_NONE` |
| `orientation` | `ORIENTATION` | `ORIENTATION_TOPLEFT` |
| `planarConfiguration` | `PLANARCONFIG` | `PLANARCONFIG_CONTIG` |

La valeur comparée est la valeur numérique du tag normalisé, pas une interprétation textuelle (par exemple, aucun nom descriptif n'est substitué au code de compression).

### 4.4 Échantillons supplémentaires et tags texte

Si `EXTRASAMPLES` existe, chacun de ses codes est copié dans un tableau `extraSamples`, dans l'ordre. En l'absence de ce tag, le rapport contient un tableau vide.

Les tags texte facultatifs présents dans le répertoire courant sont copiés sous les noms suivants :

| Tag TIFF | Propriété |
|---|---|
| `IMAGEDESCRIPTION` | `imageDescription` |
| `DOCUMENTNAME` | `documentName` |
| `MAKE` | `make` |
| `MODEL` | `model` |
| `SOFTWARE` | `software` |
| `DATETIME` | `dateTime` |
| `ARTIST` | `artist` |
| `COPYRIGHT` | `copyright` |

Un tag texte absent n'est pas une erreur et la propriété n'est simplement pas créée.

### 4.5 Profil ICC TIFF

`ICCPROFILE` est copié octet par octet si libtiff retourne une taille positive et un tampon non nul. `hasIccProfile` passe alors à `true`. Il n'y a pas de validation sémantique du profil ni d'analyse de son contenu.

### 4.6 Répertoire EXIF TIFF

Le lecteur cherche `EXIFIFD`. Le rapport reçoit systématiquement `exifDirectoryPresent` et `exifParseStatus` :

- aucun offset EXIF : `false` et `"absent"` ;
- offset trouvé : `true` et `"unread"` pendant le traitement ;
- `TIFFReadEXIFDirectory` réussit : lecture de `DATETIMEORIGINAL` et `DATETIMEDIGITIZED`, puis état `"parsed"` ;
- la lecture du répertoire échoue : état `"invalid"`.

Les dates sont émises sous `dateTimeOriginal` et `dateTimeDigitized` si les champs texte correspondants existent. Les propriétés TIFF du répertoire principal avaient déjà été lues avant le changement de répertoire.

## 5. Parcours JPEG : découpage binaire en segments

### 5.1 Format d'un segment et convention des tailles

Un marqueur JPEG est introduit par `0xFF`. La plupart des marqueurs suivis de données ont ensuite un champ longueur big-endian de 16 bits, puis un payload :

```text
FF | marqueur | longueur_hi | longueur_lo | payload...
                └──────────┘
                longueur du segment, champ longueur inclus,
                mais marqueur FF + code exclus
```

La longueur déclarée inclut ses deux propres octets. Le code calcule donc :

```text
taille_payload = longueur_segment - 2
```

Une longueur inférieure à 2 est rejetée. Le lecteur vérifie que les deux octets de longueur, puis tous les octets du payload annoncé, sont effectivement disponibles. Toute troncature fait échouer la lecture avec une erreur explicite.

Le champ longueur du segment est décodé en big-endian : `0x00 0x10` vaut 16, donc 14 octets de payload.

### 5.2 Signature et recherche des marqueurs

Le lecteur ouvre le JPEG en binaire et exige les deux premiers octets `FF D8` (SOI, Start Of Image). Il cherche ensuite le prochain `FF`, saute les `FF` répétés et utilise l'octet suivant comme code marqueur.

Le parcours se termine dès qu'il rencontre :

- `0xDA` (SOS, Start Of Scan) : le flux de données compressées commence ; l'image n'est pas lue plus loin ;
- `0xD9` (EOI, End Of Image).

Les marqueurs `TEM` (`0x01`) et restart (`0xD0` à `0xD7`) sont traités comme marqueurs sans longueur et ignorés. Pour les autres marqueurs rencontrés avant la fin du parcours, le lecteur lit un champ longueur et le payload. Il ne valide pas la structure de tous les types de segment JPEG : seuls les segments utiles à l'algorithme sont interprétés.

La fin de flux termine également la boucle : le lecteur ne demande pas explicitement qu'un marqueur EOI ait été rencontré. La finalisation exige toutefois un en-tête de frame pris en charge et valide les segments déjà parcourus. La lecture peut donc extraire dimensions et métadonnées sans consommer les données compressées ; elle ne reconstruit pas un frame à partir d'autres informations.

### 5.3 Frame : offsets et propriétés

Les marqueurs Start Of Frame sont reconnus dans l'intervalle `C0`–`CF`, sauf `C4` (DHT), `C8` (JPG) et `CC` (DAC). Le payload d'un frame doit faire au moins six octets. Les offsets ci-dessous sont relatifs au début du payload du segment, c'est-à-dire après le marqueur et le champ longueur :

| Offset | Taille | Signification | Propriété JSON |
|---:|---:|---|---|
| 0 | 1 | précision en bits par échantillon | `bitsPerSample` |
| 1 | 2 | hauteur, big-endian | `height` |
| 3 | 2 | largeur, big-endian | `width` |
| 5 | 1 | nombre de composantes/échantillons | `samplesPerPixel` |

Largeur et hauteur doivent être non nulles. Le code de marqueur SOF est stocké comme `compression`. Le lecteur ne parcourt pas les descriptions de composantes qui suivent ces six octets.

> Les valeurs de largeur/hauteur JPEG sont des entiers non signés de 16 bits, contrairement aux dimensions TIFF de 32 bits.

### 5.4 Segments APP : inventaire, JFIF, Exif et ICC

Tout marqueur `APP0` à `APP15` (`E0`–`EF`) ajoute une entrée à `properties["metadataSegments"]` contenant :

```json
{ "marker": 225, "payloadBytes": 123 }
```

Cette entrée sert d'inventaire et ne contient pas les octets du segment. Les APP sont également envoyés aux détecteurs spécialisés suivants.

#### JFIF (APP0)

Un segment n'est JFIF que si :

- son marqueur est APP0 ;
- son payload fait au moins 14 octets ;
- les cinq premiers octets sont `JFIF\0`.

Offsets du payload APP0 JFIF utilisés :

| Offset | Champ | Propriété |
|---:|---|---|
| 5 | version majeure | `jfifVersion.major` |
| 6 | version mineure | `jfifVersion.minor` |
| 12 | largeur de vignette | `jfifVersion.thumbnailWidth` |
| 13 | hauteur de vignette | `jfifVersion.thumbnailHeight` |

Les octets de densité physique ne sont pas interprétés ni comparés. Le segment JFIF complet est exclu de la liste des segments comparés octet par octet ; seules les propriétés ci-dessus sont comparées. La détection par signature évite de traiter tout APP0 comme JFIF.

#### Exif (APP1)

Un APP1 est interprété comme Exif seulement si son payload commence par les six octets `Exif\0\0`. À ce moment, le rapport reçoit `exifPresent: true`, la taille du payload dans `exifPayloadBytes`, et l'objet `exif`. Les champs Exif reconnus sont aussi remontés au niveau supérieur des propriétés JPEG : `orientation`, `make`, `model`, `software`, `dateTime`, `artist`, `copyright`.

L'APP1 Exif n'est pas retiré de la liste des segments génériques bruts : il est donc comparé à la fois par ses propriétés extraites et par ses octets complets. Un APP1 non Exif reste lui aussi un segment APP générique comparé.

Si aucun APP1 Exif n'est trouvé, les propriétés par défaut de finalisation sont `exifPresent: false`, `exifPayloadBytes: 0`, `exif: null`. En revanche, si plusieurs APP1 Exif sont présents, le lecteur traite chacun ; les dernières propriétés de même nom remplacent les précédentes, tandis que chaque APP demeure dans la séquence de comparaison brute.

#### ICC (APP2)

Un fragment ICC doit être un APP2 dont le payload commence par la signature de 12 octets `ICC_PROFILE\0`, puis contient un octet d'index et un octet de nombre total de fragments :

```text
payload offset 0..11 : "ICC_PROFILE\0"
payload offset 12    : index du fragment (commence à 1)
payload offset 13    : nombre total attendu
payload offset 14..  : octets de profil de ce fragment
```

Les invariants vérifiés sont :

- index et nombre total non nuls ;
- index inférieur ou égal au nombre total ;
- même nombre total annoncé par tous les fragments ;
- aucun index dupliqué.

Les données après l'octet 13 sont conservées sous l'index. À la fin du parcours, le nombre reçu doit correspondre au nombre attendu et chaque index `1..N` doit exister. Les octets sont concaténés dans l'ordre numérique des index, même si les APP2 apparaissent dans un autre ordre dans le fichier. Un profil incomplet, contradictoire ou dupliqué provoque une erreur, plutôt qu'une comparaison partielle. Les fragments ICC ne sont pas ajoutés aux segments génériques bruts.

Le profil est considéré présent si le résultat assemblé n'est pas vide. Un marqueur ICC valide mais sans octet de profil ne met donc pas `hasIccProfile` à `true`.

### 5.5 Commentaires et séquence brute comparée

Un marqueur COM (`FE`) augmente `commentCount` de 1 et ajoute la taille de son payload à `commentBytes`. Chaque COM est aussi ajouté aux segments bruts.

La séquence `ImageMetadata.metadataSegments` utilisée à la comparaison contient, dans l'ordre de rencontre :

1. chaque APP qui n'est pas un JFIF reconnu ni un fragment ICC reconnu ;
2. chaque segment COM.

Un APP Exif reconnu est inclus dans cette séquence. Les autres catégories de segments (par exemple DQT, DHT ou SOF) ne sont pas dans cette liste ; certaines informations SOF sont en revanche comparées comme propriétés interprétées. L'inventaire JSON des APP contient aussi JFIF et ICC, mais cet inventaire n'est pas comparé comme un tableau global.

### 5.6 Décomposition de l'Exif embarqué : offsets détaillés

Le payload d'un APP1 Exif commence par une signature de six octets `Exif\0\0`. À partir de l'octet 6 se trouve l'en-tête TIFF interne. Les offsets du format TIFF embarqué sont relatifs au début de cet en-tête, pas au début du fichier JPEG ni au début du payload APP1.

```text
APP1 payload
┌──────────────────────┬───────────────────────────────────────────┐
│ offset 0..5          │ "Exif\0\0" (signature, 6 octets)          │
├──────────────────────┼───────────────────────────────────────────┤
│ offset 6..7          │ ordre des octets : "II" ou "MM"            │
│ offset 8..9          │ magie TIFF = 42                            │
│ offset 10..13        │ offset du premier IFD (32 bits)             │
├──────────────────────┼───────────────────────────────────────────┤
│ offset 6 + IFD offset│ nombre d'entrées du premier IFD (16 bits)   │
│ ensuite              │ entrées de 12 octets chacune                │
└──────────────────────┴───────────────────────────────────────────┘
```

Les constantes utilisées par le parseur rendent ces frontières explicites :

| Constante | Valeur | Interprétation |
|---|---:|---|
| `EXIF_HEADER_OFFSET` | 6 | Début de l'en-tête TIFF interne dans le payload APP1. |
| `EXIF_HEADER_SIZE` | 8 | Taille de cet en-tête TIFF. Il faut donc au moins `6 + 8 = 14` octets pour tenter le décodage. |
| `EXIF_ENTRY_SIZE` | 12 | Taille d'une entrée IFD. |
| `EXIF_ENTRY_VALUE_OFFSET` | 8 | Offset, dans l'entrée, de la zone valeur ou de l'offset vers la valeur. |
| `EXIF_INLINE_VALUE_SIZE` | 4 | Jusqu'à quatre octets peuvent être stockés directement dans une entrée. |

Le décodeur prend en charge les deux ordres d'octets :

- `II` : little-endian ;
- `MM` : big-endian.

Les entiers de 16 et 32 bits sont lus par des helpers qui vérifient d'abord que tous les octets demandés sont dans le vecteur payload. Les offsets IFD et les offsets de valeurs sont des offsets TIFF internes ; pour atteindre un champ de valeur externe dans le payload APP1, le code ajoute `EXIF_HEADER_OFFSET` (6) à l'offset relatif lu.

Une entrée IFD est structurée comme suit :

| Offset dans l'entrée | Taille | Champ |
|---:|---:|---|
| 0 | 2 | Identifiant du tag |
| 2 | 2 | Type TIFF du champ |
| 4 | 4 | Nombre de valeurs |
| 8 | 4 | Valeur inline ou offset TIFF vers la valeur |

Le parseur valide la présence des 12 octets complets de chaque entrée. Il lit le nombre d'entrées du premier IFD et parcourt exactement celles-ci. Il ne suit pas le pointeur du prochain IFD et ne suit pas un sous-IFD Exif/GPS : les tags reconnus doivent être dans le premier IFD.

#### Tags reconnus dans le premier IFD

| Tag | ID | Type exigé | Traitement |
|---|---:|---|---|
| Orientation | `0x0112` | SHORT (3) | Un élément ou moins (count ≤ 2) : lit le SHORT inline. Si count > 2 : lit un offset 32 bits puis un SHORT à cet offset. |
| Make | `0x010F` | ASCII (2) | Texte ASCII. |
| Model | `0x0110` | ASCII (2) | Texte ASCII. |
| Software | `0x0131` | ASCII (2) | Texte ASCII. |
| DateTime | `0x0132` | ASCII (2) | Texte ASCII. |
| Artist | `0x013B` | ASCII (2) | Texte ASCII. |
| Copyright | `0x8298` | ASCII (2) | Texte ASCII. |

Pour les tags texte, une valeur de taille 1 à 4 est prise dans les quatre octets de la zone valeur de l'entrée ; au-delà, cette zone contient un offset. Le texte doit être entièrement disponible dans le segment. La chaîne retournée s'arrête au premier octet nul ou à la fin de la longueur déclarée. Les types non attendus, comptes nuls, tags inconnus ou valeurs externes hors limites sont ignorés (ils ne rendent pas nécessairement invalide tout le bloc Exif).

Les propriétés commencent avec `orientation: 1` par défaut. Un tag orientation valide remplace cette valeur. Si l'en-tête Exif, l'endianness, la magie TIFF, l'offset IFD, le compte IFD ou une entrée parcourue est invalide/tronqué, le résultat est `{"orientation": 1, "parseStatus": "invalid"}`. Après validation de l'en-tête et du parcours des entrées, `parseStatus` vaut `"parsed"`. L'orientation par défaut n'est pas une validation des valeurs autorisées du standard : les valeurs lues ne sont pas filtrées à une plage donnée.

## 6. Comparaison des métadonnées

### 6.1 Propriétés communes aux formats

La comparaison générique utilise une liste explicite de propriétés qui ont le même sens en TIFF et en JPEG :
dimensions, bits par échantillon, nombre d'échantillons, orientation et tags texte communs. Les propriétés
présentes d'un seul côté sont rapportées avec `null` pour l'autre valeur; si aucune des deux images ne possède une
propriété listée, elle est omise des comparaisons.

Les caractéristiques propres à un encodage (compression, interprétation photométrique, configuration planaire,
JFIF, marqueurs et états de parsing EXIF) restent disponibles dans `inputImage.metadata` ou
`outputImage.metadata`, mais ne sont pas transformées en différences. Les segments JPEG ne sont pas comparés aux
métadonnées d'une sortie TIFF.

### 6.2 Profils ICC

Les octets ICC assemblés/lus sont comparés par égalité exacte. Le rapport contient :

- présence de chaque profil ;
- nombre d'octets de chaque profil ;
- `identicalBytes` ;
- une valeur descriptive `colorimetricEquivalence`.

| Situation | `colorimetricEquivalence` |
|---|---|
| Aucun profil des deux côtés | `"absent_from_both"` |
| Présence d'un seul côté | `"presence_differs"` |
| Deux profils présents, octets identiques | `"identical_profile_data"` |
| Deux profils présents, octets différents | `"colorimetric_equivalence_undetermined"` |

Une différence de présence ou de contenu est ajoutée à `differences` sous `property: "iccProfile"`. Un avertissement est ajouté si un profil manque d'un côté ; si les deux sont présents mais différents, l'avertissement précise qu'aucune conclusion colorimétrique n'est possible sans moteur de gestion des couleurs.

### 6.3 Dimensions, écarts et ratios

Les dimensions déjà validées par les lecteurs sont utilisées pour créer :

- `input` et `output` : largeur et hauteur ;
- `difference` : `output - input`, donc les écarts sont signés ;
- `outputToInputRatio.width` et `.height` ;
- `outputToInputRatio.aspectRatio` : rapport entre le ratio d'aspect de la sortie et celui de l'entrée.

Ces champs conservent les dimensions stockées dans les fichiers. Le sous-objet `displayed` reprend les mêmes
champs après échange de largeur et hauteur pour les orientations 5 à 8. Le rapport ajoute un avertissement quand
cette orientation influence les dimensions affichées; il ne vérifie pas l'orientation réelle des pixels.

Le rapport de ratio d'aspect vaut :

```text
(outputWidth / outputHeight) / (inputWidth / inputHeight)
```

Une valeur supérieure à 1 signifie que le ratio largeur/hauteur est plus grand côté sortie ; elle ne suffit pas à déterminer si l'image a été étirée ou si son cadrage a changé. Les écarts dimensionnels sont décrits dans `dimensions` et les propriétés `width`/`height` sont aussi présentes dans `differences` quand elles ne sont pas égales. Ils restent informatifs et ne déclenchent aucun verdict.

## 7. Forme du rapport JSON

Le rapport de succès a cette structure de premier niveau :

```json
{
  "status": "report_ready",
  "inputImage": {
    "path": "reference.jpg",
    "format": "JPEG",
    "metadata": {}
  },
  "outputImage": {
    "path": "reconstructed.tif",
    "format": "TIFF",
    "metadata": {}
  },
  "dimensions": {},
  "metadataComparisons": {},
  "iccProfile": {},
  "differences": [],
  "warnings": []
}
```

`inputImage.metadata` et `outputImage.metadata` exposent les données extraites, y compris les détails spécifiques aux formats. `metadataComparisons` ne contient que les propriétés sémantiquement comparables; les segments JPEG ne sont pas alignés avec le TIFF. `differences` inventorie les écarts de ces propriétés et des profils ICC; `warnings` contient les réserves ICC et d'orientation, pas un verdict.

Le statut `"report_ready"` signifie que les fichiers ont pu être lus et qu'un rapport a été produit. Il ne signifie pas que les deux images sont équivalentes.

## 8. Erreurs et cas rejetés

L'algorithme lève `std::runtime_error` au lieu de fabriquer un rapport partiel notamment si :

- la sortie n'a pas une extension TIFF ;
- le format de la référence n'est pas pris en charge ;
- un TIFF/JPEG ne peut pas être ouvert ;
- un TIFF n'a pas de dimensions utilisables ou celles-ci dépassent la plage supportée ;
- la signature JPEG est absente/incorrecte ;
- un segment JPEG est tronqué ou sa longueur est invalide ;
- un frame JPEG est trop court, a une dimension nulle, ou aucun frame pris en charge n'est trouvé ;
- les fragments ICC sont invalides, dupliqués, contradictoires ou incomplets.

Un Exif JPEG malformé est en général rapporté comme Exif présent avec `parseStatus: "invalid"` et orientation par défaut ; cela n'interrompt pas en soi la comparaison. Les champs Exif texte individuellement hors limites peuvent être ignorés sans invalider les autres entrées.

## 9. Intégration dans le pipeline

`FinalVerificationStepClass` configure le step `final-verification` avec `inputImagePath` et `outputImagePath`. À l'exécution, il vide d'abord le rapport publié précédemment afin qu'une erreur ne laisse pas un résultat périmé, appelle `compareImages`, puis publie le rapport dans le DataStore sous la sortie `verificationReport`.

En cas d'exception, le step retourne `false` et place le message dans `result["error"]`. Les deux fichiers externes figurent aussi dans l'empreinte d'entrées du checkpoint (chemin, existence, taille et date de modification quand lisible), afin qu'un checkpoint puisse être invalidé si un fichier change.

Le rapport complet est sérialisé par le mécanisme de checkpoint associé au step ; ce comportement de pipeline est distinct des règles de comparaison de l'algorithme.

## 10. Vérifications de test couvertes

Les tests de [`test_final_verification_step.cpp`](../../../tests/steps/test_final_verification_step.cpp) couvrent notamment :

- les écarts de dimensions et ratios, et l'absence de comparaison de résolution ;
- les propriétés TIFF et les différences d'orientation ;
- l'absence de faux écarts d'encodage entre une référence JPEG et une sortie TIFF équivalente ;
- l'orientation EXIF dans les deux ordres d'octets, ainsi que l'EXIF tronqué ;
- l'identité binaire des profils ICC et le statut indéterminé lorsque leurs octets diffèrent ;
- l'extraction de dimensions JPEG et les erreurs d'entrée invalide, de segment tronqué, de format non pris en charge et de profil ICC incomplet ;
- la publication du rapport dans le DataStore ;
- les cas d'erreur de lecture et les entrées externes utilisées par le checkpoint.

Pour interpréter une différence, se référer à la propriété précise et à sa règle de lecture ci-dessus : le rapport décrit les métadonnées, pas les pixels ni le rendu final.