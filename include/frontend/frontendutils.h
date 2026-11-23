#pragma once

#include "project_setup_model.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

#include <filesystem>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QWidget;

namespace cv {
    class Mat;
}

namespace FrontendUtils {

extern const QStringList MODE_NAMES;

struct PipelineStepInfo {
    std::string technicalName;
    QString displayName;
};

// Étapes du script pour ces méthodes, dans l'ordre (liste de l'onglet Chaîne, relance, étape de départ).
std::vector<PipelineStepInfo> pipelineSteps(const FrontendNs::PipelineChoices &choices);

// Chemin absolu du modèle LightGlue : <dossier de l'exécutable>/LIGHTGLUE_MODEL_PATH. Le fichier peut ne pas exister
// (modèle non téléchargé, application lancée hors du dossier de build).
std::filesystem::path lightGlueModelPath();

// Détection et lecture d'un TIFF pyramidal pour l'affichage fluide
QImage matToQImage(const cv::Mat &mat);
QString findPyramidPathForSource(const QString &sourcePath);
QImage readPyramidalPreview(const QString &pyramidPath, const QSize &maximumSize);

//définition des 4 états possibles d'une paire de tuiles 
enum class PairStatus : std::uint8_t {
    Reliable,
    Dubious,
    Failed,
    Validated
};

//regroupe tout les données d'une paire qui seront affichées sur l'interface 
struct PairAlignmentInfo {
    int pairIndex = 0;
    int tileIndexA = 0;
    int tileIndexB = 1;
    QString tileA;
    QString tileB;
    QString direction;
    PairStatus status = PairStatus::Reliable;
    QString statusReason;
    double meanConfidence = 0.0;
    double nccScore = 0.0;
    double dx = 0.0;
    double dy = 0.0;
    int inlierCount = 0;
    int totalMatches = 0;
    int shownInliers = 0;
    int shownOutliers = 0;
    double optimizationErrorPx = 0.0;
    QString intervention;

    /* À décommenter lors de la connexion au backend (vrais points du canvas) ---
    QSize tileSizeA;
    QSize tileSizeB;
    QVector<QPointF> pointsInA;
    QVector<QPointF> pointsInB;
    QVector<bool> isInlier;
    */

    QString pairLabel() const { return tileA + QStringLiteral(" → ") + tileB; }
    bool isDubious() const { return status == PairStatus::Dubious; }
};

QPushButton *makeButton(const QString &text, QWidget *parent, bool primary = false);
QLabel *mutedLabel(const QString &text, QWidget *parent);

QVector<PairAlignmentInfo> defaultPairAlignments();

/* À décommenter lors de la connexion au backend (#include "backend/core/data/algorithm_types.h") ---
PairAlignmentInfo pairInfoFromBackend(int pairIndex,
                                      const QString &tileAName,
                                      const QString &tileBName,
                                      const DataNs::PairMatches &pairMatches,
                                      const DataNs::Alignment *alignment = nullptr,
                                      double nccScore = 1.0,
                                      double optimizationErrorPx = 0.0);
*/

}

QVector<int> captureOrderCells(int rows, int columns, int order);
QImage readPreviewImage(const QString &path, const QSize &maximumSize);
