#include "Frontend/workspace/workspace_status_bar.h"
#include "Frontend/app/mainwindow.h"
#include "Frontend/common/frontendutils.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>

WorkspaceStatusBar::WorkspaceStatusBar(MainWindow *mainWindow, QWidget *parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    auto *statusLayout = new QHBoxLayout(this);
    statusLayout->setContentsMargins(10, 3, 10, 3);

    statusLayout->addWidget(FrontendUtils::mutedLabel(tr("x 1 812  y 2 310 px    Tuile r00_c01"), this));
    statusLayout->addStretch();
    m_statusLabel = new QLabel(tr("Affichage ajusté (contraste)"), this);
    statusLayout->addWidget(m_statusLabel);

    // Ces deux widgets vivent dans la barre d'état de la fenêtre principale.
    m_currentStepLabel = new QLabel(tr("Aucune étape"), m_mainWindow->statusBar());
    m_progressBar = new QProgressBar(m_mainWindow->statusBar());
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setFixedWidth(160);

    m_mainWindow->statusBar()->addPermanentWidget(m_currentStepLabel);
    m_mainWindow->statusBar()->addPermanentWidget(m_progressBar);
}
