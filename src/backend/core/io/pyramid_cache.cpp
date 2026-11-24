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
#include <opencv2/imgproc.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <string>
#include <system_error>

namespace IONs {

    namespace fs = std::filesystem;

    namespace {

        // Version du format du fichier produit, écrite dans la provenance et comparée à la relecture. À incrémenter
        // dès qu'un cache écrit par une version antérieure ne peut plus être relu tel quel.
        //   2 : canaux dans l'ordre déclaré (RGB / RGBA), compression Deflate avec prédicteur horizontal ;
        //   1 : BGR déclaré RGB, compression LZW. Ces provenances n'ont pas de champ "formatVersion" : l'empreinte
        //       y est à la racine du JSON, donc elles n'ont pas de champ "source" non plus et sont invalides
        //       d'elles-mêmes. Il n'y a rien à migrer : elles sont réécrites au prochain import.
        constexpr int PYRAMID_FORMAT_VERSION = 2;

        // FNV-1a sur 32 bits : assez pour distinguer les images d'un même dossier, le nom de l'image restant lisible.
        constexpr std::uint32_t FNV_OFFSET_BASIS = 2166136261U;
        constexpr std::uint32_t FNV_PRIME = 16777619U;
        constexpr std::size_t HASH_TEXT_SIZE = 9;   // 8 chiffres hexadécimaux et le zéro final

        // Horloge murale : elle survit à un redémarrage, contrairement à steady_clock, et c'est bien d'un âge en
        // jours qu'il s'agit. L'époque n'a pas besoin d'être celle d'Unix : écriture et comparaison utilisent la même.
        std::int64_t nowInSeconds() {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count();
        }

        constexpr std::int64_t SECONDS_PER_DAY = 24 * 60 * 60;
        constexpr const char* PYRAMID_SUFFIX = "_pyramid.tif";

