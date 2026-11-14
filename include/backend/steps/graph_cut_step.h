// include/backend/steps/graph_cut_step.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <string>
#include <vector>

namespace StepsNs {

    // Entrées pour un appel direct, hors pipeline. Dans un pipeline, tuiles et positions viennent du DataStoreClass
    // et seule `scale` est lue (dans la configuration du step).
    struct GraphCutInput {
        std::vector<std::string> tilePaths;   // chemins des images des tuiles
        std::vector<DataNs::TilePositionData> positions;   // une entrée par tuile, dans le même ordre que tilePaths
        double scale = 1;   // facteur de réduction appliqué avant la coupe de graphe, dans ]0, 1] (1 = pleine résolution)
    };

    // Sortie du step.
    struct GraphCutOutput {
        std::vector<DataNs::TileTerritory> territories;   // territoire de chaque tuile, dans le repère de la mosaïque
    };

    // Calcule la ligne de raccord entre les tuiles par coupe de graphe, puis en déduit le territoire de chaque tuile
    // dans la mosaïque : les polygones des pixels qu'elle fournit à l'assemblage final.
    //
    // Nom : "graphcut".   Paramètre : scale (float).
    // Entrées : "Tiles" (clé "tiles") et "Positions" (clé "positions", une position par tuile, dans l'ordre des tuiles).
    // Sortie : "Territories" (clé "territories").
    //
    // Étapes de execute() : lecture des entrées, contrôles, prétraitement des images (tailles, coins, masques réduits),
    // coupe de graphe, extraction des polygones. Au premier problème, execute() renvoie false avec la cause dans
    // getResult()["error"].
    class GraphCutStepClass : public StepNs::StepClass {

        public:
            explicit GraphCutStepClass(const GraphCutInput& initialInput = {});
            bool execute() override;
            const GraphCutOutput& getOutput() const {
                return output;
            }

        private:
            // Résout les entrées de execute() : celles de `input` par défaut, remplacées par les données du DataStoreClass
            // quand il les fournit ; `scale` est toujours relu dans la configuration, pour tenir compte d'un JSON chargé
            // après la construction. Renvoie false si une lecture échoue (cause dans result["error"]).
            bool readInputs(GraphCutInput& resolvedInput);

            GraphCutInput input;   // sert de repli quand le DataStoreClass est absent ou n'a pas la donnée
            GraphCutOutput output;

    };

}
