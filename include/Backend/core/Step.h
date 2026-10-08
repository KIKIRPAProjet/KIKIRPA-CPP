#pragma once
#include "core/Types.h"

namespace stitching {

// Classe abstraite (en italique dans l'UML)
class Step {
protected:
    Json config;
    Json outputData;
public:
    explicit Step(const Json& cfg = {}) : config(cfg) {}
    virtual ~Step() = default;

    virtual bool execute() = 0;                         // virtuelle pure
    virtual void importConfig(const Json& cfg) { config = cfg; }
    virtual Json exportData() const { return outputData; }
};

} // namespace stitching
