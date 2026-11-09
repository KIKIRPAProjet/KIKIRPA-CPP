// include/backend/core/data/verification_report.h
#pragma once

#include <nlohmann/json_fwd.hpp>
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): le membre stocke le JSON par valeur.

namespace DataNs {

    // Rapport descriptif comparant les caractéristiques de deux images.
    struct VerificationReport {
        nlohmann::json details; // Rapport JSON complet produit par VerificationNs::compareImages.
    };

}
