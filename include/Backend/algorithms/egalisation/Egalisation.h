#pragma once
#include "algorithms/egalisation/Types.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace stitching::egalisation {

// Égalisation de luminosité entre tuiles, avant la seamline (port du notebook egalisation_gain_champ_final.ipynb).
// Seule la différence entre tuiles est corrigée, jamais l'œuvre elle-même :
//   A : un gain par tuile ;
//   B : A + un assombrissement vers les bords exp(c · r²), commun à toutes les tuiles.
// Seul header à inclure depuis le pipeline ; le détail est dans egalisation/ :
//   Preparation (luminances réduites) → Algorithme (échantillons, moindres carrés, bilan) → Sortie (correction).
//
// Utilisation, une tuile en mémoire à la fois :
//   Egalisation eg;
//   std::vector<TuileReduite> reduites;
//   for (chemin : tuiles) reduites.push_back(eg.reduire(cv::imread(chemin, cv::IMREAD_UNCHANGED)));
//   auto r = eg.estimer(reduites, paires);   // paires : translations du stitching
//   if (!r) → eg.raisonEchec()
//   for (i, chemin : tuiles) sortie = Egalisation::corriger(cv::imread(chemin, …), *r, i, Methode::GainEtChamp);
//
// Paramètres : structure Egalisation::Parametres (egalisation/Types.h). Chaque champ a une valeur par défaut ;
// std::invalid_argument à la construction si une valeur est invalide.
//   reduction   8     réduction pour l'estimation (1/8)
//   flou        1.5   sigma (px réduits) du flou avant l'estimation
//   marge       40    px ignorés au bord de chaque tuile (pleine résolution)
//   seuilBas    0.03  pixels plus sombres exclus
//   seuilHaut   0.97  pixels plus clairs exclus (saturation)
//   iterations  6     itérations IRLS
//   huber       1.5   seuil de Huber (écarts-types robustes)
//   poidsJauge  1000  poids de la contrainte « moyenne des log g = 0 »
class Egalisation {
public:
    using Parametres = egalisation::Parametres;

    // Sans argument : toutes les valeurs par défaut
    explicit Egalisation(const Parametres& p = {});

    // 1. Luminance réduite d'une tuile en pleine résolution (8/16 bits, gris ou couleur)
    TuileReduite reduire(const cv::Mat& tuile) const;

    // 1 bis. Même chose à partir d'une version déjà réduite (niveau de pyramide) et de la taille pleine résolution
    TuileReduite reduire(const cv::Mat& reduite, cv::Size pleine) const;

    // 2. Gains A, gains + champ B et bilan de qualité. Vide si l'estimation est impossible
    // (entrées invalides, aucune paire avec des échantillons) : la raison est dans raisonEchec().
    std::optional<Resultat> estimer(const std::vector<TuileReduite>& tuiles, const std::vector<PaireVoisine>& paires);

    // Pourquoi le dernier estimer() a échoué. Vide s'il a réussi.
    const std::string& raisonEchec() const { return raison; }

    // 3. Tuile corrigée en pleine résolution (voir Sortie::corriger pour le type de sortie)
    static cv::Mat corriger(const cv::Mat& tuile, const Resultat& r, int indice, Methode m);

    // Paires à partir de positions globales (coin haut-gauche de chaque tuile, ex. TilePositionData::position) :
    // translation = position[b] − position[a]
    static std::vector<PaireVoisine> pairesDepuisPositions(const std::vector<cv::Point2d>& positions,
                                                           const std::vector<std::pair<int, int>>& voisins);

private:
    Parametres parametres;   // vérifiés une seule fois, dans le constructeur
    std::string raison;      // message du dernier échec de estimer()
};

} // namespace stitching::egalisation
