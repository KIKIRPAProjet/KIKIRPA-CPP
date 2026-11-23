#ifndef ALIGNMENT_TAB_H
#define ALIGNMENT_TAB_H

#include <QWidget>
#include <QVector>
#include "projectgrid.h"
#include "frontendutils.h"

class MainWindow;
class ArtworkCanvas;
class QLabel;
class QComboBox;
class QCheckBox;
class QPushButton;

class AlignmentTab : public QWidget
{
    Q_OBJECT
public:
    explicit AlignmentTab(MainWindow *mainWindow, ArtworkCanvas *canvas, QWidget *parent = nullptr);

    void updateInfo(const QString &projectName, const ProjectGrid::State &gridState);
    void setPairs(const QVector<FrontendUtils::PairAlignmentInfo> &pairs, int currentPairIndex);
    void setCurrentPair(const FrontendUtils::PairAlignmentInfo &pair);

signals:
    void pairSelectionRequested(int pairIndex);
    void nextDubiousRequested();
    void pairValidated(int pairIndex);
    void manualOffsetApplied(int pairIndex, double dx, double dy);

private:
    void showManualAdjustmentDialog();
    void rebuildPairSelector();

    MainWindow *m_mainWindow;
    ArtworkCanvas *m_canvas;

    QVector<FrontendUtils::PairAlignmentInfo> m_pairs;
    int m_currentPairIndex = 0;

    QComboBox *m_pairCombo = nullptr;
    QCheckBox *m_onlyDubiousCheck = nullptr;
    QPushButton *m_nextDubiousButton = nullptr;

    QLabel *m_pairTitleLabel = nullptr;
    QLabel *m_statusBadgeLabel = nullptr;
    QLabel *m_reasonValueLabel = nullptr;
    QLabel *m_confidenceValueLabel = nullptr;
    QLabel *m_offsetValueLabel = nullptr;
    QLabel *m_pointsShownValueLabel = nullptr;
    QLabel *m_errorValueLabel = nullptr;
    QLabel *m_interventionValueLabel = nullptr;
    QPushButton *m_validateButton = nullptr;

    QLabel *m_projectNameLabel = nullptr;
    QLabel *m_tileCountLabel = nullptr;
    QLabel *m_projectMetadataLabel = nullptr;
};

#endif