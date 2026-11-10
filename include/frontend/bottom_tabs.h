#ifndef BOTTOM_TABS_H
#define BOTTOM_TABS_H

#include <QTabWidget>
#include <QVector>
#include "frontendutils.h"

class MainWindow;
class QTableWidget;

class BottomTabs : public QTabWidget
{
    Q_OBJECT
public:
    explicit BottomTabs(MainWindow *mainWindow, QWidget *parent = nullptr);

    void updatePairs(const QVector<FrontendUtils::PairAlignmentInfo> &pairs);
    void selectPairRow(int pairIndex);

signals:
    void pairSelected(int pairIndex);

private:
    MainWindow *m_mainWindow;
    QTableWidget *m_pairsTable = nullptr;
    int m_pairsTabIndex = -1;
};

#endif
