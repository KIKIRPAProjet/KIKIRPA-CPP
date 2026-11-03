#pragma once
#include "core/Step.h"
#include "core/Tuile.h"
#include "algorithms/stitching/correlation_phase/CorrelationPhase.h"
#include <optional>
#include <string>
#include <vector>

namespace stitching::steps {

// Contrôles de confiance appliqués à chaque paire mesurée (voir docs/algorithms/stitching/correlation_phase/EtudeConfiance.md).
// Une paire qui échoue à un contrôle est rejetée (exportData()["failures"]), comme une paire non mesurable.
struct ControleConfiance {
    double nccMin = 0.3;          // NCC médiane minimale des points locaux (fond sombre, bruit). <= 0 : contrôle désactivé
    double dispersionMax = 0.5;   // écart médian maximal (px du niveau analysé) entre les points et la translation. <= 0 : désactivé
    double autosimMax = 0.6;      // auto-similarité maximale de la bande de A (motif répété : mesure ambiguë). >= 1 : désactivé
};

// Entrée du step = ses options
struct PhaseCorrelationInput {
    float overlap = 0.2f;   // recouvrement nominal, fraction de la tuile
    int niveau = 0;         // niveau de la pyramide analysé : 0 = pleine résolution, n = 1/2^n (0 à NB_NIVEAUX - 1)
    matching::CorrelationPhase::Parametres parametres;   // longueurs en px du niveau analysé
    ControleConfiance confiance;
};

// État de confiance d'une paire
enum class EtatConfiance {
    Fiable,       // gardée, contrôles passés
    NonVerifie,   // gardée sans point local (zone commune trop étroite pour un patch, ou nbPoints = 0)
    Ambigu,       // rejetée : motif répété (auto-similarité >= autosimMax), plusieurs positions possibles
    Rejete        // rejetée : mesure impossible, NCC trop basse, points en désaccord, ou aucun point fiable
};

// Point de correspondance d'une paire et son score
struct PointConfiance {
    cv::Point2f a, b;   // dans la tuile A et dans la tuile B (px pleine résolution)
    float ncc = 0.f;    // ressemblance des deux patchs recalés (0 à 1)
};

// Indicateurs de confiance d'une paire (exportData()["confidence"]). Indicateur absent : non mesurable.
struct ConfiancePaire {
    int idA = 0, idB = 0;
    Direction dir = Direction::Right;
    EtatConfiance etat = EtatConfiance::Fiable;
    std::string raison;                  // pourquoi la paire est rejetée (vide si gardée)
    std::optional<double> ncc;           // NCC médiane des points (0 à 1) : le score de la paire
    std::optional<double> dispersion;    // écart médian (px pleine résolution) entre pointsA − pointsB et (dx, dy)
    std::optional<double> autosim;       // auto-similarité de la bande de A (0 à 1 ; 1 = motif répété)
    std::vector<PointConfiance> points;  // paires de points avec leur score
};

// Sortie du step = sa signature
struct PhaseCorrelationOutput {
    std::vector<PairMatches> pairs;          // toujours en px de pleine résolution, quel que soit le niveau analysé
    std::vector<ConfiancePaire> confiance;   // une par paire mesurée ou rejetée, dans l'ordre de parcours
};

// Translation au sous-pixel entre chaque paire de tuiles voisines, par corrélation de phase
// (voir docs/steps/PhaseCorrelationStep.md).
//
// name : "phase-correlation"
// options : overlap (float), niveau (int), et les champs de CorrelationPhase::Parametres :
//           lissage, seuilRapport, lissageLocal, ecartMax, espacementMin, marge, fractionValideMin (float),
//           nbPoints, fenetre, facteurDft (int) ; contrôles de confiance : nccMin, dispersionMax, autosimMax (float).
//           Option absente du JSON : valeur par défaut.
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
// Une paire qui échoue (mesure impossible, ou contrôle de confiance) est ignorée et listée dans
// exportData()["failures"] ({idA, idB, reason}). Chaque paire, gardée ou non, a ses indicateurs de confiance
// (ConfiancePaire) dans getOutput().confiance et dans exportData()["confidence"] (voir versJson) ;
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

    // Auto-similarité de la bande de recouvrement nominal de A, côté B (dir = Right ou Bottom) : la bande
    // ressemble-t-elle à elle-même à un décalage non nul de la marge du rail (hors ±10 px) ? 1 = motif répété
    // (plusieurs positions de B possibles), proche de 0 = aucune répétition. NaN si la bande est vide à plus de 75 %.
    // marge : Parametres::marge (0 = automatique, même règle que l'algorithme).
    static double autoSimilarite(const cv::Mat& a, Direction dir, float overlap, double marge);

    // Indicateurs et état de confiance d'une paire mesurée (mesure m et image a au niveau analysé ; idA, idB, dir
    // et points ne sont pas remplis). Tous les indicateurs mesurables sont calculés, puis la décision est prise :
    // Rejete (NCC < nccMin, dispersion > dispersionMax, aucun point alors que la zone commune en permettait),
    // Ambigu (auto-similarité >= autosimMax), sinon NonVerifie (sans point) ou Fiable.
    // Ici, dispersion est en px du niveau analysé (comparée à dispersionMax) ; execute() la ramène en pleine résolution.
    static ConfiancePaire controlerConfiance(const Matches& m, const cv::Mat& a, Direction dir, float overlap,
                                             const matching::CorrelationPhase::Parametres& p, const ControleConfiance& c);

    static const char* nomEtat(EtatConfiance e);   // "fiable", "non_verifie", "ambigu", "rejete"

    // Une paire en JSON : { idA, idB, dir, etat, ncc, dispersion, autosim, points: [{ a, b, ncc }] }
    // (indicateur absent : null ; valeurs arrondies au millième)
    static Json versJson(const ConfiancePaire& c);
};

} // namespace stitching::steps
