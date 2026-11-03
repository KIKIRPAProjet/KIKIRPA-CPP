#include "Backend/core/Tuile.h"

namespace stitching {

Tuile::Tuile(int id, std::filesystem::path source, std::filesystem::path tiff,
             int largeur, int hauteur, int type)
    : id(id), source(std::move(source)), tiff(std::move(tiff)), type(type) {
    for (int n = 0; n < NB_NIVEAUX; ++n) {
        Niveau& niv = pyramide[n];
        niv.largeur = largeur >> n;   // 1, 1/2, 1/4, 1/8
        niv.hauteur = hauteur >> n;
        niv.blocsX  = (niv.largeur + TAILLE_BLOC - 1) / TAILLE_BLOC;
        niv.blocsY  = (niv.hauteur + TAILLE_BLOC - 1) / TAILLE_BLOC;
        niv.gris    = n > 0;          // pleine résolution : conversion à la volée
    }
}

} // namespace stitching
