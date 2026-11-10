// src/frontend/project_serializer.cpp
/*
 * .cpp : le POURQUOI
 * - choix de conception : utiliser JSON rend les projets lisibles et transportables entre environnements ;
 * - cas limite : un fichier absent, invalide ou partiellement écrit doit produire une erreur explicite ;
 * - unité ou repère : les chemins relatifs sont résolus depuis le fichier projet ;
 * - raison non évidente : QSaveFile évite de laisser un projet corrompu si l'écriture est interrompue.
 */
#include "project_serializer.h"

#include "core/checkpoint/checkpoint.h"
#include "core/pipeline/pipeline_builder.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <utility>

namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;

constexpr int MODE_COUNT = 6;

QString absolutePath(const QString &path, const QString &projectFilePath)
{
    if (path.isEmpty())
        return {};
    const QFileInfo pathInfo(path);
    const QString resolved = pathInfo.isAbsolute()
        ? pathInfo.absoluteFilePath()
        : QFileInfo(projectFilePath).dir().absoluteFilePath(path);
    return QDir::cleanPath(resolved);
}

QString fingerprintForFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

bool readInteger(const QJsonObject &object, const QString &key, const QString &legacyKey,
                 int fallback, int minimum, int maximum, int *result)
{
    const QString selectedKey = object.contains(key) ? key : legacyKey;
    if (!object.contains(selectedKey)) {
        *result = fallback;
        return true;
    }
    const QJsonValue value = object.value(selectedKey);
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < minimum || number > maximum)
        return false;
    *result = static_cast<int>(number);
    return true;
}

} // namespace

bool ProjectResumeSummary::canResume() const
{
    return algorithmsAvailable && outputDirectoryAccessible && missingFiles.isEmpty();
}

QString ProjectResumeSummary::toText() const
{
    QStringList lines;
    if (!runStatus.isEmpty())
        lines.append(QStringLiteral("État enregistré : %1").arg(runStatus));
    const auto appendSection = [&lines](const QString &title, const QStringList &items) {
        lines.append(QStringLiteral("%1 (%2)").arg(title).arg(items.size()));
        for (const QString &item : items)
            lines.append(QStringLiteral("  • %1").arg(item));
    };

    appendSection(QStringLiteral("Étapes à jour"), upToDateSteps);
    appendSection(QStringLiteral("Étapes à vérifier / recalculer"), stepsToVerify);
    appendSection(QStringLiteral("Étapes jamais exécutées"), neverRunSteps);
    appendSection(QStringLiteral("Fichiers absents"), missingFiles);
    appendSection(QStringLiteral("Fichiers modifiés"), modifiedFiles);
    appendSection(QStringLiteral("Fichiers à vérifier (empreinte non sauvegardée)"),
                  unverifiedFiles);
    appendSection(QStringLiteral("Résultats sauvegardés"), availableResults);
    appendSection(QStringLiteral("Erreurs sauvegardées"), errors);
    if (!outputDirectoryAccessible)
        lines.append(QStringLiteral("Dossier de sortie inaccessible : %1")
                         .arg(outputDirectory.isEmpty()
                                  ? QStringLiteral("aucun chemin enregistré")
                                  : outputDirectory));
    if (!algorithmsAvailable)
        lines.append(QStringLiteral("Pipeline indisponible : les étapes sauvegardées ne sont pas "
                                    "toutes reconnues par cette version."));
    for (const QString &issue : issues)
        lines.append(QStringLiteral("Avertissement : %1").arg(issue));
    return lines.join(QLatin1Char('\n'));
}

