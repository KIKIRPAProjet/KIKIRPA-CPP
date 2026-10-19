#pragma once
#include "core/DataStore.h"
#include "core/Step.h"
#include <filesystem>
#include <string>

namespace stitching {

// Sauvegarde sur disque des sorties de chaque step, pour pouvoir reprendre après un arrêt ou un crash.
//
//   <root>/00_import/manifest.json      <- écrit EN DERNIER : sa présence = checkpoint complet
//   <root>/00_import/tiles.json + tiles_0000.tif ...
//   <root>/01_stitching/...
//
// Un dossier par step (et non par clé) : un step qui réécrit une clé existante (intensityCorrection : tiles -> tiles)
// ne détruit pas la version qu'avaient les steps précédents, donc on peut reprendre à n'importe quel step.
// Les types de données sont sérialisés par un registre (Checkpoint.cpp) : un type sans entrée = pas de checkpoint valide.
class Checkpoint {
    std::filesystem::path root;
    std::filesystem::path stepDir(size_t index, const std::string& name) const;
public:
    explicit Checkpoint(std::filesystem::path rootDir) : root(std::move(rootDir)) {}

    // Écrit les sorties du step (celles de sa signature présentes dans le DataStore).
    // Retourne false (+ why) si au moins une sortie n'a pas pu être persistée : le checkpoint est alors invalide.
    bool save(size_t index, const Step& step, const DataStore& data, std::string& why) const;

    // Manifest présent, complet, même step, mêmes options qu'au moment de la sauvegarde
    bool valid(size_t index, const Step& step) const;

    // Recharge la sortie `key` (de type `type`) sauvegardée par le step `index`
    void load(size_t index, const Step& step, const std::string& key, const std::string& type, DataStore& data) const;

    // Supprime les checkpoints des steps d'index >= from (devenus périmés)
    void removeFrom(size_t from) const;
};

} // namespace stitching
