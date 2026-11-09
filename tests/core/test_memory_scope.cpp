// tests/core/test_memory_scope.cpp
// Mesure de la mémoire résidente d'un bloc (MemoryNs::MemoryScopeClass, voir core/memory/memory_scope.h).
//
// GoogleTest, ctest -L unit : champs écrits dans le JSON, pic visible pour une allocation temporaire, plusieurs
// mesures dans le même JSON.

#include "core/memory/memory_scope.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace {

    using Json = nlohmann::json;

    constexpr std::size_t ALLOCATED_MIB = 64;
    constexpr std::size_t BYTES_PER_MIB = std::size_t{1024} * 1024;
    constexpr double MINIMUM_VISIBLE_MIB = 48.0;              // marge sous les 64 Mio alloués
    constexpr std::chrono::milliseconds HOLD_DURATION{100};   // largement plus que la période d'échantillonnage
    constexpr char FILL_VALUE = 1;                            // écrire chaque page pour qu'elle soit vraiment résidente

#if defined(_WIN32) || defined(__linux__)
    constexpr bool IS_MEASURED_PLATFORM = true;
#else
    constexpr bool IS_MEASURED_PLATFORM = false;
#endif

    void expectValidMeasurement(const Json& measurement) {
        for (const char* key : {"beforeMib", "peakMib", "afterMib", "processPeakMib"}) {
            ASSERT_TRUE(measurement.contains(key)) << key;
            ASSERT_TRUE(measurement.at(key).is_number()) << key;
            EXPECT_GE(measurement.at(key).get<double>(), 0.0) << key;
        }
        EXPECT_GE(measurement.at("peakMib").get<double>(), measurement.at("beforeMib").get<double>());
        EXPECT_GE(measurement.at("peakMib").get<double>(), measurement.at("afterMib").get<double>());
    }

    TEST(MemoryScope, WritesAllFieldsInMebibytes) {
        Json result = Json::object();
        { const MemoryNs::MemoryScopeClass scope(result, "bloc"); }
        ASSERT_TRUE(result.contains("bloc"));
        expectValidMeasurement(result.at("bloc"));
        if (IS_MEASURED_PLATFORM) EXPECT_GT(result.at("bloc").at("beforeMib").get<double>(), 0.0);
    }

    TEST(MemoryScope, PeakSeesTemporaryAllocation) {
        if (!IS_MEASURED_PLATFORM) GTEST_SKIP() << "mesure non disponible sur cette plateforme";
        Json result = Json::object();
        {
            const MemoryNs::MemoryScopeClass scope(result, "allocation");
            std::vector<char> buffer(ALLOCATED_MIB * BYTES_PER_MIB, FILL_VALUE);
            std::this_thread::sleep_for(HOLD_DURATION);
            EXPECT_EQ(buffer.back(), FILL_VALUE);
        }
        const Json& measurement = result.at("allocation");
        expectValidMeasurement(measurement);
        EXPECT_GE(measurement.at("peakMib").get<double>() - measurement.at("beforeMib").get<double>(), MINIMUM_VISIBLE_MIB);
        EXPECT_GE(measurement.at("processPeakMib").get<double>(), measurement.at("peakMib").get<double>());
    }

    TEST(MemoryScope, KeepsSeveralMeasurementsSideBySide) {
        Json result = Json::object();
        { const MemoryNs::MemoryScopeClass first(result, "premier"); }
        { const MemoryNs::MemoryScopeClass second(result, "second"); }
        EXPECT_TRUE(result.contains("premier"));
        EXPECT_TRUE(result.contains("second"));
    }

}
