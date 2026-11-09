#ifndef PROJECT_SERIALIZER_H
#define PROJECT_SERIALIZER_H

#include "projectgrid.h"

#include <nlohmann/json.hpp>

#include <QString>
#include <QStringList>

#include <map>

struct ProjectData
{
    QString projectName;
    QString outputDirectory;
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

#endif
