// tests/core/test_saved_state.cpp
// État sauvegardé d'un pipeline (core/pipeline/saved_state.h, tâche 16) : nombre de steps faits dont le checkpoint
// est valide, donc l'étape où un calcul reprendra. Deux steps de test : "first" écrit un entier, "second" le relit.
//
// GoogleTest, ctest -L unit : sans dossier de checkpoints, échec puis réussite, paramètre modifié.

#include "core/checkpoint/checkpoint.h"
#include "core/config/pipeline_config.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/run_state.h"
#include "core/pipeline/saved_state.h"
#include "core/step/data_store.h"
#include "core/step/step.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

    using Json = nlohmann::json;

    // Nom et clés d'un step de test.
    struct CounterSlots {
        std::string name;
        std::string inputKey;    // clé lue, vide pour le premier step
        std::string outputKey;   // clé écrite
    };

    // Step de test : écrit sa valeur (+ la valeur d'entrée s'il en a une) sous sa clé de sortie.
    // Paramètre "fails" : le step échoue (renvoie false).
    class CounterStepClass : public StepNs::StepClass {
        public:
            explicit CounterStepClass(const CounterSlots& slots) : inputKey(slots.inputKey), outputKey(slots.outputKey) {
                ConfigNs::StepConfig config;
                config.name = slots.name;
                config.parameters = {{"int", "value", 1}, {"bool", "fails", false}};
                if (!inputKey.empty()) config.inputs = {{"Counter", inputKey}};
                config.outputs = {{"Counter", outputKey}};
                configJson = config;
            }

            bool execute() override {
                result = Json::object();
                if (getParameter("fails").get<bool>()) return false;
                int value = getParameter("value").get<int>();
                if (!inputKey.empty()) value += dataStore->get<int>(inputKey);
                dataStore->replace(outputKey, value);
                return true;
            }

        private:
            std::string inputKey;
            std::string outputKey;
    };

    // Sérialisation du type de test "Counter" dans les checkpoints : un entier dans <clé>.txt.
    void registerCounterType() {
        CheckpointNs::CheckpointClass::registerType(
            "Counter",
            [](const StepNs::DataStoreClass& store, const std::string& key, const std::filesystem::path& directory) {
                std::ofstream(directory / (key + ".txt")) << store.get<int>(key);
            },
            [](StepNs::DataStoreClass& store, const std::string& key, const std::filesystem::path& directory) {
                std::ifstream file(directory / (key + ".txt"));
                int value = 0;
                if (!(file >> value)) throw std::runtime_error("unreadable counter");
                store.replace(key, value);
            });
    }

    // Pipeline "first" -> "second", avec ses checkpoints dans `checkpointDirectory` (vide : pas de checkpoint).
    PipelineNs::PipelineClass makePipeline(const std::filesystem::path& checkpointDirectory) {
        registerCounterType();
        PipelineNs::PipelineClass pipeline;
        pipeline.addStep(std::make_unique<CounterStepClass>(CounterSlots{"first", "", "x"}));
        pipeline.addStep(std::make_unique<CounterStepClass>(CounterSlots{"second", "x", "y"}));
        PipelineNs::RunState runState;
        runState.checkpointDirectory = checkpointDirectory.string();
        pipeline.setRunState(runState);
        return pipeline;
    }

    // Change un paramètre d'un step, comme le ferait un script modifié puis relu.
    void setParameter(const PipelineNs::PipelineClass& pipeline, std::size_t stepIndex, const std::string& name, const Json& value) {
        const auto& step = pipeline.getSteps()[stepIndex];
        step->importConfig({{"name", step->getConfigJson().at("name")},
                            {"parameters", {{{"valueType", value.is_boolean() ? "bool" : "int"}, {"name", name}, {"value", value}}}}});
    }

    bool runQuietly(PipelineNs::PipelineClass& pipeline) {
        return pipeline.run([](const std::string&) {});
    }

    // Dossier de checkpoints propre à un test, vidé avant et après.
    class TemporaryDirectoryClass {
        public:
            explicit TemporaryDirectoryClass(const std::string& testName)
                : path(std::filesystem::temp_directory_path() / ("kikirpa_test_saved_state_" + testName)) {
                std::filesystem::remove_all(path);
            }
            ~TemporaryDirectoryClass() {
                std::error_code errorCode;
                std::filesystem::remove_all(path, errorCode);
            }
            TemporaryDirectoryClass(const TemporaryDirectoryClass&) = delete;
            TemporaryDirectoryClass& operator=(const TemporaryDirectoryClass&) = delete;
            TemporaryDirectoryClass(TemporaryDirectoryClass&&) = delete;
            TemporaryDirectoryClass& operator=(TemporaryDirectoryClass&&) = delete;

            std::filesystem::path path;
    };

}

// Sans dossier de checkpoints, rien n'est sauvegardé : un run repart toujours du premier step.
TEST(SavedStateTest, NoCheckpointDirectory) {
    PipelineNs::PipelineClass pipeline = makePipeline("");
    ASSERT_TRUE(runQuietly(pipeline));
    EXPECT_EQ(PipelineNs::countSavedSteps(pipeline), 0U);
}

// Avant le premier run : rien ; échec au step 2 : step 1 sauvegardé, reprise au step 2 ; réussite : tout est sauvegardé.
TEST(SavedStateTest, FailureThenSuccess) {
    const TemporaryDirectoryClass directory("FailureThenSuccess");
    PipelineNs::PipelineClass pipeline = makePipeline(directory.path);
    EXPECT_EQ(PipelineNs::countSavedSteps(pipeline), 0U);

    setParameter(pipeline, 1, "fails", true);
    EXPECT_FALSE(runQuietly(pipeline));
    EXPECT_EQ(PipelineNs::countSavedSteps(pipeline), 1U);

    setParameter(pipeline, 1, "fails", false);
    ASSERT_TRUE(runQuietly(pipeline));
    EXPECT_EQ(PipelineNs::countSavedSteps(pipeline), 2U);
}

// Un paramètre modifié rend le checkpoint de ce step périmé : lui et les suivants sont à refaire.
TEST(SavedStateTest, ChangedParameter) {
    const TemporaryDirectoryClass directory("ChangedParameter");
    PipelineNs::PipelineClass pipeline = makePipeline(directory.path);
    ASSERT_TRUE(runQuietly(pipeline));
    ASSERT_EQ(PipelineNs::countSavedSteps(pipeline), 2U);

    setParameter(pipeline, 1, "value", 2);
    EXPECT_EQ(PipelineNs::countSavedSteps(pipeline), 1U);

    setParameter(pipeline, 0, "value", 3);
    EXPECT_EQ(PipelineNs::countSavedSteps(pipeline), 0U);
}
