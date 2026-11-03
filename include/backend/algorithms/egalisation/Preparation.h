#pragma once
#include "algorithms/egalisation/Types.h"
#include <string>

namespace stitching::egalisation {

// Étape 1 : des tuiles brutes (8 ou 16 bits, gris ou couleur) aux luminances réduites de l'estimation.
// Les tuiles ne sont jamais modifiées.
class Preparation {
public:
    // Lève std::invalid_argument si un paramètre est hors de son domaine
    static void verifierParametres(const Parametres& p);

    // Tuile en pleine résolution → luminance réduite au 1/p.reduction et floutée. Tuile vide : TuileReduite vide.
    static TuileReduite reduire(const cv::Mat& tuile, const Parametres& p);

    // Même chose à partir d'une version déjà réduite (ex. niveau 1/8 de la pyramide de Tuile) : seul le flou
    // est appliqué. `pleine` est la taille de la tuile en pleine résolution ; l'échelle réelle en est déduite.
    static TuileReduite depuisReduite(const cv::Mat& reduite, cv::Size pleine, const Parametres& p);

    // Message d'erreur si les entrées de l'estimation sont inexploitables, vide sinon
    // (moins de 2 tuiles, tuile vide, indice de paire hors limites, paire d'une tuile avec elle-même)
    static std::string verifierEntrees(const std::vector<TuileReduite>& tuiles, const std::vector<PaireVoisine>& paires);

    // Luminance dans [0, 1], CV_32F : 8 bits / 255, 16 bits / 65535, flottant supposé déjà dans [0, 1].
    // Couleur (BGR ou BGRA, lue par cv::imread) : luminance pondérée, canal alpha ignoré.
    static cv::Mat luminance(const cv::Mat& image);

    // Plus grande valeur d'un pixel pour la profondeur OpenCV `profondeur` (255, 65535… ; 1 pour un flottant)
    static double valeurMax(int profondeur);
};

} // namespace stitching::egalisation
