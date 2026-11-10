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

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
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
    constexpr std::uint16_t TEST_RGB_SAMPLES_PER_PIXEL = 3;
    constexpr std::uint16_t TEST_EXIF_TIFF_MAGIC = 42;
    constexpr std::uint32_t TEST_EXIF_FIRST_IFD_OFFSET = 8;
    constexpr std::uint16_t TEST_EXIF_ORIENTATION = 6;
    constexpr std::uint16_t TEST_EXIF_ORIENTATION_TAG = 0x0112;
    constexpr std::uint16_t TEST_EXIF_MAKE_TAG = 0x010F;
    constexpr std::uint8_t DIFFERENT_PROFILE_LAST_BYTE = 5;
    constexpr std::uint8_t JPEG_APP1_MARKER = 0xE1;
    constexpr std::uint8_t JPEG_APP2_MARKER_PREFIX = 0xFF;
    constexpr std::uint8_t JPEG_APP2_MARKER = 0xE2;
    constexpr std::uint8_t JPEG_START_OF_IMAGE = 0xD8;
    constexpr std::uint8_t BYTE_MASK = 0xFF;
    constexpr std::size_t BYTE_SHIFT = 8;
    constexpr std::uint16_t TRUNCATED_JPEG_SEGMENT_LENGTH = 16;

    struct TiffParameters {
        std::uint32_t width;                        // Largeur inscrite dans le répertoire TIFF.
        std::uint32_t height;                       // Hauteur inscrite dans le répertoire TIFF.
        std::vector<std::uint8_t> iccProfile = {};   // Profil optionnel utilisé par les tests ICC.
        std::uint16_t orientation = ORIENTATION_TOPLEFT; // Orientation TIFF facultative à simuler.
        std::uint16_t samplesPerPixel = 1;           // Nombre de composantes TIFF.
        std::uint16_t photometric = PHOTOMETRIC_MINISBLACK; // Interprétation des échantillons TIFF.
    };

    struct TemporaryDirectory {
        fs::path path;

        // Crée un répertoire propre à ce test afin que les exécutions parallèles restent isolées.
        TemporaryDirectory() {
            static std::atomic_uint64_t nextId = 0;
            const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (;;) {
                path = fs::temp_directory_path() /
                       ("kikirpa_final_verification_test_" + std::to_string(timestamp) + "_" +
                        std::to_string(nextId.fetch_add(1)));
                std::error_code errorCode;
                if (fs::create_directory(path, errorCode)) return;
                if (errorCode) throw fs::filesystem_error("cannot create test directory", path, errorCode);
            }
        }

        // Supprime les fixtures du test une fois les assertions terminées.
        ~TemporaryDirectory() {
            std::error_code errorCode;
            fs::remove_all(path, errorCode);
        }
    };

    // Génère une image TIFF minimale dont les tags sont entièrement contrôlés par le test.
    void writeTiff(const fs::path& path, const TiffParameters& parameters) {
        // Générer un TIFF minimal avec les tags demandés par l'algorithme, sans dépendre de données externes.
        TIFF* tiffFile = TIFFOpen(path.string().c_str(), "w");
        ASSERT_NE(tiffFile, nullptr);
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_IMAGEWIDTH, parameters.width));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_IMAGELENGTH, parameters.height));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_SAMPLESPERPIXEL, parameters.samplesPerPixel));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_BITSPERSAMPLE, TEST_BITS_PER_SAMPLE));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_PHOTOMETRIC, parameters.photometric));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_COMPRESSION, COMPRESSION_NONE));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_ORIENTATION, parameters.orientation));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_ROWSPERSTRIP, parameters.height));
        if (!parameters.iccProfile.empty())
            ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_ICCPROFILE,
                                     static_cast<std::uint32_t>(parameters.iccProfile.size()),
                                     parameters.iccProfile.data()));

        const std::size_t rowSize = static_cast<std::size_t>(parameters.width) * parameters.samplesPerPixel;
        std::vector<std::uint8_t> row(rowSize, 0);
        for (std::uint32_t rowIndex = 0; rowIndex < parameters.height; ++rowIndex)
            ASSERT_GE(TIFFWriteScanline(tiffFile, row.data(), rowIndex, 0), 0);
        TIFFClose(tiffFile);
    }

    // Écrit exactement les octets fournis pour créer des entrées JPEG invalides ou tronquées.
    void writeBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(output.good());
    }

    // Insère un segment applicatif après SOI pour tester un chemin de lecture JPEG ciblé.
    void addJpegSegment(
        const fs::path& path, std::uint8_t marker, const std::vector<std::uint8_t>& payload
    ) {
        std::ifstream input(path, std::ios::binary);
        const std::vector<char> jpegBytes(
            (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>{});
        ASSERT_GE(jpegBytes.size(), 2u);

        const std::uint16_t segmentLength = static_cast<std::uint16_t>(payload.size() + 2);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(jpegBytes.data(), 2);
        const std::uint8_t markerAndLength[] = {
            JPEG_APP2_MARKER_PREFIX, marker,
            static_cast<std::uint8_t>(segmentLength >> BYTE_SHIFT),
            static_cast<std::uint8_t>(segmentLength & BYTE_MASK)
        };
        output.write(reinterpret_cast<const char*>(markerAndLength), sizeof(markerAndLength));
        output.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        output.write(jpegBytes.data() + 2,
                     static_cast<std::streamsize>(jpegBytes.size() - 2));
        ASSERT_TRUE(output.good());
    }

    // Insère un profil ICC dans un segment APP2 pour exercer son assemblage ordonné.
    void addJpegIccProfile(
        const fs::path& path, const std::vector<std::uint8_t>& profile,
        std::uint8_t chunkIndex = 1, std::uint8_t chunkCount = 1
    ) {
        std::vector<std::uint8_t> payload{
            'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0, chunkIndex, chunkCount
        };
        payload.insert(payload.end(), profile.begin(), profile.end());
        addJpegSegment(path, JPEG_APP2_MARKER, payload);
    }

    // Ajoute un entier au payload EXIF dans l'ordre des octets annoncé par son en-tête TIFF.
    void appendExifInteger(std::vector<std::uint8_t>& payload, std::uint32_t value, std::size_t byteCount,
                           bool littleEndian) {
        for (std::size_t index = 0; index < byteCount; ++index) {
            const std::size_t shiftIndex = littleEndian ? index : byteCount - index - 1;
            payload.push_back(static_cast<std::uint8_t>(value >> (shiftIndex * BYTE_SHIFT)));
        }
    }

    // Construit un segment EXIF minimal avec une orientation et un tag Make, ou un IFD tronqué.
    std::vector<std::uint8_t> makeExifPayload(bool littleEndian, bool truncated = false) {
        std::vector<std::uint8_t> payload{'E', 'x', 'i', 'f', 0, 0};
        payload.push_back(littleEndian ? 'I' : 'M');
        payload.push_back(littleEndian ? 'I' : 'M');
        appendExifInteger(payload, TEST_EXIF_TIFF_MAGIC, 2, littleEndian);
        appendExifInteger(payload, TEST_EXIF_FIRST_IFD_OFFSET, 4, littleEndian);
        appendExifInteger(payload, truncated ? 1 : 2, 2, littleEndian);
        if (truncated) return payload;

        appendExifInteger(payload, TEST_EXIF_ORIENTATION_TAG, 2, littleEndian);
        appendExifInteger(payload, 3, 2, littleEndian);
        appendExifInteger(payload, 1, 4, littleEndian);
        appendExifInteger(payload, TEST_EXIF_ORIENTATION, 2, littleEndian);
        appendExifInteger(payload, 0, 2, littleEndian);

        appendExifInteger(payload, TEST_EXIF_MAKE_TAG, 2, littleEndian);
        appendExifInteger(payload, 2, 2, littleEndian);
        appendExifInteger(payload, 4, 4, littleEndian);
        payload.insert(payload.end(), {'K', 'I', 'K', 0});
        appendExifInteger(payload, 0, 4, littleEndian);
        return payload;
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
        writeTiff(outputPath, {
            JPEG_WIDTH, JPEG_OUTPUT_HEIGHT, {}, ORIENTATION_TOPLEFT,
            TEST_RGB_SAMPLES_PER_PIXEL, PHOTOMETRIC_RGB
        });

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_EQ(report.at("inputImage").at("format"), "JPEG");
        EXPECT_EQ(report.at("outputImage").at("format"), "TIFF");
        EXPECT_EQ(report.at("dimensions").at("input").at("width"), JPEG_WIDTH);
        EXPECT_EQ(report.at("dimensions").at("input").at("height"), JPEG_HEIGHT);
        EXPECT_EQ(report.at("dimensions").at("difference").at("height"), JPEG_OUTPUT_HEIGHT - JPEG_HEIGHT);
        EXPECT_FALSE(report.at("metadataComparisons").contains("compression"));
        EXPECT_FALSE(report.at("metadataComparisons").contains("photometricInterpretation"));
        EXPECT_FALSE(report.at("metadataComparisons").contains("planarConfiguration"));
        for (const auto& difference : report.at("differences"))
            EXPECT_NE(difference.at("property"), "jpegMetadataSegment");
    }

    // Vérifie qu'une référence JPEG et un TIFF équivalents ne produisent pas de faux écarts d'encodage.
    TEST(FinalVerification, DoesNotReportFormatSpecificDifferencesForEquivalentImages) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.jpg";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        ASSERT_TRUE(cv::imwrite(inputPath.string(), cv::Mat::zeros(JPEG_HEIGHT, JPEG_WIDTH, CV_8UC3)));
        writeTiff(outputPath, {
            JPEG_WIDTH, JPEG_HEIGHT, {}, ORIENTATION_TOPLEFT,
            TEST_RGB_SAMPLES_PER_PIXEL, PHOTOMETRIC_RGB
        });

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_TRUE(report.at("differences").empty()) << report.at("differences").dump();
        EXPECT_TRUE(report.at("metadataComparisons").at("bitsPerSample").at("equal").get<bool>());
        EXPECT_TRUE(report.at("metadataComparisons").at("samplesPerPixel").at("equal").get<bool>());
        EXPECT_TRUE(report.at("metadataComparisons").at("orientation").at("equal").get<bool>());
        EXPECT_FALSE(report.at("metadataComparisons").contains("compression"));
        EXPECT_TRUE(report.at("inputImage").at("metadata").contains("compression"));
        EXPECT_TRUE(report.at("outputImage").at("metadata").contains("compression"));
    }

    // Vérifie que l'orientation TIFF 6 inverse les dimensions affichées sans écraser les dimensions stockées.
    TEST(FinalVerification, ReportsStoredAndDisplayedDimensionsForTiffOrientation) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.tif";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        writeTiff(inputPath, {SMALL_IMAGE_WIDTH, SMALL_IMAGE_HEIGHT, {}, ORIENTATION_RIGHTTOP});
        writeTiff(outputPath, {SMALL_IMAGE_HEIGHT, SMALL_IMAGE_WIDTH});

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_EQ(report.at("dimensions").at("input").at("width"), SMALL_IMAGE_WIDTH);
        EXPECT_EQ(report.at("dimensions").at("input").at("height"), SMALL_IMAGE_HEIGHT);
        EXPECT_EQ(report.at("dimensions").at("displayed").at("input").at("width"), SMALL_IMAGE_HEIGHT);
        EXPECT_EQ(report.at("dimensions").at("displayed").at("input").at("height"), SMALL_IMAGE_WIDTH);
        EXPECT_EQ(report.at("dimensions").at("displayed").at("difference").at("width"), 0);
        EXPECT_EQ(report.at("dimensions").at("displayed").at("difference").at("height"), 0);
        EXPECT_FALSE(report.at("warnings").empty());
    }

    // Vérifie le décodage EXIF little-endian et big-endian, y compris les champs texte inline.
    TEST(FinalVerification, ReadsJpegExifInBothByteOrders) {
        TemporaryDirectory directory;
        const fs::path outputPath = directory.path / "reconstructed.tif";
        const fs::path littleEndianPath = directory.path / "little-endian.jpg";
        const fs::path bigEndianPath = directory.path / "big-endian.jpg";
        ASSERT_TRUE(cv::imwrite(
            littleEndianPath.string(), cv::Mat::zeros(JPEG_HEIGHT, JPEG_WIDTH, CV_8UC3)));
        ASSERT_TRUE(cv::imwrite(
            bigEndianPath.string(), cv::Mat::zeros(JPEG_HEIGHT, JPEG_WIDTH, CV_8UC3)));
        addJpegSegment(littleEndianPath, JPEG_APP1_MARKER, makeExifPayload(true));
        addJpegSegment(bigEndianPath, JPEG_APP1_MARKER, makeExifPayload(false));
        writeTiff(outputPath, {JPEG_HEIGHT, JPEG_WIDTH});

        const auto littleEndianReport = VerificationNs::compareImages(littleEndianPath, outputPath);
        const auto bigEndianReport = VerificationNs::compareImages(bigEndianPath, outputPath);
        EXPECT_EQ(littleEndianReport.at("inputImage").at("metadata").at("exif").at("parseStatus"), "parsed");
        EXPECT_EQ(bigEndianReport.at("inputImage").at("metadata").at("exif").at("parseStatus"), "parsed");
        EXPECT_EQ(littleEndianReport.at("inputImage").at("metadata").at("exif").at("orientation"), 6);
        EXPECT_EQ(bigEndianReport.at("inputImage").at("metadata").at("exif").at("orientation"), 6);
        EXPECT_EQ(littleEndianReport.at("inputImage").at("metadata").at("exif").at("make"), "KIK");
        EXPECT_EQ(bigEndianReport.at("inputImage").at("metadata").at("exif").at("make"), "KIK");
        EXPECT_EQ(
            littleEndianReport.at("dimensions").at("displayed").at("difference").at("width"), 0);
        EXPECT_EQ(
            littleEndianReport.at("dimensions").at("displayed").at("difference").at("height"), 0);
        EXPECT_EQ(
            bigEndianReport.at("dimensions").at("displayed").at("difference").at("width"), 0);
        EXPECT_EQ(
            bigEndianReport.at("dimensions").at("displayed").at("difference").at("height"), 0);
    }

    // Vérifie qu'un IFD tronqué est signalé dans le rapport sans lire hors du segment.
    TEST(FinalVerification, ReportsTruncatedJpegExifAsInvalid) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.jpg";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        ASSERT_TRUE(cv::imwrite(inputPath.string(), cv::Mat::zeros(JPEG_HEIGHT, JPEG_WIDTH, CV_8UC3)));
        addJpegSegment(inputPath, JPEG_APP1_MARKER, makeExifPayload(true, true));
        writeTiff(outputPath, {JPEG_WIDTH, JPEG_HEIGHT});

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_EQ(report.at("inputImage").at("metadata").at("exif").at("parseStatus"), "invalid");
    }

    // Vérifie que les entrées mal formées et les extensions interdites échouent explicitement.
    TEST(FinalVerification, RejectsInvalidAndUnsupportedImageInputs) {
        TemporaryDirectory directory;
        const fs::path outputPath = directory.path / "reconstructed.tif";
        const fs::path invalidJpegPath = directory.path / "invalid.jpg";
        const fs::path truncatedJpegPath = directory.path / "truncated.jpg";
        const fs::path unsupportedPath = directory.path / "reference.bmp";
        writeTiff(outputPath, {JPEG_WIDTH, JPEG_HEIGHT});
        writeBytes(invalidJpegPath, {0, 1, 2, 3});
        writeBytes(truncatedJpegPath, {
            JPEG_APP2_MARKER_PREFIX, JPEG_START_OF_IMAGE, JPEG_APP2_MARKER_PREFIX, JPEG_APP1_MARKER,
            static_cast<std::uint8_t>(TRUNCATED_JPEG_SEGMENT_LENGTH >> BYTE_SHIFT),
            static_cast<std::uint8_t>(TRUNCATED_JPEG_SEGMENT_LENGTH & BYTE_MASK), 1, 2
        });
        writeBytes(unsupportedPath, {0});

        EXPECT_THROW(VerificationNs::compareImages(invalidJpegPath, outputPath), std::runtime_error);
        EXPECT_THROW(VerificationNs::compareImages(truncatedJpegPath, outputPath), std::runtime_error);
        EXPECT_THROW(VerificationNs::compareImages(unsupportedPath, outputPath), std::runtime_error);
        EXPECT_THROW(
            VerificationNs::compareImages(outputPath, directory.path / "output.jpg"), std::runtime_error);
    }

    // Vérifie qu'un profil ICC JPEG incomplet est refusé plutôt que comparé partiellement.
    TEST(FinalVerification, RejectsIncompleteJpegIccProfile) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.jpg";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        ASSERT_TRUE(cv::imwrite(inputPath.string(), cv::Mat::zeros(JPEG_HEIGHT, JPEG_WIDTH, CV_8UC3)));
        addJpegIccProfile(inputPath, {1, 2, 3, 4}, 1, 2);
        writeTiff(outputPath, {JPEG_WIDTH, JPEG_HEIGHT});

        EXPECT_THROW(VerificationNs::compareImages(inputPath, outputPath), std::runtime_error);
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
