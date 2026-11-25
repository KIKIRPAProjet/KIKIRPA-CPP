// include/frontend/pipeline_results.h
#pragma once

#include "frontendutils.h"
#include "project_setup_model.h"

#include "core/data/algorithm_types.h"
#include "core/data/pair_residual.h"
#include "core/data/tile.h"

#include <nlohmann/json_fwd.hpp>

#include <QMetaType>
#include <QPolygonF>
#include <QSize>
#include <QString>
#include <QVector>

#include <vector>

// Tuile importée, pour la vue d'ensemble de l'alignement : image, taille pleine résolution et case de la grille.
struct ResultTile {
    int id = 0;   // TileClass::getId(), comme PairAlignmentInfo::tileIndexA / tileIndexB
    QString name;
    QString imagePath;
    QSize size;
    int row = 0;
    int column = 0;
};

// Lignes de couture dans le repère de l'aperçu de la mosaïque (pixels de `frame`). Le canevas les trace en vecteur,
// mises à l'échelle de l'image affichée : leur épaisseur reste la même quelle que soit la taille de la vue.
struct SeamLines {
    QSize frame;
    QVector<QPolygonF> lines;
    bool isEmpty() const { return lines.isEmpty(); }
};

// Résultats d'un calcul affichés par l'interface : les tuiles et leurs paires (vue Alignement) et les lignes de
// couture (vue Ligne de couture). Construits par le worker une fois le pipeline arrêté (fin, échec ou pause).
struct PipelineResults {
    QVector<ResultTile> tiles;
    QVector<FrontendUtils::PairAlignmentInfo> pairs;   // paires appariées, puis paires en échec
    SeamLines seams;   // vide sans couture
    bool hasFusion = false;   // vrai dès que la mosaïque fusionnée est disponible
};
Q_DECLARE_METATYPE(PipelineResults)

// Ce que l'onglet Paire montre d'une paire : l'état en un mot, une explication en français clair (avec ce qu'il faut
// faire) et, pour l'infobulle, les mesures techniques et la raison brute renvoyée par les steps.
struct PairOperatorText {
    QString state;         // « Fiable », « À vérifier », « Échec » ou « Validée »
    QString explanation;
    QString details;
};

namespace FrontendNs {

    // Une entrée par tuile importée, dans l'ordre de la liste.
    QVector<ResultTile> buildResultTiles(const DataNs::TileList &tiles);

    // Une entrée par paire de `matches` (points, translation, état), puis une par paire en échec citée dans les
    // résultats des steps (`failures`). L'état vient du verdict de l'appariement quand il existe (`confidence` de
    // phase-correlation : fiable, ambigu, non vérifié, rejeté), sinon des points eux-mêmes. Une paire corrigée par
    // manual-matches (`correctedPairs`) est « validée ». L'erreur affichée est
    // l'écart de la paire après positionnement (`pairResiduals` d'absolute-pos), 0 s'il n'est pas calculé.
    QVector<FrontendUtils::PairAlignmentInfo> buildPairInfos(const DataNs::TileList &tiles,
                                                             const std::vector<DataNs::PairMatches> &matches,
                                                             const std::vector<nlohmann::json> &stepResults,
                                                             const std::vector<DataNs::PairResidual> &residuals);

    // Recopie dans chaque paire la zone commune estimée par overlap-estimation (clé "overlapEstimates"). Une paire sans
    // point (échec) prend aussi la translation estimée : point de départ de sa correction manuelle.
    void attachOverlapEstimates(QVector<FrontendUtils::PairAlignmentInfo> &pairs, const DataNs::OverlapEstimates &estimates);

    // Correction manuelle équivalente au décalage (dx, dy) de B dans A : un point au centre de la zone commune, dans A
    // et dans B (B = A - décalage). manual-matches en tire exactement cette translation.
    ManualCorrection manualCorrectionFor(const FrontendUtils::PairAlignmentInfo &pair, double dx, double dy);

    // `corrections` avec `correction` à la place de celle de la même paire (dans un sens ou l'autre), ou ajoutée.
    std::vector<ManualCorrection> withCorrection(std::vector<ManualCorrection> corrections, const ManualCorrection &correction);

    // Texte de la paire pour l'opérateur : la raison des steps (phase-correlation, contrôle de confiance, interface) est
    // reconnue par mots-clés et reformulée ; une raison inconnue est reprise telle quelle.
    PairOperatorText describePairForOperator(const FrontendUtils::PairAlignmentInfo &pair);

    // Lignes de couture : frontières entre les territoires de deux tuiles différentes (les bords extérieurs de la
    // mosaïque n'en sont pas). Les territoires sont dans le repère de la mosaïque (DataNs::computeMosaicCorners),
    // l'aperçu (`previewSize`) en est une réduction de facteur `previewScale`. Vide si rien n'est à dessiner.
    SeamLines buildSeamLines(const std::vector<DataNs::TileTerritory> &territories, const QSize &previewSize,
                             double previewScale);

}
