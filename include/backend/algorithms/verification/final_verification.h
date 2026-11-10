// include/backend/algorithms/verification/final_verification.h
#pragma once

#include <filesystem>
#include <nlohmann/json_fwd.hpp>

namespace VerificationNs {

    // Compare les métadonnées d'une référence TIFF/JPEG et d'une sortie TIFF, sans lire les pixels.
    // Retourne le rapport JSON; lève std::runtime_error si un fichier est illisible ou son format non pris en charge.
    nlohmann::json compareImages(
        const std::filesystem::path& inputImagePath,
        const std::filesystem::path& outputImagePath
    );

}
