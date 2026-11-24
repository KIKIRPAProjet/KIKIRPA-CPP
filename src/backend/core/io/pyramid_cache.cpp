// src/backend/core/io/pyramid_cache.cpp
#include "core/io/pyramid_cache.h"

#include "core/io/file_fingerprint.h"
#include "core/io/file_replacement.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/pyramidal_tiff_writer.h"
#include "core/io/unicode_path.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>

namespace IONs {

    namespace fs = std::filesystem;

    namespace {

        // FNV-1a sur 32 bits : assez pour distinguer les images d'un même dossier, le nom de l'image restant lisible.
        constexpr std::uint32_t FNV_OFFSET_BASIS = 2166136261U;
        constexpr std::uint32_t FNV_PRIME = 16777619U;
        constexpr std::size_t HASH_TEXT_SIZE = 9;   // 8 chiffres hexadécimaux et le zéro final

        // Chemin absolu normalisé : la même source donne la même clé, qu'elle vienne du script (backend) ou de Qt
        // (séparateurs '/' sous Windows).
        fs::path normalizedSource(const fs::path& sourcePath) {
            std::error_code errorCode;
            const fs::path absolutePath = fs::absolute(sourcePath, errorCode);
            return (errorCode ? sourcePath : absolutePath).lexically_normal();
        }

        std::string pathHash(const fs::path& sourcePath) {
            std::uint32_t hash = FNV_OFFSET_BASIS;
            for (const char character : pathToUtf8(normalizedSource(sourcePath))) {
                hash ^= static_cast<unsigned char>(character);
                hash *= FNV_PRIME;
            }
            std::array<char, HASH_TEXT_SIZE> text{};
            std::snprintf(text.data(), text.size(), "%08x", static_cast<unsigned int>(hash));
            return text.data();
        }

        fs::path provenancePath(const fs::path& pyramidPath) {
            fs::path path = pyramidPath;
            path += ".source.json";
            return path;
        }

        // Lève std::filesystem::filesystem_error si la source existe mais ne peut pas être inspectée.
        nlohmann::json sourceFingerprint(const fs::path& sourcePath) {
            return computeFileFingerprint(normalizedSource(sourcePath));
        }

        // JSON nul si le fichier manque, valeur « discarded » s'il est invalide : jamais égal à une empreinte.
        nlohmann::json readProvenance(const fs::path& pyramidPath) {
            std::ifstream stream(provenancePath(pyramidPath), std::ios::binary);
            if (!stream) return {};
            return nlohmann::json::parse(stream, nullptr, false);
        }

        bool writeProvenance(const fs::path& pyramidPath, const nlohmann::json& fingerprint, std::string& errorMessage) {
            const fs::path destination = provenancePath(pyramidPath);
            const fs::path temporary = makeTemporaryPath(destination);
            bool written = false;
            {
                std::ofstream stream(temporary, std::ios::binary);   // ouverture en écriture : contenu remplacé
                stream << fingerprint.dump();
                written = static_cast<bool>(stream);
            }
            if (written && replaceFile(temporary, destination, errorMessage)) return true;
            if (!written) errorMessage = "cannot write " + pathToUtf8(temporary);
            std::error_code removeError;
            fs::remove(temporary, removeError);
            return false;
        }

        // Dimensions et type de la pleine résolution, lus dans la pyramide sans charger ses pixels : un pixel du
        // dernier niveau suffit pour le type.
        bool readPyramidShape(PyramidCacheEntry& entry, std::string& errorMessage) {
            const PyramidalTiffReaderClass reader(entry.pyramidPath);
            if (!reader.getLevelDimensions(0, entry.width, entry.height, errorMessage)) return false;
            cv::Mat pixel;
            if (!reader.readRegion(reader.getLevelCount() - 1, cv::Rect(0, 0, 1, 1), pixel, errorMessage)) return false;
            entry.type = pixel.type();
            return true;
        }

        bool createPyramid(const fs::path& sourcePath, PyramidCacheEntry& entry, std::string& errorMessage) {
            // Empreinte prise avant la lecture : si la source change pendant la conversion, la provenance écrite est
            // celle de l'ancienne version, et le cache sera refait au prochain passage.
            const nlohmann::json fingerprint = sourceFingerprint(sourcePath);
            const cv::Mat image = readImage(sourcePath, cv::IMREAD_UNCHANGED);   // IMREAD_UNCHANGED garde les 16 bits
            if (image.empty()) {
                errorMessage = "cannot read " + pathToUtf8(sourcePath);
                return false;
            }
            std::vector<uint8_t> iccProfile;
            double scaleX = 1.0;
            double scaleY = 1.0;
            std::string metaError;
            PyramidalTiffReaderClass(sourcePath).readMetadata(iccProfile, scaleX, scaleY, metaError);

            std::error_code removeError;
            fs::remove(provenancePath(entry.pyramidPath), removeError);   // cache invalide pendant l'écriture
            if (!createPyramidalTiledTiff(image, entry.pyramidPath, errorMessage, iccProfile)) return false;
            if (!writeProvenance(entry.pyramidPath, fingerprint, errorMessage)) return false;
            entry.width = image.cols;
            entry.height = image.rows;
            entry.type = image.type();
            entry.reused = false;
            return true;
        }

    }

    fs::path pyramidCacheDirectory(const fs::path& imagesDirectory) {
        return imagesDirectory / ".cache" / "pyramids";
    }

    // Deux chemins de même type : l'ordre (dossier du cache, puis source) est celui de toute l'API du cache.
    fs::path pyramidCachePath(const fs::path& cacheDirectory,  // NOLINT(bugprone-easily-swappable-parameters)
                              const fs::path& sourcePath) {
        fs::path fileName = sourcePath.stem();
        fileName += pathFromUtf8("_" + pathHash(sourcePath) + "_pyramid.tif");
        return cacheDirectory / fileName;
    }

    bool isPyramidCacheValid(const fs::path& sourcePath,  // NOLINT(bugprone-easily-swappable-parameters) : source, puis pyramide
                             const fs::path& pyramidPath) {
        try {
            std::error_code errorCode;
            if (!fs::is_regular_file(pyramidPath, errorCode)) return false;
            const std::uintmax_t pyramidSize = fs::file_size(pyramidPath, errorCode);
            if (errorCode || pyramidSize == 0) return false;
            const nlohmann::json provenance = readProvenance(pyramidPath);
            return provenance.is_object() && provenance == sourceFingerprint(sourcePath);
        } catch (const std::exception&) {
            return false;
        }
    }

    bool preparePyramidCache(const fs::path& sourcePath, const fs::path& cacheDirectory, PyramidCacheEntry& entry,
                             std::string& errorMessage) {
        entry = {};
        entry.pyramidPath = pyramidCachePath(cacheDirectory, sourcePath);
        errorMessage.clear();
        try {
            std::error_code errorCode;
            fs::create_directories(cacheDirectory, errorCode);
            if (errorCode) {
                errorMessage = "cannot create " + pathToUtf8(cacheDirectory) + ": " + errorCode.message();
                return false;
            }
            if (isPyramidCacheValid(sourcePath, entry.pyramidPath) && readPyramidShape(entry, errorMessage)) {
                entry.reused = true;
                return true;
            }
            errorMessage.clear();   // cache illisible : on le refait
            return createPyramid(sourcePath, entry, errorMessage);
        } catch (const std::exception& exception) {
            errorMessage = exception.what();
            return false;
        }
    }

}
