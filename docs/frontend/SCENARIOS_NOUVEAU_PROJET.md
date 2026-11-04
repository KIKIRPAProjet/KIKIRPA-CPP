# Parcours « nouveau projet » — scénarios à vérifier avec Nathan

## Objectif

Vérifier qu'un utilisateur peut sélectionner ses dossiers, organiser six tuiles dans une grille 3 × 2, régler les paramètres et produire un JSON accepté par le backend.

## Préparation

Créer un dossier contenant exactement ces six TIFF lisibles :

- `img_C0_R0.tif`, `img_C1_R0.tif` ;
- `img_C0_R1.tif`, `img_C1_R1.tif` ;
- `img_C0_R2.tif`, `img_C1_R2.tif`.

Créer également un dossier de sortie vide.

## Scénario nominal

1. Ouvrir l'application et saisir un nom de projet.
2. Sélectionner le dossier d'entrée et le dossier de sortie.
3. Garder les valeurs par défaut : grille 3 × 2, recouvrement 20 %, niveau 0 et format `img_C(\d+)_R(\d+)\.tif`.
4. Cliquer sur **Importer les TIFF**.
5. Vérifier que les six noms sont affichés à la bonne ligne et à la bonne colonne.
6. Cliquer sur **Valider et générer le JSON**.
7. Vérifier qu'un fichier `<nom-du-projet>.json` apparaît dans le dossier de sortie.
8. Vérifier dans le JSON : les deux chemins absolus, `grid.rows = 3`, `grid.columns = 2`, les six tuiles, le recouvrement `0.2`, le step `import`, le step `phase-correlation` et `run.checkpointDirectory`.

Résultat attendu : un message confirme que le JSON a été validé par le backend.

## Scénarios d'erreur

### Tuile dupliquée

Déplacer une tuile vers une position incompatible ou préparer deux entrées identiques dans le modèle.

Résultat attendu : la génération est bloquée et le doublon est nommé.

### Fichier supprimé après import

Importer les tuiles, supprimer l'un des fichiers, puis générer.

Résultat attendu : la génération est bloquée et le chemin absent est affiché.

### Grille incohérente

Passer la grille à 2 × 2 en conservant six fichiers, ou à 3 × 2 avec seulement cinq fichiers.

Résultat attendu : la génération est bloquée avec le nombre de cases et de tuiles attendu/trouvé.

### Position incompatible avec le nom

Sélectionner `img_C0_R0.tif`, cliquer sur **Déplacer après**, puis générer.

Résultat attendu : la génération est bloquée, car le backend déduit la colonne et la ligne des deux groupes du nom.

### Mauvais format

Saisir une expression régulière invalide, sans deux groupes, ou ne correspondant pas aux fichiers.

Résultat attendu : la génération est bloquée avec une explication précise.

### Dossiers invalides

Ne pas sélectionner le dossier de sortie, ou déplacer le dossier d'entrée après l'import.

Résultat attendu : la génération est bloquée et le dossier concerné est indiqué.

## Point hors périmètre de ce parcours

Le bouton génère et valide le script ; il ne lance pas encore le calcul. Le raccordement de **Lancer** au backend dans un thread séparé constitue l'étape suivante, après validation de ce parcours par Nathan.
