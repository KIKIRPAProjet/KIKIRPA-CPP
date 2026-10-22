#include "Backend/core/PyramidalTiffWriter.h"

#include <opencv2/imgproc.hpp>
#include <tiffio.h>

#include <algorithm>
#include <cstdint>
#include <system_error>

namespace stitching {

namespace {

constexpr uint32_t TILE_SIZE = 512;
constexpr int PYRAMID_LEVELS = 4;

// Configure les métadonnées / tags du niveau courant de la pyramide (est donc exécuté pour chaque niveau)
bool setDirectoryFields(TIFF* file, const cv::Mat& level, std::string& error) {
    const int depth = level.depth();
    const int channels = level.channels();
    const int bitsPerSample = depth == CV_8U ? 8 : depth == CV_16U ? 16 : 0;

    // Vérification que cv::Mat est bien codé sur 8 ou 16 bits et qu'elle possède 1, 3 ou 4 canaux (1 = niveau de gris, 3 = RGB, 4 = RGBA)
    if (bitsPerSample == 0 || (channels != 1 && channels != 3 && channels != 4)) {
        error = "only CV_8U/CV_16U images with 1, 3, or 4 channels are supported";
        return false;
    }

    // TIFFSetField permet de configurer les tags du répertoire TIFF courant -> renvoie 1 (succès) ou 0 (échec)
    // On configure :
    //  - TIFFTAG_IMAGEWIDTH & TIFFTAG_IMAGELENGTH : dimensions de l'image ;
    //  - TIFFTAG_SAMPLESPERPIXEL : nombre de canaux ;
    //  - TIFFTAG_BITSPERSAMPLE : nombre de bits par canal ;
    //  - TIFFTAG_ORIENTATION : l'origine de l'image ;
    //  - TIFFTAG_PLANARCONFIG : l'entrelacement des données en mémoire ;
    //  - TIFFTAG_PHOTOMETRIC : le modèle colorimétrique ;
    //  - TIFFTAG_COMPRESSION : le type de compression ;
    //  - TIFFTAG_TILEWIDTH & TIFFTAG_TILELENGTH : dimensions des tuiles ;
    const uint16_t photometric =
        channels == 1 ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB;
    if (!TIFFSetField(file, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(level.cols)) ||
        !TIFFSetField(file, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(level.rows)) ||
        !TIFFSetField(file, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(channels)) ||
        !TIFFSetField(file, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(bitsPerSample)) ||
        !TIFFSetField(file, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) || // ORIENTATION_TOPLEFT = origine en haut à gauche
        !TIFFSetField(file, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) || // PLANARCONFIG_CONTIG = "RGBRGB et non RRRGGGBBB"
        !TIFFSetField(file, TIFFTAG_PHOTOMETRIC, photometric) ||
        !TIFFSetField(file, TIFFTAG_COMPRESSION, COMPRESSION_LZW) ||
        !TIFFSetField(file, TIFFTAG_TILEWIDTH, TILE_SIZE) || // Les blocs de 512 x 512 pixels
        !TIFFSetField(file, TIFFTAG_TILELENGTH, TILE_SIZE)) {
        error = "cannot configure TIFF directory";
        return false;
    }

    // Gestion du canal alpha -> si l'image en possède 4 canaux, on configure le tag TIFFTAG_EXTRASAMPLES pour indiquer que le 4ème canal est un canal alpha (EXTRASAMPLE_UNASSALPHA)
    if (channels == 4 &&
        !TIFFSetField(file, TIFFTAG_EXTRASAMPLES, 1, static_cast<uint16_t>(EXTRASAMPLE_UNASSALPHA))) {
        error = "cannot configure TIFF alpha channel";
        return false;
    }

    return true;
}

// Découpage et écriture en tuiles
bool writeLevel(TIFF* file, const cv::Mat& level, std::string& error) {
    // Calcul du poids en octets d'une tuile standard (512 x 512 x le nombre d'octets par pixel de l'image) pour le niveau courant de la pyramide
    const size_t tileBytes = static_cast<size_t>(TILE_SIZE) * TILE_SIZE * level.elemSize();
    // Image noire de 512 x 512 pixels -> sert de buffer
    cv::Mat tile = cv::Mat::zeros(
        static_cast<int>(TILE_SIZE),
        static_cast<int>(TILE_SIZE),
        level.type()
    );

    // Parcours en grille -> de haut en bas et de gauche à droite en faisant des pas de 512 pixels
    for (int y = 0; y < level.rows; y += static_cast<int>(TILE_SIZE)) {
        for (int x = 0; x < level.cols; x += static_cast<int>(TILE_SIZE)) {
            // On remet "à zéro" le buffer avant de gérer la nouvelle tuile
            tile.setTo(cv::Scalar::all(0));
            // Calcul de la zone exacte à copier 
            const cv::Rect sourceRect{
                x,
                y,
                std::min(static_cast<int>(TILE_SIZE), level.cols - x),
                std::min(static_cast<int>(TILE_SIZE), level.rows - y)
            };
            // Copie du morceau
            level(sourceRect).copyTo(tile(cv::Rect{
                0, 0, sourceRect.width, sourceRect.height
            }));

            // Écriture dans le fichier TIFF
            const ttile_t tileIndex = TIFFComputeTile(
                file, static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0, 0);
            if (TIFFWriteEncodedTile(
                    file, tileIndex, tile.data, static_cast<tmsize_t>(tileBytes)) == -1) {
                error = "cannot write TIFF tile";
                return false;
            }
        }
    }

    if (!TIFFWriteDirectory(file)) {
        error = "cannot finalize TIFF directory";
        return false;
    }
    return true;
}

} // namespace

bool createPyramidalTiledTiff(
    const cv::Mat& image,
    const std::filesystem::path& outputPath,
    std::string& error
) {
    // Vérification
    error.clear();
    if (image.empty()) {
        error = "input image is empty";
        return false;
    }
    if (image.dims != 2 || image.cols <= 0 || image.rows <= 0) {
        error = "input image must be a non-empty two-dimensional matrix";
        return false;
    }

    // Dossier de sortie
    std::error_code directoryError;
    const auto parent = outputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directoryError);
        if (directoryError) {
            error = "cannot create output directory: " + directoryError.message();
            return false;
        }
    }

    // Ouverture du fichier TIFF en mode écriture
    TIFF* file = TIFFOpen(outputPath.string().c_str(), "w");
    if (file == nullptr) {
        error = "cannot open output TIFF";
        return false;
    }

    // Écriture pour la gestion des erreurs -> on break si une erreur durant le process survient
    bool success = false;
    do {
        // TIFFTAG_SUBIFD = tag pour indiquer les sous-répertoires pour les niveaux de la pyramide
        toff_t subIfdOffsets[PYRAMID_LEVELS - 1] = {};
        if (!TIFFSetField(file, TIFFTAG_SUBIFD, PYRAMID_LEVELS - 1, subIfdOffsets)) {
            error = "cannot configure TIFF pyramid directories";
            break;
        }

        // Écriture du niveau 0 (l'image originale) -> si une des fonctions échoue, on break
        if (!setDirectoryFields(file, image, error) || !writeLevel(file, image, error)) {
            break;
        }

        cv::Mat previous = image;
        // On boucle sur tous les (sous-) niveaux de la pyramide (1 à 3)
        for (int levelIndex = 1; levelIndex < PYRAMID_LEVELS; ++levelIndex) {
            cv::Mat level;
            // On réduit l'image précédente de moitié pour créer le niveau suivant de la pyramide
            cv::resize(previous, level, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
            if (level.empty()) {
                error = "cannot create reduced pyramid level";
                break;
            }
            if (!setDirectoryFields(file, level, error) || !writeLevel(file, level, error)) {
                break;
            }
            previous = std::move(level);
        }

        success = error.empty();
    } while (false);

    // Nettoyage -> ferme le pointeur de fichier (TIFFClose). En cas d'erreur durant n'importe quelle étape de la boucle, le fichier corrompu/incomplet est automatiquement supprimé du disque.
    TIFFClose(file);
    if (!success) {
        std::error_code removeError;
        std::filesystem::remove(outputPath, removeError);
    }
    return success;
}

} // namespace stitching
