// include/backend/core/io/unicode_path.h
#pragma once

#include <opencv2/core/mat.hpp>
#include <tiffio.h>

#include <filesystem>
#include <string>

namespace IONs {

    // Règle du projet pour les chemins (docs/CONVENTIONS.md §8, « Chemins ») : toute chaîne qui contient un chemin (script JSON,
    // checkpoint, résultat, message) est en UTF-8. On ne passe jamais par std::string vers std::filesystem::path, ni
    // par path.string() : sous Windows, ces deux conversions utilisent la page de code du système et perdent (ou
    // refusent) les caractères qu'elle n'a pas, comme « 作品 ».

    // Chemin depuis une chaîne UTF-8 (paramètre du script, champ de checkpoint, argument converti).
    std::filesystem::path pathFromUtf8(const std::string& utf8Path);

    // Chaîne UTF-8 d'un chemin, pour un message, un JSON ou une expression régulière.
    std::string pathToUtf8(const std::filesystem::path& path);

    // TIFFOpen pour tout chemin : TIFFOpenW sous Windows. Renvoie nullptr si le fichier ne s'ouvre pas.
    TIFF* openTiff(const std::filesystem::path& path, const char* mode);

    // cv::imread pour tout chemin. Image vide si le fichier est absent ou illisible, comme cv::imread.
    cv::Mat readImage(const std::filesystem::path& path, int flags);

    // cv::imwrite pour tout chemin (format déduit de l'extension). Renvoie false en cas d'échec.
    bool writeImage(const std::filesystem::path& path, const cv::Mat& image);

}
