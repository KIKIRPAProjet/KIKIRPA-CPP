#ifndef ASSEMBLY_TAB_H
#define ASSEMBLY_TAB_H

#include <QWidget>

class MainWindow;
class QListWidget;

class AssemblyTab : public QWidget
{
    Q_OBJECT
public:
    explicit AssemblyTab(MainWindow *mainWindow, QWidget *parent = nullptr);
    void setActiveStep(int stepNumber);

private:
    MainWindow *m_mainWindow;
    QListWidget *m_stepList = nullptr;
    int m_activeStep = 0;
};

#endif 