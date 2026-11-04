// include/backend/algorithms/rejection/vote_2d/vote_rejector.h
#pragma once

// Règle de style 'misc-include-cleaner' neutralisée ici :
// Les types métier (Json, Matches, Alignment) sont fournis par l'en-tête parent outlier_rejector.h.
// Cela évite des inclusions directes superflues sans compromettre aucune sécurité logique ou mémoire.
// NOLINTBEGIN(misc-include-cleaner)
#include "algorithms/rejection/outlier_rejector.h"
#include "core/data/algorithm_types.h"

#include <nlohmann/json.hpp>

// Règle de nommage 'readability-identifier-naming' neutralisée ici :
// Conservation du namespace historique 'stitching::rejection' pour garantir la compatibilité
// avec l'ensemble du projet sans casser le code existant.
namespace stitching::rejection { // NOLINT(readability-identifier-naming)

using OutlierRejector = RejectionNs::OutlierRejectorClass;
using Matches = DataNs::Matches;
using Alignment = DataNs::Alignment;
using PairMatches = DataNs::PairMatches;
using Json = nlohmann::json;

// Rejet des aberrants par Vote de Hough 2D et médiane itérative (pure translation 2D).
class VoteRejectorClass : public OutlierRejector {
public:
    VoteRejectorClass() = default;
    explicit VoteRejectorClass(const Json& parameters);
    bool estimate(const Matches& matches, Alignment& out) override;
    bool filterMatches(Matches& matches, Alignment& out);
    bool filterPairMatches(PairMatches& pairMatches, Alignment& out);
    static Json toEqualizationJson(int tileIdA, int tileIdB, const Alignment& alignment);

private:
    Json parameters = Json::object();
};

using VoteRejector = VoteRejectorClass;

} // namespace stitching::rejection

namespace RejectionNs {
using VoteRejectorClass = stitching::rejection::VoteRejectorClass;
using VoteRejector = stitching::rejection::VoteRejector;
} // namespace RejectionNs

namespace RansacNs {
using VoteRejectorClass = stitching::rejection::VoteRejectorClass;
using VoteRejector = stitching::rejection::VoteRejector;
} // namespace RansacNs
// NOLINTEND(misc-include-cleaner)