bool ProjectSerializer::loadFromFile(const QString &path, ProjectData *projectData,
                                     QString *errorMessage)
{
    const auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };
    if (!projectData)
        return fail(QStringLiteral("Aucune destination n'a été fournie pour le projet."));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Le fichier ne peut pas être lu : %1").arg(file.errorString()));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("Ce fichier n'est pas un projet JSON valide : %1")
                        .arg(parseError.errorString()));

    const QJsonObject project = document.object();
    const QString version = project.value(QStringLiteral("formatVersion")).toString(
        QStringLiteral("1.0"));
    if (version != QStringLiteral("1.0") && version != QStringLiteral("2.0"))
        return fail(QStringLiteral("Version de projet non compatible : %1.").arg(version));

    const auto valueFor = [&project](const QString &camelCaseKey,
                                     const QString &legacyKey) {
        return project.contains(camelCaseKey) ? project.value(camelCaseKey)
                                              : project.value(legacyKey);
    };
    const auto requireStringIfPresent = [&project](const QString &key) {
        return !project.contains(key) || project.value(key).isString();
    };
    for (const QString &key : {QStringLiteral("projectName"), QStringLiteral("project_name"),
                               QStringLiteral("tileDirectory"), QStringLiteral("tile_directory"),
                               QStringLiteral("outputDirectory"), QStringLiteral("output_directory"),
                               QStringLiteral("referenceImage"), QStringLiteral("reference_image")}) {
        if (!requireStringIfPresent(key))
            return fail(QStringLiteral("Le champ « %1 » doit être une chaîne.").arg(key));
    }

    ProjectData loadedProject;
    loadedProject.projectName = valueFor(QStringLiteral("projectName"),
                                         QStringLiteral("project_name"))
                                    .toString(QFileInfo(path).completeBaseName());
    loadedProject.outputDirectory = absolutePath(
        valueFor(QStringLiteral("outputDirectory"), QStringLiteral("output_directory")).toString(),
        path);

    ProjectGrid::State &gridState = loadedProject.gridState;
    gridState.tileDirectory = absolutePath(
        valueFor(QStringLiteral("tileDirectory"), QStringLiteral("tile_directory")).toString(),
        path);
    if (!readInteger(project, QStringLiteral("mode"), QStringLiteral("mode"), 0, 0,
                     MODE_COUNT - 1,
                     &loadedProject.mode) ||
        !readInteger(project, QStringLiteral("gridRows"), QStringLiteral("grid_rows"), 3, 1, 20,
                     &gridState.gridRows) ||
        !readInteger(project, QStringLiteral("gridColumns"), QStringLiteral("grid_columns"), 2, 1,
                     20, &gridState.gridColumns) ||
        !readInteger(project, QStringLiteral("captureOrder"), QStringLiteral("capture_order"), 0,
                     0, 5, &gridState.captureOrder) ||
        !readInteger(project, QStringLiteral("horizontalOverlap"),
                     QStringLiteral("horizontal_overlap"), 20, 0, 90,
                     &gridState.horizontalOverlap) ||
        !readInteger(project, QStringLiteral("verticalOverlap"),
                     QStringLiteral("vertical_overlap"), 20, 0, 90,
                     &gridState.verticalOverlap))
        return fail(QStringLiteral("Les dimensions ou paramètres de grille sont invalides."));
    const int cellCount = gridState.gridRows * gridState.gridColumns;

    const auto readBool = [&project, &valueFor](const QString &camelCaseKey,
                                                const QString &legacyKey,
                                                bool fallback, bool *result) {
        const QString key = project.contains(camelCaseKey) ? camelCaseKey : legacyKey;
        if (!project.contains(key)) {
            *result = fallback;
            return true;
        }
        if (!valueFor(camelCaseKey, legacyKey).isBool())
            return false;
        *result = valueFor(camelCaseKey, legacyKey).toBool();
        return true;
    };
    if (!readBool(QStringLiteral("overlapsLinked"), QStringLiteral("overlaps_linked"), true,
                  &gridState.overlapsLinked) ||
        !readBool(QStringLiteral("automaticUncertainty"),
                  QStringLiteral("automatic_uncertainty"), true,
                  &gridState.automaticUncertainty) ||
        !readBool(QStringLiteral("gridValidated"), QStringLiteral("grid_validated"), false,
                  &gridState.gridValidated))
        return fail(QStringLiteral("Un paramètre booléen du projet est invalide."));
    if (gridState.overlapsLinked)
        gridState.verticalOverlap = gridState.horizontalOverlap;

    const QJsonValue modality = valueFor(QStringLiteral("modality"), QStringLiteral("modality"));
    if (!modality.isUndefined() && !modality.isString())
        return fail(QStringLiteral("Le champ « modality » doit être une chaîne."));
    gridState.modality = modality.toString(QStringLiteral("Infrarouge"));
    const auto readNonNegativeNumber = [&project, &valueFor](const QString &key,
                                                             const QString &legacyKey,
                                                             double *result) {
        const QString selectedKey = project.contains(key) ? key : legacyKey;
        if (!project.contains(selectedKey)) {
            *result = 0.0;
            return true;
        }
        const QJsonValue value = valueFor(key, legacyKey);
        const double number = value.toDouble(-1.0);
        if (!value.isDouble() || !std::isfinite(number) || number < 0.0)
            return false;
        *result = number;
        return true;
    };
    if (!readNonNegativeNumber(QStringLiteral("scaleMmPerPixel"),
                               QStringLiteral("scale_mm_per_pixel"),
                               &gridState.scaleMmPerPixel) ||
        !readNonNegativeNumber(QStringLiteral("uncertaintyMargin"),
                               QStringLiteral("uncertainty_margin"),
                               &gridState.uncertaintyMargin))
        return fail(QStringLiteral("L'échelle ou la marge d'incertitude est invalide."));
    gridState.referenceImagePath = absolutePath(
        valueFor(QStringLiteral("referenceImage"), QStringLiteral("reference_image")).toString(),
        path);

    const QJsonValue tileValues = project.value(QStringLiteral("tiles"));
    if (!tileValues.isUndefined() && !tileValues.isArray())
        return fail(QStringLiteral("Le champ « tiles » doit être un tableau."));
    QSet<QString> uniquePaths;
    for (const QJsonValue &tile : tileValues.toArray()) {
        if (!tile.isString() || tile.toString().isEmpty())
            return fail(QStringLiteral("Chaque entrée de « tiles » doit être un chemin non vide."));
        const QString resolved = absolutePath(tile.toString(), path);
        if (uniquePaths.contains(resolved))
            return fail(QStringLiteral("Le projet contient des chemins d'image en double : %1")
                            .arg(resolved));
        uniquePaths.insert(resolved);
        gridState.tilePaths.append(resolved);
    }

    const QJsonValue gridValue = valueFor(QStringLiteral("gridTiles"),
                                           QStringLiteral("grid_tiles"));
    if (!gridValue.isUndefined() && !gridValue.isArray())
        return fail(QStringLiteral("Le champ « gridTiles » doit être un tableau."));
    const QJsonArray grid = gridValue.toArray();
    if (!grid.isEmpty() && grid.size() != cellCount)
        return fail(QStringLiteral("La grille sauvegardée contient %1 cases, mais ses dimensions "
                                   "en définissent %2.")
                        .arg(grid.size()).arg(cellCount));
    if (version == QStringLiteral("2.0") && grid.size() != cellCount)
        return fail(QStringLiteral("La grille du projet est incomplète."));

    QSet<QString> gridPaths;
    for (const QJsonValue &cellValue : grid) {
        if (!cellValue.isObject())
            return fail(QStringLiteral("Une case de la grille n'est pas un objet."));
        const QJsonObject cell = cellValue.toObject();
        if (!cell.value(QStringLiteral("path")).isString() ||
            !cell.value(QStringLiteral("rotation")).isDouble() ||
            !cell.value(QStringLiteral("flipped")).isBool() ||
            !cell.value(QStringLiteral("empty")).isBool())
            return fail(QStringLiteral("Une case de la grille contient des paramètres invalides."));
        const double savedRotation = cell.value(QStringLiteral("rotation")).toDouble();
        if (!std::isfinite(savedRotation) || std::floor(savedRotation) != savedRotation ||
            static_cast<int>(savedRotation) % 90 != 0)
            return fail(QStringLiteral("La rotation d'une tuile doit être un multiple de 90°."));
        GridTile tile;
        tile.path = absolutePath(cell.value(QStringLiteral("path")).toString(), path);
        tile.rotation = ((static_cast<int>(savedRotation) % 360) + 360) % 360;
        tile.flipped = cell.value(QStringLiteral("flipped")).toBool();
        tile.empty = cell.value(QStringLiteral("empty")).toBool();
        if (!tile.path.isEmpty()) {
            if (gridPaths.contains(tile.path) || !uniquePaths.contains(tile.path))
                return fail(QStringLiteral("La grille contient un chemin dupliqué ou absent de "
                                          "« tiles » : %1").arg(tile.path));
            gridPaths.insert(tile.path);
        }
        gridState.gridTiles.append(tile);
    }
    if (!grid.isEmpty() && gridPaths != uniquePaths)
        return fail(QStringLiteral("Les chemins de la grille ne correspondent pas aux tuiles "
                                   "sauvegardées."));
    if (!readInteger(project, QStringLiteral("selectedGridCell"),
                     QStringLiteral("selected_grid_cell"), 0, 0,
                     std::max(0, cellCount - 1), &gridState.selectedGridCell))
        return fail(QStringLiteral("La cellule sélectionnée est hors de la grille."));

    Json expectedFingerprints = Json::object();
    const QJsonValue fingerprintsValue = project.value(QStringLiteral("sourceFingerprints"));
    if (!fingerprintsValue.isUndefined()) {
        if (!fingerprintsValue.isObject())
            return fail(QStringLiteral("Le champ « sourceFingerprints » doit être un objet."));
        const QJsonObject fingerprints = fingerprintsValue.toObject();
        for (auto it = fingerprints.begin(); it != fingerprints.end(); ++it) {
            if (!it.value().isString())
                return fail(QStringLiteral("Une empreinte de fichier est invalide."));
            expectedFingerprints[absolutePath(it.key(), path).toStdString()] =
                it.value().toString().toStdString();
        }
    }
    QStringList sourcePaths = gridState.tilePaths;
    if (!gridState.referenceImagePath.isEmpty())
        sourcePaths.append(gridState.referenceImagePath);
    sourcePaths.removeDuplicates();
    for (const QString &sourcePath : sourcePaths) {
        if (!QFileInfo(sourcePath).isFile()) {
            loadedProject.missingFiles.append(sourcePath);
            continue;
        }
        const auto expected = expectedFingerprints.find(sourcePath.toStdString());
        const QString actual = fingerprintForFile(sourcePath);
        if (expected == expectedFingerprints.end() || expected.value().get<std::string>().empty()) {
            loadedProject.unverifiedFiles.append(sourcePath);
        } else if (actual.isEmpty() ||
                   actual.toStdString() != expected.value().get<std::string>()) {
            loadedProject.modifiedFiles.append(sourcePath);
        }
    }

    const QJsonValue backendValue = project.value(QStringLiteral("backendScript"));
    if (!backendValue.isUndefined()) {
        if (!backendValue.isObject())
            return fail(QStringLiteral("Le champ « backendScript » doit être un objet."));
        try {
            loadedProject.backendScript =
                Json::parse(QJsonDocument(backendValue.toObject()).toJson(
                                QJsonDocument::Compact).toStdString());
            if (!loadedProject.backendScript.contains("pipeline") ||
                !loadedProject.backendScript["pipeline"].is_object() ||
                !loadedProject.backendScript["pipeline"].contains("steps") ||
                !loadedProject.backendScript["pipeline"]["steps"].is_array())
                return fail(QStringLiteral("La configuration backend du projet est invalide."));

            for (const Json &step : loadedProject.backendScript["pipeline"]["steps"]) {
                if (!step.is_object() || step.value("name", Json()).is_string() == false ||
                    step.value("name", "") != "import" || !step.contains("parameters") ||
                    !step["parameters"].is_array())
                    continue;
                for (const Json &parameter : step["parameters"]) {
                    if (!parameter.is_object() || parameter.value("name", "") != "tiles")
                        continue;
                    if (!parameter.contains("value") || !parameter["value"].is_string())
                        return fail(QStringLiteral("La liste explicite des tuiles backend est invalide."));
                    const Json explicitTiles = Json::parse(
                        parameter["value"].get<std::string>(), nullptr, false);
                    if (explicitTiles.is_discarded() || !explicitTiles.is_array())
                        return fail(QStringLiteral("La liste explicite des tuiles backend est invalide."));
                    if (explicitTiles.empty())
                        break;

                    std::map<std::string, std::pair<int, int>> expectedByPath;
                    for (int index = 0; index < gridState.gridTiles.size(); ++index) {
                        const GridTile &tile = gridState.gridTiles.at(index);
                        if (!tile.path.isEmpty() && !tile.empty)
                            expectedByPath[tile.path.toStdString()] = {
                                index / gridState.gridColumns, index % gridState.gridColumns};
                    }
                    std::map<std::string, std::pair<int, int>> actualByPath;
                    std::set<std::pair<int, int>> occupiedCells;
                    for (const Json &tile : explicitTiles) {
                        if (!tile.is_object() || !tile.contains("path") ||
                            !tile["path"].is_string() || !tile.contains("row") ||
                            !tile["row"].is_number_integer() || !tile.contains("column") ||
                            !tile["column"].is_number_integer())
                            return fail(QStringLiteral("Une tuile backend doit contenir path, row et column."));
                        const QString tilePath = absolutePath(
                            QString::fromStdString(tile["path"].get<std::string>()), path);
                        const int row = tile["row"].get<int>();
                        const int column = tile["column"].get<int>();
                        if (row < 0 || row >= gridState.gridRows || column < 0 ||
                            column >= gridState.gridColumns ||
                            !occupiedCells.insert({row, column}).second ||
                            !actualByPath.emplace(tilePath.toStdString(),
                                                  std::make_pair(row, column)).second)
                            return fail(QStringLiteral("La liste backend contient un doublon ou une position hors grille."));
                    }
                    if (!gridState.gridTiles.isEmpty() &&
                        actualByPath != expectedByPath)
                        return fail(QStringLiteral("Les tuiles du pipeline ne correspondent pas à la grille enregistrée."));
                    break;
                }
            }
        } catch (const std::exception &) {
            return fail(QStringLiteral("La configuration backend du projet est invalide."));
        }

        const Json runConfig = loadedProject.backendScript.value("run", Json::object());
        const Json sidecarPath = runConfig.is_object()
            ? runConfig.value("checkpointDirectory", Json()) : Json();
        if (sidecarPath.is_string()) {
            const QString checkpointPath = absolutePath(
                QString::fromStdString(sidecarPath.get<std::string>()), path);
            const fs::path runStatePath =
                fs::path(checkpointPath.toStdString()) / "run.json";
            std::ifstream runStateFile(runStatePath, std::ios::binary);
            if (runStateFile) {
                try {
                    const Json savedRun = Json::parse(runStateFile);
                    if (savedRun.is_object() &&
                        savedRun.value("pipeline", Json()) == loadedProject.backendScript["pipeline"] &&
                        savedRun.contains("run"))
                        loadedProject.backendScript["run"] = savedRun["run"];
                    else
                        loadedProject.backendScriptError =
                            QStringLiteral("L'état run.json ne correspond pas au pipeline du projet.");
                } catch (const std::exception &) {
                    loadedProject.backendScriptError =
                        QStringLiteral("Le fichier run.json est absent ou corrompu.");
                }
            }
        }
    }

    *projectData = std::move(loadedProject);
    return true;
}

