# Rognage de mosaïque TIFF

`CroppingNs::cropMosaic()` découpe un rectangle dans une `DataNs::Mosaic`
produite par `fusion`. L'ancien format de blocs PNG n'est plus utilisé.

## Entrée et sortie

`CropInput` contient la mosaïque source, le rectangle en pixels pleine résolution
et le nouveau dossier de sortie.

Le rectangle doit être non vide et inclus dans `canvasSize`.
Ses bords droit et inférieur sont exclus.

La sortie est une `DataNs::Mosaic` :

- `imagePath` : TIFF `CV_16UC3`, RGB ;
- `provenancePath` : TIFF `CV_16UC1` ;
- `maskPath` : TIFF `CV_8UC1` ;
- `canvasSize` : taille du rectangle conservé.

Les identifiants de provenance et les pixels invalides du masque sont conservés.
L'origine du résultat devient `(0, 0)`.

## Traitement par blocs

Les en-têtes des trois sources sont vérifiés : TIFF tuilés, dimensions conformes
à `canvasSize` et types conformes au contrat de fusion.

`PyramidalTiffReaderClass` lit le niveau 0.
`TiledTiffWriterClass` écrit une nouvelle grille de blocs de 512 pixels.

Une origine non multiple de 512 est acceptée. Chaque bloc de sortie est lu dans
la région source décalée correspondante. Les blocs des bords peuvent être plus
petits. Aucune image complète n'est chargée par l'algorithme.

Les pixels sont copiés sans conversion de profondeur ou de canaux.
Le lecteur TIFF conserve l'ordre RGB des échantillons.
Les chemins sont gérés avec les fonctions Unicode du projet.

Les métadonnées ICC et de résolution ne sont pas recopiées par ce traitement.

## Publication et erreurs

Le dossier de sortie doit être inexistant et son parent doit déjà exister.
Les sources ne sont jamais modifiées.

La sortie n'est publiée qu'après fermeture réussie des trois writers.
En cas d'échec, le dossier créé et ses fichiers sont supprimés si le système le
permet. Le nettoyage n'est pas garanti après interruption brutale du processus.

La fonction renvoie `false`, vide la sortie et renseigne `errorMessage` en anglais
en cas d'échec. En cas de succès, elle renvoie `true` et le message est vide.

## Tests

Les images synthétiques vérifient :

- une découpe traversant plusieurs blocs ;
- les trois canaux RGB distincts ;
- la provenance et le masque ;
- les chemins Unicode ;
- les coordonnées hors limites ;
- le refus d'un dossier existant ;
- le refus d'un type source incohérent.