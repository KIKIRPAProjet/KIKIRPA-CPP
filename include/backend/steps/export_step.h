// include/backend/steps/export_step.h
#pragma once

#include "core/data/algorithm_types.h"
#include "core/step/step.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace StepsNs {

/**
 * @brief Paramètres d'entrée du step d'export.
 */
struct ExportInput {
    /// Dossier de destination du fichier de sortie.
    /// Si laissé vide, le step utilise automatiquement le dossier "Téléchargements" de l'utilisateur.
    std::string outputDirectory = "";
};

/**
 * @brief Données et métadonnées issues de l'export.
 */
struct ExportOutput {
    /// Chemin complet vers le fichier TIFF composite final généré (ex: /path/to/mosaic.tif).
    std::filesystem::path imagePath;

    /// Dimensions totales de la mosaïque en pixels (largeur, hauteur).
    cv::Size canvasSize;

    /// Nombre total de blocs composites écrits dans le TIFF.
    std::size_t blockCount = 0;

    /// Indique si le fichier a été encodé au format BigTIFF (décalages 64 bits si taille estimée >= 4 Go).
    bool isBigTiff = false;
};

/**
 * @brief Step d'exportation finale de la mosaïque au format TIFF tuilé (TIFF standard ou BigTIFF).
 *
 * Ce step rassemble tous les blocs composites ("compositeBlocks") produits par `FusionStepClass`,
 * vérifie l'intégrité de la grille (alignement, dimensions, couverture complète sans trou ni doublon),
 * puis écrit directement les tuiles compressées en LZW dans un unique fichier TIFF multi-résolution.
 *
 * Pour garantir la robustesse face aux plantages ou aux écritures concurrentes, le fichier est d'abord
 * écrit dans un fichier temporaire (.tmp) puis substitué atomiquement à son emplacement final.
 *
 * Contrat de step :
 *  - Nom du step : "export"
 *  - Paramètre : "outputDirectory" (chaîne, chemin du dossier de sortie)
 *  - Entrée requise dans DataStore : "CompositeBlocks" ("compositeBlocks")
 *  - Sortie générée sur disque : mosaic.tif
 */
class ExportStepClass : public StepNs::StepClass {
public:
    explicit ExportStepClass(const ExportInput& initialInput = {});
    ~ExportStepClass() override = default;

    /**
     * @brief Exécute l'assemblage et l'écriture du TIFF tuilé :
     *  1. Récupère les "compositeBlocks" depuis le DataStore.
     *  2. Indexe les blocs dans une grille (colonne, ligne) et valide leur alignement.
     *  3. Détermine si le format standard (32 bits) ou BigTIFF (64 bits) est requis.
     *  4. Configure les en-têtes libtiff (LZW, dimensions, tuiles de 512x512).
     *  5. Écrit chaque tuile encodée dans un fichier temporaire.
     *  6. Remplace atomiquement le fichier cible ("mosaic.tif").
     * @return true si l'écriture a réussi, false en cas d'erreur.
     */
    bool execute() override;

    /**
     * @brief Calcule une empreinte (taille, date de modification) du fichier de sortie pour le cache du pipeline.
     */
    Json externalInputsFingerprint() const override;

    /// Accès aux résultats d'exportation.
    const ExportOutput& getOutput() const { return output; }

private:
    ExportInput input;
    ExportOutput output;
};

} // namespace StepsNs
