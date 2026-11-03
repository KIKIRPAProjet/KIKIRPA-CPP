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
    ProjectData loadedProject;
    loadedProject.projectName = project.value(QStringLiteral("project_name"))
                                    .toString(QFileInfo(path).completeBaseName());
    loadedProject.outputDirectory = project.value(QStringLiteral("output_directory")).toString();

    ProjectGrid::State &gridState = loadedProject.gridState;
    gridState.tileDirectory = project.value(QStringLiteral("tile_directory")).toString();
    gridState.gridRows = qBound(1, project.value(QStringLiteral("grid_rows")).toInt(3), 20);
    gridState.gridColumns = qBound(1, project.value(QStringLiteral("grid_columns")).toInt(2), 20);
    gridState.captureOrder = qBound(0, project.value(QStringLiteral("capture_order")).toInt(0), 5);
    gridState.horizontalOverlap = qBound(
        0, project.value(QStringLiteral("horizontal_overlap")).toInt(20), 90);
    gridState.verticalOverlap = qBound(
        0, project.value(QStringLiteral("vertical_overlap")).toInt(20), 90);
    gridState.overlapsLinked = project.value(QStringLiteral("overlaps_linked")).toBool(true);
    if (gridState.overlapsLinked)
        gridState.verticalOverlap = gridState.horizontalOverlap;
    gridState.modality = project.value(QStringLiteral("modality"))
                             .toString(QStringLiteral("Infrarouge"));
    gridState.scaleMmPerPixel = qMax(
        0.0, project.value(QStringLiteral("scale_mm_per_pixel")).toDouble());
    gridState.uncertaintyMargin = qMax(
        0.0, project.value(QStringLiteral("uncertainty_margin")).toDouble());
    gridState.automaticUncertainty =
        project.value(QStringLiteral("automatic_uncertainty")).toBool(true);
    gridState.referenceImagePath = project.value(QStringLiteral("reference_image")).toString();
    for (const QJsonValue &tile : project.value(QStringLiteral("tiles")).toArray())
        gridState.tilePaths.append(tile.toString());

    const QJsonArray grid = project.value(QStringLiteral("grid_tiles")).toArray();
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
    project.insert(QStringLiteral("format_version"), QStringLiteral("1.0"));
    project.insert(QStringLiteral("project_name"), projectName);
    project.insert(QStringLiteral("saved_at"), QDateTime::currentDateTime().toString(Qt::ISODate));
    project.insert(QStringLiteral("mode"), mode);
    project.insert(QStringLiteral("tile_directory"), gridState.tileDirectory);
    project.insert(QStringLiteral("output_directory"), outputDirectory);
    project.insert(QStringLiteral("grid_rows"), gridState.gridRows);
    project.insert(QStringLiteral("grid_columns"), gridState.gridColumns);
    project.insert(QStringLiteral("capture_order"), gridState.captureOrder);
    project.insert(QStringLiteral("horizontal_overlap"), gridState.horizontalOverlap);
    project.insert(QStringLiteral("vertical_overlap"), gridState.verticalOverlap);
    project.insert(QStringLiteral("overlaps_linked"), gridState.overlapsLinked);
    project.insert(QStringLiteral("modality"), gridState.modality);
    project.insert(QStringLiteral("scale_mm_per_pixel"), gridState.scaleMmPerPixel);
    project.insert(QStringLiteral("uncertainty_margin"), gridState.uncertaintyMargin);
    project.insert(QStringLiteral("automatic_uncertainty"), gridState.automaticUncertainty);
    project.insert(QStringLiteral("reference_image"), gridState.referenceImagePath);

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
    project.insert(QStringLiteral("grid_tiles"), grid);

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
