// tests/steps/test_final_verification_step.cpp
#include "algorithms/verification/final_verification.h"
#include "core/checkpoint/checkpoint.h"
#include "core/step/data_store.h"
#include "steps/final_verification_step.h"

#include <gtest/gtest.h>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <tiff.h>
#include <tiffio.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

namespace {

    namespace fs = std::filesystem;

    constexpr std::uint32_t REFERENCE_WIDTH = 100;
    constexpr std::uint32_t REFERENCE_HEIGHT = 50;
    constexpr std::uint32_t OUTPUT_WIDTH = 120;
    constexpr std::uint32_t OUTPUT_HEIGHT = 60;
    constexpr std::uint32_t SMALL_IMAGE_WIDTH = 20;
    constexpr std::uint32_t SMALL_IMAGE_HEIGHT = 10;
    constexpr std::uint32_t JPEG_WIDTH = 24;
    constexpr std::uint32_t JPEG_HEIGHT = 12;
    constexpr std::uint32_t JPEG_OUTPUT_HEIGHT = 16;
    constexpr std::uint32_t CHECKPOINT_IMAGE_WIDTH = 30;
    constexpr std::uint32_t CHECKPOINT_IMAGE_HEIGHT = 20;
    constexpr std::uint32_t CHECKPOINT_OUTPUT_WIDTH = 32;
    constexpr std::uint16_t TEST_BITS_PER_SAMPLE = 8;
    constexpr std::uint8_t DIFFERENT_PROFILE_LAST_BYTE = 5;
    constexpr std::uint8_t JPEG_APP2_MARKER_PREFIX = 0xFF;
    constexpr std::uint8_t JPEG_APP2_MARKER = 0xE2;
    constexpr std::uint8_t BYTE_MASK = 0xFF;
    constexpr std::size_t BYTE_SHIFT = 8;

    struct TiffParameters {
        std::uint32_t width;                        // Largeur inscrite dans le répertoire TIFF.
        std::uint32_t height;                       // Hauteur inscrite dans le répertoire TIFF.
        std::vector<std::uint8_t> iccProfile = {};   // Profil optionnel utilisé par les tests ICC.
        std::uint16_t orientation = ORIENTATION_TOPLEFT; // Orientation TIFF facultative à simuler.
    };

    struct TemporaryDirectory {
        fs::path path = fs::temp_directory_path() / "kikirpa_final_verification_test";

        // Recrée le répertoire connu afin que chaque test parte d'un espace vide.
        TemporaryDirectory() {
            fs::remove_all(path);
            fs::create_directories(path);
        }

        // Supprime les fixtures du test une fois les assertions terminées.
        ~TemporaryDirectory() { fs::remove_all(path); }
    };

