// include/backend/algorithms/rejection/vote_2d/vote_rejector.h
#pragma once

// Règle de style 'misc-include-cleaner' neutralisée ici :
// Les types métier (Json, Matches, Alignment) sont fournis par l'en-tête parent OutlierRejector.h.
// Cela évite des inclusions directes superflues sans compromettre aucune sécurité logique ou mémoire.
// NOLINTBEGIN(misc-include-cleaner)
#include "algorithms/rejection/OutlierRejector.h"

// Règle de nommage 'readability-identifier-naming' neutralisée ici :
// Conservation du namespace historique 'stitching::rejection' pour garantir la compatibilité
// avec l'ensemble du projet sans casser le code existant.
namespace stitching::rejection { // NOLINT(readability-identifier-naming)

// Rejet des aberrants par Vote de Hough 2D et médiane itérative (pure translation 2D).
class VoteRejectorClass : public OutlierRejector {
public:
    VoteRejectorClass() = default;
    explicit VoteRejectorClass(const Json& parameters);
    bool estimate(const Matches& matches, Alignment& out) override;

private:
    Json parameters = Json::object();
};

using VoteRejector = VoteRejectorClass;

} // namespace stitching::rejection
// NOLINTEND(misc-include-cleaner)


