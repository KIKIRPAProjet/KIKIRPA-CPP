#pragma once
#include "core/Step.h"
#include "core/Tuile.h"
#include "algorithms/stitching/correlation_phase/CorrelationPhase.h"
#include <vector>

namespace stitching::steps {

// Entrée du step = ses options
struct PhaseCorrelationInput {
    float overlap = 0.2f;   // recouvrement nominal, fraction de la tuile
    int niveau = 0;         // niveau de la pyramide analysé : 0 = pleine résolution, n = 1/2^n (0 à NB_NIVEAUX - 1)
    matching::CorrelationPhase::Parametres parametres;   // longueurs en px du niveau analysé
};

// Sortie du step = sa signature
struct PhaseCorrelationOutput {
    std::vector<PairMatches> pairs;   // toujours en px de pleine résolution, quel que soit le niveau analysé
};

// Translation au sous-pixel entre chaque paire de tuiles voisines, par corrélation de phase
// (voir docs/steps/PhaseCorrelationStep.md).
//
// name : "phase-correlation"
// options : overlap (float), niveau (int), et les champs de CorrelationPhase::Parametres :
//           lissage, seuilRapport, lissageLocal, ecartMax, espacementMin, marge, fractionValideMin (float),
//           nbPoints, fenetre, facteurDft (int). Option absente du JSON : valeur par défaut.
// pipelineOptions : { "type": "Tiles", "key": "tiles" }
// signature : { "type": "PairMatches", "key": "matches" }
//
// Ordre des images : pour chaque tuile A, d'abord sa voisine de droite, puis celle du dessous (B).
//   PairMatches = { idA, idB, dir = Right ou Bottom : position de B par rapport à A }.
// Axes : repère image, origine au centre du pixel haut-gauche de A, x vers la droite, y vers le bas.
// Signe : (dx, dy) = coin haut-gauche de B dans le repère de A. Un pixel (X, Y) de A correspond au pixel
//   (X − dx, Y − dy) de B, et pointsA[i] − pointsB[i] ≈ (dx, dy).
//   Right : dx > 0, dy du signe du décalage vertical de B (négatif si B est plus haute que A).
//   Bottom : dy > 0, dx négatif si B est décalée vers la gauche.
// Niveau réduit (niveau = n, s = 2^n) : la mesure est faite sur les images réduites, puis ramenée en
//   pleine résolution : (dx, dy) × s, et points p ↦ (p + 0,5)·s − 0,5 (centre des pixels moyennés).
//   Les longueurs des paramètres (marge, fenetre, ecartMax...) et les messages d'échec sont en px du niveau analysé.
//
// Une paire qui échoue est ignorée et listée dans exportData()["failures"] ({idA, idB, reason}) ;
// le step échoue (exportData()["error"]) si aucune paire ne réussit, si une option est invalide,
// s'il n'y a pas de tuiles ou si une tuile est illisible.
class PhaseCorrelationStep : public Step {
    PhaseCorrelationOutput output;
public:
    explicit PhaseCorrelationStep(const PhaseCorrelationInput& input = {});
    bool execute() override;
    const PhaseCorrelationOutput& getOutput() const { return output; }

    // Pixels de la tuile au niveau n de la pyramide, type d'origine (8/16 bits, gris ou couleur).
    // Seul point d'accès aux pixels du step : à remplacer par la lecture commune readRegion(tuile, niveau, rect)
    // (Mathias B.) dès qu'elle existe. En attendant : fichier source, réduit comme PyramidalTiffWriter.
    // Vide si le fichier est illisible.
    static cv::Mat lirePixels(const Tuile& tuile, int niveau);

    // Ramène une mesure faite au niveau n vers la pleine résolution (voir « Niveau réduit » ci-dessus)
    static void versPleineResolution(Matches& m, int niveau);
};

} // namespace stitching::steps
