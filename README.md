# KIK-IRPA Assemblage — manuel d’utilisation

## 1. À quoi sert le logiciel ?

KIK-IRPA Assemblage est une interface destinée à préparer et à suivre l’assemblage de photographies en tuiles de peintures planes, notamment des images haute résolution prises en infrarouge. L’objectif d’un tel assemblage est de réunir les prises de vue partielles en une mosaïque cohérente, en contrôlant leur ordre, leur alignement, les zones à exclure et les raccords entre images.

> **État actuel : prototype d’interface.** Cette version permet de créer et enregistrer un projet, de référencer des fichiers image, de naviguer dans les vues de la maquette et d’enregistrer un rapport HTML de démonstration. Le calcul d’assemblage n’est pas connecté à l’interface : les images choisies ne sont pas analysées, les boutons de lancement affichent une progression simulée et aucune mosaïque n’est produite. Les résultats visibles (vignettes, mesures, paires et historique) sont des exemples illustratifs, pas des résultats calculés sur vos images.

## 2. Démarrer l’application

### Sur Windows

1. Ouvrir le dossier où l’application a été installée ou décompressée.
2. Double-cliquer sur **`Frontend.exe`**.

Garder les fichiers fournis avec l’exécutable dans le même dossier : les bibliothèques Qt et les composants de plateforme nécessaires au démarrage peuvent en dépendre.

## 3. Prise en main rapide

1. Dans l’écran d’accueil, choisir **Nouveau projet…**, saisir un nom, puis choisir
   le dossier contenant les tuiles. Les images reconnues directement dans ce dossier
   sont ajoutées au projet.
2. Si nécessaire, dans l’espace de travail, choisir **Tuiles → Importer un dossier de photos…**
   ou utiliser `Ctrl+I` pour ajouter d’autres images.
3. Enregistrer le projet avec **Enregistrer** ou `Ctrl+S`.
4. Parcourir les six modes à l’aide de la barre d’étapes et inspecter les panneaux.
5. Utiliser **Tout lancer** ou `F5` pour voir la démonstration de progression. Cette
   action ne calcule pas l’assemblage.
6. Utiliser **Fichier → Exporter / rapport…** pour exporter le résultat final en
   JPEG après l’exécution, ou pour créer le rapport HTML de démonstration.

Les images sources ne sont pas modifiées par l’import. L’interface conserve seulement
les chemins des fichiers sélectionnés dans le projet.

## 4. Écran d’accueil

- **Nouveau projet…** : demande un nom et le dossier des tuiles,
  puis ouvre la vue de grille. Les fichiers image présents directement dans le dossier
  des tuiles sont référencés automatiquement. Le dossier de sortie est proposé par
  défaut lors de l’enregistrement du rapport HTML.
- **Fichier → Retour à l’accueil** : revient à l’écran d’accueil sans fermer le projet.
- **Ouvrir un projet…** : ouvre un fichier projet JSON (`.kikproj` ou `.json`).
- **Importer une chaîne de traitement (JSON)…** : sélectionne un fichier et ouvre
  l’espace de travail, mais ne lit pas ni n’applique les étapes de la chaîne. Vérifier
  les chemins et paramètres reste à faire.
- **Projets récents** : affiche les projets récemment enregistrés ou ouverts. Un
  nouveau projet non enregistré est également visible pendant la session ; il devient
  réouvrable depuis la liste après son premier enregistrement.

## 5. Espace de travail

### Barre supérieure

- Le titre indique le nom de projet et le contexte illustré.
- **Enregistrer** sauvegarde le projet courant au format JSON.
- **Tout lancer** démarre la simulation de la chaîne.
- **Comparer…** ouvre la boîte de comparaison des algorithmes de couture.

### Les six modes

Dans la barre d’étapes, le mode actuellement affiché est grisé pour vous aider à vous
repérer. Les autres modes restent cliquables pour naviguer.

| Mode | À quoi il est destiné | Comportement actuel |
| --- | --- | --- |
| **Projet** | Vue générale et résumé du projet. | Affiche l’illustration générale. |
| **Tuiles** | Vérifier et organiser l’ordre des prises de vue. | Affiche les images importées dans une grille configurable. Les permutations, rotations, retournements, cases vides et paramètres de prise de vue sont enregistrés dans le projet. |
| **Masques** | Repérer et corriger les bords sombres ou les zones à exclure. | Affiche un masque rouge illustratif. Certains outils et le calque **Masques** peuvent masquer ou afficher cette illustration. Aucun masque n’est calculé ni enregistré. |
| **Alignement** | Examiner une paire de tuiles et ses points de contrôle. | Affiche deux images fictives et des points de correspondance dessinés. Les mesures de la table sont des exemples. |
| **Assemblage** | Examiner la mosaïque, les coutures et les erreurs d’alignement. | Affiche une illustration avec une couture dessinée. |
| **Export** | Choisir le format et préparer les livrables. | Permet uniquement d’enregistrer le rapport HTML décrit à la section Export. |

