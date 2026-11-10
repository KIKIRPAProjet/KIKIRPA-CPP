#ifndef ASSEMBLY_TAB_H
#define ASSEMBLY_TAB_H

#include <QWidget>

class MainWindow;

class AssemblyTab : public QWidget
{
    Q_OBJECT
public:
    explicit AssemblyTab(MainWindow *mainWindow, QWidget *parent = nullptr);

private:
    MainWindow *m_mainWindow;
};

#endif 