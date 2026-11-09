// include/backend/algorithms/verification/final_verification.h
#pragma once

#include <filesystem>
#include <nlohmann/json_fwd.hpp>

namespace VerificationNs {

    // Lit les métadonnées d'images TIFF/JPEG et retourne les différences observées.
    // Lève std::runtime_error si une image est illisible ou dans un format non pris en charge.
    nlohmann::json compareImages(
        const std::filesystem::path& inputImagePath,
        const std::filesystem::path& outputImagePath
    );

}