La barre descriptive sous les modes indique la vue active. **Tuiles → Organiser la
grille** ouvre la grille et ses paramètres.

### Choix de l’ordre des tuiles

Dans l’onglet **Grille** ou dans le menu déroulant de la barre supérieure, le choix
d’ordre met à jour les numéros de parcours affichés sur les cases :

- **Automatique (par lignes)**
- **Serpentin vertical, bas gauche**
- **Serpentin vertical, haut droite**
- **Serpentin horizontal**
- **Par lignes**
- **Par colonnes**

Le nombre de lignes et de colonnes est réglable dans l’onglet **Grille**. Cliquez sur
une case pour la sélectionner, puis glissez-la sur une autre pour permuter les images.
**Pivoter 90°** et **Retourner** modifient l’aperçu de la tuile sélectionnée.
**Marquer une case vide** masque sa tuile sans supprimer son chemin ; **Restaurer la
tuile** annule cette marque.

Les recouvrements horizontal et vertical sont réglables en pourcentage. **Lier les deux
valeurs** les synchronise. Le mode de prise de vue, l’échelle et la marge d’incertitude
sont conservés dans le fichier projet. Ces réglages ne recalculent pas les images.

**Importer une image de référence…** mémorise une image dans le projet. Sélectionnez
ensuite une tuile et utilisez **Comparer à l’image de référence** pour afficher les deux
images côte à côte. **Valider la grille** vérifie la capacité, les fichiers, les doublons
et le placement de toutes les images importées. Enregistrez le projet pour conserver
les réglages.

### Barre d’outils à gauche

Les outils indiquent une intention d’interaction avec la toile. Cliquer sur un outil
met à jour le message **Outil actif**. Les raccourcis affichés entre parenthèses sont
des indications de maquette : ils ne sont pas reliés à une action clavier spécifique
dans cette version.

| Outil affiché | Usage prévu |
| --- | --- |
| Main (`H`) | Déplacer ou parcourir la vue. |
| Zoom (`Z`) | Zoomer dans la vue. |
| Sélection (`V`) | Sélectionner une tuile ou un élément. |
| Pinceau (`B`) | Peindre une zone de masque. |
| Gomme (`E`) | Effacer une zone de masque. |
| Lasso (`L`) | Délimiter une zone libre. |
| Rectangle (`M`) | Délimiter une zone rectangulaire. |
| Point (`P`) | Placer un point de contrôle. |
| Mesure (`I`) | Mesurer une distance. |

En mode Masques, le pinceau, la gomme, le lasso et le rectangle basculent l’affichage
du masque illustratif. Ils ne modifient pas une image.

### Zoom

- **−** et **+** changent la valeur de zoom affichée par pas de 5 %.
- **Ajuster** remet la vue à l’échelle de la fenêtre.
- **100 %** affiche la valeur 100 %.
- Le menu **Affichage → Zoom avant / Zoom arrière** change la valeur par pas de 10 %.
- **Affichage → Ajuster à la fenêtre** ou `Ctrl+0` réinitialise la valeur à 100 %.

Le prototype dessine une illustration à l’échelle de la toile ; la valeur n’est pas
utilisée pour agrandir des pixels d’image réellement chargée.

### Panneaux de droite

#### Calques

Cases proposées : **Lignes de couture**, **Erreur d’alignement**, **Tuiles**,
**Mosaïque** et **Masques**. Cliquer sur une case met à jour le message d’état.
Seul le calque **Masques** commande aussi l’affichage du masque d’exemple. Les autres
calques ne sont pas superposés à des données réelles.

#### Chaîne

La liste montre huit étapes :

1. Import
2. Masques
3. Correspondance
4. Optimisation
5. Calage local
6. Égalisation
7. Ligne de couture
8. Fusion et export

Un double-clic sur une étape, ou **Relancer à partir d’ici**, démarre la progression
simulée en prenant cette étape comme point de départ. Une confirmation montre les
étapes qui seraient réutilisées et celles qui seraient recalculées. Aucun résultat
intermédiaire n’est réellement chargé ou recalculé.

Le menu d’algorithme propose **Coupe de graphe**, **Programmation dynamique** et
**Dijkstra**. Le choix ne fait qu’afficher un message et signaler que les étapes
suivantes devraient être refaites ; il ne sélectionne pas un calcul effectif.

