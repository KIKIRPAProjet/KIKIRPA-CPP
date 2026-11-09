// tests/steps/test_final_verification_step.cpp
#include "algorithms/verification/final_verification.h"
#include "core/checkpoint/checkpoint.h"
#include "core/step/data_store.h"
#include "steps/final_verification_step.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <tiffio.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

    namespace fs = std::filesystem;

    struct TemporaryDirectory {
        fs::path path = fs::temp_directory_path() / "kikirpa_final_verification_test";

        TemporaryDirectory() {
            fs::remove_all(path);
            fs::create_directories(path);
        }

        ~TemporaryDirectory() { fs::remove_all(path); }
    };

    void writeTiff(const fs::path& path, std::uint32_t width, std::uint32_t height,
                   std::vector<std::uint8_t> iccProfile = {}, std::uint16_t orientation = ORIENTATION_TOPLEFT) {
        TIFF* tiffFile = TIFFOpen(path.string().c_str(), "w");
        ASSERT_NE(tiffFile, nullptr);
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_IMAGEWIDTH, width));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_IMAGELENGTH, height));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_SAMPLESPERPIXEL, 1));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_BITSPERSAMPLE, 8));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_COMPRESSION, COMPRESSION_NONE));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_ORIENTATION, orientation));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG));
        ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_ROWSPERSTRIP, height));
        if (!iccProfile.empty())
            ASSERT_TRUE(TIFFSetField(tiffFile, TIFFTAG_ICCPROFILE,
                                     static_cast<std::uint32_t>(iccProfile.size()),
                                     iccProfile.data()));

        std::vector<std::uint8_t> row(width, 0);
        for (std::uint32_t rowIndex = 0; rowIndex < height; ++rowIndex)
            ASSERT_GE(TIFFWriteScanline(tiffFile, row.data(), rowIndex, 0), 0);
        TIFFClose(tiffFile);
    }

    void addJpegIccProfile(const fs::path& path, const std::vector<std::uint8_t>& profile) {
        std::ifstream input(path, std::ios::binary);
        const std::vector<std::uint8_t> jpegBytes(
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        ASSERT_GE(jpegBytes.size(), 2u);

        const std::vector<std::uint8_t> iccPayload{
            'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0, 1, 1
        };
        const std::uint16_t segmentLength = static_cast<std::uint16_t>(iccPayload.size() + profile.size() + 2);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(jpegBytes.data()), 2);
        const std::uint8_t markerAndLength[] = {
            0xFF, 0xE2,
            static_cast<std::uint8_t>(segmentLength >> 8),
            static_cast<std::uint8_t>(segmentLength & 0xFF)
        };
        output.write(reinterpret_cast<const char*>(markerAndLength), sizeof(markerAndLength));
        output.write(reinterpret_cast<const char*>(iccPayload.data()), static_cast<std::streamsize>(iccPayload.size()));
        output.write(reinterpret_cast<const char*>(profile.data()), static_cast<std::streamsize>(profile.size()));
        output.write(reinterpret_cast<const char*>(jpegBytes.data() + 2),
                     static_cast<std::streamsize>(jpegBytes.size() - 2));
        ASSERT_TRUE(output.good());
    }

    TEST(FinalVerification, ReportsDimensionsAndAspectRatios) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.tif";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        writeTiff(inputPath, 100, 50);
        writeTiff(outputPath, 120, 60);

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_EQ(report.at("status"), "report_ready");
        EXPECT_EQ(report.at("dimensions").at("difference").at("width"), 20);
        EXPECT_EQ(report.at("dimensions").at("difference").at("height"), 10);
        EXPECT_DOUBLE_EQ(report.at("dimensions").at("outputToInputRatio").at("width").get<double>(), 1.2);
        EXPECT_DOUBLE_EQ(report.at("dimensions").at("outputToInputRatio").at("height").get<double>(), 1.2);
        EXPECT_FALSE(report.contains("resolution"));
    }

    TEST(FinalVerification, ReportsMetadataAndIccByteIdentity) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.tif";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        const std::vector<std::uint8_t> inputProfile{1, 2, 3, 4};
        const std::vector<std::uint8_t> outputProfile{1, 2, 3, 5};
        writeTiff(inputPath, 20, 10, inputProfile, ORIENTATION_TOPLEFT);
        writeTiff(outputPath, 20, 10, outputProfile, ORIENTATION_BOTLEFT);

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_EQ(report.at("iccProfile").at("colorimetricEquivalence"), "colorimetric_equivalence_undetermined");
        EXPECT_FALSE(report.at("iccProfile").at("identicalBytes").get<bool>());
        EXPECT_FALSE(report.at("metadataComparisons").at("orientation").at("equal").get<bool>());
        EXPECT_FALSE(report.at("differences").empty());
        EXPECT_FALSE(report.at("warnings").empty());
    }

    TEST(FinalVerification, ReportsIdenticalIccProfileBytes) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.tif";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        const std::vector<std::uint8_t> profile{1, 2, 3, 4};
        writeTiff(inputPath, 20, 10, profile);
        writeTiff(outputPath, 20, 10, profile);

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_TRUE(report.at("iccProfile").at("identicalBytes").get<bool>());
        EXPECT_EQ(report.at("iccProfile").at("colorimetricEquivalence"), "identical_profile_data");
    }

    TEST(FinalVerification, HandlesJpegMetadata) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.jpg";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        ASSERT_TRUE(cv::imwrite(inputPath.string(), cv::Mat::zeros(12, 24, CV_8UC3)));
        writeTiff(outputPath, 24, 16);

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_EQ(report.at("inputImage").at("format"), "JPEG");
        EXPECT_EQ(report.at("outputImage").at("format"), "TIFF");
        EXPECT_EQ(report.at("dimensions").at("input").at("width"), 24);
        EXPECT_EQ(report.at("dimensions").at("input").at("height"), 12);
        EXPECT_EQ(report.at("dimensions").at("difference").at("height"), 4);
    }

    TEST(FinalVerification, ReadsJpegIccProfileSegments) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.jpg";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        const std::vector<std::uint8_t> profile{10, 20, 30, 40};
        ASSERT_TRUE(cv::imwrite(inputPath.string(), cv::Mat::zeros(12, 24, CV_8UC3)));
        addJpegIccProfile(inputPath, profile);
        writeTiff(outputPath, 24, 12, profile);

        const auto report = VerificationNs::compareImages(inputPath, outputPath);
        EXPECT_TRUE(report.at("iccProfile").at("inputPresent").get<bool>());
        EXPECT_TRUE(report.at("iccProfile").at("identicalBytes").get<bool>());
    }

    TEST(FinalVerificationStep, PublishesReportInDataStore) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.tif";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        writeTiff(inputPath, 30, 20);
        writeTiff(outputPath, 30, 20);

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

    TEST(FinalVerificationStep, SavesAndRestoresReportCheckpoint) {
        TemporaryDirectory directory;
        const fs::path inputPath = directory.path / "reference.tif";
        const fs::path outputPath = directory.path / "reconstructed.tif";
        const fs::path checkpointPath = directory.path / "checkpoints";
        writeTiff(inputPath, 30, 20);
        writeTiff(outputPath, 32, 20);

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
