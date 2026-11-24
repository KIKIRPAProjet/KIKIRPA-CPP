// tests/steps/test_mosaic_crop_step.cpp
#include "cropping/crop_test_support.h"

#include "core/data/algorithm_types.h"
#include "core/io/unicode_path.h"
#include "core/step/data_store.h"
#include "steps/mosaic_crop_step.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <filesystem>

#include <cstdint>
#include <limits>

namespace CroppingTestsNs {

    using MosaicCropStepTestClass = CropFixtureClass;

    TEST_F(MosaicCropStepTestClass, PublishesMosaicContractAndTracksOutputFiles) {
        StepNs::DataStoreClass store;
        store.replace("mosaic", source);
        StepsNs::MosaicCropStepClass step({ CROP_LEFT, CROP_TOP, CROP_WIDTH, CROP_HEIGHT,
                                          IONs::pathToUtf8(outputDirectory()) });
        step.setDataStore(store);
        const auto previousFingerprint = step.externalInputsFingerprint();
        ASSERT_TRUE(step.execute()) << step.getResult();
        const auto& cropped = store.get<DataNs::Mosaic>("mosaic");
        EXPECT_EQ(cropped.canvasSize, cropRectangle().size());
        EXPECT_NE(cropped.imagePath, source.imagePath);
        EXPECT_EQ(step.getConfigJson()["inputs"][0]["dataType"], "Mosaic");
        EXPECT_EQ(step.getConfigJson()["outputs"][0]["dataType"], "Mosaic");
        EXPECT_NE(previousFingerprint, step.externalInputsFingerprint());
    }

    TEST_F(MosaicCropStepTestClass, PreservesInputOnFailure) {
        StepNs::DataStoreClass store;
        store.replace("mosaic", source);
        StepsNs::MosaicCropStepClass step({ -1, 0, 1, 1, IONs::pathToUtf8(outputDirectory()) });
        step.setDataStore(store);
        EXPECT_FALSE(step.execute());
        EXPECT_EQ(store.get<DataNs::Mosaic>("mosaic").imagePath, source.imagePath);
        EXPECT_TRUE(step.getOutput().value.imagePath.empty());
    }

    TEST_F(MosaicCropStepTestClass, RejectsIntegerOverflow) {
        StepNs::DataStoreClass store;
        store.replace("mosaic", source);
        StepsNs::MosaicCropStepClass step;
        step.setDataStore(store);
        const auto excessive = static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1;
        step.importConfig({ {"parameters", {{{"valueType", "int"}, {"name", "x"}, {"value", excessive}}}} });
        EXPECT_FALSE(step.execute());
        EXPECT_EQ(store.get<DataNs::Mosaic>("mosaic").imagePath, source.imagePath);
    }

    TEST_F(MosaicCropStepTestClass, RerunsAfterOutputFileDisappears) {
        StepNs::DataStoreClass store;
        store.replace("mosaic", source);

        StepsNs::MosaicCropStepClass step({
            CROP_LEFT, CROP_TOP, CROP_WIDTH, CROP_HEIGHT,
            IONs::pathToUtf8(outputDirectory())
            });
        step.setDataStore(store);

        ASSERT_TRUE(step.execute()) << step.getResult();

        const auto previousFingerprint = step.externalInputsFingerprint();
        const DataNs::Mosaic previous = store.get<DataNs::Mosaic>("mosaic");

        ASSERT_TRUE(fs::remove(previous.maskPath));
        EXPECT_NE(previousFingerprint, step.externalInputsFingerprint());

        // À la reprise, le rognage reçoit la mosaïque assemblée par fusion.
        store.replace("mosaic", source);

        ASSERT_TRUE(step.execute()) << step.getResult();

        const auto& cropped = store.get<DataNs::Mosaic>("mosaic");
        EXPECT_EQ(cropped.canvasSize, cropRectangle().size());
        EXPECT_EQ(cropped.imagePath, previous.imagePath);
        EXPECT_EQ(cropped.provenancePath, previous.provenancePath);
        EXPECT_EQ(cropped.maskPath, previous.maskPath);

        EXPECT_TRUE(fs::is_regular_file(cropped.imagePath));
        EXPECT_TRUE(fs::is_regular_file(cropped.provenancePath));
        EXPECT_TRUE(fs::is_regular_file(cropped.maskPath));
    }

}