// include/backend/steps/phase_correlation_step.h
#pragma once

#include "algorithms/stitching/phase_correlation/phase_correlation_matcher.h"
#include "core/data/algorithm_types.h"
#include "core/data/tile.h"
#include "core/step/step.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <optional>
#include <string>
#include <vector>

namespace StepsNs {

    // Contrôles de confiance appliqués à chaque paire mesurée (voir docs/algorithms/stitching/phase_correlation/EtudeConfiance.md).
    // Une paire qui échoue à un contrôle est rejetée (getResult()["failures"]), comme une paire non mesurable.
    struct ConfidenceControl {
        double nccMin = 0.3;          // NCC médiane minimale des points locaux (fond sombre, bruit). <= 0 : contrôle désactivé
        double dispersionMax = 0.5;   // écart médian maximal (px du niveau analysé) entre les points et la translation. <= 0 : désactivé
        double autosimMax = 0.6;      // auto-similarité maximale de la bande de A (motif répété : mesure ambiguë). >= 1 : désactivé
    };

    // État de confiance d'une paire
    enum class ConfidenceState {
        Reliable,     // "fiable" : gardée, contrôles passés
        Unverified,   // "non_verifie" : gardée sans point local (zone commune trop étroite pour un patch, ou nbPoints = 0)
        Ambiguous,    // "ambigu" : rejetée, motif répété (auto-similarité >= autosimMax), plusieurs positions possibles
        Rejected      // "rejete" : rejetée, mesure impossible, NCC trop basse, points en désaccord ou aucun point fiable
    };

    // Point de correspondance d'une paire et son score
    struct PointConfidence {
        cv::Point2f pointInA, pointInB;   // dans la tuile A et dans la tuile B (px pleine résolution)
        float ncc = 0.f;                  // ressemblance des deux patchs recalés (0 à 1)
    };

    // Indicateurs de confiance d'une paire (getResult()["confidence"]). Indicateur absent : non mesurable.
    struct PairConfidence {
        int tileIdA = 0, tileIdB = 0;
        DataNs::Direction direction = DataNs::Direction::Right;
        ConfidenceState state = ConfidenceState::Reliable;
        std::string reason;                   // pourquoi la paire est rejetée (vide si gardée)
        std::optional<double> ncc;            // NCC médiane des points (0 à 1) : le score de la paire
        std::optional<double> dispersion;     // écart médian (px pleine résolution) entre pointsInA − pointsInB et la translation
        std::optional<double> autosim;        // auto-similarité de la bande de A (0 à 1 ; 1 = motif répété)
        std::vector<PointConfidence> points;  // paires de points avec leur score
    };

    // Valeurs initiales des paramètres du step.
    struct PhaseCorrelationInput {
        float overlap = 0.2f;   // recouvrement nominal entre deux tuiles voisines, en fraction de la tuile
        int level = 0;          // niveau de la pyramide analysé : 0 = pleine résolution, n = 1/2^n (0 à LEVEL_COUNT - 1)
        PhaseCorrelationNs::Parameters parameters;   // réglages de l'algorithme, en px du niveau analysé
        ConfidenceControl confiance;   // seuils des contrôles de confiance
    };

    // Sortie du step.
    struct PhaseCorrelationOutput {
        std::vector<DataNs::PairMatches> pairs;   // toujours en px de pleine résolution, quel que soit le niveau analysé
        std::vector<PairConfidence> confidence;   // une par paire mesurée ou rejetée, dans l'ordre de parcours
    };

    // Mesure, par corrélation de phase, la translation au sous-pixel entre chaque paire de tuiles voisines
    // (voir docs/steps/PhaseCorrelationStep.md).
    //
    // Nom : "phase-correlation".
    // Paramètres : overlap (float), niveau (int, champ level) et les champs de PhaseCorrelationNs::Parameters, sous leur
    //   nom d'origine : lissage, seuilRapport, lissageLocal, ecartMax, espacementMin, marge, fractionValideMin (float) ;
    //   nbPoints, fenetre, facteurDft (int). Correspondance avec les champs : phase_correlation_types.h.
    //   Contrôles de confiance : nccMin, dispersionMax, autosimMax (float).
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
    // Confiance : chaque paire mesurée passe 3 contrôles (NCC, dispersion, auto-similarité). Une paire qui en échoue un
    //   est rejetée comme une paire non mesurable. Chaque paire, gardée ou non, a ses indicateurs (PairConfidence) dans
    //   getOutput().confidence et dans getResult()["confidence"] (voir toJson).
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

            // Auto-similarité de la bande de recouvrement nominal de A, côté B (Right ou Bottom) : la bande ressemble-t-elle
            // à elle-même à un décalage non nul de la marge du rail (hors ±10 px) ? 1 = motif répété (plusieurs positions
            // de B possibles), proche de 0 = aucune répétition. NaN si la bande est vide à plus de 75 % ou trop étroite.
            // margin : Parameters::railMargin (0 = automatique, même règle que l'algorithme).
            static double selfSimilarity(const cv::Mat& imageA, DataNs::Direction direction, float overlap, double margin);

            // Indicateurs et état de confiance d'une paire mesurée (matches et imageA au niveau analysé ; tileIdA, tileIdB
            // et points ne sont pas remplis). Tous les indicateurs mesurables sont calculés, puis la décision est prise :
            // Rejected (NCC < nccMin, dispersion > dispersionMax, aucun point alors que la zone commune en permettait),
            // Ambiguous (auto-similarité >= autosimMax), sinon Unverified (sans point) ou Reliable.
            // Ici, dispersion est en px du niveau analysé (comparée à dispersionMax) ; execute() la ramène en pleine résolution.
            static PairConfidence checkConfidence(const DataNs::Matches& matches, const cv::Mat& imageA,
                                                  DataNs::Direction direction, float overlap,
                                                  const PhaseCorrelationNs::Parameters& parameters,
                                                  const ConfidenceControl& control);

            static const char* stateName(ConfidenceState state);   // "fiable", "non_verifie", "ambigu", "rejete"

            // Une paire en JSON : { idA, idB, dir, etat, ncc, dispersion, autosim, points: [{ a, b, ncc }] }
            // (indicateur absent : null ; valeurs arrondies au millième)
            static nlohmann::json toJson(const PairConfidence& confidence);

        private:
            PhaseCorrelationOutput output;

    };

}
