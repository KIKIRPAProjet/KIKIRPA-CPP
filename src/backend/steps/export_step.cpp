// src/backend/steps/export_step.cpp
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "steps/export_step.h"

#include "core/config/pipeline_config.h"
#include "core/data/tile.h"

#include <tiffio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace StepsNs {

    namespace fs = std::filesystem;

    namespace {

        // Index associant une coordonnée de grille (colonne, ligne) au pointeur vers le bloc composite correspondant
        using BlockIndex = std::map<std::pair<int, int>, const DataNs::CompositeBlock*>;

        // Chemins des fichiers générés par l'export
        struct OutputPaths {
            fs::path image;
        };

        // Représente un fichier temporaire en cours d'écriture destiné à être renommé atomiquement
        struct PendingFile {
            fs::path temporaryPath;
            fs::path destinationPath;
        };

        /**
         * @brief Détermine le chemin vers le dossier standard "Téléchargements" selon le système d'exploitation.
         *        Sous Windows : utilise l'API Win32 `SHGetKnownFolderPath(FOLDERID_Downloads, ...)` avec repli sur %USERPROFILE%.
         *        Sous Linux / macOS : utilise la variable d'environnement $HOME.
         *        Repli par défaut : sous-dossier "./output" du répertoire courant.
         */
        fs::path downloadsDirectory() {
#ifdef _WIN32
            PWSTR knownFolderPath = nullptr;
            const HRESULT result = SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &knownFolderPath);
            if (SUCCEEDED(result) && knownFolderPath) {
                const fs::path path(knownFolderPath);
                CoTaskMemFree(knownFolderPath);
                return path;
            }
            if (knownFolderPath) CoTaskMemFree(knownFolderPath);
            const wchar_t* profilePath = _wgetenv(L"USERPROFILE");
            if (profilePath && *profilePath) return fs::path(profilePath) / L"Downloads";
#else
            const char* homePath = std::getenv("HOME");
            if (homePath && *homePath) return fs::path(homePath) / "Downloads";
#endif
            return fs::current_path() / "output";
        }

        // Construit le chemin complet du fichier TIFF final au sein du répertoire cible
        OutputPaths makeOutputPaths(const fs::path& directory) {
            return {directory / "mosaic.tif"};
        }

        /**
         * @brief Génère un chemin de fichier temporaire unique basé sur l'horloge système et un compteur atomique.
         *        Exemple : /chemin/mosaic.tif.1731154000.0.tmp
         */
        fs::path temporaryPath(const fs::path& path) {
            static std::atomic<std::uint64_t> sequence{0};
            const auto timeValue = std::chrono::steady_clock::now().time_since_epoch().count();
            fs::path result = path;
            result += "." + std::to_string(timeValue) + "." + std::to_string(sequence.fetch_add(1)) + ".tmp";
            return result;
        }

        /**
         * @brief Valide les dimensions, l'alignement sur la grille et le format d'image d'un bloc composite.
         *        - Les coordonnées x, y doivent être des multiples exacts de DataNs::BLOCK_SIZE.
         *        - Les dimensions ne doivent pas dépasser DataNs::BLOCK_SIZE.
         *        - L'image doit être 2D en CV_16UC3.
         */
        bool validateBlock(const DataNs::CompositeBlock& block, std::string& errorMessage) {
            const cv::Rect& rectangle = block.blockRect;
            if (rectangle.x < 0 || rectangle.y < 0 || rectangle.width <= 0 || rectangle.height <= 0 ||
                rectangle.width > DataNs::BLOCK_SIZE || rectangle.height > DataNs::BLOCK_SIZE ||
                rectangle.x % DataNs::BLOCK_SIZE != 0 || rectangle.y % DataNs::BLOCK_SIZE != 0) {
                errorMessage = "Le rectangle du bloc composite doit etre positif et aligne sur la grille de blocs";
                return false;
            }
            if (block.imageRgb.dims != 2 || block.imageRgb.type() != CV_16UC3 ||
                block.imageRgb.size() != rectangle.size()) {
                errorMessage = "L'image du bloc composite presente un type ou des dimensions inattendues (attendu: CV_16UC3)";
                return false;
            }
            return true;
        }

        /**
         * @brief Insère un bloc dans l'index de grille et met à jour les dimensions maximales du canevas.
         *        Vérifie qu'aucun autre bloc n'occupe déjà la même position (colonne, ligne).
         */
        bool addBlockToIndex(const DataNs::CompositeBlock& block, BlockIndex& blockByGridPosition,
                             int& canvasWidth, int& canvasHeight, std::string& errorMessage) {
            if (!validateBlock(block, errorMessage)) return false;

            const cv::Rect& rectangle = block.blockRect;
            const std::int64_t right = static_cast<std::int64_t>(rectangle.x) + rectangle.width;
            const std::int64_t bottom = static_cast<std::int64_t>(rectangle.y) + rectangle.height;
            if (right > std::numeric_limits<int>::max() || bottom > std::numeric_limits<int>::max()) {
                errorMessage = "Les dimensions globales de la mosaique depassent la limite supportee";
                return false;
            }
            const auto gridPosition = std::make_pair(rectangle.x / DataNs::BLOCK_SIZE, rectangle.y / DataNs::BLOCK_SIZE);
            if (!blockByGridPosition.emplace(gridPosition, &block).second) {
                errorMessage = "Plusieurs blocs composites occupent la meme position de grille";
                return false;
            }
            canvasWidth = std::max(canvasWidth, static_cast<int>(right));
            canvasHeight = std::max(canvasHeight, static_cast<int>(bottom));
            return true;
        }

        /**
         * @brief Vérifie que la grille de blocs est complète, sans aucun trou ni débordement :
         *        Tous les (col, lig) de 0 à (nbCols-1, nbLignes-1) doivent être présents,
         *        et les dimensions des blocs sur les bords doivent correspondre exactement au rognage attendu.
         */
        bool validateBlockGrid(const BlockIndex& blockByGridPosition, cv::Size canvasSize, std::string& errorMessage) {
            const int columnCount = 1 + (canvasSize.width - 1) / DataNs::BLOCK_SIZE;
            const int rowCount = 1 + (canvasSize.height - 1) / DataNs::BLOCK_SIZE;
            const std::size_t expectedBlockCount = static_cast<std::size_t>(columnCount) * static_cast<std::size_t>(rowCount);
            if (blockByGridPosition.size() != expectedBlockCount) {
                errorMessage = "Les blocs composites ne couvrent pas l'integralite de la grille de sortie";
                return false;
            }
            for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
                for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
                    const auto blockEntry = blockByGridPosition.find({columnIndex, rowIndex});
                    if (blockEntry == blockByGridPosition.end()) {
                        errorMessage = "Bloc composite manquant dans la grille de sortie";
                        return false;
                    }
                    const cv::Rect& rectangle = blockEntry->second->blockRect;
                    if (rectangle.width != std::min(DataNs::BLOCK_SIZE, canvasSize.width - rectangle.x) ||
                        rectangle.height != std::min(DataNs::BLOCK_SIZE, canvasSize.height - rectangle.y)) {
                        errorMessage = "Les dimensions du bloc composite ne correspondent pas a sa position dans la grille";
                        return false;
                    }
                }
            }
            return true;
        }

        /**
         * @brief Indexe l'ensemble de la liste des blocs et valide la grille complète.
         */
        bool indexBlocks(const DataNs::CompositeBlockList& blocks, BlockIndex& blockByGridPosition,
                         cv::Size& canvasSize, std::string& errorMessage) {
            if (blocks.empty()) {
                errorMessage = "Aucun bloc composite fourni en entree";
                return false;
            }
            int canvasWidth = 0;
            int canvasHeight = 0;
            for (const DataNs::CompositeBlock& block : blocks) {
                if (!addBlockToIndex(block, blockByGridPosition, canvasWidth, canvasHeight, errorMessage)) return false;
            }
            canvasSize = {canvasWidth, canvasHeight};
            return validateBlockGrid(blockByGridPosition, canvasSize, errorMessage);
        }

        /**
         * @brief Configure les en-têtes et tags libtiff pour une image tuilée RGB 16 bits compressée en LZW.
         */
        bool configureTiff(TIFF* tiffFile, cv::Size canvasSize, const cv::Mat& sampleImage, std::string& errorMessage) {
            const std::uint16_t channelCount = static_cast<std::uint16_t>(sampleImage.channels());
            const std::uint16_t bitsPerSample = static_cast<std::uint16_t>(sampleImage.elemSize1() * 8);
            const std::uint16_t photometric = channelCount == 1 ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB;
            if (!TIFFSetField(tiffFile, TIFFTAG_IMAGEWIDTH, static_cast<std::uint32_t>(canvasSize.width)) ||
                !TIFFSetField(tiffFile, TIFFTAG_IMAGELENGTH, static_cast<std::uint32_t>(canvasSize.height)) ||
                !TIFFSetField(tiffFile, TIFFTAG_SAMPLESPERPIXEL, channelCount) ||
                !TIFFSetField(tiffFile, TIFFTAG_BITSPERSAMPLE, bitsPerSample) ||
                !TIFFSetField(tiffFile, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT) ||
                !TIFFSetField(tiffFile, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) ||
                !TIFFSetField(tiffFile, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
                !TIFFSetField(tiffFile, TIFFTAG_PHOTOMETRIC, photometric) ||
                !TIFFSetField(tiffFile, TIFFTAG_COMPRESSION, COMPRESSION_LZW) ||
                !TIFFSetField(tiffFile, TIFFTAG_TILEWIDTH, static_cast<std::uint32_t>(DataNs::BLOCK_SIZE)) ||
                !TIFFSetField(tiffFile, TIFFTAG_TILELENGTH, static_cast<std::uint32_t>(DataNs::BLOCK_SIZE))) {
                errorMessage = "Impossible de configurer les tags de l'image TIFF";
                return false;
            }
            return true;
        }

        /**
         * @brief Détermine de manière conservatrice si la taille du fichier dépasse 4 Go (limite du TIFF 32-bits),
         *        imposant l'utilisation du format BigTIFF (décalages 64 bits).
         */
        bool shouldUseBigTiff(cv::Size canvasSize, const cv::Mat& sampleImage) {
            const long double pixelCount = static_cast<long double>(canvasSize.width) * canvasSize.height;
            const long double bytesPerPixel = sampleImage.elemSize();
            const long double tileCount =
                static_cast<long double>(1 + (canvasSize.width - 1) / DataNs::BLOCK_SIZE) *
                (1 + (canvasSize.height - 1) / DataNs::BLOCK_SIZE);
            const long double conservativeFileSize = pixelCount * bytesPerPixel * 1.5L + tileCount * 256 + 1024 * 1024;
            return conservativeFileSize >= std::numeric_limits<std::uint32_t>::max();
        }

        /**
         * @brief Écrit chaque tuile dans le fichier TIFF via libtiff.
         *        La spécification TIFF exige que toutes les tuiles aient exactement les dimensions déclarées
         *        (ici 512x512). Pour les blocs de bordure (dont la taille réelle est inférieure), ils sont
         *        recopiés dans un tampon temporaire de 512x512 comblé de zéros (padding).
         */
        bool writeTileGrid(TIFF* tiffFile, const BlockIndex& blockByGridPosition, cv::Size canvasSize,
                           std::string& errorMessage) {
            const cv::Mat& sampleImage = blockByGridPosition.begin()->second->imageRgb;
            const std::size_t tileBytes = static_cast<std::size_t>(DataNs::BLOCK_SIZE) * DataNs::BLOCK_SIZE * sampleImage.elemSize();
            cv::Mat tileBuffer = cv::Mat::zeros(DataNs::BLOCK_SIZE, DataNs::BLOCK_SIZE, sampleImage.type());
            const int columnCount = 1 + (canvasSize.width - 1) / DataNs::BLOCK_SIZE;
            const int rowCount = 1 + (canvasSize.height - 1) / DataNs::BLOCK_SIZE;

            for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
                for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
                    const DataNs::CompositeBlock& block = *blockByGridPosition.at({columnIndex, rowIndex});
                    const cv::Mat& blockImage = block.imageRgb;

                    // Réinitialise le tampon à zéro puis copie l'image du bloc
                    tileBuffer.setTo(cv::Scalar::all(0));
                    blockImage.copyTo(tileBuffer(cv::Rect{0, 0, blockImage.cols, blockImage.rows}));

                    // Calcul de l'indice de tuile selon la position (x, y) dans le canevas
                    const ttile_t tileIndex = TIFFComputeTile(
                        tiffFile,
                        static_cast<std::uint32_t>(block.blockRect.x),
                        static_cast<std::uint32_t>(block.blockRect.y),
                        0,
                        0
                    );

                    // Écriture de la tuile encodée
                    if (TIFFWriteEncodedTile(tiffFile, tileIndex, tileBuffer.data, static_cast<tmsize_t>(tileBytes)) == -1) {
                        errorMessage = "Echec de l'ecriture de la tuile TIFF";
                        return false;
                    }
                }
            }
            return true;
        }

        /**
         * @brief Ouvre le fichier TIFF (en mode classique "w" ou BigTIFF "w8"), configure les tags
         *        et écrit l'ensemble des tuiles de la grille.
         */
        bool writeImageTiff(const BlockIndex& blockByGridPosition, cv::Size canvasSize,
                            const fs::path& outputPath, bool& isBigTiff, std::string& errorMessage) {
            const cv::Mat& sampleImage = blockByGridPosition.begin()->second->imageRgb;
            isBigTiff = shouldUseBigTiff(canvasSize, sampleImage);

            // Mode "w8" active BigTIFF dans libtiff, "w" ouvre en TIFF classique
            TIFF* rawTiffFile = TIFFOpen(outputPath.string().c_str(), isBigTiff ? "w8" : "w");
            if (!rawTiffFile) {
                errorMessage = "Impossible d'ouvrir le fichier TIFF en ecriture : '" + outputPath.string() + "'";
                return false;
            }
            const std::unique_ptr<TIFF, decltype(&TIFFClose)> tiffFile(rawTiffFile, &TIFFClose);

            if (!configureTiff(tiffFile.get(), canvasSize, sampleImage, errorMessage)) return false;
            if (!writeTileGrid(tiffFile.get(), blockByGridPosition, canvasSize, errorMessage)) return false;
            if (!TIFFWriteDirectory(tiffFile.get())) {
                errorMessage = "Impossible de finaliser le repertoire de l'image TIFF";
                return false;
            }
            return true;
        }

        /**
         * @brief Remplace de façon atomique le fichier de destination par le fichier temporaire écrit avec succès.
         *        Sous Windows : utilise MoveFileExW avec MOVEFILE_REPLACE_EXISTING et MOVEFILE_WRITE_THROUGH.
         *        Sous POSIX : utilise std::filesystem::rename.
         */
        void publishFile(const PendingFile& file) {
#ifdef _WIN32
            if (!MoveFileExW(file.temporaryPath.c_str(), file.destinationPath.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "Impossible de remplacer atomiquement le fichier '" + file.destinationPath.string() + "'");
            }
#else
            std::error_code errorCode;
            fs::rename(file.temporaryPath, file.destinationPath, errorCode);
            if (errorCode) {
                throw fs::filesystem_error("Impossible de remplacer atomiquement le fichier", file.temporaryPath, file.destinationPath, errorCode);
            }
#endif
        }

        /**
         * @brief Garde RAII pour supprimer les fichiers temporaires en cas d'interruption ou d'exception.
         */
        struct TemporaryFileCleanup {
            std::vector<fs::path> paths;
            ~TemporaryFileCleanup() {
                for (const fs::path& path : paths) {
                    std::error_code ignored;
                    fs::remove(path, ignored);
                }
            }
        };

        /**
         * @brief Calcule l'empreinte de la sortie (existence, taille, date de modification) pour le cache.
         */
        Json outputFingerprint(const OutputPaths& paths) {
            const fs::path& path = paths.image;
            std::error_code errorCode;
            const bool exists = fs::exists(path, errorCode);
            if (errorCode) throw fs::filesystem_error("Impossible d'inspecter la sortie d'export", path, errorCode);
            if (!exists) {
                return {{"path", path.string()}, {"exists", false}};
            }
            const std::uintmax_t fileSize = fs::file_size(path, errorCode);
            if (errorCode) throw fs::filesystem_error("Impossible de lire la taille du fichier d'export", path, errorCode);
            const auto modifiedTime = fs::last_write_time(path, errorCode);
            if (errorCode) throw fs::filesystem_error("Impossible de lire la date de modification du fichier d'export", path, errorCode);
            return {{"path", path.string()},
                    {"exists", true},
                    {"size", fileSize},
                    {"modified", modifiedTime.time_since_epoch().count()}};
        }

        /**
         * @brief Extrait le dossier de sortie configuré dans le JSON ou se replie sur le dossier Téléchargements.
         */
        fs::path configuredOutputDirectory(const Json& configJson) {
            std::string directory;
            if (configJson.contains("parameters") && configJson["parameters"].is_array()) {
                for (const Json& parameter : configJson.at("parameters")) {
                    if (parameter.value("name", "") == "outputDirectory") {
                        directory = parameter.value("value", "");
                        break;
                    }
                }
            }
            return directory.empty() ? downloadsDirectory() : fs::path(directory);
        }

    } // namespace

    // -----------------------------------------------------------------------------
    // Constructeur : déclaration du contrat de step
    // -----------------------------------------------------------------------------
    ExportStepClass::ExportStepClass(const ExportInput& initialInput)
        : input(initialInput) {
        ConfigNs::StepConfig stepConfig;
        stepConfig.name = "export";
        stepConfig.parameters = {{"string", "outputDirectory", initialInput.outputDirectory}};
        stepConfig.inputs = {{"CompositeBlocks", "compositeBlocks"}};
        stepConfig.outputs = {};
        configJson = stepConfig;
    }

    Json ExportStepClass::externalInputsFingerprint() const {
        return outputFingerprint(makeOutputPaths(configuredOutputDirectory(configJson)));
    }

    // -----------------------------------------------------------------------------
    // Méthode principale d'exécution de l'export
    // -----------------------------------------------------------------------------
    bool ExportStepClass::execute() {
        result = Json::object();
        output = {};

        try {
            // 1. Contrôle de la présence du DataStore et de la liste des blocs composites
            if (!dataStore) throw std::runtime_error("DataStore non disponible pour ExportStep");
            if (!dataStore->has("compositeBlocks")) {
                throw std::runtime_error("Entree obligatoire 'compositeBlocks' manquante dans le DataStore");
            }
            const DataNs::CompositeBlockList& blocks = dataStore->get<DataNs::CompositeBlockList>("compositeBlocks");

            // 2. Indexation et validation de la structure de grille
            BlockIndex blockByGridPosition;
            std::string errorMessage;
            if (!indexBlocks(blocks, blockByGridPosition, output.canvasSize, errorMessage)) {
                result["error"] = errorMessage;
                return false;
            }

            // 3. Préparation du répertoire de destination
            const fs::path directory = configuredOutputDirectory(configJson);
            std::error_code directoryError;
            fs::create_directories(directory, directoryError);
            if (directoryError) {
                result["error"] = "Impossible de creer le dossier de sortie '" + directory.string() + "': " + directoryError.message();
                return false;
            }

            // 4. Écriture atomique dans un fichier temporaire .tmp protégé par RAII
            const OutputPaths paths = makeOutputPaths(directory);
            const PendingFile pendingFile{temporaryPath(paths.image), paths.image};
            TemporaryFileCleanup cleanup;
            cleanup.paths.push_back(pendingFile.temporaryPath);
            fs::remove(pendingFile.temporaryPath);

            if (!writeImageTiff(blockByGridPosition, output.canvasSize,
                                pendingFile.temporaryPath, output.isBigTiff, errorMessage)) {
                result["error"] = errorMessage;
                return false;
            }

            // 5. Publication atomique du fichier final
            publishFile(pendingFile);

            // 6. Enregistrement des métadonnées de sortie dans le rapport de résultat
            output.imagePath = paths.image;
            output.blockCount = blocks.size();
            result["image"] = {{"path", paths.image.string()}, {"bigTiff", output.isBigTiff}};
            result["canvasSize"] = {output.canvasSize.width, output.canvasSize.height};
            result["blockCount"] = output.blockCount;
            return true;
        } catch (const std::exception& exception) {
            result["error"] = exception.what();
            return false;
        }
    }

} // namespace StepsNs
