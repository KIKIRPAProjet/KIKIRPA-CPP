// include/backend/steps/fusion_step.h
#pragma once

#include "core/data/tile.h"
#include "core/step/step.h"

#include <string>

namespace StepsNs {

/**
 * @brief Paramètres d'entrée du step de fusion (assemblage).
 *
 * Ce step découpe le canevas global de la mosaïque en blocs réguliers
 * (par défaut 512x512) et affecte à chaque pixel la valeur de la tuile
 * désignée par la ligne de couture (territoires calculés par GraphCut).
 */
struct FusionInput {
    /// Taille en pixels du côté de chaque bloc composite carré (par défaut 512).
    int blockSize = DataNs::BLOCK_SIZE;

    /// Dossier optionnel pour écrire sur disque les blocs intermédiaires (PNG).
    /// Si vide, les blocs sont uniquement stockés en mémoire dans le DataStore.
    std::string outputPath = "";

    /// Active ou désactive le fondu/lissage des raccords (réservé pour évolution future).
    bool enableBlending = false;
};

/**
 * @brief Données de sortie du step de fusion.
 */
struct FusionOutput {
    /// Chemin du dossier où les blocs ont été écrits (ou vide si en mémoire uniquement).
    std::string mosaicPath = "";

    /// Nombre total de blocs composites générés pour couvrir le canevas.
    int totalBlocks = 0;
};

/**
 * @brief Step d'assemblage / fusion des tuiles en blocs composites (CompositeBlocks).
 *
 * Entrées requises dans le DataStore :
 *  - "tiles" (TileList) : Liste des tuiles importées avec leurs chemins d'accès et métadonnées.
 *  - "positions" (std::vector<TilePositionData>) : Positions absolues (X, Y) de chaque tuile.
 *  - "territories" (std::vector<TileTerritory>) : Polygones des zones de validité issus du GraphCut.
 *
 * Entrée optionnelle dans le DataStore :
 *  - "intensityCorrection" (IntensityCorrection) : Gains photométriques pour harmoniser l'exposition.
 *
 * Sortie produite dans le DataStore :
 *  - "compositeBlocks" (CompositeBlockList) : Liste de blocs composites (image RGB 16 bits, masque, provenance).
 */
class FusionStepClass : public StepNs::StepClass {
public:
    explicit FusionStepClass(const FusionInput& initialInput = {});
    ~FusionStepClass() override = default;

    /**
     * @brief Exécute l'assemblage complet :
     *  1. Récupère les tuiles, positions, territoires et corrections du DataStore.
     *  2. Normalise les coordonnées globales en ramenant le coin supérieur gauche à (0,0).
     *  3. Calcule la boîte englobante totale de la mosaïque.
     *  4. Parcourt la grille par blocs de taille `blockSize`.
     *  5. Pour chaque bloc, projette les tuiles intersectées avec leur masque de territoire et gain.
     *  6. Enregistre les blocs générés dans le DataStore sous la clé "compositeBlocks".
     * @return true si l'exécution a réussi, false en cas d'erreur.
     */
    bool execute() override;

    /// Accès aux résultats d'exécution.
    const FusionOutput& getOutput() const { return output; }

private:
    FusionInput input;
    FusionOutput output;
};

} // namespace StepsNs