ProjectResumeSummary ProjectSerializer::summarize(const ProjectData &projectData)
{
    ProjectResumeSummary summary;
    summary.missingFiles = projectData.missingFiles;
    summary.modifiedFiles = projectData.modifiedFiles;
    summary.unverifiedFiles = projectData.unverifiedFiles;
    if (!projectData.backendScriptError.isEmpty())
        summary.issues.append(projectData.backendScriptError);

    const QFileInfo outputInfo(projectData.outputDirectory);
    summary.outputDirectory = projectData.outputDirectory;
    summary.outputDirectoryAccessible = outputInfo.isDir() && outputInfo.isWritable();
    if (projectData.backendScript.is_null() || projectData.backendScript.empty()) {
        summary.issues.append(QStringLiteral("Ce projet ne contient pas de configuration de pipeline."));
        summary.algorithmsAvailable = false;
        return summary;
    }

    const Json runJson = projectData.backendScript.value("run", Json::object());
    if (runJson.is_object()) {
        const Json status = runJson.value("status", Json("idle"));
        if (status.is_string())
            summary.runStatus = QString::fromStdString(status.get<std::string>());
        const Json stepResults = runJson.value("stepResults", Json::object());
        if (stepResults.is_object()) {
            for (auto it = stepResults.begin(); it != stepResults.end(); ++it) {
                QString value = QString::fromStdString(it.value().dump());
                if (value.size() > 500)
                    value = value.left(497) + QStringLiteral("...");
                summary.availableResults.append(
                    QStringLiteral("%1 : %2")
                        .arg(QString::fromStdString(it.key()), value));
            }
        }
        const Json failedStep = runJson.value("failedStep", Json(""));
        const Json error = runJson.value("error", Json(""));
        if (failedStep.is_string() && error.is_string() &&
            !failedStep.get<std::string>().empty())
            summary.errors.append(
                QStringLiteral("%1 : %2")
                    .arg(QString::fromStdString(failedStep.get<std::string>()),
                         QString::fromStdString(error.get<std::string>())));
    }

    try {
        PipelineNs::PipelineClass pipeline =
            PipelineNs::buildPipeline(projectData.backendScript);
        const auto &steps = pipeline.getSteps();
        const auto &completed = pipeline.getRunState().completedSteps;
        const auto checkpointDirectory = pipeline.getRunState().checkpointDirectory;
        CheckpointNs::CheckpointClass checkpoints(checkpointDirectory);
        bool firstIncomplete = false;
        const bool sourceRequiresReimport = !summary.missingFiles.isEmpty() ||
            !summary.modifiedFiles.isEmpty() || !summary.unverifiedFiles.isEmpty();
        for (std::size_t index = 0; index < steps.size(); ++index) {
            const std::string name = steps[index]->getConfigJson().value("name", "");
            if (index >= completed.size() || completed[index] != name) {
                summary.neverRunSteps.append(QString::fromStdString(name));
                firstIncomplete = true;
            } else if (!firstIncomplete && !sourceRequiresReimport &&
                       checkpoints.valid(index, *steps[index])) {
                summary.upToDateSteps.append(QString::fromStdString(name));
            } else {
                summary.stepsToVerify.append(QString::fromStdString(name));
                firstIncomplete = true;
            }
        }
    } catch (const std::exception &exception) {
        summary.algorithmsAvailable = false;
        summary.issues.append(QString::fromUtf8(exception.what()));
        const Json &steps = projectData.backendScript["pipeline"]["steps"];
        const Json savedCompleted = runJson.is_object()
            ? runJson.value("completedSteps", Json::array()) : Json::array();
        const Json completed = savedCompleted.is_array() ? savedCompleted : Json::array();
        for (std::size_t index = 0; index < steps.size(); ++index) {
            const Json &step = steps[index];
            const Json nameValue = step.is_object()
                ? step.value("name", Json("")) : Json("");
            const std::string name = nameValue.is_string()
                ? nameValue.get<std::string>() : std::string();
            if (name.empty())
                continue;
            if (index < completed.size() && completed[index].is_string() &&
                completed[index].get<std::string>() == name)
                summary.stepsToVerify.append(QString::fromStdString(name));
            else
                summary.neverRunSteps.append(QString::fromStdString(name));
        }
    }
    return summary;
}

