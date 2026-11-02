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
    //
    //   <root>/00_import/manifest.json       écrit EN DERNIER : sa présence = checkpoint complet
    //   <root>/00_import/tiles.json ...      fichiers écrits par la sérialisation de chaque sortie
    //   <root>/01_phase-correlation/...
    //
    // Un dossier par step (et non par clé) : un step qui réécrit une clé existante ne détruit pas la version
    // des steps précédents, on peut donc reprendre à n'importe quel step.
    // Chaque type de donnée (le "type" d'une PipelineOption : "Tiles", "PairMatches"...) a sa (dé)sérialisation,
    // déclarée par registerType. Un step dont une sortie n'a pas de sérialisation n'a pas de checkpoint valide :
    // il sera refait à la reprise.
    class CheckpointClass {

        public:
            using Saver  = std::function<void(const StepNs::DataStoreClass&, const std::string& key, const std::filesystem::path& directory)>;
            using Loader = std::function<void(StepNs::DataStoreClass&, const std::string& key, const std::filesystem::path& directory)>;

            // Déclare la (dé)sérialisation d'un type ; remplace une déclaration existante. Pas thread-safe :
            // à appeler avant de lancer des pipelines.
            static void registerType(const std::string& dataType, Saver saver, Loader loader);
            static bool hasType(const std::string& dataType);

            // root vide : checkpoints désactivés (save ne fait rien, valid renvoie false)
            explicit CheckpointClass(std::filesystem::path rootDirectory) : rootDirectory(std::move(rootDirectory)) {}
            bool enabled() const { return !rootDirectory.empty(); }

            // Écrit les sorties du step présentes dans le DataStoreClass, puis le manifest. Renvoie false (raison dans `why`)
            // si une sortie n'a pas pu être écrite : le checkpoint est alors marqué incomplet.
            bool save(size_t stepIndex, const StepNs::StepClass& step, const StepNs::DataStoreClass& dataStore, std::string& failureReason) const;

            // Manifest présent, complet, même step, mêmes options et même empreinte des entrées (StepClass::inputFingerprint)
            // qu'à la sauvegarde
            bool valid(size_t stepIndex, const StepNs::StepClass& step) const;

            // Recharge la sortie `key` (de type `type`) sauvegardée par le step `index`. Lève std::runtime_error si
            // le checkpoint est illisible.
            void load(size_t stepIndex, const StepNs::StepClass& step, const std::string& key, const std::string& dataType, StepNs::DataStoreClass& dataStore) const;

            // Supprime les checkpoints des steps d'index >= from (devenus périmés)
            void removeFrom(size_t firstStepIndex) const;

        private:
            std::filesystem::path rootDirectory;
            std::filesystem::path stepDir(size_t stepIndex, const std::string& stepName) const;

    };

}
