#pragma once
#include "core/DataStore.h"
#include "core/Types.h"
#include <string>

namespace stitching {

// Classe abstraite (en italique dans l'UML)
class Step {
protected:
    Json config;                 // { name, options, pipelineOptions, signature } (voir core/Config.h)
    Json outputData;
    DataStore* data = nullptr;   // lit ses pipelineOptions ici, y écrit sa signature (posé par Pipeline)

    // Valeur de l'option "key" dans config["options"] ; lève std::out_of_range si absente
    Json option(const std::string& key) const;
public:
    explicit Step(const Json& cfg = {}) : config(cfg) {}
    virtual ~Step() = default;

    virtual bool execute() = 0;                         // virtuelle pure
    // Applique un JSON { name, options, ... } à la config par défaut du step : seules les valeurs des options
    // sont reprises, une option absente garde sa valeur. Lève std::invalid_argument si le JSON est mal formé,
    // si le nom ne correspond pas, si une option est inconnue ou si sa valeur n'a pas le type déclaré.
    virtual void importConfig(const Json& cfg);
    virtual Json exportData() const { return outputData; }
    // Empreinte de ce que le step lit hors du pipeline (ex. fichiers sources : nom, taille, date). Gardée dans
    // le checkpoint : si elle change, le checkpoint est périmé. Par défaut null : seules les options comptent.
    virtual Json inputFingerprint() const { return Json(); }
    const Json& getConfig() const { return config; }
    void setDataStore(DataStore& d) { data = &d; }
};

} // namespace stitching
