// Egalisation.cpp
#include "algorithms/egalisation/Egalisation.h"
#include "algorithms/egalisation/Algorithme.h"
#include "algorithms/egalisation/Preparation.h"
#include "algorithms/egalisation/Sortie.h"
#include <cmath>

namespace stitching::egalisation {

Egalisation::Egalisation(const Parametres& p) : parametres(p) {
    Preparation::verifierParametres(parametres);
}

TuileReduite Egalisation::reduire(const cv::Mat& tuile) const {
    return Preparation::reduire(tuile, parametres);
}

TuileReduite Egalisation::reduire(const cv::Mat& reduite, cv::Size pleine) const {
    return Preparation::depuisReduite(reduite, pleine, parametres);
}

std::optional<Resultat> Egalisation::estimer(const std::vector<TuileReduite>& tuiles,
                                             const std::vector<PaireVoisine>& paires) {
    raison = Preparation::verifierEntrees(tuiles, paires);
    if (!raison.empty()) return std::nullopt;

    // 1. Échantillons de chaque paire : écarts log dans les recouvrements
    Resultat r;
    std::vector<Echantillons> echantillons;
    for (std::size_t k = 0; k < paires.size(); ++k) {
        auto e = Algorithme::echantillons(tuiles[paires[k].a], tuiles[paires[k].b], paires[k], parametres);
        if (e.d.empty()) {
            r.pairesVides.push_back(static_cast<int>(k));
            continue;
        }
        r.nbEchantillons += e.d.size();
        echantillons.push_back(std::move(e));
    }
    if (echantillons.empty()) {
        raison = Sortie::echecSansEchantillon(paires.size());
        return std::nullopt;
    }

    // 2. Moindres carrés robustes : A (gains seuls) puis B (gains + champ)
    const int n = static_cast<int>(tuiles.size());
    const Solution A = Algorithme::resoudre(echantillons, n, false, parametres);
    const Solution B = Algorithme::resoudre(echantillons, n, true, parametres);
    for (int i = 0; i < n; ++i) {
        r.gainsA.push_back(std::exp(A.logGains[i]));
        r.gainsB.push_back(std::exp(B.logGains[i]));
    }
    r.c = B.c;
    std::vector<bool> relie(n, false);
    for (const auto& e : echantillons) relie[e.a] = relie[e.b] = true;
    for (int i = 0; i < n; ++i)
        if (!relie[i]) r.tuilesIsolees.push_back(i);

    // 3. Bilan avant / A / B
    Algorithme::residus(echantillons, {}, r.avant);
    Algorithme::residus(echantillons, A, r.apresA);
    Algorithme::residus(echantillons, B, r.apresB);
    Algorithme::mesurer(tuiles, paires, {}, r.avant);
    Algorithme::mesurer(tuiles, paires, A, r.apresA);
    Algorithme::mesurer(tuiles, paires, B, r.apresB);
    return r;
}

cv::Mat Egalisation::corriger(const cv::Mat& tuile, const Resultat& r, int indice, Methode m) {
    return Sortie::corriger(tuile, r.gain(indice, m), r.champ(m));
}

std::vector<PaireVoisine> Egalisation::pairesDepuisPositions(const std::vector<cv::Point2d>& positions,
                                                             const std::vector<std::pair<int, int>>& voisins) {
    std::vector<PaireVoisine> paires;
    for (const auto& [a, b] : voisins)
        paires.push_back({a, b, positions.at(b) - positions.at(a)});
    return paires;
}

} // namespace stitching::egalisation
