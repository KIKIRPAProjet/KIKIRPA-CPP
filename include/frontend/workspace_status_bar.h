#ifndef WORKSPACE_STATUS_BAR_H
#define WORKSPACE_STATUS_BAR_H

#include <QWidget>

class MainWindow;
class QLabel;
class QProgressBar;

class WorkspaceStatusBar : public QWidget
{
    Q_OBJECT
public:
    explicit WorkspaceStatusBar(MainWindow *mainWindow, QWidget *parent = nullptr);

    QLabel *statusLabel() const { return m_statusLabel; }
    QLabel *currentStepLabel() const { return m_currentStepLabel; }
    QProgressBar *progressBar() const { return m_progressBar; }

private:
    MainWindow *m_mainWindow;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_currentStepLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
};

#endif // WORKSPACE_STATUS_BAR_H