bool ProjectSerializer::saveToFile(const QString &path, const QString &projectName,
                                   const QString &outputDirectory, int mode,
                                   const ProjectGrid::State &gridState,
                                   QString *errorMessage,
                                   const nlohmann::json &backendScript)
{
    if (mode < 0 || mode >= MODE_COUNT) {
        if (errorMessage)
            *errorMessage = QStringLiteral("La page de travail du projet est invalide.");
        return false;
    }

    QJsonObject project;
    project.insert(QStringLiteral("formatVersion"), QStringLiteral("2.0"));
    project.insert(QStringLiteral("projectName"), projectName);
    project.insert(QStringLiteral("savedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    project.insert(QStringLiteral("mode"), mode);
    project.insert(QStringLiteral("tileDirectory"), gridState.tileDirectory);
    project.insert(QStringLiteral("outputDirectory"), outputDirectory);
    project.insert(QStringLiteral("gridRows"), gridState.gridRows);
    project.insert(QStringLiteral("gridColumns"), gridState.gridColumns);
    project.insert(QStringLiteral("captureOrder"), gridState.captureOrder);
    project.insert(QStringLiteral("selectedGridCell"), gridState.selectedGridCell);
    project.insert(QStringLiteral("horizontalOverlap"), gridState.horizontalOverlap);
    project.insert(QStringLiteral("verticalOverlap"), gridState.verticalOverlap);
    project.insert(QStringLiteral("overlapsLinked"), gridState.overlapsLinked);
    project.insert(QStringLiteral("modality"), gridState.modality);
    project.insert(QStringLiteral("scaleMmPerPixel"), gridState.scaleMmPerPixel);
    project.insert(QStringLiteral("uncertaintyMargin"), gridState.uncertaintyMargin);
    project.insert(QStringLiteral("automaticUncertainty"), gridState.automaticUncertainty);
    project.insert(QStringLiteral("gridValidated"), gridState.gridValidated);
    project.insert(QStringLiteral("referenceImage"), gridState.referenceImagePath);

    QJsonArray tiles;
    QJsonObject fingerprints;
    QStringList sourcePaths = gridState.tilePaths;
    if (!gridState.referenceImagePath.isEmpty())
        sourcePaths.append(gridState.referenceImagePath);
    sourcePaths.removeDuplicates();
    for (const QString &tile : gridState.tilePaths)
        tiles.append(tile);
    for (const QString &sourcePath : sourcePaths) {
        const QString resolvedPath = QFileInfo(sourcePath).absoluteFilePath();
        const QString fingerprint = fingerprintForFile(resolvedPath);
        if (!fingerprint.isEmpty())
            fingerprints.insert(QDir::cleanPath(resolvedPath), fingerprint);
    }
    project.insert(QStringLiteral("tiles"), tiles);
    project.insert(QStringLiteral("sourceFingerprints"), fingerprints);

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
    if (!backendScript.is_null() && !backendScript.empty()) {
        const QByteArray scriptBytes = QByteArray::fromStdString(backendScript.dump());
        const QJsonDocument scriptDocument = QJsonDocument::fromJson(scriptBytes);
        if (!scriptDocument.isObject()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("La configuration du pipeline n'est pas un objet JSON.");
            return false;
        }
        project.insert(QStringLiteral("backendScript"), scriptDocument.object());
    }

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
