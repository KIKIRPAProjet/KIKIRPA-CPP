// include/Backend/core/checkpoint/checkpoint.h
#pragma once

#include "core/step/data_store.h"
#include "core/step/step.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>

namespace CheckpointNs {

    // Sauvegarde sur disque des sorties de chaque step, pour reprendre un pipeline après un arrêt, une pause ou un crash.
    //   <racine>/00_import/manifest.json   écrit EN DERNIER : sa présence prouve que le checkpoint est complet
    //   <racine>/00_import/tiles.json      une sortie sérialisée par fichier <clé>.json
    //   <racine>/01_phase-correlation/...
    // Un dossier par step (et non par clé) : réécrire une clé ne détruit pas la version des steps précédents.
    // Chaque type de donnée a sa (dé)sérialisation (voir registerType) ; un step dont une sortie n'en a pas
    // n'a pas de checkpoint valide et sera refait à la reprise.
    class CheckpointClass {

        public:
            // Écrit (Saver) ou lit (Loader) la donnée `key` du dataStore dans le dossier `directory`.
            using Saver  = std::function<void(const StepNs::DataStoreClass&, const std::string& key, const std::filesystem::path& directory)>;
            using Loader = std::function<void(StepNs::DataStoreClass&, const std::string& key, const std::filesystem::path& directory)>;

            // Déclare la (dé)sérialisation d'un type de donnée (remplace une déclaration existante).
            // Non thread-safe : à appeler avant de lancer un pipeline.
            static void registerType(const std::string& dataType, Saver saver, Loader loader);
            static bool hasType(const std::string& dataType);

            // Racine vide : checkpoints désactivés (save ne fait rien, valid renvoie false).
            explicit CheckpointClass(std::filesystem::path rootDirectory) : rootDirectory(std::move(rootDirectory)) {}
            bool enabled() const { return !rootDirectory.empty(); }

            // Écrit les sorties du step présentes dans le dataStore, puis le manifest. Renvoie false si une sortie
            // n'a pas pu être écrite (cause dans failureReason) : le checkpoint est alors marqué incomplet.
            bool save(size_t stepIndex, const StepNs::StepClass& step, const StepNs::DataStoreClass& dataStore, std::string& failureReason) const;

            // Vrai si le manifest existe, est complet et correspond au step : même nom, mêmes paramètres et même
            // empreinte des entrées externes (externalInputsFingerprint) qu'à la sauvegarde.
            bool valid(size_t stepIndex, const StepNs::StepClass& step) const;

            // Recharge dans le dataStore la sortie `key` de type `dataType` sauvegardée par le step `stepIndex`.
            // Lève std::runtime_error si le checkpoint est illisible.
            void load(size_t stepIndex, const StepNs::StepClass& step, const std::string& key, const std::string& dataType, StepNs::DataStoreClass& dataStore) const;

            // Supprime les checkpoints des steps d'index >= firstStepIndex (devenus périmés).
            void removeFrom(size_t firstStepIndex) const;

        private:
            std::filesystem::path rootDirectory;
            // Dossier d'un step : <racine>/<index sur 2 chiffres>_<nom du step>.
            std::filesystem::path stepDir(size_t stepIndex, const std::string& stepName) const;

    };

}
