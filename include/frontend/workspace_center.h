#ifndef WORKSPACE_CENTER_H
#define WORKSPACE_CENTER_H

#include <QWidget>
#include "projectgrid.h"

class MainWindow;
class QLabel;
class ArtworkCanvas;
class QLineEdit;

class WorkspaceCenter : public QWidget
{
    Q_OBJECT
public:
    explicit WorkspaceCenter(MainWindow *mainWindow, QWidget *parent = nullptr);

    ArtworkCanvas *canvas() const { return m_canvas; }
    QWidget *projectInfoPanel() const { return m_projectInfoPanel; }
    QLabel *zoomLabel() const { return m_zoomLabel; }

    void updateProjectInfo(const QString &projectName, const ProjectGrid::State &gridState);
    void setResumeSummary(const QString &summary);

private:
    void createProjectMode();
    void createCanvasAndControls();

    MainWindow *m_mainWindow;
    ArtworkCanvas *m_canvas = nullptr;
    QWidget *m_projectInfoPanel = nullptr;

    QLineEdit *m_projectNameEdit = nullptr;
    QLabel *m_projectFolderLabel = nullptr;
    QLabel *m_outputDirectoryLabel = nullptr;
    QLabel *m_projectFormatLabel = nullptr;
    QLabel *m_resumeSummaryLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
};

#endif // WORKSPACE_CENTER_H