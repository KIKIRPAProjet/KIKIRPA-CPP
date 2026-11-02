// include/Backend/core/step/step.h
#pragma once

#include "core/step/data_store.h"

#include <nlohmann/json.hpp>

#include <string>

namespace StepNs {

    using Json = nlohmann::json;

    // Classe abstraite (en italique dans l'UML)
    class StepClass {

        public:
            explicit StepClass(const Json& initialConfig = {}) : configJson(initialConfig) {}
            virtual ~StepClass() = default;

            virtual bool execute() = 0;                         // virtuelle pure
            // Applique un JSON { name, options, ... } à la config par défaut du step : seules les valeurs des options
            // sont reprises, une option absente garde sa valeur. Lève std::invalid_argument si le JSON est mal formé,
            // si le nom ne correspond pas, si une option est inconnue ou si sa valeur n'a pas le type déclaré.
            virtual void importConfig(const Json& newConfig);
            virtual Json getResult() const { return result; }
            // Empreinte de ce que le step lit hors du pipeline (ex. fichiers sources : nom, taille, date). Gardée dans
            // le checkpoint : si elle change, le checkpoint est périmé. Par défaut null : seules les options comptent.
            virtual Json externalInputsFingerprint() const { return Json(); }
            const Json& getConfigJson() const { return configJson; }
            void setDataStore(DataStoreClass& store) { dataStore = &store; }

        protected:
            Json configJson;                 // { name, options, pipelineOptions, signature } (voir core/Config.h)
            Json result;
            DataStoreClass* dataStore = nullptr;   // lit ses pipelineOptions ici, y écrit sa signature (posé par PipelineClass)

            // Valeur de l'option "key" dans config["options"] ; lève std::out_of_range si absente
            Json getParameter(const std::string& name) const;

    };

}
