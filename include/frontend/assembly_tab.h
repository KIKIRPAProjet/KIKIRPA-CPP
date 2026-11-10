#ifndef ASSEMBLY_TAB_H
#define ASSEMBLY_TAB_H

#include <QStringList>
#include <QWidget>

class MainWindow;
class QLabel;
class QListWidget;

class AssemblyTab : public QWidget
{
    Q_OBJECT
public:
    explicit AssemblyTab(MainWindow *mainWindow, QWidget *parent = nullptr);
    void setActiveStep(int stepNumber);
    // Affiche l'état sauvegardé du projet : ✓ étapes faites, ▶ étape où le calcul reprendra, ○ étapes suivantes.
    // stepNames vide : aucun calcul sauvegardé.
    void showSavedState(const QStringList &stepNames, int savedStepCount);

private:
    MainWindow *m_mainWindow;
    QListWidget *m_stepList = nullptr;
    QLabel *m_savedStateLabel = nullptr;
    int m_activeStep = 0;
};

#endif 