// src/backend/core/io/file_replacement.cpp
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "core/io/file_replacement.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace IONs {

    namespace fs = std::filesystem;

    fs::path makeTemporaryPath(const fs::path& destinationPath) {
        // Horloge et compteur : deux écritures simultanées vers la même destination ne partagent pas de temporaire.
        static std::atomic<std::uint64_t> sequence{0};
        const auto timeValue = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path temporaryPath = destinationPath;
        temporaryPath += "." + std::to_string(timeValue) + "." + std::to_string(sequence.fetch_add(1)) + ".tmp";
        return temporaryPath;
    }

    bool replaceFile(const fs::path& temporaryPath, const fs::path& destinationPath, std::string& errorMessage) {
#ifdef _WIN32
        // fs::rename ne remplace pas toujours un fichier existant sous Windows ; MoveFileExW le fait.
        if (MoveFileExW(temporaryPath.c_str(), destinationPath.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        errorMessage = "cannot replace '" + destinationPath.string() + "': " +
                       std::system_category().message(static_cast<int>(GetLastError()));
        return false;
#else
        std::error_code errorCode;
        fs::rename(temporaryPath, destinationPath, errorCode);
        if (!errorCode) return true;
        errorMessage = "cannot replace '" + destinationPath.string() + "': " + errorCode.message();
        return false;
#endif
    }

    bool copyFileReplacing(const fs::path& sourcePath, const fs::path& destinationPath, std::string& errorMessage) {
        std::error_code errorCode;
        if (fs::equivalent(sourcePath, destinationPath, errorCode)) return true;
        const fs::path temporaryPath = makeTemporaryPath(destinationPath);
        fs::copy_file(sourcePath, temporaryPath, errorCode);
        if (errorCode) {
            errorMessage = "cannot copy '" + sourcePath.string() + "': " + errorCode.message();
        } else if (replaceFile(temporaryPath, destinationPath, errorMessage)) {
            return true;
        }
        fs::remove(temporaryPath, errorCode);   // échec déjà signalé : le temporaire ne doit pas rester
        return false;
    }

}
