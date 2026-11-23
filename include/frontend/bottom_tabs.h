#ifndef BOTTOM_TABS_H
#define BOTTOM_TABS_H

#include <QTabWidget>
#include <QVector>
#include "frontendutils.h"

class MainWindow;
class QTableWidget;
class QListWidget;

class BottomTabs : public QTabWidget
{
    Q_OBJECT
public:
    explicit BottomTabs(MainWindow *mainWindow, QWidget *parent = nullptr);

    void appendLog(const QString &message);
    void updatePairs(const QVector<FrontendUtils::PairAlignmentInfo> &pairs);
    void selectPairRow(int pairIndex);

signals:
    void pairSelected(int pairIndex);

private:
    MainWindow *m_mainWindow;
    QTableWidget *m_pairsTable = nullptr;
    QListWidget *m_journalList = nullptr;
    int m_pairsTabIndex = -1;
};

#endif
