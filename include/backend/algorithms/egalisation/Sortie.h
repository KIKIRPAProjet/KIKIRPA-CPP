#pragma once
#include "algorithms/egalisation/Types.h"
#include <string>

namespace stitching::egalisation {

// Étape 3 : application de la correction aux tuiles en pleine résolution, et bilan lisible.
class Sortie {
public:
    // tuile / (gain · exp(c · r²)), pixel par pixel : multiplication pure par un champ lisse, aucun filtre
    // (grain et craquelures conservés). Même facteur sur les canaux couleur (teinte conservée), alpha recopié.
    // Entier 8 ou 16 bits → 16 bits (8 bits multiplié par 257, comme le notebook) ; flottant → même type.
    static cv::Mat corriger(const cv::Mat& tuile, double gain, double c);

    // Gains, champ et bilan avant / A / B, sur plusieurs lignes (journal)
    static std::string rapport(const Resultat& r);

    // Échec de Egalisation::estimer : aucune paire n'a d'échantillon exploitable
    static std::string echecSansEchantillon(std::size_t nbPaires);

private:
    // Nombre écrit avec `decimales` chiffres après la virgule
    static std::string nombre(double valeur, int decimales);
};

} // namespace stitching::egalisation
