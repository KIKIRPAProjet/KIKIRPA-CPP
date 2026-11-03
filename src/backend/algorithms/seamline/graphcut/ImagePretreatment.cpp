#include "algorithms/seamline/graphcut/ImagePretreatment.h"
#include <cmath>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <map>

using namespace std;
using namespace cv;

namespace Seamline {

// Charge une image et la convertit en BGR à trois canaux, en entiers non signés sur 16 bits
// La transparence est supprimée, les niveaux de gris sont dupliqués sur trois canaux
// Renvoie une image vide si la lecture échoue

Mat ImagePretreatment::read16(const string& path) {
    Mat raw = imread(path, IMREAD_UNCHANGED); 
    if (raw.empty()) return raw; 
    // Uniformiser les canaux avant de convertir les valeurs des pixels
    // Pour les images à deux canaux, seul le premier est conservé
    if (raw.channels() == 4) cvtColor(raw, raw, COLOR_BGRA2BGR); 
    else if (raw.channels() == 2) { vector<Mat> ch; split(raw, ch); raw = ch[0]; } 
    if (raw.channels() == 1) cvtColor(raw, raw, COLOR_GRAY2BGR); 
    
    // Convertir les valeurs en entiers non signés sur 16 bits
    // Le facteur 257 transforme exactement la valeur 255 en 65535
    // Les valeurs signées sur 16 bits sont doublées, les valeurs négatives deviennent nulles
    // Le cas par défaut suppose des valeurs flottantes comprises entre 0 et 1
    Mat out;
    switch (raw.depth()) {
        case CV_16U: out = raw; break;
        case CV_8U:  raw.convertTo(out, CV_16UC3, 257.0); break;       // 255 -> 65535
        case CV_16S: raw.convertTo(out, CV_16UC3, 2.0); break;
        default:     raw.convertTo(out, CV_16UC3, 65535.0); break;     // flottant 0–1
    } 
    return out; 
}

// Prépare une image réduite à trois canaux flottants pour le calcul de la ligne de raccord
// Le redimensionnement est effectué en 16 bits, puis les valeurs passent de [0, 65535] à [0, 255]
// Le facteur fourni doit respecter : 0 < scale <= 1
Mat ImagePretreatment::readSmallFloat(const string& path, double scale) { 
    Mat img = read16(path);
    if (img.empty()) return img;
    Mat small16 = img;
    
    if (scale < 1.0) resize(img, small16, Size(), scale, scale, INTER_LINEAR); 
    img.release();
    Mat f;
    small16.convertTo(f, CV_32FC3, 255.0 / 65535.0);   
    return f;
}

// Extrait la ligne et la colonne d'un nom se terminant par _r<nombre>_c<nombre>.tif ou .tiff
// Construit un identifiant tel que "r03_c07", utilisé pour rechercher la position
// Renvoie false si le nom ne correspond pas au format attendu

bool ImagePretreatment::parseTileName(const string& path, int& row, int& col, string& id) {
    static const regex pattern("_r(\\d+)_c(\\d+)\\.tiff?$", regex::icase); 

    // Conserver uniquement le nom du fichier, sans son répertoire
    string file_name = path;
    size_t last_slash = file_name.find_last_of("/\\");
    if (last_slash != string::npos) file_name = file_name.substr(last_slash + 1);

    smatch matches;
    if (!regex_search(file_name, matches, pattern) || matches.size() <= 2) return false;
    row = stoi(matches[1].str()); 
    col = stoi(matches[2].str()); 

    // Utiliser au moins deux chiffres par coordonnée pour former l'identifiant
    char buf[32];
    snprintf(buf, sizeof(buf), "r%02d_c%02d", row, col); 
    id = buf;
    return true;
}

// Prépare les dimensions, les masques réduits et les positions des tuiles aux deux résolutions
// Les positions sont fournies par input_positions, et non calculées à partir des images
//
// Les vecteurs de sortie doivent être vides avant l'appel
// Les résultats suivent l'ordre des fichiers dans tile_paths
// Renvoie 1 en cas de succès, ou -1 lorsqu'une erreur est détectée
// Une erreur peut laisser les vecteurs de sortie partiellement remplis

int ImagePretreatment::processImages(//Input
                                    const vector<TilePositionData>& input_positions, 
                                     const vector<string>& tile_paths, 
                                     double scale, 
                                    //Output
                                     vector<Size>& tile_sizes, 
                                     vector<UMat>& masks_umat_small, 
                                     vector<Point>& original_corners, 
                                     vector<Point>& corners_small) { 
    //  1. Vérifier le facteur de réduction et indexer les positions fournies 
    if (!(scale > 0.0 && scale <= 1.0)) { 
        cout << "Scale expected below 1.0 : " << scale << endl;
        return -1;
    }
    
    // Créer une map temporaire à partir de la structure pour une recherche rapide
    map<string, Point2d> positions_map;
    for (const auto& tp : input_positions) {
        positions_map[tp.tileId] = tp.position; 
    }

    vector<Point2d> raw_corners;

    //  2. Lire les dimensions, initialiser les masques et retrouver les positions 
    for (size_t t = 0; t < tile_paths.size(); ++t) {
        const string& img_path = tile_paths[t];
        Size full_size;
        // Ne conserver l'image pleine résolution que le temps de lire ses dimensions
        {
            Mat img = read16(img_path);
            if (img.empty()) {
                cout << "No images found : " << img_path << endl;
                return -1;
            }
            full_size = img.size(); 

            const Size small_size(saturate_cast<int>(full_size.width * scale), saturate_cast<int>(full_size.height * scale));
            if (small_size.width < 1 || small_size.height < 1) {
                cout << "tile quality too low : " << img_path << endl;
                return -1;
            }
            // Tous les pixels sont initialement utilisables
            // Le calcul de la ligne de raccord modifiera ensuite ces masques
            
            masks_umat_small.push_back(UMat(small_size, CV_8U, Scalar(255))); 
        }   
        tile_sizes.push_back(full_size);

        // Associer le fichier à sa position grâce à l'identifiant extrait de son nom
        int row = 0, col = 0;
        string id;
        if (!parseTileName(img_path, row, col, id)) { 
            cout << "wrong tile name: " << img_path << endl;
            return -1;
        }

        // Recherche dans la structure d'input
        auto p = positions_map.find(id); 
        if (p == positions_map.end()) {
            cout << "Position missing for " << id << " in the input structure" << endl;
            return -1;
        }
        raw_corners.push_back(p->second);
    }
    
    if (tile_sizes.empty()) {
        cout << "No images found" << endl;
        return -1;
    }

    // 3. Déplacer les positions dans un repère de mosaïque à coordonnées positives ou nulles 
    // Soustraire les minima arrondis vers le bas, puis arrondir les positions au pixel
    // Toutes les tuiles subissent la même translation avant cet arrondi
    double minX = 1e300, minY = 1e300;
    for (const Point2d& p : raw_corners) { minX = min(minX, p.x); minY = min(minY, p.y); }
    const double ox = floor(minX), oy = floor(minY);
    for (const Point2d& p : raw_corners) original_corners.push_back(Point(cvRound(p.x - ox), cvRound(p.y - oy)));

    // 4. Convertir les positions pleine résolution vers la grille réduite 
    // Arrondir vers le bas pour obtenir des coordonnées entières sur cette grille
    for (size_t i = 0; i < original_corners.size(); ++i)
        corners_small.push_back(Point(cvFloor(original_corners[i].x * scale), cvFloor(original_corners[i].y * scale))); 
         
    return 1;
}

}  // namespace Seamline
