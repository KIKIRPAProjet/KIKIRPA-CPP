// tests/steps/test_mosaic_crop_step.cpp
#include "steps/mosaic_crop_step.h"

#include "core/checkpoint/checkpoint_types.h"
#include "core/data/mosaic_files.h"
#include "core/step/data_store.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace MosaicCropStepTestsNs {

    namespace {

        using Json = nlohmann::json;
        namespace fs = std::filesystem;

        class MosaicCropStepTestClass : public testing::Test {

        protected:
            void SetUp() override {
                static std::atomic<unsigned int> sequence{ 0 };
                const auto stamp =
                    std::chrono::steady_clock::now().time_since_epoch().count();

                directory = fs::temp_directory_path() / (
                    "crop_step_" + std::to_string(stamp) + "_" +
                    std::to_string(sequence.fetch_add(1)));

                ASSERT_TRUE(fs::create_directory(directory));
                ASSERT_TRUE(fs::create_directory(directory / "source"));

                cv::Mat color(4, 4, CV_16UC3);
                for (int row = 0; row < color.rows; ++row) {
                    for (int column = 0; column < color.cols; ++column) {
                        const auto value = static_cast<unsigned short>(
                            row * color.cols + column);
                        color.at<cv::Vec3w>(row, column) =
                            cv::Vec3w(value, value, value);
                    }
                }

                const cv::Mat provenance(4, 4, CV_16UC1, cv::Scalar(7));
                const cv::Mat mask(4, 4, CV_8UC1, cv::Scalar(255));

                ASSERT_TRUE(cv::imwrite(
                    (directory / "source/rgb_x0_y0.png").string(), color));
                ASSERT_TRUE(cv::imwrite(
                    (directory / "source/provenance_x0_y0.png").string(),
                    provenance));
                ASSERT_TRUE(cv::imwrite(
                    (directory / "source/mask_x0_y0.png").string(), mask));

                source = { (directory / "source").string(), 1 };
                store.replace("mosaic", source);
            }

            void TearDown() override {
                std::error_code error;
                fs::remove_all(directory, error);
                EXPECT_FALSE(error) << error.message();
            }

            StepsNs::MosaicCropInput makeInput() const {
                return { 1, 2, 2, 1, (directory / "cropped").string() };
            }

            void expectFailure(StepsNs::MosaicCropStepClass& step) {
                step.setDataStore(store);

                EXPECT_FALSE(step.execute());
                EXPECT_EQ(
                    step.getResult().at("error").get<std::string>()
                    .find("mosaic-crop: "),
                    0U);
                EXPECT_TRUE(step.getOutput().value.mosaicPath.empty());
                EXPECT_EQ(step.getOutput().value.totalBlocks, 0);
                EXPECT_EQ(
                    store.get<DataNs::MosaicFiles>("mosaic").mosaicPath,
                    source.mosaicPath);
            }

            fs::path directory;
            DataNs::MosaicFiles source;
            StepNs::DataStoreClass store;

        };

        TEST_F(MosaicCropStepTestClass, PublishesCroppedMosaicUnderSameKey) {
            StepsNs::MosaicCropStepClass step(makeInput());
            step.setDataStore(store);

            ASSERT_TRUE(step.execute()) << step.getResult().dump();

            const auto& mosaic = store.get<DataNs::MosaicFiles>("mosaic");
            EXPECT_EQ(mosaic.mosaicPath, (directory / "cropped").string());
            EXPECT_EQ(mosaic.totalBlocks, 1);

            const cv::Mat color = cv::imread(
                (directory / "cropped/rgb_x0_y0.png").string(),
                cv::IMREAD_UNCHANGED);

            ASSERT_EQ(color.size(), cv::Size(2, 1));
            ASSERT_EQ(color.type(), CV_16UC3);
            EXPECT_EQ(color.at<cv::Vec3w>(0, 0), cv::Vec3w(9, 9, 9));
            EXPECT_TRUE(
                step.getResult().at("error").get<std::string>().empty());
            EXPECT_TRUE(
                fs::is_regular_file(directory / "source/rgb_x0_y0.png"));
        }

        TEST_F(MosaicCropStepTestClass, RejectsMissingDataStore) {
            StepsNs::MosaicCropStepClass step(makeInput());

            EXPECT_FALSE(step.execute());
            EXPECT_FALSE(
                step.getResult().at("error").get<std::string>().empty());
        }

        TEST_F(MosaicCropStepTestClass, RejectsMissingMosaic) {
            StepNs::DataStoreClass emptyStore;
            StepsNs::MosaicCropStepClass step(makeInput());
            step.setDataStore(emptyStore);

            EXPECT_FALSE(step.execute());
            EXPECT_FALSE(emptyStore.has("mosaic"));
        }

        TEST_F(MosaicCropStepTestClass, RejectsWrongStoredTypeWithoutThrowing) {
            store.replace("mosaic", 42);
            StepsNs::MosaicCropStepClass step(makeInput());
            step.setDataStore(store);

            EXPECT_FALSE(step.execute());
            EXPECT_EQ(store.get<int>("mosaic"), 42);
        }

        TEST_F(MosaicCropStepTestClass, PreservesInputWhenRectangleIsInvalid) {
            auto input = makeInput();
            input.width = 0;
            StepsNs::MosaicCropStepClass step(input);

            expectFailure(step);
            EXPECT_FALSE(fs::exists(directory / "cropped"));
        }

        TEST_F(MosaicCropStepTestClass, PreservesInputWhenOutputAlreadyExists) {
            ASSERT_TRUE(fs::create_directory(directory / "cropped"));
            StepsNs::MosaicCropStepClass step(makeInput());

            expectFailure(step);
            EXPECT_TRUE(fs::is_directory(directory / "cropped"));
        }

        TEST_F(MosaicCropStepTestClass, RejectsWideJsonIntegerWithoutTruncation) {
            StepsNs::MosaicCropStepClass step(makeInput());
            const auto oversized = static_cast<unsigned long long>(
                std::numeric_limits<int>::max()) + 1;

            step.importConfig({
                {"name", "mosaic-crop"},
                {"parameters", {
                    {{"valueType", "int"}, {"name", "x"}, {"value", oversized}}
                }}
                });

            expectFailure(step);
        }

        TEST_F(MosaicCropStepTestClass, UsesParametersImportedFromJson) {
            StepsNs::MosaicCropStepClass step;
            step.importConfig({
                {"name", "mosaic-crop"},
                {"parameters", {
                    {{"valueType", "int"}, {"name", "width"}, {"value", 4}},
                    {{"valueType", "int"}, {"name", "height"}, {"value", 4}},
                    {
                        {"valueType", "string"},
                        {"name", "outputDirectory"},
                        {"value", (directory / "cropped").string()}
                    }
                }}
                });
            step.setDataStore(store);

            ASSERT_TRUE(step.execute()) << step.getResult().dump();

            const cv::Mat color = cv::imread(
                (directory / "cropped/rgb_x0_y0.png").string(),
                cv::IMREAD_UNCHANGED);
            EXPECT_EQ(color.size(), cv::Size(4, 4));
        }

        TEST_F(MosaicCropStepTestClass, ClearsPreviousOutputAndErrorOnReexecution) {
            StepsNs::MosaicCropStepClass step(makeInput());
            step.setDataStore(store);
            ASSERT_TRUE(step.execute()) << step.getResult().dump();

            const auto cropped = store.get<DataNs::MosaicFiles>("mosaic");

            EXPECT_FALSE(step.execute());
            EXPECT_TRUE(step.getOutput().value.mosaicPath.empty());
            EXPECT_EQ(
                store.get<DataNs::MosaicFiles>("mosaic").mosaicPath,
                cropped.mosaicPath);

            ASSERT_TRUE(fs::remove_all(directory / "cropped") > 0);
            store.replace("mosaic", source);

            ASSERT_TRUE(step.execute()) << step.getResult().dump();
            EXPECT_TRUE(
                step.getResult().at("error").get<std::string>().empty());
        }

        TEST_F(MosaicCropStepTestClass, CheckpointRoundTripPreservesDescriptor) {
            ASSERT_TRUE(fs::create_directory(directory / "checkpoint"));
            const auto functions =
                CheckpointNs::pipelineTypes().at("MosaicFiles");

            functions.first(store, "mosaic", directory / "checkpoint");
            StepNs::DataStoreClass restored;
            functions.second(restored, "mosaic", directory / "checkpoint");

            EXPECT_EQ(
                restored.get<DataNs::MosaicFiles>("mosaic").mosaicPath,
                source.mosaicPath);
            EXPECT_EQ(
                restored.get<DataNs::MosaicFiles>("mosaic").totalBlocks, 1);
        }

        TEST_F(MosaicCropStepTestClass, CheckpointRejectsMissingMosaicDirectory) {
            ASSERT_TRUE(fs::create_directory(directory / "checkpoint"));
            const auto functions =
                CheckpointNs::pipelineTypes().at("MosaicFiles");

            functions.first(store, "mosaic", directory / "checkpoint");
            fs::remove_all(directory / "source");
            StepNs::DataStoreClass restored;

            EXPECT_THROW(
                functions.second(restored, "mosaic", directory / "checkpoint"),
                std::runtime_error);
            EXPECT_FALSE(restored.has("mosaic"));
        }

    }

}