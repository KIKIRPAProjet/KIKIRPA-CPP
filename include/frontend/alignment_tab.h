#pragma once

#include <QMap>
#include <QString>
#include <QWidget>
#include <QVector>
#include "frontendutils.h"

class MainWindow;
class ArtworkCanvas;
class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QVBoxLayout;
class QWidget;

// Onglet « Paire » : le tableau de toutes les paires calculées (état, points, écart), triable, puis la fiche de la paire
// choisie (explication en clair, mesures) avec ses deux actions, « Corriger… » et « Valider ».
class AlignmentTab : public QWidget
{
    Q_OBJECT
public:
    explicit AlignmentTab(MainWindow *mainWindow, ArtworkCanvas *canvas, QWidget *parent = nullptr);

    void setPairs(const QVector<FrontendUtils::PairAlignmentInfo> &pairs, int currentPairIndex);
    void setCurrentPair(const FrontendUtils::PairAlignmentInfo &pair);

signals:
    void pairSelectionRequested(int pairIndex);
    void nextDubiousRequested();
    void pairValidated(int pairIndex);
    void manualOffsetApplied(int pairIndex, double dx, double dy);

private:
    void createSummary(QVBoxLayout *layout);
    void createTable(QVBoxLayout *layout);
    void createDetails(QVBoxLayout *layout);
    void rebuildTable();
    void fillRow(int row, const FrontendUtils::PairAlignmentInfo &pair);
    void selectCurrentRow();
    void showManualAdjustmentDialog();
    QString shortPairLabel(const FrontendUtils::PairAlignmentInfo &pair) const;

    MainWindow *m_mainWindow;
    ArtworkCanvas *m_canvas;

    QVector<FrontendUtils::PairAlignmentInfo> m_pairs;
    int m_currentPairIndex = 0;
    QMap<QString, QString> m_shortTileNames;   // nom complet -> nom sans la partie commune à toutes les tuiles

    QLabel *m_summaryLabel = nullptr;
    QCheckBox *m_toCheckOnly = nullptr;
    QPushButton *m_nextToCheckButton = nullptr;
    QTableWidget *m_table = nullptr;

    QWidget *m_details = nullptr;
    QLabel *m_pairTitleLabel = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_explanationLabel = nullptr;
    QLabel *m_offsetValueLabel = nullptr;
    QLabel *m_pointsValueLabel = nullptr;
    QLabel *m_errorValueLabel = nullptr;
    QLabel *m_correctionTitleLabel = nullptr;
    QLabel *m_correctionValueLabel = nullptr;
    QPushButton *m_correctButton = nullptr;
    QPushButton *m_validateButton = nullptr;
};