#### Infos

Ce panneau affiche le nom du projet et le nombre de chemins de tuiles référencés.

- **Détecter les bords noirs** affiche le masque rouge de démonstration.
- **Ouvrir la vue Paire** passe en mode Alignement.
- **Valider la paire** affiche une confirmation dans la barre d’état ; elle ne valide
  ni n’enregistre de correspondance.
- **Saisir un décalage…** ouvre une boîte pour `dx` et `dy` en pixels. Les valeurs
  acceptées sont rapportées dans la barre d’état mais ne sont pas appliquées ni
  sauvegardées.

### Panneaux du bas

- **Paires** : table d’exemples avec sens, statut, confiance, contrôle NCC, décalages et
  erreur. Cliquer une ligne indique la paire sélectionnée ; double-cliquer ouvre la
  vue Alignement.
- **Journal** : affiche quelques messages d’exemple, non alimentés par une exécution
  réelle.
- **Positions** : affiche des coordonnées d’exemple pour quatre tuiles. Les valeurs ne
  sont pas calculées à partir des images importées.

## 6. Projet : créer, importer, enregistrer et rouvrir

### Créer

Choisir **Nouveau projet…** depuis l’accueil ou **Fichier → Nouveau projet…**. Saisir
un nom non vide. Le projet commence sans tuile référencée et s’ouvre en mode Tuiles.

### Importer des images

Choisir **Tuiles → Importer des tuiles…** ou appuyer sur `Ctrl+I`. Le sélecteur accepte
notamment TIFF (`.tif`, `.tiff`), PNG, JPEG et BMP. Les chemins choisis sont ajoutés à
la liste du projet, le compteur est mis à jour et l’interface passe en mode Tuiles.

L’import ne lit pas les pixels et ne vérifie pas le contenu ou la compatibilité des
fichiers. Il ne crée pas les vignettes fictives affichées par la maquette à partir de
vos images.

### Enregistrer

Choisir **Fichier → Enregistrer le projet** ou appuyer sur `Ctrl+S`. Lors du premier
enregistrement, choisir le nom et l’emplacement du fichier `.kikproj` ou `.json`.
Ensuite, l’enregistrement réutilise le chemin du fichier courant.

Le fichier contient une structure JSON de ce type :

```json
{
  "format_version": "1.0",
  "project_name": "Mon projet",
  "saved_at": "2026-10-20T12:00:00",
  "mode": 1,
  "tiles": [
    "C:/images/tuile_01.tif",
    "C:/images/tuile_02.tif"
  ]
}
```

Seuls le nom, l’heure de sauvegarde, le mode courant et les chemins des tuiles sont
enregistrés. Les masques, décalages saisis, choix de couture, résultats, journal,
positions et réglages de comparaison ne sont pas persistés.

### Ouvrir

Choisir **Fichier → Ouvrir un projet…** ou `Ctrl+O`, puis sélectionner un fichier JSON
de projet. Le logiciel vérifie que le fichier peut être lu et qu’il contient un objet
JSON, restaure le nom, les chemins et le mode, puis affiche le projet sans recalcul.
Il faut vérifier que les fichiers référencés existent toujours : l’ouverture ne
relocalise pas les images et ne les charge pas.

## 7. Lancer ou reprendre une chaîne

- **Tout lancer** (ou `F5`) démarre la démonstration depuis l’étape 1.
- **Assemblage → Relancer à partir de…** démarre la démonstration à partir de l’étape
  6.
- Dans le panneau **Chaîne**, double-cliquer une étape ou utiliser **Relancer à partir
  d’ici**.
- Une relance à partir d’une étape après la première affiche une confirmation décrivant
  les étapes prétendument réutilisées et recalculées.
- Une boîte de confirmation apparaît si aucune tuile n’est référencée. Accepter ne
  transforme pas la démonstration en calcul réel.
- Pendant la progression, **Annuler** arrête l’animation.

La barre avance par incréments jusqu’à 100 % en environ 2,5 secondes. À la fin, l’état
indique explicitement qu’aucun calcul d’image n’a été effectué. Il s’agit uniquement
d’une démonstration de l’interface.

## 8. Comparer deux méthodes de couture

Ouvrir **Comparer…** ou choisir **Assemblage → Comparer deux exécutions…**. Sélectionner
deux méthodes parmi :

- Programmation dynamique
- Coupe de graphe
- Dijkstra

**Comparer** bascule une illustration de comparaison. **Garder A** ou **Garder B**
affiche un message indiquant que ce résultat serait conservé. Les méthodes choisies ne
sont pas exécutées et aucune mesure comparative n’est calculée dans cette interface.

