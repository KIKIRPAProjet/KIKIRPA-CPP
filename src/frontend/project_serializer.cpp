/*
 * .cpp : le POURQUOI
 * - choix de conception : utiliser JSON rend les projets lisibles et transportables entre environnements ;
 * - cas limite : un fichier absent, invalide ou partiellement écrit doit produire une erreur explicite ;
 * - unité ou repère : les chemins sont stockés comme chaînes compatibles avec le système de fichiers ;
 * - raison non évidente : QSaveFile évite de laisser un projet corrompu si l'écriture est interrompue.
 */
/*
 * Header : le QUOI
 * Charge et enregistre les projets au format JSON.
 * - ce que ça fait : lit les données d'un fichier JSON ou écrit les données du projet dans un fichier ;
 * - ce que ça renvoie : true ou false selon la réussite de l'opération, avec un message d'erreur si besoin ;
 * - quand ça lève une exception : si l'accès au fichier ou la conversion des données échoue.
 */
//  charge et enregistre les projets au format JSON
#include "project_serializer.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <utility>

bool ProjectSerializer::loadFromFile(const QString &path, ProjectData *projectData,
                                     QString *errorMessage)
{
    if (!projectData) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Aucune destination n'a été fournie pour le projet.");
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Le fichier ne peut pas être lu.");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Ce fichier n'est pas un projet JSON valide.");
        return false;
    }

    const QJsonObject project = document.object();
    const auto valueFor = [&project](const QString &camelCaseKey,
                                     const QString &legacyKey) {
        return project.contains(camelCaseKey) ? project.value(camelCaseKey)
                                              : project.value(legacyKey);
    };
    ProjectData loadedProject;
    loadedProject.projectName = valueFor(QStringLiteral("projectName"),
                                         QStringLiteral("project_name"))
                                    .toString(QFileInfo(path).completeBaseName());
    loadedProject.outputDirectory =
        valueFor(QStringLiteral("outputDirectory"), QStringLiteral("output_directory")).toString();

    ProjectGrid::State &gridState = loadedProject.gridState;
    gridState.tileDirectory =
        valueFor(QStringLiteral("tileDirectory"), QStringLiteral("tile_directory")).toString();
    gridState.gridRows = qBound(
        1, valueFor(QStringLiteral("gridRows"), QStringLiteral("grid_rows")).toInt(3), 20);
    gridState.gridColumns = qBound(
        1, valueFor(QStringLiteral("gridColumns"), QStringLiteral("grid_columns")).toInt(2), 20);
    gridState.captureOrder = qBound(
        0, valueFor(QStringLiteral("captureOrder"), QStringLiteral("capture_order")).toInt(0), 5);
    gridState.horizontalOverlap = qBound(
        0, valueFor(QStringLiteral("horizontalOverlap"),
                    QStringLiteral("horizontal_overlap")).toInt(20), 90);
    gridState.verticalOverlap = qBound(
        0, valueFor(QStringLiteral("verticalOverlap"),
                    QStringLiteral("vertical_overlap")).toInt(20), 90);
    gridState.overlapsLinked =
        valueFor(QStringLiteral("overlapsLinked"), QStringLiteral("overlaps_linked")).toBool(true);
    if (gridState.overlapsLinked)
        gridState.verticalOverlap = gridState.horizontalOverlap;
    gridState.modality = valueFor(QStringLiteral("modality"), QStringLiteral("modality"))
                             .toString(QStringLiteral("Infrarouge"));
    gridState.scaleMmPerPixel = qMax(
        0.0, valueFor(QStringLiteral("scaleMmPerPixel"),
                      QStringLiteral("scale_mm_per_pixel")).toDouble());
    gridState.uncertaintyMargin = qMax(
        0.0, valueFor(QStringLiteral("uncertaintyMargin"),
                      QStringLiteral("uncertainty_margin")).toDouble());
    gridState.automaticUncertainty =
        valueFor(QStringLiteral("automaticUncertainty"),
                 QStringLiteral("automatic_uncertainty")).toBool(true);
    gridState.referenceImagePath =
        valueFor(QStringLiteral("referenceImage"), QStringLiteral("reference_image")).toString();
    for (const QJsonValue &tile : project.value(QStringLiteral("tiles")).toArray())
        gridState.tilePaths.append(tile.toString());

    const QJsonArray grid =
        valueFor(QStringLiteral("gridTiles"), QStringLiteral("grid_tiles")).toArray();
    if (!grid.isEmpty()) {
        gridState.gridTiles.reserve(qMax(gridState.gridRows * gridState.gridColumns,
                                         static_cast<int>(grid.size())));
        for (const QJsonValue &cellValue : grid) {
            const QJsonObject cell = cellValue.toObject();
            GridTile tile;
            tile.path = cell.value(QStringLiteral("path")).toString();
            const int savedRotation = cell.value(QStringLiteral("rotation")).toInt();
            tile.rotation = ((savedRotation % 360) + 360) % 360;
            tile.flipped = cell.value(QStringLiteral("flipped")).toBool();
            tile.empty = cell.value(QStringLiteral("empty")).toBool();
            gridState.gridTiles.append(tile);
        }
    }
    gridState.selectedGridCell = 0;
    gridState.gridValidated = false;

    *projectData = std::move(loadedProject);
    return true;
}

bool ProjectSerializer::saveToFile(const QString &path, const QString &projectName,
                                   const QString &outputDirectory, int mode,
                                   const ProjectGrid::State &gridState,
                                   QString *errorMessage)
{
    QJsonObject project;
    project.insert(QStringLiteral("formatVersion"), QStringLiteral("1.0"));
    project.insert(QStringLiteral("projectName"), projectName);
    project.insert(QStringLiteral("savedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    project.insert(QStringLiteral("mode"), mode);
    project.insert(QStringLiteral("tileDirectory"), gridState.tileDirectory);
    project.insert(QStringLiteral("outputDirectory"), outputDirectory);
    project.insert(QStringLiteral("gridRows"), gridState.gridRows);
    project.insert(QStringLiteral("gridColumns"), gridState.gridColumns);
    project.insert(QStringLiteral("captureOrder"), gridState.captureOrder);
    project.insert(QStringLiteral("horizontalOverlap"), gridState.horizontalOverlap);
    project.insert(QStringLiteral("verticalOverlap"), gridState.verticalOverlap);
    project.insert(QStringLiteral("overlapsLinked"), gridState.overlapsLinked);
    project.insert(QStringLiteral("modality"), gridState.modality);
    project.insert(QStringLiteral("scaleMmPerPixel"), gridState.scaleMmPerPixel);
    project.insert(QStringLiteral("uncertaintyMargin"), gridState.uncertaintyMargin);
    project.insert(QStringLiteral("automaticUncertainty"), gridState.automaticUncertainty);
    project.insert(QStringLiteral("referenceImage"), gridState.referenceImagePath);

    QJsonArray tiles;
    for (const QString &tile : gridState.tilePaths)
        tiles.append(tile);
    project.insert(QStringLiteral("tiles"), tiles);

    QJsonArray grid;
    for (const GridTile &tile : gridState.gridTiles) {
        QJsonObject cell;
        cell.insert(QStringLiteral("path"), tile.path);
        cell.insert(QStringLiteral("rotation"), tile.rotation);
        cell.insert(QStringLiteral("flipped"), tile.flipped);
        cell.insert(QStringLiteral("empty"), tile.empty);
        grid.append(cell);
    }
    project.insert(QStringLiteral("gridTiles"), grid);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    const QByteArray json = QJsonDocument(project).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size() || !file.commit()) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    return true;
}
