// include/backend/core/io/file_fingerprint.h
#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>

namespace IONs {

    // Empreinte d'un fichier produit par un step, pour StepClass::externalInputsFingerprint : le checkpoint du step
    // se périme si le fichier disparaît ou change. Renvoie {path, exists: false}, ou {path, exists: true, size,
    // modified}. Lève std::filesystem::filesystem_error si le fichier existe mais ne peut pas être inspecté.
    nlohmann::json computeFileFingerprint(const std::filesystem::path& path);

}
