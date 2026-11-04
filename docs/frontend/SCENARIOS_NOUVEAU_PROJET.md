# Parcours « nouveau projet » — scénarios à vérifier avec Nathan

## Objectif

Vérifier qu'un utilisateur peut sélectionner ses dossiers, organiser six tuiles dans une grille 3 × 2, régler les paramètres et produire un JSON accepté par le backend.

## Préparation

Créer un dossier contenant exactement six TIFF lisibles. Les noms peuvent être
libres : leur position est définie dans la grille de l'interface.

- `tuile-01.tif`, `tuile-02.tif` ;
- `tuile-03.tif`, `tuile-04.tif` ;
- `tuile-05.tif`, `tuile-06.tif`.

Créer également un dossier de sortie vide.

## Scénario nominal

1. Sur l'accueil existant, cliquer sur **Nouveau projet…** et saisir un nom.
2. Sélectionner successivement le dossier des tuiles et le dossier de sortie.
3. Ouvrir le mode **Tuiles**. Vérifier les valeurs par défaut : grille 3 × 2 et recouvrements horizontal et vertical de 20 %.
4. Réorganiser les tuiles par glisser-déposer si nécessaire.
5. Vérifier que les six noms sont affichés à la bonne ligne et à la bonne colonne.
6. Cliquer sur **Valider la grille**.
7. Vérifier qu'un fichier `<nom-du-projet>.json` apparaît dans le dossier de sortie.
8. Vérifier dans le JSON : les deux chemins absolus, `grid.rows = 3`, `grid.columns = 2`, les six tuiles, le recouvrement `0.2`, le step `import`, le step `phase-correlation` et `run.checkpointDirectory`.

Résultat attendu : un message confirme que le JSON a été validé par le backend
et affiche son chemin. L'utilisateur reste dans l'interface complète.

## Scénarios d'erreur

### Tuile dupliquée

Importer deux fois le même chemin ou affecter la même image à deux cases.

Résultat attendu : la génération est bloquée et le doublon est nommé.

### Fichier supprimé après import

Importer les tuiles, supprimer l'un des fichiers, puis générer.

Résultat attendu : la génération est bloquée et le chemin absent est affiché.

### Grille incohérente

Passer la grille à 2 × 2 en conservant six fichiers, ou à 3 × 2 avec seulement cinq fichiers.

Résultat attendu : la génération est bloquée avec le nombre de cases et de tuiles attendu/trouvé.

### Dossiers invalides

Ne pas sélectionner le dossier de sortie, ou déplacer le dossier d'entrée après l'import.

Résultat attendu : la génération est bloquée et le dossier concerné est indiqué.

## Point hors périmètre de ce parcours

La validation génère et valide le script ; elle ne lance pas encore le calcul.
Le raccordement de **Lancer** au backend dans un thread séparé constitue
l'étape suivante, après validation de ce parcours par Nathan.
