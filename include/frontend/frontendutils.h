#pragma once

#include "project_setup_model.h"

#include "core/data/algorithm_types.h"

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

#include <filesystem>
#include <optional>
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
void setPyramidCacheDirectory(const QString &directory);
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
    std::optional<double> nccScore;   // vide : non mesuré (SIFT, LightGlue, step repris d'un checkpoint)
    double dx = 0.0;
    double dy = 0.0;
    int inlierCount = 0;
    int totalMatches = 0;
    int shownInliers = 0;
    int shownOutliers = 0;
    std::optional<double> optimizationErrorPx;   // écart après positionnement ; vide : non calculé
    QString intervention;

    // Données calculées par le backend (vide tant qu'aucun calcul n'a été fait) : images, tailles pleine résolution et
    // points appariés, chacun dans le repère de sa tuile.
    QString imagePathA;
    QString imagePathB;
    QSize tileSizeA;
    QSize tileSizeB;
    QVector<QPointF> pointsInA;
    QVector<QPointF> pointsInB;
    QVector<bool> isInlier;
    // Zone commune estimée par le step overlap-estimation (pixels pleine résolution, repères de A et de B) ; vide si ce
    // step n'a pas été lancé ou n'a pas pu mesurer la paire.
    QRect overlapInA;
    QRect overlapInB;

    QString pairLabel() const { return tileA + QStringLiteral(" → ") + tileB; }
    bool isDubious() const { return status == PairStatus::Dubious; }
};

QPushButton *makeButton(const QString &text, QWidget *parent, bool primary = false);
QLabel *mutedLabel(const QString &text, QWidget *parent);
// Logo du projet dans un carré de sideInPixels pixels logiques, net sur écran haute densité.
// resourcePath : ":/logo/logo.png" (avec fond, en-têtes) ou ":/logo/logo_home.png" (gris sans fond, accueil).
// Sans la ressource (cible qui n'embarque pas resources.qrc, comme les tests), le label est vide et caché.
QLabel *logoLabel(int sideInPixels, QWidget *parent, const QString &resourcePath = QStringLiteral(":/logo/logo.png"));

PairAlignmentInfo pairInfoFromBackend(int pairIndex,
                                      const QString &tileAName,
                                      const QString &tileBName,
                                      const DataNs::PairMatches &pairMatches,
                                      const DataNs::Alignment *alignment = nullptr,
                                      std::optional<double> nccScore = std::nullopt,
                                      std::optional<double> optimizationErrorPx = std::nullopt);

// Valeur mesurée avec `decimals` décimales (virgule) suivie de `suffix`, ou « non disponible ».
QString formatMeasure(const std::optional<double> &value, int decimals, const QString &suffix = QString());

}

QVector<int> captureOrderCells(int rows, int columns, int order);
QImage readPreviewImage(const QString &path, const QSize &maximumSize);
