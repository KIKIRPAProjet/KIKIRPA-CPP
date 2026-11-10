#ifndef ALIGNMENT_TAB_H
#define ALIGNMENT_TAB_H

#include <QWidget>
#include "projectgrid.h" 

class MainWindow;
class ArtworkCanvas;
class QLabel;

class AlignmentTab : public QWidget
{
    Q_OBJECT
public:
    explicit AlignmentTab(MainWindow *mainWindow, ArtworkCanvas *canvas, QWidget *parent = nullptr);
    
    void updateInfo(const QString &projectName, const ProjectGrid::State &gridState);

private:
    void showManualAdjustmentDialog();

    MainWindow *m_mainWindow;
    ArtworkCanvas *m_canvas;

    QLabel *m_projectNameLabel = nullptr;
    QLabel *m_tileCountLabel = nullptr;
    QLabel *m_projectMetadataLabel = nullptr;
};

#endif 