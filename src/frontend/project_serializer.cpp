// src/frontend/project_serializer.cpp
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

#include <array>
#include <utility>
#include <vector>

namespace {

constexpr int LAST_PROJECT_MODE = 6;   // 0 Projet … 4 Ligne de couture, 5 Assemblage, 6 Export

// Les anciens projets n'ont pas de script ; ils restent consultables avec le mode par défaut.
bool readResumeData(const QJsonObject &project, ProjectData &loaded, QString *errorMessage)
{
    const QJsonValue mode = project.value(QStringLiteral("mode"));
    if (!mode.isUndefined() && (!mode.isDouble() || mode.toDouble() != mode.toInt(-1) ||
                               mode.toInt(-1) < 0 || mode.toInt(-1) > LAST_PROJECT_MODE)) {
        if (errorMessage) *errorMessage = QStringLiteral("La page de travail enregistrée est invalide.");
        return false;
    }
    loaded.mode = mode.toInt(0);
    if (!project.contains(QStringLiteral("backendScript"))) return true;
    const QJsonValue script = project.value(QStringLiteral("backendScript"));
    if (!script.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("Le script backend enregistré est invalide.");
        return false;
    }
    loaded.backendScript = nlohmann::json::parse(
        QJsonDocument(script.toObject()).toJson().toStdString());
    return true;
}

QJsonArray pointsToJson(const std::vector<std::array<double, 2>> &points)
{
    QJsonArray json;
    for (const auto &point : points) json.append(QJsonArray{point[0], point[1]});
    return json;
}

// Points [[x, y], ...] ; un point mal formé est ignoré.
std::vector<std::array<double, 2>> pointsFromJson(const QJsonArray &json)
{
    std::vector<std::array<double, 2>> points;
    for (const QJsonValue &value : json) {
        const QJsonArray point = value.toArray();
        if (point.size() == 2 && point.at(0).isDouble() && point.at(1).isDouble())
            points.push_back({point.at(0).toDouble(), point.at(1).toDouble()});
    }
    return points;
}

QJsonArray correctionsToJson(const std::vector<FrontendNs::ManualCorrection> &corrections)
{
    QJsonArray json;
    for (const FrontendNs::ManualCorrection &correction : corrections) {
        json.append(QJsonObject{{QStringLiteral("tileIdA"), correction.tileIdA},
                                {QStringLiteral("tileIdB"), correction.tileIdB},
                                {QStringLiteral("pointsInA"), pointsToJson(correction.pointsInA)},
                                {QStringLiteral("pointsInB"), pointsToJson(correction.pointsInB)}});
    }
    return json;
}

// Une correction sans point, ou aux listes de tailles différentes, est ignorée : manual-matches la refuserait.
std::vector<FrontendNs::ManualCorrection> correctionsFromJson(const QJsonArray &json)
{
    std::vector<FrontendNs::ManualCorrection> corrections;
    for (const QJsonValue &value : json) {
        const QJsonObject object = value.toObject();
        FrontendNs::ManualCorrection correction{object.value(QStringLiteral("tileIdA")).toInt(-1),
                                                object.value(QStringLiteral("tileIdB")).toInt(-1),
                                                pointsFromJson(object.value(QStringLiteral("pointsInA")).toArray()),
                                                pointsFromJson(object.value(QStringLiteral("pointsInB")).toArray())};
        const bool valid = correction.tileIdA >= 0 && correction.tileIdB >= 0 && !correction.pointsInA.empty() &&
                           correction.pointsInA.size() == correction.pointsInB.size();
        if (valid) corrections.push_back(std::move(correction));
    }
    return corrections;
}

// Méthodes de l'onglet Chaîne, enregistrées dans l'objet "pipeline" du projet.
QJsonObject pipelineChoicesToJson(const FrontendNs::PipelineChoices &choices)
{
    QJsonObject json;
    json.insert(QStringLiteral("estimateOverlap"), choices.estimateOverlap);
    json.insert(QStringLiteral("matchingStep"), QString::fromStdString(choices.matchingStep));
    json.insert(QStringLiteral("intensityMethod"), QString::fromStdString(choices.intensityMethod));
    json.insert(QStringLiteral("seamStep"), QString::fromStdString(choices.seamStep));
    json.insert(QStringLiteral("manualCorrections"), correctionsToJson(choices.manualCorrections));
    return json;
}

// Une clé absente (projet enregistré avant cet ajout) ou une méthode inconnue garde la valeur par défaut.
FrontendNs::PipelineChoices pipelineChoicesFromJson(const QJsonObject &json)
{
    FrontendNs::PipelineChoices choices;
    const auto text = [&json](const char *key, const std::string &fallback) {
        return json.value(QLatin1String(key)).toString(QString::fromStdString(fallback)).toStdString();
    };
    choices.estimateOverlap = json.value(QStringLiteral("estimateOverlap")).toBool(choices.estimateOverlap);
    choices.matchingStep = text("matchingStep", choices.matchingStep);
    choices.intensityMethod = text("intensityMethod", choices.intensityMethod);
    choices.seamStep = text("seamStep", choices.seamStep);
    choices.manualCorrections = correctionsFromJson(json.value(QStringLiteral("manualCorrections")).toArray());
    return FrontendNs::validatedChoices(choices);
}

}

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
    if (!readResumeData(project, loadedProject, errorMessage)) return false;

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
    gridState.pipelineChoices = pipelineChoicesFromJson(project.value(QStringLiteral("pipeline")).toObject());
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
                                   QString *errorMessage,
                                   const nlohmann::json &backendScript)
{
    if (mode < 0 || mode > LAST_PROJECT_MODE) {
        if (errorMessage) *errorMessage = QStringLiteral("La page de travail est invalide.");
        return false;
    }
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
    project.insert(QStringLiteral("pipeline"), pipelineChoicesToJson(gridState.pipelineChoices));
    if (backendScript.is_object())
        project.insert(QStringLiteral("backendScript"), QJsonDocument::fromJson(
            QByteArray::fromStdString(backendScript.dump())).object());

    QJsonArray tiles;
    for (const QString &tile : gridState.tilePaths)
        tiles.append(tile);
    project.insert(QStringLiteral("tiles"), tiles);

    QJsonArray grid;
    for (const GridTile &tile : gridState.gridTiles) {
        QJsonObject cell;
        cell.insert(QStringLiteral("path"), tile.path);
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
