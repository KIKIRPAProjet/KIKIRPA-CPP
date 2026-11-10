Après la fusion de `main`, le registre contient 10 steps (`export` et `dynamic-programming-dijkstra` sont arrivés entre-temps), mais `REGISTERED_STEP_COUNT` est resté à 9 dans `tests/core/test_pipeline.cpp`. Il faut le passer à `10`, avec le message `"10 steps enregistrés"`.

### 1. Avec une référence JPEG, `differences` contient surtout de faux écarts
`compareMetadata` compare toutes les clés présentes d'un côté ou de l'autre, y compris celles qui n'existent que dans un format :
- `compression` vaut le marqueur JPEG (`0xC0` = 192) d'un côté et le code TIFF de l'autre (LZW = 5 pour le `mosaic.tif` de l'export) ;
- `photometricInterpretation`, `planarConfiguration`, `sampleFormat`, `extraSamples`, `exifDirectoryPresent` et `exifParseStatus` n'existent que côté TIFF ;
- `exif`, `exifPresent`, `exifPayloadBytes` et `jfifVersion` n'existent que côté JPEG.

Une reconstruction parfaite d'une référence JPEG affiche donc une dizaine de « différences ». Même entre deux TIFF, « non compressé » contre LZW remonte comme un écart, alors que c'est un détail d'encodage.

`jpegMetadataSegments` ne peut jamais rien apprendre d'utile : la sortie est toujours un TIFF, qui n'a pas de segments. Soit les deux listes sont vides, soit chaque segment APP du JPEG compte comme une différence.

Proposition : ne comparer que les propriétés communes aux deux formats (dimensions, `bitsPerSample`, `samplesPerPixel`, orientation, ICC, tags texte), lister les détails d'encodage propres à chaque format à titre informatif (hors `differences`), et retirer la comparaison des segments JPEG. Il faudrait aussi qu'un test vérifie `differences` dans le cas JPEG → TIFF (`HandlesJpegMetadata` ne le regarde pas).

### 2. L'orientation EXIF n'est pas prise en compte dans les dimensions
Pour une photo prise en portrait (orientation EXIF 5 à 8), la largeur et la hauteur stockées sont inversées par rapport à l'image affichée. Le rapport donne alors des ratios de 0,67 et 1,5 alors que la reconstruction correspond. Il faudrait au moins un avertissement dans `warnings` quand l'orientation vaut 5 à 8, et idéalement des dimensions « affichées » (après rotation) à côté des dimensions stockées.

### 3. Tests manquants
Le parseur EXIF JPEG (environ 150 lignes de lecture binaire avec des offsets) n'a aucun test : orientation, tags texte, ordre des octets `II`/`MM`, EXIF tronqué → `parseStatus: "invalid"`. Côté erreurs, seul le fichier absent est testé. Il manque : signature JPEG invalide, segment tronqué, extension non prise en charge, sortie qui n'est pas un TIFF, profil ICC réparti sur plusieurs segments mais incomplet. On peut construire ces JPEG à la main dans le test, comme le fait déjà `addJpegIccProfile`.

### Points mineurs
- **Dossier temporaire fixe** (`kikirpa_final_verification_test`) : `gtest_discover_tests` lance chaque `TEST` dans son propre processus, donc avec `ctest -j` les tests suppriment le dossier les uns des autres. `test_export_step.cpp` génère un nom unique, on peut reprendre ce modèle.
- **Lien avec l'export** : l'export ne publie pas son chemin dans le DataStore, donc `outputImagePath` doit être recopié à la main (`<outputDirectory>/mosaic.tif`), et rien n'empêche de placer le step avant l'export. Ce n'est pas bloquant pour cette PR, à discuter pour une suite.
- **Structure** : l'algorithme tient dans un seul `.cpp` de 739 lignes. Les conventions (§8) demandent un header façade et un module par phase (lecture TIFF, lecture JPEG, comparaison), dans `algorithms/verification/final_verification/`. Les deux pages `verification.md` et `verification_working.md` se recouvrent beaucoup : on pourrait les fusionner.
- **Détails** : `JPEG_LENGTH_FIELD_SIZE` sert aussi pour la taille d'un champ EXIF 16 bits et pour le nombre maximal de valeurs SHORT stockées directement dans l'entrée. Des constantes dédiées seraient plus lisibles. `json_fwd.hpp` et `json.hpp` sont inclus ensemble avec des `NOLINT` : `json.hpp` seul suffit.



Tu n'es pas obligé de tout corriger dans les points mineurs avant de merge on peut y regarder plus tard mais les point d'avants doivent être modifier

Merci
