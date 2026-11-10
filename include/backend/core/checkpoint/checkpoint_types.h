// include/backend/core/checkpoint/checkpoint_types.h
#pragma once

#include "core/checkpoint/checkpoint.h"

#include <map>
#include <string>
#include <utility>

namespace CheckpointNs {

    // (Dé)sérialisations des types de données du pipeline, par nom de type, pour CheckpointClass.
    // Une donnée = un fichier JSON <clé>.json dans le dossier du step :
    //   "Tiles"        TileList : fiches des tuiles, voisins par identifiant (les pixels restent dans les TIFF, non recopiés)
    //   "PairMatches"  std::vector<PairMatches>
    //   "Positions"    std::vector<TilePositionData>
    //   "Seamlines"    Seamlines
    //   "Territories"  std::vector<TileTerritory>
    //   "IntensityCorrection"  IntensityCorrection : gains par tuile et force du champ d'éclairage
    //   "Mosaic"       Mosaic : chemins des trois TIFF et dimensions (les pixels restent dans les TIFF, non recopiés)
    // Nouveau type : une entrée dans pipelineTypes() (checkpoint_types.cpp) ou un appel à CheckpointClass::registerType.
    std::map<std::string, std::pair<CheckpointClass::Saver, CheckpointClass::Loader>> pipelineTypes();

}
