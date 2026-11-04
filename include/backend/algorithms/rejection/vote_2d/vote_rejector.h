// include/backend/algorithms/rejection/vote_2d/vote_rejector.h
#pragma once

#include "algorithms/rejection/outlier_rejector.h"
#include "core/data/algorithm_types.h"

#include <nlohmann/json.hpp>

namespace RejectionNs {

    using Json = nlohmann::json;

    // Rejet des aberrants par Vote de Hough 2D et médiane itérative (pure translation 2D).
    class VoteRejectorClass : public OutlierRejectorClass {

        public:
            VoteRejectorClass() = default;
            explicit VoteRejectorClass(const Json& parameters);

            bool estimate(const DataNs::Matches& matches, DataNs::Alignment& alignment) override;
            bool filterMatches(DataNs::Matches& matches, DataNs::Alignment& out);
            bool filterPairMatches(DataNs::PairMatches& pairMatches, DataNs::Alignment& out);
            static Json toEqualizationJson(int tileIdA, int tileIdB, const DataNs::Alignment& alignment);

        private:
            Json parameters = Json::object();

    };

    using VoteRejector = VoteRejectorClass;

} // namespace RejectionNs

namespace RansacNs {
    using VoteRejectorClass = RejectionNs::VoteRejectorClass;
    using VoteRejector = RejectionNs::VoteRejectorClass;
} // namespace RansacNs

namespace stitching::rejection {
    using VoteRejectorClass = RejectionNs::VoteRejectorClass;
    using VoteRejector = RejectionNs::VoteRejectorClass;
} // namespace stitching::rejection
