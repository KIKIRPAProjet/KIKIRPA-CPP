// src/Backend/core/checkpoint/checkpoint_types.h
#pragma once

#include "core/checkpoint/checkpoint.h"

#include <map>
#include <string>
#include <utility>

namespace CheckpointNs {

    // (Dé)sérialisation des types qui circulent entre les steps, pour les checkpoints (core/CheckpointClass.h).
    // Un fichier JSON <clé>.json par sortie, dans le dossier du step :
    //   "Tiles"        Tiles (fiches Tuile : chemins, tailles, type, voisins ; les pixels restent dans les TIFF
    //                  source et pyramidaux, qui ne sont pas recopiés)
    //   "PairMatches"  std::vector<PairMatches>
    //   "Positions"    std::vector<TilePositionData>
    //   "Territories"  std::vector<Seamline::TileTerritory>
    // Pour un nouveau type : une entrée dans pipelineTypes() (CheckpointTypes.cpp), ou CheckpointClass::registerType.
    std::map<std::string, std::pair<CheckpointClass::Saver, CheckpointClass::Loader>> pipelineTypes();

}
