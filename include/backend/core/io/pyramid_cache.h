// include/backend/core/io/pyramid_cache.h
#pragma once

#include <filesystem>
#include <string>

namespace IONs {

    // Cache des TIFF pyramidaux des images importées, partagé par le step import et par l'interface (vignettes et
    // génération en arrière-plan) : une seule règle de nommage et de validité pour les deux.
    //
    // - Nom : <dossier du cache>/<nom de l'image>_<empreinte du chemin complet>_pyramid.tif. L'empreinte rend le nom
    //   propre à une source : photo.tif et photo.tiff, ou deux images de même nom dans deux dossiers, n'ont jamais le
    //   même cache, quelle que soit leur case dans la grille.
    // - Provenance : à côté, <pyramide>.source.json garde l'empreinte de la source (chemin, taille, date) lue juste
    //   avant de la convertir. Le cache n'est valable que si cette empreinte est identique à celle de la source
    //   actuelle : une source remplacée, même par un fichier plus ancien, invalide son cache.

    // <dossier des images>/.cache/pyramids
    std::filesystem::path pyramidCacheDirectory(const std::filesystem::path& imagesDirectory);

    // Chemin du TIFF pyramidal de `sourcePath` dans `cacheDirectory` (voir la règle de nommage ci-dessus).
    std::filesystem::path pyramidCachePath(const std::filesystem::path& cacheDirectory,
                                           const std::filesystem::path& sourcePath);

    // true si `pyramidPath` existe, n'est pas vide et a été produit à partir de la version actuelle de `sourcePath`.
    // Ne lève jamais : un fichier illisible ou une provenance absente rendent le cache invalide.
    bool isPyramidCacheValid(const std::filesystem::path& sourcePath, const std::filesystem::path& pyramidPath);

    // Pyramide prête à l'emploi : son chemin, les dimensions et le type OpenCV de la pleine résolution, et si le cache
    // existant a été réutilisé.
    struct PyramidCacheEntry {
        std::filesystem::path pyramidPath;
        int width = 0;
        int height = 0;
        int type = 0;
        bool reused = false;
    };

    // Réutilise la pyramide de `sourcePath` si elle est valide (sans lire la source), sinon lit la source et la
    // (re)crée, puis écrit sa provenance. Le TIFF est publié d'un coup (fichier temporaire puis renommage) et la
    // provenance est retirée avant, puis écrite après : un lecteur ne voit jamais une pyramide neuve avec une
    // ancienne provenance. Crée `cacheDirectory` si besoin. Renvoie false avec la cause dans errorMessage.
    bool preparePyramidCache(const std::filesystem::path& sourcePath, const std::filesystem::path& cacheDirectory,
                             PyramidCacheEntry& entry, std::string& errorMessage);

}
