//Preparation.cpp
#include "algorithms/egalisation/Preparation.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <stdexcept>

namespace stitching::egalisation {

void Preparation::verifierParametres(const Parametres& p) {
    // Vérifié une fois à la construction : une valeur hors domaine est une erreur de configuration,
    // pas un échec d'estimation, d'où l'exception plutôt qu'un résultat vide
    if (p.reduction < 1 || p.flou < 0 || p.marge < 0 || p.seuilBas < 0 || p.seuilHaut > 1
        || p.seuilBas >= p.seuilHaut || p.iterations < 1 || p.huber <= 0 || p.poidsJauge <= 0)
        throw std::invalid_argument("egalisation : paramètre invalide (reduction >= 1, iterations >= 1, flou >= 0, "
                                    "marge >= 0, 0 <= seuilBas < seuilHaut <= 1, huber > 0, poidsJauge > 0)");
}

TuileReduite Preparation::reduire(const cv::Mat& tuile, const Parametres& p) {
    if (tuile.empty()) return {};
    // Réduction AVANT la conversion en luminance : la tuile n'est jamais copiée en flottant pleine résolution
    // (1,2 Go pour une tuile couleur de 100 Mpx). La luminance et la moyenne INTER_AREA sont linéaires,
    // l'ordre ne change pas le résultat. Une tuile 8 bits passe d'abord en 16 bits (× 257) pour que la moyenne
    // ne soit pas arrondie au 1/255.
    cv::Mat source = tuile;
    if (tuile.depth() == CV_8U) tuile.convertTo(source, CV_MAKETYPE(CV_16U, tuile.channels()), 257.0);
    const cv::Size taille(std::max(1, cvRound(tuile.cols / static_cast<double>(p.reduction))),
                          std::max(1, cvRound(tuile.rows / static_cast<double>(p.reduction))));
    cv::Mat petite;
    cv::resize(source, petite, taille, 0, 0, cv::INTER_AREA);
    return depuisReduite(petite, tuile.size(), p);
}

TuileReduite Preparation::depuisReduite(const cv::Mat& reduite, cv::Size pleine, const Parametres& p) {
    if (reduite.empty() || pleine.area() <= 0) return {};
    TuileReduite t;
    t.lum = luminance(reduite);
    if (p.flou > 0) cv::GaussianBlur(t.lum, t.lum, cv::Size(0, 0), p.flou);
    t.pleine = pleine;
    return t;
}

std::string Preparation::verifierEntrees(const std::vector<TuileReduite>& tuiles,
                                         const std::vector<PaireVoisine>& paires) {
    if (tuiles.size() < 2) return "égalisation : il faut au moins 2 tuiles";
    if (paires.empty()) return "égalisation : aucune paire de tuiles voisines";
    for (std::size_t i = 0; i < tuiles.size(); ++i)
        if (tuiles[i].lum.empty() || tuiles[i].pleine.area() <= 0)
            return "égalisation : tuile " + std::to_string(i) + " vide (lecture impossible ?)";
    const int n = static_cast<int>(tuiles.size());
    for (std::size_t k = 0; k < paires.size(); ++k) {
        const auto& pr = paires[k];
        if (pr.a < 0 || pr.a >= n || pr.b < 0 || pr.b >= n || pr.a == pr.b)
            return "égalisation : paire " + std::to_string(k) + " invalide (" + std::to_string(pr.a) + ", "
                   + std::to_string(pr.b) + ") pour " + std::to_string(n) + " tuiles";
    }
    return {};
}

cv::Mat Preparation::luminance(const cv::Mat& image) {
    cv::Mat f;
    image.convertTo(f, CV_MAKETYPE(CV_32F, image.channels()), 1.0 / valeurMax(image.depth()));
    if (f.channels() == 3) cv::cvtColor(f, f, cv::COLOR_BGR2GRAY);         // cv::imread : ordre BGR
    else if (f.channels() == 4) cv::cvtColor(f, f, cv::COLOR_BGRA2GRAY);   // alpha ignoré
    else if (f.channels() != 1) cv::extractChannel(f, f, 0);
    return f;
}

double Preparation::valeurMax(int profondeur) {
    switch (profondeur) {
        case CV_8U:  return 255.0;
        case CV_8S:  return 127.0;
        case CV_16U: return 65535.0;
        case CV_16S: return 32767.0;
        case CV_32S: return 2147483647.0;
        default:     return 1.0;   // flottant : supposé déjà dans [0, 1]
    }
}

} // namespace stitching::egalisation