## 9. Exporter

Choisir **Fichier → Exporter / rapport…** ou passer en mode Export. Les formats proposés
pour la mosaïque sont :

- **TIFF 16 bits** : format envisagé pour conserver une grande précision ;
- **TIFF 8 bits** : réduction de précision par rapport au 16 bits ;
- **PSB** : format envisagé pour les très grandes images ;
- **JPEG** : compression avec perte.

Le profil ICC est indiqué comme conservé dans la maquette. Le résultat final disponible
dans l’interface peut être exporté en JPEG après l’exécution de l’assemblage.

Pour exporter le résultat final :

1. Sélectionner **JPEG — perte de précision**.
2. Cliquer **Enregistrer…**.
3. Choisir le chemin du fichier `.jpg` ou `.jpeg`.

Pour enregistrer le rapport HTML, sélectionner un autre format, laisser cochée
**Générer le rapport d’assemblage (HTML)**, puis cliquer **Enregistrer…**.

Le rapport généré contient le nom du projet, la date, le format demandé et le nombre de
chemins de tuiles référencés. Il précise que les mesures sont celles de la maquette ;
il ne contient pas de résultat mesuré. Si la case du rapport est décochée, l’interface
signale que le backend est requis et ne produit pas d’image.

## 10. Menus et raccourcis clavier

| Menu / raccourci | Action |
| --- | --- |
| `Ctrl+N` | Nouveau projet |
| `Ctrl+O` | Ouvrir un projet |
| `Ctrl+I` | Importer des tuiles |
| `Ctrl+S` | Enregistrer le projet |
| `Ctrl+E` | Ouvrir Exporter / rapport |
| `F5` | Lancer la démonstration de chaîne |
| `Ctrl+Z` | Afficher le message « Dernière action annulée » ; aucune modification n’est réellement annulée |
| `Ctrl+Y` | Afficher le message « Action rétablie » ; aucune modification n’est réellement rétablie |
| `Ctrl++` / raccourci Zoom avant du système | Augmenter le zoom affiché de 10 % |
| `Ctrl+-` / raccourci Zoom arrière du système | Réduire le zoom affiché de 10 % |
| `Ctrl+0` | Ajuster la vue (valeur remise à 100 %) |
| `Ctrl+Q` | Quitter |
| `Tab` | Afficher ou masquer les panneaux de droite et du bas |

Les lettres `H`, `Z`, `V`, `B`, `E`, `L`, `M`, `P` et `I` sont affichées comme
raccourcis d’outils dans les info-bulles, mais ne sont pas encore implémentées comme
raccourcis clavier. Le menu **Affichage → Afficher / masquer les panneaux** réalise
également l’action de `Tab`.

Les commandes **Annuler** et **Rétablir** sont elles aussi illustratives : elles
changent le message d’état sans restaurer ou modifier des données.

## 11. Conseils et limites à connaître

- Conserver les images sources originales : le prototype ne les modifie pas.
- Enregistrer le projet pour garder le nom, le mode courant et les chemins sélectionnés.
- Après avoir déplacé des images, vérifier leurs chemins ; aucun mécanisme de recherche
  ou de réparation n’est fourni.
- Ne pas interpréter la confiance, le NCC, les décalages, les positions ou les coutures
  visibles comme des mesures de vos images : ils sont codés comme exemples.
- Ne pas utiliser la progression simulée comme preuve que les étapes d’assemblage ont
  été exécutées.
- La sélection d’algorithme, l’ordre des tuiles, la correction manuelle, la validation
  de paire, les calques autres que Masques et les outils de dessin ne modifient pas de
  résultats persistants.
- Le code du dépôt comporte des composants backend d’algorithmes, mais la cible
  d’application définie dans `CMakeLists.txt` ne compile actuellement que l’interface
  et celle-ci indique que le backend n’est pas connecté. Leur présence dans le dépôt
  ne signifie donc pas que les calculs sont accessibles depuis cette fenêtre.

## 12. Dépannage

### L’application ne démarre pas

Vérifier que tous les fichiers fournis avec `Frontend.exe` sont toujours présents dans
le même dossier. Si Windows signale une bibliothèque manquante ou si le problème
persiste, demander le paquet complet de l’application à la personne qui l’a distribué.

### Le projet s’ouvre, mais les images ne sont pas visibles

C’est le comportement actuel : l’import stocke les chemins sans afficher les véritables
images. Les illustrations de l’interface sont des exemples intégrés.

### Le lancement de la chaîne ne produit pas de mosaïque

C’est normal dans cette version : **Tout lancer** anime une barre de progression, mais
le backend de calcul n’est pas connecté à la fenêtre.
