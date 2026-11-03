// include/backend/steps/phase_correlation_step.h
#pragma once

#include "algorithms/stitching/correlation_phase/CorrelationPhase.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/step.h"

#include <opencv2/core.hpp>

#include <vector>

namespace StepsNs {

    // Valeurs initiales des paramètres du step.
    struct PhaseCorrelationInput {
        float overlap = 0.2f;   // recouvrement nominal entre deux tuiles voisines, en fraction de la tuile
        int niveau = 0;         // niveau de la pyramide analysé : 0 = pleine résolution, n = 1/2^n (0 à LEVEL_COUNT - 1)
        stitching::matching::CorrelationPhase::Parametres parametres;   // réglages de l'algorithme, en px du niveau analysé
    };

    // Sortie du step.
    struct PhaseCorrelationOutput {
        std::vector<DataNs::PairMatches> pairs;   // toujours en px de pleine résolution, quel que soit le niveau analysé
    };

    // Mesure, par corrélation de phase, la translation au sous-pixel entre chaque paire de tuiles voisines
    // (voir docs/steps/PhaseCorrelationStep.md).
    //
    // Nom : "phase-correlation".
    // Paramètres : overlap (float), niveau (int) et les champs de CorrelationPhase::Parametres : lissage, seuilRapport,
    //   lissageLocal, ecartMax, espacementMin, marge, fractionValideMin (float) ; nbPoints, fenetre, facteurDft (int).
    //   Un paramètre absent du JSON garde sa valeur par défaut.
    // Entrée : "Tiles" (clé "tiles").   Sortie : "PairMatches" (clé "matches").
    //
    // Paires : pour chaque tuile A, sa voisine de droite puis celle du dessous (B). Chaque PairMatches contient
    //   tileIdA, tileIdB et direction (Right ou Bottom : position de B par rapport à A).
    // Repère : origine au centre du pixel haut-gauche de A, x vers la droite, y vers le bas.
    // Signe : (translationX, translationY) = coin haut-gauche de B dans le repère de A. Un pixel (X, Y) de A correspond
    //   au pixel (X − translationX, Y − translationY) de B, et pointsInA[i] − pointsInB[i] ≈ (translationX, translationY).
    //   Right : translationX > 0, translationY du signe du décalage vertical de B (négatif si B est plus haute que A).
    //   Bottom : translationY > 0, translationX négatif si B est décalée vers la gauche.
    // Niveau réduit (niveau = n, s = 2^n) : la mesure est faite sur les images réduites, puis ramenée en pleine
    //   résolution : translation × s, et chaque point p devient (p + 0,5)·s − 0,5 (centre des pixels moyennés). Les
    //   longueurs des paramètres (marge, fenetre, ecartMax...) et les messages d'échec sont en px du niveau analysé.
    //
    // Échecs : une paire non mesurable est ignorée et listée dans getResult()["failures"] ({idA, idB, reason}). Le
    //   step lui-même échoue (cause dans getResult()["error"]) si aucune paire ne réussit, si un paramètre est invalide,
    //   si le DataStoreClass n'a pas de tuiles, ou si une image est illisible.
    class PhaseCorrelationStepClass : public StepNs::StepClass {

        public:
            explicit PhaseCorrelationStepClass(const PhaseCorrelationInput& initialInput = {});
            bool execute() override;
            const PhaseCorrelationOutput& getOutput() const { return output; }

            // Pixels de la tuile au niveau `level` de la pyramide, type d'origine (8/16 bits, gris ou couleur). Vide si
            // le fichier est illisible. Seul point d'accès aux pixels du step : à remplacer par la lecture commune d'une
            // région (readRegion) dès qu'elle existera. En attendant, lit l'image d'origine et la réduit de moitié
            // autant de fois que nécessaire, comme le fait createPyramidalTiledTiff.
            static cv::Mat readPixels(const DataNs::TileClass& tile, int level);

            // Ramène une mesure faite au niveau `level` vers la pleine résolution (voir « Niveau réduit » ci-dessus).
            static void scaleToFullResolution(DataNs::Matches& matches, int level);

        private:
            PhaseCorrelationOutput output;

    };

}
