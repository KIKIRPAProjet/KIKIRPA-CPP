// include/backend/core/data/mosaic_files.h
#pragma once

#include <string>

namespace DataNs {

    // Décrit une mosaïque stockée sous forme de blocs PNG (venant de l'assemblage) :
    // couleur, provenance et masque, avec leurs positions dans les noms.
    struct MosaicFiles {

        // Chemin du dossier contenant les fichiers de tous les blocs.
        std::string mosaicPath;

        // Nombre de blocs, chaque bloc possédant trois fichiers.
        int totalBlocks = 0;

    };

}