// include/backend/algorithms/rejection/ransac/ransac_preprocessor.h
#pragma once

#include "algorithms/rejection/ransac/ransac_types.h"

namespace RansacNs {

    class RansacPreprocessorClass {

        public:
            // Retire les correspondances dont un point n'est pas fini (NaN, infini) en gardant l'indice d'origine.
            // false si les deux listes de points n'ont pas la même taille, ou s'il reste moins de
            // parameters.minimumMatchCount correspondances.
            bool prepare(const RansacInput& input, const RansacParameters& parameters, RansacPreparedInput& output) const;

    };

}
