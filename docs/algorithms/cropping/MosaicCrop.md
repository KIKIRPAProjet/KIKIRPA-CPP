# Rognage de mosaïque TIFF

`CroppingNs::cropMosaic()` découpe un rectangle dans une `DataNs::Mosaic`
produite par `fusion`. L'ancien format de blocs PNG n'est plus utilisé.

## Entrée et sortie

`CropInput` contient la mosaïque source, le rectangle en pixels pleine résolution
et le dossier de sortie.

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

## Publication et erreurs

Le parent du dossier de sortie doit déjà exister. Les fichiers sources ne
sont jamais modifiés et ne doivent pas se trouver dans le dossier de sortie.

Les trois TIFF sont écrits dans un dossier temporaire voisin. La publication
ne commence qu'après fermeture réussie des trois writers.

Un dossier de sortie existant peut être remplacé s'il contient le marqueur
`.mosaic-crop-owner` avec la signature attendue, et uniquement les fichiers
prévus. Un ou plusieurs TIFF peuvent manquer : la relance les recrée.

Les dossiers sans marqueur valide, les entrées étrangères et les liens
symboliques sont refusés. Les résultats d'une ancienne version sans marqueur
doivent être déplacés manuellement avant de réutiliser leur chemin.

Lors du remplacement, l'ancien résultat est renommé en sauvegarde, puis
le nouveau résultat prend sa place. La sauvegarde est ensuite supprimée
si le système le permet.

Si la publication échoue, l'ancien résultat est restauré. Si la restauration
échoue également, la sauvegarde est conservée et son chemin est indiqué
dans le message d'erreur.

La fonction renvoie `false`, vide la sortie et renseigne `errorMessage`
en anglais en cas d'échec. En cas de succès, elle renvoie `true` et publie
les chemins définitifs dans la sortie.

Les renommages successifs ne constituent pas une transaction résistante
à une interruption brutale du processus. Le nettoyage des temporaires
n'est pas garanti après une telle interruption. Les exécutions simultanées
vers le même dossier ne sont pas prises en charge.

Pendant une relance, les sources, l'ancien résultat et le nouveau résultat
coexistent sur disque. La mémoire utilisée reste limitée au traitement
par blocs.

## Tests

## Tests

Les images synthétiques vérifient :

- une découpe traversant plusieurs blocs ;
- les trois canaux RGB distincts ;
- la provenance et le masque ;
- les chemins Unicode ;
- les coordonnées hors limites ;
- le refus d'un type source incohérent ;
- le remplacement d'un précédent résultat avec un autre rectangle ;
- la recréation de chacun des trois TIFF après sa suppression ;
- la conservation du précédent résultat si les nouvelles sources sont invalides ;
- le refus d'un dossier étranger ou contenant des entrées étrangères ;
- le refus de remplacer un dossier contenant les sources.

Ces tests ne couvrent pas encore les échecs pendant l'écriture, la publication
ou la restauration de la sauvegarde.