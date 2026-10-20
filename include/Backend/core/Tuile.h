#pragma once
#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace stitching {

constexpr int TAILLE_BLOC = 512;   // blocs de 512 x 512 pixels (taille à discuter)
constexpr int NB_NIVEAUX  = 4;     // 1, 1/2, 1/4, 1/8

// Un niveau de la pyramide. Les blocs restent sur disque (TIFF tuilé) : seules
// les dimensions sont gardées en RAM, la grille de blocs s'en déduit.
struct Niveau {
    int largeur = 0, hauteur = 0;
    int blocsX = 0, blocsY = 0;   // nombre de blocs en x et en y
    bool gris = false;            // niveaux réduits stockés en niveaux de gris
};

// Identifiant d'un bloc : (tuile, niveau, bx, by), clé du cache
struct BlocId {
    int tuile = 0, niveau = 0, bx = 0, by = 0;
    bool operator==(const BlocId& o) const {
        return tuile == o.tuile && niveau == o.niveau && bx == o.bx && by == o.by;
    }
};

// Fiche d'une tuile, gardée en RAM. Aucun pixel ici : les algorithmes passent
// par readRegion(tuile, niveau, rectangle).
class Tuile {
    Tuile *left = nullptr, *right = nullptr, *top = nullptr, *bottom = nullptr;
    int id = 0;
    std::filesystem::path source;   // fichier d'origine, jamais modifié
    std::filesystem::path tiff;     // TIFF tuilé pyramidal créé à l'import
    int type = 0;                   // type OpenCV de la pleine résolution (ex. CV_16UC3)
    std::array<Niveau, NB_NIVEAUX> pyramide;
public:
    Tuile(int id, std::filesystem::path source, std::filesystem::path tiff,
          int largeur, int hauteur, int type);

    Tuile* getLeft()   const { return left; }
    Tuile* getRight()  const { return right; }
    Tuile* getTop()    const { return top; }
    Tuile* getBottom() const { return bottom; }
    void setVoisins(Tuile* l, Tuile* r, Tuile* t, Tuile* b) { left = l; right = r; top = t; bottom = b; }

    int getId() const { return id; }
    const std::filesystem::path& getSource() const { return source; }
    const std::filesystem::path& getTiff()   const { return tiff; }
    int getType() const { return type; }
    const Niveau& getNiveau(int n) const { return pyramide.at(n); }

    // Bloc contenant le pixel (x, y) du niveau n
    BlocId blocDe(int n, int x, int y) const { return {id, n, x / TAILLE_BLOC, y / TAILLE_BLOC}; }
};

// PipelineOption "Tiles" : les fiches créées par le step import.
// shared_ptr : les copies (DataStore) gardent les mêmes Tuile, donc les voisins restent valides.
using Tiles = std::vector<std::shared_ptr<Tuile>>;

} // namespace stitching
