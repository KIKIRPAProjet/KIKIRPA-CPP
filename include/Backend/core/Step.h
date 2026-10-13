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
    virtual void importConfig(const Json& cfg) { config = cfg; }
    virtual Json exportData() const { return outputData; }
    const Json& getConfig() const { return config; }
    void setDataStore(DataStore& d) { data = &d; }
};

} // namespace stitching
