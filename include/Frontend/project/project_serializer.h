#ifndef PROJECT_SERIALIZER_H
#define PROJECT_SERIALIZER_H

#include "Frontend/project/projectgrid.h"

#include <QString>

struct ProjectData
{
    QString projectName;
    QString outputDirectory;
    ProjectGrid::State gridState;
};

class ProjectSerializer
{
public:
    static bool loadFromFile(const QString &path, ProjectData *projectData,
                             QString *errorMessage = nullptr);
    static bool saveToFile(const QString &path, const QString &projectName,
                           const QString &outputDirectory, int mode,
                           const ProjectGrid::State &gridState,
                           QString *errorMessage = nullptr);
};

#endif
