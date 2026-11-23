// src/backend/core/io/file_fingerprint.cpp
#include "core/io/file_fingerprint.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <system_error>

namespace IONs {

    nlohmann::json computeFileFingerprint(const std::filesystem::path& path) {
        std::error_code errorCode;
        const bool exists = std::filesystem::exists(path, errorCode);
        if (errorCode) throw std::filesystem::filesystem_error("cannot inspect output file", path, errorCode);
        nlohmann::json fingerprint = nlohmann::json::object({{"path", path.u8string()}, {"exists", exists}});
        if (!exists) return fingerprint;
        const std::uintmax_t fileSize = std::filesystem::file_size(path, errorCode);
        if (errorCode) throw std::filesystem::filesystem_error("cannot read output file size", path, errorCode);
        const auto modifiedTime = std::filesystem::last_write_time(path, errorCode);
        if (errorCode) throw std::filesystem::filesystem_error("cannot read output file time", path, errorCode);
        fingerprint["size"] = fileSize;
        fingerprint["modified"] = modifiedTime.time_since_epoch().count();
        return fingerprint;
    }

}
