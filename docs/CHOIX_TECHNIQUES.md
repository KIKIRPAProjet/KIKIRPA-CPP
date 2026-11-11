# Choix techniques

Ce document explique les principaux choix techniques de KIKIRPA-CPP et leur
rôle dans le projet.

## Langage : C++17

Le C++ convient au traitement d'images de grande taille : il donne un contrôle
fin sur les ressources et les performances, tout en permettant de structurer
le programme en classes et composants réutilisables. Il s'intègre directement
aux bibliothèques de vision par ordinateur et à l'interface Qt utilisées ici.
La norme C++17 fournit des fonctionnalités modernes sans imposer un
environnement de compilation trop récent. Elle est définie par les cibles
CMake du backend et du frontend.

## Bibliothèques

| Technologie | Rôle et justification |
|---|---|
| **OpenCV 4** | Fournit les opérations courantes sur les images et plusieurs briques algorithmiques (matrices, traitement, détection et appariement). Elle évite de réimplémenter des traitements éprouvés et prépare notamment les entrées du modèle LightGlue via son module `dnn` ; l'inférence est exécutée par ONNX Runtime. |
| **Qt 6 (Widgets, Concurrent)** | `Widgets` permet de construire une interface de bureau native en C++. `Concurrent` fournit des outils de travail asynchrone, utiles pour ne pas bloquer l'interface pendant les traitements. La bibliothèque frontend est séparée du backend. |
| **libtiff** | Prend en charge les fichiers TIFF, notamment les images tuilées et pyramidales adaptées aux grandes mosaïques. |
| **ONNX Runtime 1.20.1** | Exécute le modèle neuronal LightGlue au format ONNX pour l'appariement de points entre images. Sa version est fixée et l'archive téléchargée par CMake est vérifiée par empreinte SHA-256 afin de rendre les configurations plus reproductibles. |
| **nlohmann/json** | Fournit la lecture et l'écriture du format JSON du pipeline. C'est une bibliothèque C++ légère, intégrable au projet et disponible via le gestionnaire de dépendances CMake si elle n'est pas déjà installée. |
| **GoogleTest** | Sert à écrire des tests unitaires isolés pour le backend, les étapes de traitement et l'interface. CTest les découvre et les exécute. |

## Architecture frontend / backend

Le code est divisé en deux bibliothèques :

- **`kikirpa_frontend`** regroupe l'interface utilisateur Qt ;
- **`kikirpa_backend`** regroupe le traitement, les données et les algorithmes,
  sans dépendre de l'interface graphique.

Cette séparation permet de faire évoluer et de tester les traitements sans
lancer l'interface, et de travailler sur les deux parties avec moins de
couplage. L'exécutable principal assemble ces bibliothèques.

Le pipeline constitue le contrat d'exécution du backend. Sa configuration
décrit en JSON une liste ordonnée d'étapes (*steps*) et leurs paramètres.
Chaque étape déclare ses entrées et sorties typées ; le pipeline vérifie avant
l'exécution que les étapes existent, que les paramètres sont valides et que
les données nécessaires sont produites en amont. Les résultats passent par un
`DataStore`, plutôt que d'être transmis directement d'une étape à l'autre.

### Steps : un traitement modulaire

Un *step* est une unité de traitement autonome du backend, par exemple
l'import des images, leur appariement ou le calcul de leur position. Il
déclare un nom, des paramètres (avec des valeurs par défaut), les données
qu'il attend et celles qu'il produit. Il lit ses entrées dans le `DataStore`,
effectue son traitement, puis y publie ses sorties. Il ne dépend donc pas
directement de l'interface ni de l'implémentation interne des steps voisins :
le contrat entre eux est constitué des clés et types des données échangées.

Cette organisation vise la modularité et le travail en équipe. Les personnes
peuvent développer des étapes différentes en parallèle, à condition de se
mettre d'accord sur les types et les clés partagés, sans devoir modifier un
traitement monolithique ni coordonner chaque détail d'implémentation. Une étape
peut être testée seule, réutilisée dans plusieurs pipelines ou remplacée par
une autre implémentation qui respecte le même contrat. Par exemple, plusieurs
steps d'appariement peuvent produire le même type `PairMatches` ; le pipeline
peut alors choisir l'algorithme sans changer les étapes qui consomment ces
résultats.

Pour garder cette indépendance, un step se limite à l'orchestration de ses
entrées, paramètres, erreurs et sorties. Un calcul substantiel est placé dans
un algorithme distinct, qui ne connaît ni le JSON ni le `DataStore` et peut
être testé indépendamment. Cette séparation rend les tests plus faciles à
écrire, à lancer et à diagnostiquer individuellement : on peut vérifier
l'algorithme ou le comportement d'un step sans exécuter tout le pipeline ni
préparer l'interface. Les tests de step vérifient son contrat et son
comportement ; la validation du pipeline vérifie que les steps assemblés sont
compatibles. Une fiche et des tests accompagnent chaque nouvelle étape.
La procédure complète est décrite dans [Ajouter un step](steps/AJOUTER_UN_STEP.md)
et les conventions dans [CONVENTIONS.md](CONVENTIONS.md).

Le format JSON est lisible et facile à générer ou à archiver. Il rend la
configuration explicite et permet de conserver le script associé à une
exécution. Les checkpoints optionnels sauvegardent les sorties sérialisables
après chaque étape : une reprise peut réutiliser les résultats valides et
recalculer l'étape dont les paramètres ou les entrées externes ont changé.
Les images sources elles-mêmes ne sont pas copiées dans ces checkpoints ; les
fichiers TIFF restent nécessaires.

L'objectif est qu'un utilisateur puisse rouvrir dans l'interface le script
JSON d'une exécution terminée, modifier sa configuration et choisir l'étape à
partir de laquelle reprendre. Le pipeline réutilise alors les checkpoints
valides des étapes précédentes et recalcule l'étape choisie ainsi que celles
qui la suivent, sans refaire inutilement tout le traitement. Si une étape
modifiée ou une entrée externe rend un checkpoint invalide, cette étape et
toutes les suivantes sont recalculées pour éviter de mélanger des résultats
incohérents. Le script conserve l'état d'exécution et les étapes terminées ;
le répertoire de checkpoints conserve leurs sorties réutilisables.

Le backend fournit déjà les mécanismes de reprise (`startFromStep`,
`skipCompletedSteps` et checkpoints). En revanche, la reprise depuis
l'interface — ouvrir le script, le modifier et sélectionner l'étape — reste
à intégrer ; il ne s'agit pas encore d'un parcours utilisateur disponible.
Les images sources et autres fichiers externes nécessaires doivent rester
accessibles. Les détails du format, de la validation et des checkpoints sont
décrits dans [Pipeline](core/Pipeline.md).

## Construction et portabilité

CMake configure le projet et organise ses cibles ; les presets fournissent
des configurations communes pour Windows, Linux et macOS. Cette approche
centralise la découverte des dépendances et permet d'utiliser les mêmes
commandes de compilation et de test sur les plateformes prises en charge.
Les presets Windows et Linux utilisent Ninja ; celui de macOS utilise
Unix Makefiles.

Les dépendances sont recherchées sur le système lorsque c'est pertinent.
CMake peut récupérer automatiquement certaines dépendances fixées à une
version, notamment ONNX Runtime et, si nécessaire, nlohmann/json. Cela
simplifie la mise en place tout en limitant les différences de version ; la
première configuration nécessite une connexion Internet pour les dépendances
téléchargées. Les instructions détaillées figurent dans [BUILD.md](BUILD.md).
