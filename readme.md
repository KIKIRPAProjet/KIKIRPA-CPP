feat(pipeline): ajout de RecouvrementStep — estimation du recouvrement entre tuiles

## Contexte
Nouvelle étape du pipeline d'assemblage qui estime automatiquement la zone
de recouvrement entre tuiles voisines (droite et dessous), en réutilisant
l'algorithme existant EstimationRecouvrement (corrélation de phase).

## Fichiers créés

### include/Backend/steps/RecouvrementStep.h
- Déclaration de la classe RecouvrementStep (hérite de Step)
- Structures RecouvrementInput (options : overlap, lissage, seuilRapport,
  marge, fractionValideMin) et RecouvrementOutput (liste de paires)
- Documentation complète de la convention de repère (origine coin
  haut-gauche, X vers la droite, Y vers le bas, unités en pixels)

### src/Backend/steps/RecouvrementStep.cpp
- Implémentation de execute() :
  * Lecture à la demande des images via cv::imread(IMREAD_UNCHANGED)
    → au plus 2 images en RAM simultanément, profondeur 16 bits préservée
  * Validation du paramètre overlap dans ]0, 1[
  * Vérification de compatibilité (type OpenCV, dimensions) avant estimation
  * Appel à EstimationRecouvrement::estimer() + zonesCommunes()
  * Publication du résultat dans le DataStore sous la clé "recouvrements"
  * Traçabilité des échecs dans outputData["failures"] avec raison explicite

### tests/algorithms/stitching/recouvrement/test_recouvrement_step.cpp
- Suite de 35 assertions couvrant :
  1. Entrées invalides (overlap hors ]0,1[, DataStore absent, paramètres invalides)
  2. Images manquantes, types incompatibles (CV_8U vs CV_16U),
     dimensions incompatibles (500×500 vs 600×600)
  3. Paires sans recouvrement et images uniformes (pic ambigu)
  4. Grille synthétique 2×2 avec vérité terrain :
     - Erreur translation mesurée : 0.0 px (tolérance 1.0 px)
     - Erreur recouvrement mesuré : 0.00 % (tolérance 0.5 %)
     - Vérification géométrique : zoneB == zoneA − round(translation)
     - Vérification toPairContext() pour compatibilité avec l'étape suivante
  5. Intégration complète ImportStep → RecouvrementStep dans un Pipeline

## Fichiers modifiés

### include/Backend/core/Types.h
- Ajout de la structure PairRecouvrement
  { idA, idB, direction, recouvrement (RecouvrementReel), zoneA, zoneB }
- Ajout du type alias : using Recouvrements = vector<PairRecouvrement>
- Ajout de la méthode helper toPairContext() pour convertir vers
  l'interface PairContext utilisée par les étapes de matching en aval

### cmake/algorithms.cmake
- Enregistrement de l'exécutable test_recouvrement_step
- Ajout du test CTest correspondant avec label "unit"

## Résultats des tests (exécutés sur Ubuntu 24.04 / WSL)
  correlation_phase  .... Passed   5.67 s
  seamline           .... Passed   0.05 s
  chaine_paire       .... Passed   3.89 s
  recouvrement_step  .... Passed   0.86 s
  100 % tests passés (4/4)

## Remarques pour l'équipe
- L'algorithme EstimationRecouvrement n'a pas été modifié.
- Les interfaces publiques existantes sont intactes.
- readRegion() n'étant pas encore implémenté dans Tuile, les images
  sont lues en pleine résolution (même comportement que PhaseCorrelationStep).
- L'enregistrement JSON dans PipelineJson ("recouvrement" → RecouvrementStep)
  reste à faire pour permettre le chargement depuis un fichier de pipeline.