    // Génère une image TIFF minimale dont les tags sont entièrement contrôlés par le test.
    void writeTiff(const fs::path& path, const TiffParameters& parameters) {
        // Générer un TIFF minimal avec les tags demandés par l'algorithme, sans dépendre de données externes.
        TIFF* tiffFile = TIFFOpen(path.string().c_str(), "w");
        ASSERT_NE(tiffFile, nullptr);
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_IMAGEWIDTH, parameters.width));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_IMAGELENGTH, parameters.height));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_SAMPLESPERPIXEL, 1));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_BITSPERSAMPLE, TEST_BITS_PER_SAMPLE));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_COMPRESSION, COMPRESSION_NONE));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_ORIENTATION, parameters.orientation));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_ROWSPERSTRIP, parameters.height));
        if (!parameters.iccProfile.empty())
            ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_ICCPROFILE,
                                     static_cast<std::uint32_t>(parameters.iccProfile.size()),
                                     parameters.iccProfile.data()));

        std::vector<std::uint8_t> row(parameters.width, 0);
        for (std::uint32_t rowIndex = 0; rowIndex < parameters.height; ++rowIndex)
            ASSERT_GE(TIFFWriteScanline(tiffFile, row.data(), rowIndex, 0), 0);
        TIFFClose(tiffFile);
    }

    // Insère un profil ICC dans un segment APP2 pour exercer le parcours ICC propre au format JPEG.
    void addJpegIccProfile(const fs::path& path, const std::vector<std::uint8_t>& profile) {
        std::ifstream input(path, std::ios::binary);
        const std::vector<char> jpegBytes(
            (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>{});
        ASSERT_GE(jpegBytes.size(), 2u);

        const std::vector<std::uint8_t> iccPayload{
            'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0, 1, 1
        };
        // Insérer un segment APP2 valide après SOI pour tester l'extraction ICC du lecteur JPEG.
        const std::uint16_t segmentLength = static_cast<std::uint16_t>(iccPayload.size() + profile.size() + 2);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(jpegBytes.data(), 2);
        const std::uint8_t markerAndLength[] = {
            JPEG_APP2_MARKER_PREFIX, JPEG_APP2_MARKER,
            static_cast<std::uint8_t>(segmentLength >> BYTE_SHIFT),
            static_cast<std::uint8_t>(segmentLength & BYTE_MASK)
        };
        output.write(reinterpret_cast<const char*>(markerAndLength), sizeof(markerAndLength));
        output.write(reinterpret_cast<const char*>(iccPayload.data()), static_cast<std::streamsize>(iccPayload.size()));
        output.write(reinterpret_cast<const char*>(profile.data()), static_cast<std::streamsize>(profile.size()));
        output.write(jpegBytes.data() + 2,
                     static_cast<std::streamsize>(jpegBytes.size() - 2));
        ASSERT_TRUE(output.good());
    }

    // Vérifie les écarts dimensionnels et les ratios, sans introduire de comparaison de résolution.
    TEST(FinalVerification, ReportsDimensionsAndAspectRatios) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.tif";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        writeTiff(inputPath, {REFERENCE_WIDTH, REFERENCE_HEIGHT});
        writeTiff(outputPath, {OUTPUT_WIDTH, OUTPUT_HEIGHT});

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_EQ(report.at("status"), "report_ready");
        EXPECT_EQ(report.at("dimensions").at("difference").at("width"), OUTPUT_WIDTH - REFERENCE_WIDTH);
        EXPECT_EQ(report.at("dimensions").at("difference").at("height"), OUTPUT_HEIGHT - REFERENCE_HEIGHT);
        EXPECT_DOUBLE_EQ(report.at("dimensions").at("outputToInputRatio").at("width").get<double>(), 1.2);
        EXPECT_DOUBLE_EQ(report.at("dimensions").at("outputToInputRatio").at("height").get<double>(), 1.2);
        EXPECT_FALSE(report.contains("resolution"));
    }

    // Vérifie qu'une différence de profil est signalée sans prétendre conclure sur l'équivalence visuelle.
    TEST(FinalVerification, ReportsMetadataAndIccByteIdentity) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.tif";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        const std::vector<std::uint8_t> inputProfile{1, 2, 3, 4};
        const std::vector<std::uint8_t> outputProfile{1, 2, 3, DIFFERENT_PROFILE_LAST_BYTE};
        writeTiff(inputPath, {SMALL_IMAGE_WIDTH, SMALL_IMAGE_HEIGHT, inputProfile, ORIENTATION_TOPLEFT});
        writeTiff(outputPath, {SMALL_IMAGE_WIDTH, SMALL_IMAGE_HEIGHT, outputProfile, ORIENTATION_BOTLEFT});

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_EQ(report.at("iccProfile").at("colorimetricEquivalence"), "colorimetric_equivalence_undetermined");
        EXPECT_FALSE(report.at("iccProfile").at("identicalBytes").get<bool>());
        EXPECT_FALSE(report.at("metadataComparisons").at("orientation").at("equal").get<bool>());
        EXPECT_FALSE(report.at("differences").empty());
        EXPECT_FALSE(report.at("warnings").empty());
    }

    // Vérifie que des profils identiques octet par octet reçoivent le statut correspondant.
    TEST(FinalVerification, ReportsIdenticalIccProfileBytes) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.tif";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        const std::vector<std::uint8_t> profile{1, 2, 3, 4};
        writeTiff(inputPath, {SMALL_IMAGE_WIDTH, SMALL_IMAGE_HEIGHT, profile});
        writeTiff(outputPath, {SMALL_IMAGE_WIDTH, SMALL_IMAGE_HEIGHT, profile});

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_TRUE(report.at("iccProfile").at("identicalBytes").get<bool>());
        EXPECT_EQ(report.at("iccProfile").at("colorimetricEquivalence"), "identical_profile_data");
    }

    // Vérifie la lecture d'une référence JPEG et sa comparaison à une sortie TIFF.
    TEST(FinalVerification, HandlesJpegMetadata) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.jpg";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        ASSERT_TRUE(cv::imwrite(inputPath.string(), cv::Mat::zeros(JPEG_HEIGHT, JPEG_WIDTH, CV_8UC3)));
        writeTiff(outputPath, {JPEG_WIDTH, JPEG_OUTPUT_HEIGHT});

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_EQ(report.at("inputImage").at("format"), "JPEG");
        EXPECT_EQ(report.at("outputImage").at("format"), "TIFF");
        EXPECT_EQ(report.at("dimensions").at("input").at("width"), JPEG_WIDTH);
        EXPECT_EQ(report.at("dimensions").at("input").at("height"), JPEG_HEIGHT);
        EXPECT_EQ(report.at("dimensions").at("difference").at("height"), JPEG_OUTPUT_HEIGHT - JPEG_HEIGHT);
    }

    // Vérifie que le profil réparti dans un segment APP2 est réassemblé avant comparaison.
    TEST(FinalVerification, ReadsJpegIccProfileSegments) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.jpg";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        const std::vector<std::uint8_t> profile{1, 2, 3, 4};
        ASSERT_TRUE(cv::imwrite(inputPath.string(), cv::Mat::zeros(JPEG_HEIGHT, JPEG_WIDTH, CV_8UC3)));
        addJpegIccProfile(inputPath, profile);
        writeTiff(outputPath, {JPEG_WIDTH, JPEG_HEIGHT, profile});

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_TRUE(report.at("iccProfile").at("inputPresent").get<bool>());
        EXPECT_TRUE(report.at("iccProfile").at("identicalBytes").get<bool>());
    }

    // Vérifie le contrat d'exécution du step et la publication du rapport dans le DataStore.
    TEST(FinalVerificationStep, PublishesReportInDataStore) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.tif";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        writeTiff(inputPath, {CHECKPOINT_IMAGE_WIDTH, CHECKPOINT_IMAGE_HEIGHT});
        writeTiff(outputPath, {CHECKPOINT_IMAGE_WIDTH, CHECKPOINT_IMAGE_HEIGHT});

        StepNs::DataStoreClass store;
        StepsNs::FinalVerificationStepClass step({
            inputPath.string(),
            outputPath.string()
        });
        step.setDataStore(store);

        ASSERT_TRUE(step.execute()) << step.getResult().dump();
        EXPECT_EQ(store.get<DataNs::VerificationReport>("verificationReport").details.at("status"), "report_ready");
        EXPECT_EQ(step.getResult().at("dimensions").at("difference").at("width"), 0);
    }

    // Vérifie qu'un checkpoint restaure le même rapport après une sauvegarde réussie.
    TEST(FinalVerificationStep, SavesAndRestoresReportCheckpoint) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.tif";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        const fs::path checkpointPath = directory.path / "checkpoints";
        writeTiff(inputPath, {CHECKPOINT_IMAGE_WIDTH, CHECKPOINT_IMAGE_HEIGHT});
        writeTiff(outputPath, {CHECKPOINT_OUTPUT_WIDTH, CHECKPOINT_IMAGE_HEIGHT});

        StepNs::DataStoreClass store;
        StepsNs::FinalVerificationStepClass step({inputPath.string(), outputPath.string()});
        step.setDataStore(store);
        ASSERT_TRUE(step.execute()) << step.getResult().dump();

        CheckpointNs::CheckpointClass checkpoint(checkpointPath);
        std::string failureReason;
        ASSERT_TRUE(checkpoint.save(0, step, store, failureReason)) << failureReason;
        ASSERT_TRUE(checkpoint.valid(0, step));

        StepNs::DataStoreClass restoredStore;
        checkpoint.load(0, step, "verificationReport", "VerificationReport", restoredStore);
        EXPECT_EQ(
            restoredStore.get<DataNs::VerificationReport>("verificationReport").details,
            store.get<DataNs::VerificationReport>("verificationReport").details
        );
    }

    // Vérifie qu'une entrée absente produit un échec explicite au lieu d'un rapport valide vide.
    TEST(FinalVerificationStep, ReportsUnreadableImage) {
        StepNs::DataStoreClass store;
        StepsNs::FinalVerificationStepClass step({
            "missing-reference.tif",
            "missing-output.tif"
        });
        step.setDataStore(store);
        EXPECT_FALSE(step.execute());
        EXPECT_FALSE(step.getResult().at("error").get<std::string>().empty());
    }

}
