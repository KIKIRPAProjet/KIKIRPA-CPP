// src/backend/core/checkpoint/intensity_correction_checkpoint.cpp
#include "core/checkpoint/intensity_correction_checkpoint.h"

#include "core/checkpoint/checkpoint.h"
#include "core/data/intensity_correction.h"
#include "core/step/data_store.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace CheckpointNs {

    namespace {

        using Json = nlohmann::json;

        void validateCorrection(const DataNs::IntensityCorrection& correction) {
            if (!std::isfinite(correction.fieldStrength)) throw std::invalid_argument("intensity checkpoint: non-finite field strength");
            for (auto gain : correction.tileGains)
                if (!std::isfinite(gain) || gain <= 0.0) throw std::invalid_argument("intensity checkpoint: invalid tile gain");
            for (auto index : correction.isolatedTileIndices)
                if (index < 0 || static_cast<std::size_t>(index) >= correction.tileGains.size())
                    throw std::invalid_argument("intensity checkpoint: invalid isolated tile index");
        }

        void saveCorrection(const StepNs::DataStoreClass& store, const std::string& key, const std::filesystem::path& directory) {
            auto& correction = store.get<DataNs::IntensityCorrection>(key);
            validateCorrection(correction);
            Json data = {{"tileGains", correction.tileGains}, {"fieldStrength", correction.fieldStrength},
                         {"isolatedTileIndices", correction.isolatedTileIndices}};
            std::ofstream output(directory / (key + ".json"), std::ios::binary | std::ios::trunc);
            output << data.dump(2);
            output.close();
            if (!output) throw std::runtime_error("intensity checkpoint: cannot write " + key);
        }

        void loadCorrection(StepNs::DataStoreClass& store, const std::string& key, const std::filesystem::path& directory) {
            std::ifstream input(directory / (key + ".json"), std::ios::binary);
            if (!input) throw std::runtime_error("intensity checkpoint: cannot read " + key);
            auto data = Json::parse(input);
            DataNs::IntensityCorrection correction;
            correction.tileGains = data.at("tileGains").get<std::vector<double>>();
            correction.fieldStrength = data.at("fieldStrength").get<double>();
            correction.isolatedTileIndices = data.at("isolatedTileIndices").get<std::vector<int>>();
            validateCorrection(correction);
            store.replace(key, std::move(correction));
        }

    }

    void registerIntensityCorrectionCheckpoint() {
        if (!CheckpointClass::hasType("IntensityCorrection"))
            CheckpointClass::registerType("IntensityCorrection", saveCorrection, loadCorrection);
    }

}
