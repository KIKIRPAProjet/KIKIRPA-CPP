// include/backend/core/step/step.h
#pragma once

#include "core/step/data_store.h"

#include <nlohmann/json.hpp>

#include <string>

namespace StepNs {

    using Json = nlohmann::json;

    // Étape d'un pipeline (classe abstraite). Un step lit ses entrées dans le DataStoreClass, s'exécute,
    // puis y écrit ses sorties. Son constructeur par défaut fixe sa configuration par défaut (voir ConfigNs::StepConfig).
    class StepClass {

        public:
            explicit StepClass(const Json& initialConfig = {}) : configJson(initialConfig) {}
            virtual ~StepClass() = default;

            // Exécute le step. Renvoie false en cas d'échec (détail dans getResult()).
            virtual bool execute() = 0;

            // Applique un JSON { name, parameters, ... } à la configuration par défaut : seules les valeurs des
            // paramètres sont reprises (le reste est fixé par le step) ; un paramètre absent garde sa valeur.
            // Lève std::invalid_argument si le JSON est mal formé, si le nom ne correspond pas, si un paramètre
            // est inconnu ou si sa valeur n'a pas le type déclaré.
            virtual void importConfig(const Json& newConfig);

            // Compte rendu du dernier execute() (erreurs, compteurs...), affiché dans les messages du pipeline.
            virtual Json getResult() const { return result; }

            // Empreinte de ce que le step lit hors du pipeline (ex. fichiers sources : nom, taille, date). Enregistrée
            // dans le checkpoint : si elle change, le checkpoint est périmé. Par défaut nulle : seuls les paramètres comptent.
            virtual Json externalInputsFingerprint() const { return Json(); }

            const Json& getConfigJson() const { return configJson; }
            // Posé par le pipeline avant execute().
            void setDataStore(DataStoreClass& store) { dataStore = &store; }

        protected:
            Json configJson;   // { name, parameters, inputs, outputs }, voir ConfigNs::StepConfig
            Json result;
            DataStoreClass* dataStore = nullptr;   // non propriétaire : appartient au pipeline

            // Valeur du paramètre `name`. Lève std::out_of_range s'il n'existe pas.
            Json getParameter(const std::string& name) const;

    };

}
