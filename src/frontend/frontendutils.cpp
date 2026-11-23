// src/frontend/frontendutils.cpp
/*
 * .cpp : le POURQUOI
 * - choix de conception : regrouper les composants graphiques communs évite de dupliquer leur style ;
 * - cas limite : le bouton peut être primaire ou secondaire selon le contexte de l'interface ;
 * - unité ou repère : les dimensions et propriétés suivent les règles visuelles du frontend ;
 * - raison non évidente : les propriétés Qt permettent au style global de personnaliser les widgets.
 */
/*
 * Header : le QUOI
 * Fournit les outils communs de l'interface frontend.
 * - ce que ça fait : crée les boutons, les libellés et les éléments graphiques réutilisables ;
 * - ce que ça renvoie : les widgets Qt configurés pour être utilisés par l'interface ;
 * - quand ça lève une exception : si la création ou la configuration d'un widget échoue.
 */
// fournit les outils communs de linterface frontend
#include "frontendutils.h"

#include "core/io/pyramid_cache.h"
#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/unicode_path.h"

#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QLabel>
#include <QObject>
#include <QPixmap>
#include <QPushButton>
#include <QtMath>

namespace FrontendUtils {

const QStringList MODE_NAMES = {
    QObject::tr("Projet"), QObject::tr("Tuiles"), QObject::tr("Masques"),
    QObject::tr("Alignement"), QObject::tr("Assemblage"), QObject::tr("Image Finale")
};

// Libellé affiché de chaque step du registre que l'interface peut mettre dans le script.
static QString stepDisplayName(const std::string &technicalName)
{
    if (technicalName == "import") return QObject::tr("Importation des tuiles");
    if (technicalName == "overlap-estimation") return QObject::tr("Recouvrement réel (première passe)");
    if (technicalName == "phase-correlation") return QObject::tr("Appariement (corrélation de phase)");
    if (technicalName == "sift") return QObject::tr("Appariement (SIFT)");
    if (technicalName == "lightglue") return QObject::tr("Appariement (LightGlue)");
    if (technicalName == "vote-2d") return QObject::tr("Rejet des aberrants (Vote 2D)");
    if (technicalName == "absolute-pos") return QObject::tr("Positionnement global");
    if (technicalName == "intensity-correction") return QObject::tr("Égalisation de luminosité");
    if (technicalName == "graphcut") return QObject::tr("Ligne de couture (GraphCut)");
    if (technicalName == "dynamic-programming-dijkstra") return QObject::tr("Ligne de couture (programmation dynamique)");
    if (technicalName == "fusion") return QObject::tr("Fusion des tuiles");
    if (technicalName == "export") return QObject::tr("Export de la mosaïque");
    return QString::fromStdString(technicalName);
}

std::vector<PipelineStepInfo> pipelineSteps(const FrontendNs::PipelineChoices &choices) {
    std::vector<PipelineStepInfo> steps;
    for (const std::string &technicalName : FrontendNs::pipelineStepNames(choices)) {
        const QString number = QString::number(static_cast<int>(steps.size()) + 1);
        steps.push_back({technicalName, number + QStringLiteral("  ") + stepDisplayName(technicalName)});
    }
    return steps;
}

// Le dossier de l'exécutable, et non le dossier courant : stitching et models/ sont tous deux dans le dossier de build,
// d'où que l'application soit lancée.
std::filesystem::path lightGlueModelPath()
{
    const std::filesystem::path applicationDirectory =
        IONs::pathFromUtf8(QCoreApplication::applicationDirPath().toStdString());
    return (applicationDirectory / FrontendNs::LIGHTGLUE_MODEL_PATH).lexically_normal();
}

QPushButton *makeButton(const QString &text, QWidget *parent, bool primary)
{
    auto *button = new QPushButton(text, parent);
    button->setProperty("primary", primary);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QLabel *mutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("muted", true);
    return label;
}

QLabel *logoLabel(int sideInPixels, QWidget *parent, const QString &resourcePath)
{
    auto *label = new QLabel(parent);
    const QPixmap source(resourcePath);
    if (source.isNull()) {
        label->hide();
        return label;
    }
    // Le PNG est plus grand que l'affichage : on le réduit à la densité de l'écran pour éviter un logo flou sur un écran 150 % ou 200 %.
    const qreal pixelRatio = qGuiApp != nullptr ? qGuiApp->devicePixelRatio() : 1.0;
    const int physicalSide = qRound(sideInPixels * pixelRatio);
    QPixmap scaled = source.scaled(physicalSide, physicalSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(pixelRatio);
    label->setPixmap(scaled);
    label->setFixedSize(sideInPixels, sideInPixels);
    return label;
}

//paires d'exemple de la maquette - à supprimé lors de la connection au backend
QVector<PairAlignmentInfo> defaultPairAlignments()
{
    return {
        {0, 0, 1, QStringLiteral("r00_c00"), QStringLiteral("r00_c01"),
         QObject::tr("horizontal"), PairStatus::Dubious,
         QObject::tr("contrôle NCC faible (0,37)"),
         0.91, 0.37, 4065.5, -20.7, 263, 1401, 14, 3, 3.95, QObject::tr("aucune")},
        {1, 0, 2, QStringLiteral("r00_c00"), QStringLiteral("r01_c00"),
         QObject::tr("vertical"), PairStatus::Reliable,
         QObject::tr("corrélation et géométrie cohérentes"),
         0.99, 0.76, 23.5, 4086.7, 678, 1232, 16, 1, 0.04, QObject::tr("aucune")},
        {2, 1, 3, QStringLiteral("r00_c01"), QStringLiteral("r01_c01"),
         QObject::tr("vertical"), PairStatus::Reliable,
         QObject::tr("corrélation et géométrie cohérentes"),
         0.99, 0.92, 23.8, 4133.7, 1105, 1429, 18, 1, 0.04, QObject::tr("aucune")},
        {3, 2, 3, QStringLiteral("r01_c00"), QStringLiteral("r01_c01"),
         QObject::tr("horizontal"), PairStatus::Reliable,
         QObject::tr("corrélation et géométrie cohérentes"),
         0.98, 0.65, 4064.7, 29.9, 421, 1492, 15, 2, 0.02, QObject::tr("aucune")}
    };
}

/* À décommenter lors de la connexion au backend (convertit DataNs::PairMatches en PairAlignmentInfo) ---
PairAlignmentInfo pairInfoFromBackend(int pairIndex,
                                      const QString &tileAName,
                                      const QString &tileBName,
                                      const DataNs::PairMatches &pairMatches,
                                      const DataNs::Alignment *alignment,
                                      double nccScore,
                                      double optimizationErrorPx)
{
    PairAlignmentInfo info;
    info.pairIndex = pairIndex;
    info.tileA = tileAName;
    info.tileB = tileBName;
    info.direction = (pairMatches.direction == DataNs::Direction::Bottom ||
                      pairMatches.direction == DataNs::Direction::Top)
                         ? QObject::tr("vertical")
                         : QObject::tr("horizontal");

    const auto &matches = pairMatches.featureMatches;
    info.dx = matches.translationX;
    info.dy = matches.translationY;
    info.totalMatches = static_cast<int>(matches.pointsInA.size());
    info.nccScore = nccScore;
    info.optimizationErrorPx = optimizationErrorPx;
    info.intervention = QObject::tr("aucune");

    double sumConfidence = 0.0;
    int inliers = 0;

    for (std::size_t i = 0; i < matches.pointsInA.size(); ++i) {
        const bool inlier = (alignment == nullptr ||
                             i >= alignment->inlierMask.size() ||
                             alignment->inlierMask[i] != 0);
        info.pointsInA.append(QPointF(matches.pointsInA[i].x, matches.pointsInA[i].y));
        info.pointsInB.append(QPointF(matches.pointsInB[i].x, matches.pointsInB[i].y));
        info.isInlier.append(inlier);

        if (inlier) {
            if (i < matches.confidenceScores.size())
                sumConfidence += matches.confidenceScores[i];
            ++inliers;
        }
    }

    info.inlierCount = inliers;
    info.shownInliers = inliers;
    info.shownOutliers = info.totalMatches - inliers;
    info.meanConfidence = inliers > 0 ? (sumConfidence / inliers) : 0.0;

    // Détection automatique de paire douteuse à partir des scores de confiance et du NCC
    if (info.totalMatches == 0 || info.inlierCount < 4) {
        info.status = PairStatus::Failed;
        info.statusReason = QObject::tr("nombre de points insuffisant (%1)").arg(info.inlierCount);
    } else if (info.nccScore < 0.50) {
        info.status = PairStatus::Dubious;
        info.statusReason = QObject::tr("contrôle NCC faible (%1)")
                                .arg(QString::number(info.nccScore, 'f', 2).replace('.', ','));
    } else if (info.meanConfidence < 0.75) {
        info.status = PairStatus::Dubious;
        info.statusReason = QObject::tr("score de confiance faible (%1)")
                                .arg(QString::number(info.meanConfidence, 'f', 2).replace('.', ','));
    } else {
        info.status = PairStatus::Reliable;
        info.statusReason = QObject::tr("corrélation et géométrie cohérentes");
    }
    return info;
}
*/

QImage matToQImage(const cv::Mat &mat)
{
    if (mat.empty())
        return {};

    cv::Mat mat8;
    if (mat.depth() == CV_16U) {
        mat.convertTo(mat8, CV_8U, 1.0 / 256.0);
    } else if (mat.depth() == CV_8U) {
        mat8 = mat;
    } else {
        return {};
    }

    if (mat8.channels() == 1) {
        return QImage(mat8.data, mat8.cols, mat8.rows, static_cast<int>(mat8.step),
                      QImage::Format_Grayscale8).copy();
    }
    if (mat8.channels() == 3) {
        return QImage(mat8.data, mat8.cols, mat8.rows, static_cast<int>(mat8.step),
                      QImage::Format_BGR888).copy();
    }
    if (mat8.channels() == 4) {
        return QImage(mat8.data, mat8.cols, mat8.rows, static_cast<int>(mat8.step),
                      QImage::Format_ARGB32).copy();
    }
    return {};
}

// Pyramide de `sourcePath` dans le cache partagé avec le backend (core/io/pyramid_cache.h), à côté de l'image.
// Chemin vide si elle n'existe pas encore ou si elle a été produite à partir d'une autre version de l'image : l'aperçu
// relit alors la source.
QString findPyramidPathForSource(const QString &sourcePath)
{
    if (sourcePath.isEmpty())
        return {};

    // Le chemin passé est déjà un TIFF pyramidal
    if (sourcePath.endsWith(QStringLiteral("_pyramid.tif"), Qt::CaseInsensitive) ||
        sourcePath.endsWith(QStringLiteral("_pyramid.tiff"), Qt::CaseInsensitive)) {
        return QFileInfo::exists(sourcePath) ? sourcePath : QString();
    }

    const std::filesystem::path source = IONs::pathFromUtf8(sourcePath.toStdString());
    const std::filesystem::path pyramid =
        IONs::pyramidCachePath(IONs::pyramidCacheDirectory(source.parent_path()), source);
    if (!IONs::isPyramidCacheValid(source, pyramid))
        return {};
    return QString::fromStdString(IONs::pathToUtf8(pyramid));
}

QImage readPyramidalPreview(const QString &pyramidPath, const QSize &maximumSize)
{
    if (pyramidPath.isEmpty())
        return {};

    IONs::PyramidalTiffReaderClass reader(IONs::pathFromUtf8(pyramidPath.toStdString()));
    const int levelCount = reader.getLevelCount();
    if (levelCount <= 0)
        return {};

    int chosenLevel = levelCount - 1;
    for (int level = levelCount - 1; level >= 0; --level) {
        int width = 0;
        int height = 0;
        std::string err;
        if (reader.getLevelDimensions(level, width, height, err)) {
            if (width >= maximumSize.width() && height >= maximumSize.height()) {
                chosenLevel = level;
                break;
            }
        }
    }

    cv::Mat levelMat;
    std::string errorMessage;
    if (!reader.readLevel(chosenLevel, levelMat, errorMessage))
        return {};

    QImage image = matToQImage(levelMat);
    if (!image.isNull() && (image.width() > maximumSize.width() || image.height() > maximumSize.height())) {
        image = image.scaled(maximumSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

}

QVector<int> captureOrderCells(int rows, int columns, int order)
{
    QVector<int> cells;
    cells.reserve(rows * columns);
    switch (order) {
    case 1:
        for (int column = 0; column < columns; ++column) {
            const bool bottomUp = (column % 2 == 0);
            for (int offset = 0; offset < rows; ++offset) {
                const int row = bottomUp ? rows - 1 - offset : offset;
                cells.append(row * columns + column);
            }
        }
        break;
    case 2:
        for (int offset = 0; offset < columns; ++offset) {
            const int column = columns - 1 - offset;
            const bool topDown = (offset % 2 == 0);
            for (int rowOffset = 0; rowOffset < rows; ++rowOffset) {
                const int row = topDown ? rowOffset : rows - 1 - rowOffset;
                cells.append(row * columns + column);
            }
        }
        break;
    case 3:
        for (int row = 0; row < rows; ++row) {
            for (int offset = 0; offset < columns; ++offset) {
                const int column = row % 2 == 0 ? offset : columns - 1 - offset;
                cells.append(row * columns + column);
            }
        }
        break;
    case 4:
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
                cells.append(row * columns + column);
        break;
    case 5:
        for (int column = 0; column < columns; ++column)
            for (int row = 0; row < rows; ++row)
                cells.append(row * columns + column);
        break;
    default:
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column)
                cells.append(row * columns + column);
        }
        break;
    }
    return cells;
}

QImage readPreviewImage(const QString &path, const QSize &maximumSize)
{
    const QString pyramidPath = FrontendUtils::findPyramidPathForSource(path);
    if (!pyramidPath.isEmpty()) {
        const QImage pyramidPreview = FrontendUtils::readPyramidalPreview(pyramidPath, maximumSize);
        if (!pyramidPreview.isNull())
            return pyramidPreview;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        const double scale = qMin(static_cast<double>(maximumSize.width()) / sourceSize.width(),
                                  static_cast<double>(maximumSize.height()) / sourceSize.height());
        if (scale > 0.0 && scale < 1.0)
            reader.setScaledSize(QSize(qMax(1, qRound(sourceSize.width() * scale)),
                                       qMax(1, qRound(sourceSize.height() * scale))));
    }
    QImage image = reader.read();
    if (!image.isNull())
        image = image.scaled(maximumSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}