        bool endsWith(const std::string& text, const std::string& suffix) {
            return text.size() >= suffix.size() &&
                   text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

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

        // Contenu du .source.json : voir la règle dans core/io/pyramid_cache.h. Seuls `source` et `formatVersion`
        // sont comparés par isPyramidCacheValid ; `lastRead`, comme tout champ ajouté ici plus tard, reste hors
        // de cette comparaison. Une pyramide qui vient d'être écrite compte comme employée maintenant.
        nlohmann::json makeProvenance(const nlohmann::json& fingerprint) {
            return nlohmann::json::object({{"source", fingerprint},
                                           {"formatVersion", PYRAMID_FORMAT_VERSION},
                                           {"lastRead", nowInSeconds()}});
        }

        // Lecture défensive : la provenance est un fichier sur disque, un champ peut manquer ou avoir le mauvais
        // type. 0 n'est la version d'aucun format écrit : un fichier douteux est donc invalide.
        int provenanceFormatVersion(const nlohmann::json& provenance) {
            const auto field = provenance.find("formatVersion");
            return field != provenance.end() && field->is_number_integer() ? field->get<int>() : 0;
        }

        // Canaux dans l'ordre que le TIFF déclare : OpenCV lit en BGR (BGRA avec alpha), le writer déclare
        // PHOTOMETRIC_RGB. Sans cet échange, le fichier mentirait sur ses canaux et chaque lecteur devrait défaire
        // l'erreur de son côté (dette T6/T7). En place : une seconde image pleine résolution ferait 578 Mio de plus
        // sur une tuile du KIKIRPA, et l'échange est local à chaque pixel.
        void swapToDeclaredChannelOrder(cv::Mat& image) {
            if (image.channels() == 3) cv::cvtColor(image, image, cv::COLOR_BGR2RGB);
            if (image.channels() == 4) cv::cvtColor(image, image, cv::COLOR_BGRA2RGBA);
        }

        bool writeProvenance(const fs::path& pyramidPath, const nlohmann::json& fingerprint, std::string& errorMessage) {
            const fs::path destination = provenancePath(pyramidPath);
            const fs::path temporary = makeTemporaryPath(destination);
            bool written = false;
            {
                std::ofstream stream(temporary, std::ios::binary);   // ouverture en écriture : contenu remplacé
                stream << makeProvenance(fingerprint).dump();
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
            cv::Mat image = readImage(sourcePath, cv::IMREAD_UNCHANGED);   // IMREAD_UNCHANGED garde les 16 bits
            if (image.empty()) {
                errorMessage = "cannot read " + pathToUtf8(sourcePath);
                return false;
            }
            swapToDeclaredChannelOrder(image);   // BGR d'OpenCV -> RGB déclaré par le TIFF ; les dimensions et le
                                                 // type ne changent pas, entry les garde tels quels.
            std::error_code removeError;
            fs::remove(provenancePath(entry.pyramidPath), removeError);   // cache invalide pendant l'écriture
            if (!createPyramidalTiledTiff(image, entry.pyramidPath, errorMessage)) return false;
            if (!writeProvenance(entry.pyramidPath, fingerprint, errorMessage)) return false;
            entry.width = image.cols;
            entry.height = image.rows;
            entry.type = image.type();
            entry.reused = false;
            return true;
        }

        // Rafraîchit `lastRead` sans toucher au reste de la provenance : lire, modifier, réécrire par le
        // temporaire. Un jour de granularité : assez fin pour une politique en jours, et ça évite de réécrire la
        // provenance à chaque vignette que l'interface demande — ce chemin est déjà en course avec la publication
        // atomique. Un échec est sans conséquence (le cache reste valable), il n'est donc pas remonté.
        void touchProvenance(const fs::path& pyramidPath) {
            nlohmann::json provenance = readProvenance(pyramidPath);
            if (!provenance.is_object()) return;
            const auto field = provenance.find("lastRead");
            const std::int64_t recorded =
                field != provenance.end() && field->is_number_integer() ? field->get<std::int64_t>() : 0;
            const std::int64_t now = nowInSeconds();
            if (now - recorded < SECONDS_PER_DAY) return;
            provenance["lastRead"] = now;

            const fs::path destination = provenancePath(pyramidPath);
            const fs::path temporary = makeTemporaryPath(destination);
            bool written = false;
            {
                std::ofstream stream(temporary, std::ios::binary);
                stream << provenance.dump();
                written = static_cast<bool>(stream);
            }
            std::string ignoredError;
            if (!written || !replaceFile(temporary, destination, ignoredError)) {
                std::error_code removeError;
                fs::remove(temporary, removeError);
            }
        }

        // Parcourt les pyramides du cache et supprime celles que `shouldRemove` désigne, avec leur provenance.
        // Le suffixe EXACT est indispensable : makeTemporaryPath fabrique « ….tif.<horloge>.<n>.tmp », et un test
        // par « le nom contient _pyramid.tif » attraperait un fichier en cours de publication par un autre
        // processus.
        std::size_t removePyramidsIf(const fs::path& cacheDirectory,
                                     const std::function<bool(const nlohmann::json&)>& shouldRemove) {
            std::size_t removedCount = 0;
            std::error_code errorCode;
            // Avec error_code, un dossier absent donne une plage vide au lieu de lever.
            for (const fs::directory_entry& entry : fs::directory_iterator(cacheDirectory, errorCode)) {
                if (!entry.is_regular_file(errorCode)) continue;
                if (!endsWith(pathToUtf8(entry.path().filename()), PYRAMID_SUFFIX)) continue;
                const nlohmann::json provenance = readProvenance(entry.path());
                if (!provenance.is_object() || !shouldRemove(provenance)) continue;   // dans le doute, on garde
                std::error_code removeError;
                if (fs::remove(entry.path(), removeError)) ++removedCount;
                if (fs::remove(provenancePath(entry.path()), removeError)) ++removedCount;
            }
            return removedCount;
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
            if (!provenance.is_object()) return false;
            // La version d'abord : un entier contre trois appels système, et un cache d'un autre format n'a pas à
            // être comparé à sa source pour être refusé.
            if (provenanceFormatVersion(provenance) != PYRAMID_FORMAT_VERSION) return false;
            return provenance.value("source", nlohmann::json()) == sourceFingerprint(sourcePath);
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
                touchProvenance(entry.pyramidPath);
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

    std::size_t removeOrphanPyramids(const fs::path& cacheDirectory) {
        return removePyramidsIf(cacheDirectory, [](const nlohmann::json& provenance) {
            // Le nom du fichier ne permet pas de retrouver la source : pathHash est à sens unique. Seule la
            // provenance garde le chemin.
            const auto source = provenance.find("source");
            if (source == provenance.end() || !source->is_object()) return false;
            const auto path = source->find("path");
            if (path == source->end() || !path->is_string()) return false;
            std::error_code errorCode;
            return !fs::exists(pathFromUtf8(path->get<std::string>()), errorCode);
        });
    }

    std::size_t removeStalePyramids(const fs::path& cacheDirectory, int maximumAgeDays) {
        if (maximumAgeDays <= 0) return 0;
        const std::int64_t limit = static_cast<std::int64_t>(maximumAgeDays) * SECONDS_PER_DAY;
        return removePyramidsIf(cacheDirectory, [limit](const nlohmann::json& provenance) {
            const auto field = provenance.find("lastRead");
            if (field == provenance.end() || !field->is_number_integer()) return false;   // date inconnue : on garde
            return nowInSeconds() - field->get<std::int64_t>() > limit;
        });
    }

    std::size_t removeAllPyramids(const fs::path& cacheDirectory) {
        return removePyramidsIf(cacheDirectory, [](const nlohmann::json&) { return true; });
    }
}
