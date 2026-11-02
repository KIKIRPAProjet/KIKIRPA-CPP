#pragma once
#include "core/DataStore.h"
#include "core/Step.h"
#include <filesystem>
#include <functional>
#include <string>

namespace stitching {

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
class Checkpoint {
public:
    using Saver  = std::function<void(const DataStore&, const std::string& key, const std::filesystem::path& dir)>;
    using Loader = std::function<void(DataStore&, const std::string& key, const std::filesystem::path& dir)>;

    // Déclare la (dé)sérialisation d'un type ; remplace une déclaration existante. Pas thread-safe :
    // à appeler avant de lancer des pipelines.
    static void registerType(const std::string& type, Saver save, Loader load);
    static bool hasType(const std::string& type);

    // root vide : checkpoints désactivés (save ne fait rien, valid renvoie false)
    explicit Checkpoint(std::filesystem::path root) : root(std::move(root)) {}
    bool enabled() const { return !root.empty(); }

    // Écrit les sorties du step présentes dans le DataStore, puis le manifest. Renvoie false (raison dans `why`)
    // si une sortie n'a pas pu être écrite : le checkpoint est alors marqué incomplet.
    bool save(size_t index, const Step& step, const DataStore& data, std::string& why) const;

    // Manifest présent, complet, même step, mêmes options qu'à la sauvegarde
    bool valid(size_t index, const Step& step) const;

    // Recharge la sortie `key` (de type `type`) sauvegardée par le step `index`. Lève std::runtime_error si
    // le checkpoint est illisible.
    void load(size_t index, const Step& step, const std::string& key, const std::string& type, DataStore& data) const;

    // Supprime les checkpoints des steps d'index >= from (devenus périmés)
    void removeFrom(size_t from) const;

private:
    std::filesystem::path root;
    std::filesystem::path stepDir(size_t index, const std::string& name) const;
};

} // namespace stitching
