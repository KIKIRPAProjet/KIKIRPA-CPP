#pragma once
#include "core/Checkpoint.h"
#include <map>
#include <string>
#include <utility>

namespace stitching {

// (Dé)sérialisation des types qui circulent entre les steps, pour les checkpoints (core/Checkpoint.h).
// Un fichier JSON <clé>.json par sortie, dans le dossier du step :
//   "Tiles"        Tiles (fiches Tuile : chemins, tailles, type, voisins ; les pixels restent dans les TIFF
//                  source et pyramidaux, qui ne sont pas recopiés)
//   "PairMatches"  std::vector<PairMatches>
//   "Positions"    std::vector<TilePositionData>
//   "Territories"  std::vector<Seamline::TileTerritory>
// Pour un nouveau type : une entrée dans pipelineTypes() (CheckpointTypes.cpp), ou Checkpoint::registerType.
std::map<std::string, std::pair<Checkpoint::Saver, Checkpoint::Loader>> pipelineTypes();

} // namespace stitching
