#ifndef BOTTOM_TABS_H
#define BOTTOM_TABS_H

#include <QTabWidget>

class MainWindow;
class QTableWidget;
class QListWidget;

class BottomTabs : public QTabWidget
{
    Q_OBJECT
public:
    explicit BottomTabs(MainWindow *mainWindow, QWidget *parent = nullptr);
    void appendLog(const QString &message);

private:
    MainWindow *m_mainWindow;
    QTableWidget *m_pairsTable = nullptr;
    QListWidget *m_journalList = nullptr;
};

#endif 