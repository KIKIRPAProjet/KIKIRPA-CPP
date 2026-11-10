// include/frontend/project_serializer.h
#pragma once

#include "projectgrid.h"

#include <nlohmann/json.hpp>

#include <QString>
#include <QStringList>

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

    bool canResume() const;
    QString toText() const;
};

class ProjectSerializer
{
public:
    static bool loadFromFile(const QString &path, ProjectData *projectData,
                             QString *errorMessage = nullptr);
    static ProjectResumeSummary summarize(const ProjectData &projectData);
    static bool saveToFile(const QString &path, const QString &projectName,
                           const QString &outputDirectory, int mode,
                           const ProjectGrid::State &gridState,
                           QString *errorMessage = nullptr,
                           const nlohmann::json &backendScript = {});
};
