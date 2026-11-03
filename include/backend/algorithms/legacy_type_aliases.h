// include/backend/algorithms/legacy_type_aliases.h
#pragma once

#include "core/data/algorithm_types.h"

#include <nlohmann/json.hpp>

// TEMPORAIRE. Les types partagés vivent maintenant dans DataNs (core/data/algorithm_types.h). Les algorithmes
// les écrivaient sans préfixe dans le namespace stitching : ces alias leur évitent de tout préfixer d'un coup.
// À supprimer quand chaque fichier d'algorithme qualifie lui-même ses types (DataNs::Matches...).
namespace stitching {

    using Json = nlohmann::json;

    using DataNs::Alignment;
    using DataNs::Direction;
    using DataNs::Matches;
    using DataNs::PairContext;
    using DataNs::PairMatches;
    using DataNs::TileImage;
    using DataNs::TileImages;
    using DataNs::TilePositionData;
    using RecouvrementReel = DataNs::OverlapEstimate;

}
