// include/backend/core/data/verification_report.h
#pragma once

#include <nlohmann/json.hpp>

namespace DataNs {

    // Rapport descriptif comparant les caractéristiques de deux images.
    struct VerificationReport {
        nlohmann::json details;
    };

}
