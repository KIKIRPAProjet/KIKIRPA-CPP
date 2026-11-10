// include/frontend/project_serializer.h
#pragma once

#include "projectgrid.h"

#include <nlohmann/json.hpp>

#include <QString>
#include <QStringList>

// État d'un projet chargé, avec les paramètres de grille, le pipeline et les fichiers source à vérifier.
struct ProjectData
{
    QString projectName;
    QString outputDirectory;
    // Page de travail affichée lors du dernier enregistrement, entre 0 et 5.
    int mode = 0;
    ProjectGrid::State gridState;
    nlohmann::json backendScript;
    QStringList missingFiles;
    QStringList modifiedFiles;
    QStringList unverifiedFiles;
    QString backendScriptError;
};

struct ProjectResumeSummary
{
    QStringList upToDateSteps;
    QStringList stepsToVerify;
    QStringList neverRunSteps;
    QStringList missingFiles;
    QStringList modifiedFiles;
    QStringList unverifiedFiles;
    QStringList availableResults;
    QStringList errors;
    QStringList issues;
    QString outputDirectory;
    QString runStatus;
    bool outputDirectoryAccessible = false;
    bool algorithmsAvailable = true;

    // Indique si les prérequis permettent une reprise du pipeline.
    bool canResume() const;
    // Produit le résumé lisible des étapes, résultats et problèmes détectés.
    QString toText() const;
};

// Charge, valide, résume et enregistre les projets frontend au format JSON.
class ProjectSerializer
{
public:
    // Charge le projet et signale les erreurs de format ou de cohérence via errorMessage.
    static bool loadFromFile(const QString &path, ProjectData *projectData,
                             QString *errorMessage = nullptr);
    // Vérifie les checkpoints, la disponibilité des étapes, les fichiers source et le dossier de sortie.
    static ProjectResumeSummary summarize(const ProjectData &projectData);
    // Enregistre le projet de façon atomique, avec son état de grille et sa configuration backend.
    static bool saveToFile(const QString &path, const QString &projectName,
                           const QString &outputDirectory, int mode,
                           const ProjectGrid::State &gridState,
                           QString *errorMessage = nullptr,
                           const nlohmann::json &backendScript = {});
};
