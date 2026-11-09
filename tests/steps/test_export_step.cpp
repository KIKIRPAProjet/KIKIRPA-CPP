// tests/steps/test_export_step.cpp
#include "steps/export_step.h"

#include "core/data/algorithm_types.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>

#include <chrono>
#include <filesystem>
#include <vector>

namespace {

/**
 * @brief Gestionnaire RAII de répertoire temporaire pour isoler les fichiers créés par les tests d'export.
 *        Le dossier et son contenu sont automatiquement purgés à la destruction.
 */
class TempDir {
public:
    TempDir() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("kikirpa_test_export_" + std::to_string(now));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::filesystem::path path;
};

} // namespace

/**
 * @brief Vérifie que le step d'export refuse de s'exécuter si le DataStore ou les blocs composites sont absents.
 */
TEST(ExportStepTest, RejectsMissingDataStoreOrBlocks) {
    StepsNs::ExportStepClass step;
    // 1. Aucun DataStore associé
    EXPECT_FALSE(step.execute());

    // 2. DataStore présent mais sans clé 'compositeBlocks'
    StepNs::DataStoreClass store;
    step.setDataStore(store);
    EXPECT_FALSE(step.execute());
}

/**
 * @brief Test d'exportation d'un bloc composite valide vers un fichier TIFF réel :
 *  - Prépare un bloc composite 100x100 (RGB 16 bits, provenance, masque).
 *  - Exécute ExportStepClass en ciblant un répertoire temporaire.
 *  - Vérifie que le fichier 'mosaic.tif' est créé, non vide, et correspond aux dimensions attendues.
 */
TEST(ExportStepTest, ExportsValidCompositeBlocksToTiff) {
    TempDir tempDir;

    // Création d'un bloc composite synthétique de 100x100 pixels
    DataNs::CompositeBlock block;
    block.blockRect = cv::Rect(0, 0, 100, 100);
    block.imageRgb = cv::Mat(100, 100, CV_16UC3, cv::Scalar(1000, 2000, 3000));
    block.provenance = cv::Mat(100, 100, CV_16UC1, cv::Scalar(1));
    block.mask = cv::Mat(100, 100, CV_8UC1, cv::Scalar(255));

    DataNs::CompositeBlockList blocks = {block};

    // Injection dans le DataStore
    StepNs::DataStoreClass store;
    store.add("compositeBlocks", blocks);

    // Configuration du dossier d'export
    StepsNs::ExportInput input;
    input.outputDirectory = tempDir.path.string();
    StepsNs::ExportStepClass step(input);
    step.setDataStore(store);

    EXPECT_TRUE(step.execute());

    // Vérification de la sortie
    const auto& output = step.getOutput();
    EXPECT_TRUE(std::filesystem::exists(output.imagePath));
    EXPECT_GT(std::filesystem::file_size(output.imagePath), 0u);
    EXPECT_EQ(output.canvasSize.width, 100);
    EXPECT_EQ(output.canvasSize.height, 100);
    EXPECT_EQ(output.blockCount, 1u);
}
