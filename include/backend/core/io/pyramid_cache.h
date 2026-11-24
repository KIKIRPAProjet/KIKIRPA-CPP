// include/backend/core/io/pyramid_cache.h
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace IONs {

    // Cache des TIFF pyramidaux des images importées, partagé par le step import et par l'interface (vignettes et
    // génération en arrière-plan) : une seule règle de nommage et de validité pour les deux.
    //
    // - Nom : <dossier du cache>/<nom de l'image>_<empreinte du chemin complet>_pyramid.tif. L'empreinte rend le nom
    //   propre à une source : photo.tif et photo.tiff, ou deux images de même nom dans deux dossiers, n'ont jamais le
    //   même cache, quelle que soit leur case dans la grille.
    // - Provenance : à côté, <pyramide>.source.json décrit ce qui a été converti et comment :
    //     { "source": { chemin, existence, taille, date }, "formatVersion": <entier>, "lastRead": <secondes> }
    //   `source` est l'empreinte de l'image lue juste avant la conversion : une source remplacée, même par un
    //   fichier plus ancien, invalide son cache. `formatVersion` décrit le fichier produit (ordre des canaux,
    //   compression) : un cache écrit par une autre version est invalide, donc réécrit, au lieu d'être relu de
    //   travers. Le cache n'est valable que si les deux concordent. `lastRead` est la date du dernier emploi de
    //   la pyramide : elle ne sert qu'au ménage sur l'âge et n'entre jamais dans la comparaison de validité,
    //   pas plus qu'un champ ajouté plus tard (emplacement du niveau 0).
    // - Canaux : le TIFF du cache contient ce qu'il déclare, donc du **RGB** (RGBA avec un canal alpha), et non
    //   l'ordre BGR d'OpenCV. La conversion est faite à l'écriture, une fois, au lieu d'être défaite par chaque
    //   lecteur.

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

    // Ménage du cache. À n'appeler que quand aucun calcul ne tourne sur ce dossier : supprimer une pyramide sous
    // les pieds d'un pipeline provoque une course avec sa publication. Les trois fonctions ne lèvent jamais
    // et renvoient le nombre de fichiers supprimés (la pyramide et sa provenance comptent pour deux). Une pyramide
    // dont la provenance est absente ou illisible est gardée : on ne peut pas prouver qu'elle est inutile.

    // Pyramides dont la source enregistrée n'existe plus : image renommée, retirée, ou cache d'un dossier déplacé.
    std::size_t removeOrphanPyramids(const std::filesystem::path& cacheDirectory);

    // Pyramides dont le dernier emploi dépasse `maximumAgeDays`. Zéro ou négatif : ne supprime rien.
    std::size_t removeStalePyramids(const std::filesystem::path& cacheDirectory, int maximumAgeDays);

    // Toutes les pyramides du cache : le « Libérer l'espace » demandé explicitement par l'utilisateur.
    std::size_t removeAllPyramids(const std::filesystem::path& cacheDirectory);

}
