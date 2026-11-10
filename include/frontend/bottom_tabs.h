#ifndef BOTTOM_TABS_H
#define BOTTOM_TABS_H

#include <QTabWidget>

class MainWindow;
class QTableWidget;

class BottomTabs : public QTabWidget
{
    Q_OBJECT
public:
    explicit BottomTabs(MainWindow *mainWindow, QWidget *parent = nullptr);

private:
    MainWindow *m_mainWindow;
    QTableWidget *m_pairsTable = nullptr; 
};

#endif 