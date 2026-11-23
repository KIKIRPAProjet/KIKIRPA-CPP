// include/frontend/project_serializer.h
#pragma once

#include "projectgrid.h"

#include <QString>
#include <QStringList>
#include <nlohmann/json.hpp>

struct ProjectData
{
    QString projectName;
    QString outputDirectory;
    ProjectGrid::State gridState;
    int mode = 0;
    nlohmann::json backendScript;
};

// État consultable avant de lancer un calcul ; les checkpoints sont contrôlés sans les modifier.
struct ProjectResumeSummary
{
    QStringList upToDateSteps;
    QStringList stepsToVerify;
    QStringList neverRunSteps;
    QStringList issues;
    QString runStatus;
    bool canResume = false;
    QString text() const;
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
                           const nlohmann::json &backendScript = nullptr);
};
