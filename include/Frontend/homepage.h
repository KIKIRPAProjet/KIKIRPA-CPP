/**
 * @brief page d'accueil de l'application, permettant de créer un nouveau projet ou d'ouvrir un projet existant
 */
#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include <QJsonObject>
#include <QStringList>
#include <QWidget>

class QHBoxLayout;
class QLabel;

class HomePage : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr);

public slots:
    void createProject();
    void openProject();
    void rememberRecentProject(const QString &path);
    void setCurrentProjectInfo(bool created, const QString &path,
                               const QString &name, int tileCount);

signals:
    void projectCreationRequested(const QString &name, const QString &tileDirectory,
                                  const QStringList &tilePaths);
    void projectLoaded(const QString &path, const QJsonObject &project);
    void currentProjectRequested();

private:
    void refreshRecentProjects();
    bool loadProjectFromPath(const QString &path);

    QHBoxLayout *recentCardsLayout = nullptr;
    QLabel *recentEmptyLabel = nullptr;
    bool currentProjectCreated = false;
    QString currentProjectPath;
    QString currentProjectName;
    int currentTileCount = 0;
};

#endif
