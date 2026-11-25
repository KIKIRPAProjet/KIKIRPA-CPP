// src/backend/core/io/pyramid_cache.cpp
#include "core/io/pyramid_cache.h"

#include "core/data/tile.h"
#include "core/io/file_fingerprint.h"
#include "core/io/file_replacement.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/pyramidal_tiff_writer.h"
#include "core/io/tile_pixel_reader.h"
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
        //   3 : le niveau 0 n'est écrit que si l'image de l'œuvre ne sait pas servir une région (champ "level0") ;
        //   2 : canaux dans l'ordre déclaré (RGB / RGBA), compression Deflate avec prédicteur horizontal ;
        //   1 : BGR déclaré RGB, compression LZW. Ces provenances n'ont pas de champ "formatVersion" : l'empreinte
        //       y est à la racine du JSON, donc elles n'ont pas de champ "source" non plus et sont invalides
        //       d'elles-mêmes. Il n'y a rien à migrer : elles sont réécrites au prochain import.
        constexpr int PYRAMID_FORMAT_VERSION = 3;

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

        // Clé d'une source : son chemin **canonique**, pour qu'un même fichier atteint de plusieurs façons n'ait
        // qu'un seul cache. fs::canonical résout les liens, les « .. », les séparateurs, la casse réelle du disque
        // et, sous Windows, ramène un lecteur réseau monté à son chemin UNC : les mêmes images vues par Z:\oeuvre\
        // et par \\serveur\partage\oeuvre\ donnent donc la même clé, au lieu de deux caches complets (919 Mio
        // par image en double). Il faut pour cela que le fichier existe : sinon (source disparue, ou clé d'un
        // chemin qu'on vérifie) on retombe sur la normalisation purement lexicale, qui ne lit pas le disque.
        fs::path normalizedSource(const fs::path& sourcePath) {
            std::error_code errorCode;
            const fs::path canonicalPath = fs::canonical(sourcePath, errorCode);
            if (!errorCode) return canonicalPath;
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

        bool writeProvenance(const fs::path& pyramidPath, const nlohmann::json& fingerprint,
                             std::string& errorMessage) {
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

        // Dimensions et type de la pleine résolution, sans charger de pixels : un pixel du dernier niveau suffit
        // pour le type. Passe par TilePixelReaderClass, qui sait où vit le niveau 0 de cette pyramide : les
        // dimensions doivent être celles de la pleine résolution, pas celles du premier niveau du fichier.
        bool readPyramidShape(const fs::path& sourcePath, PyramidCacheEntry& entry, std::string& errorMessage) {
            const TilePixelReaderClass reader(sourcePath, entry.pyramidPath);
            if (!reader.getLevelDimensions(0, entry.width, entry.height, errorMessage)) return false;
            cv::Mat pixel;
            if (!reader.readRegion(reader.getLevelCount() - 1, cv::Rect(0, 0, 1, 1), pixel, errorMessage)) return false;
            entry.type = pixel.type();
            entry.levelZeroInSource = reader.readsLevelZeroFromSource();
            return true;
        }

        bool createPyramid(const fs::path& sourcePath, PyramidCacheEntry& entry, std::string& errorMessage) {
            // Empreinte prise avant la lecture : si la source change pendant la conversion, la provenance écrite est
            // celle de l'ancienne version, et le cache sera refait au prochain passage.
            const nlohmann::json fingerprint = sourceFingerprint(sourcePath);
            // La sonde décide, image par image, si le niveau 0 peut rester dans l'œuvre : voir
            // core/io/tile_pixel_reader.h.
            const SourceRegionSupport support = probeSource(sourcePath);
            cv::Mat image = readImage(sourcePath, cv::IMREAD_UNCHANGED);   // IMREAD_UNCHANGED garde les 16 bits
            if (image.empty()) {
                errorMessage = "cannot read " + pathToUtf8(sourcePath);
                return false;
            }
            swapToDeclaredChannelOrder(image);   // BGR d'OpenCV -> RGB déclaré par le TIFF ; les dimensions et le
                                                 // type ne changent pas, entry les garde tels quels.
            entry.width = image.cols;
            entry.height = image.rows;
            entry.type = image.type();
            entry.reused = false;
            entry.levelZeroInSource = support.servesRegionsCheaply;

            std::error_code removeError;
            fs::remove(provenancePath(entry.pyramidPath), removeError);   // cache invalide pendant l'écriture
            if (entry.levelZeroInSource) {
                // Le niveau 0 sera lu dans l'image de l'œuvre : le cache ne garde que les niveaux réduits, un peu
                // plus du quart de ce qu'il pèserait. La demi-résolution est calculée ici exactement comme le
                // writer l'aurait faite, pour que le niveau 1 de la pyramide soit le même dans les deux cas.
                cv::Mat reducedLevel;
                cv::resize(image, reducedLevel, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
                image.release();   // une seule image pleine résolution à la fois
                if (!createPyramidalTiledTiff(reducedLevel, entry.pyramidPath, errorMessage,
                                              DataNs::LEVEL_COUNT - 1))
                    return false;
            } else if (!createPyramidalTiledTiff(image, entry.pyramidPath, errorMessage)) {
                return false;
            }
            return writeProvenance(entry.pyramidPath, fingerprint, errorMessage);
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
        // `enumerationFailed`, quand il est fourni, dit que le parcours s'est **interrompu** : le nombre rendu
        // n'est alors qu'un compte partiel, et l'appelant ne doit pas l'annoncer comme un ménage terminé.
        std::size_t removePyramidsIf(
            const fs::path& cacheDirectory,
            const std::function<bool(const fs::path&, const nlohmann::json&)>& shouldRemove,
            bool* enumerationFailed = nullptr) {
            std::size_t removedCount = 0;
            // Boucle explicite, et non « for (… : directory_iterator(…, errorCode)) » : l'error_code ne couvre
            // que la construction de l'itérateur, et l'incrément, lui, **lève**. Or removeAllPyramids est
            // appelé depuis un slot Qt (le bouton « Libérer l'espace »), et Qt ne rattrape pas les exceptions
            // des slots : une erreur d'énumération y terminerait l'application, après avoir déjà supprimé une
            // partie des fichiers. Avec error_code, un dossier absent donne une plage vide au lieu de lever.
            std::error_code walkError;
            // Séparé du parcours : l'échec d'un test sur une entrée ne doit pas arrêter le balayage des autres.
            std::error_code entryError;
            auto entry = fs::directory_iterator(cacheDirectory, walkError);
            for (const auto end = fs::directory_iterator(); !walkError && entry != end;
                 entry.increment(walkError)) {
                if (!entry->is_regular_file(entryError) || entryError) continue;
                if (!endsWith(pathToUtf8(entry->path().filename()), PYRAMID_SUFFIX)) continue;
                // La provenance est passée telle quelle, même absente ou illisible : c'est au prédicat de
                // décider. Les balayages sur l'âge et sur la source la réclament et gardent le fichier sans
                // elle ; une demande de l'utilisateur (removeAllPyramids) n'en a pas besoin, et refuser
                // laisserait sur place un fichier que plus rien ne peut récupérer.
                const nlohmann::json provenance = readProvenance(entry->path());
                if (!shouldRemove(entry->path(), provenance)) continue;
                std::error_code removeError;
                const bool removed = fs::remove(entry->path(), removeError);
                // La provenance ne part **qu'avec** son TIFF. Un TIFF verrouillé (un lecteur l'a ouvert à
                // cet instant) dont on aurait supprimé la provenance devient irrécupérable : l'âge et le
                // chemin de la source ne se lisent que là, et pathHash est à sens unique. Le prochain
                // balayage le laisserait donc en place à jamais.
                if (!removed && fs::exists(entry->path(), removeError)) continue;
                if (removed) ++removedCount;
                if (fs::remove(provenancePath(entry->path()), removeError)) ++removedCount;
            }
            // Un dossier de cache absent n'est pas un échec de parcours : il n'y a simplement rien à balayer.
            if (enumerationFailed != nullptr)
                *enumerationFailed = static_cast<bool>(walkError) && fs::exists(cacheDirectory, entryError);
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
            if (isPyramidCacheValid(sourcePath, entry.pyramidPath) &&
                readPyramidShape(sourcePath, entry, errorMessage)) {
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
        return removePyramidsIf(cacheDirectory, [&cacheDirectory](const fs::path& pyramidPath,
                                                                  const nlohmann::json& provenance) {
            // Le nom du fichier ne permet pas de retrouver la source : pathHash est à sens unique. Seule la
            // provenance garde le chemin.
            const auto source = provenance.find("source");
            if (source == provenance.end() || !source->is_object()) return false;
            const auto path = source->find("path");
            if (path == source->end() || !path->is_string()) return false;
            const fs::path recordedSource = pathFromUtf8(path->get<std::string>());
            std::error_code errorCode;
            if (!fs::exists(recordedSource, errorCode)) return true;   // la source a disparu
            // Clé périmée : ce fichier ne porte plus le nom que sa propre source produirait aujourd'hui, donc
            // personne ne le retrouvera. Arrive quand la clé change de règle (chemin enregistré avant la
            // canonicalisation) ou quand le dossier de l'œuvre a été déplacé.
            return pyramidCachePath(cacheDirectory, recordedSource).filename() != pyramidPath.filename();
        });
    }

    std::size_t removeStalePyramids(const fs::path& cacheDirectory, int maximumAgeDays) {
        if (maximumAgeDays <= 0) return 0;
        const std::int64_t limit = static_cast<std::int64_t>(maximumAgeDays) * SECONDS_PER_DAY;
        return removePyramidsIf(cacheDirectory, [limit](const fs::path&, const nlohmann::json& provenance) {
            const auto field = provenance.find("lastRead");
            if (field == provenance.end() || !field->is_number_integer()) return false;   // date inconnue : on garde
            return nowInSeconds() - field->get<std::int64_t>() > limit;
        });
    }

    std::size_t removeAllPyramids(const fs::path& cacheDirectory, bool* enumerationFailed) {
        return removePyramidsIf(
            cacheDirectory, [](const fs::path&, const nlohmann::json&) { return true; }, enumerationFailed);
    }
}
