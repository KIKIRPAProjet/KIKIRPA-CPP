#include "workspace_status_bar.h"
#include "mainwindow.h"
#include "frontendutils.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>


WorkspaceStatusBar::WorkspaceStatusBar(MainWindow *mainWindow, QWidget *parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    auto *statusLayout = new QHBoxLayout(this);
    statusLayout->setContentsMargins(10, 3, 10, 3);

    m_pairViewLabel = FrontendUtils::mutedLabel(
        tr("Aucune paire active"), this);
    statusLayout->addWidget(m_pairViewLabel);
    statusLayout->addSpacing(12);

    m_statusLabel = new QLabel(tr("Prêt"), this);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();

    m_dubiousSummaryLabel = new QLabel(tr("Aucune paire analysée"), this);
    m_dubiousSummaryLabel->setStyleSheet(QStringLiteral("color:#999999; font-weight:600;"));
    statusLayout->addWidget(m_dubiousSummaryLabel);

    // Ces deux widgets vivent dans la barre d'état de la fenêtre principale.
    m_currentStepLabel = new QLabel(tr("Aucune étape"), m_mainWindow->statusBar());
    m_progressBar = new QProgressBar(m_mainWindow->statusBar());
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedWidth(160);

    auto *progressPercentLabel = new QLabel(QStringLiteral("0 %"), m_mainWindow->statusBar());
    progressPercentLabel->setMinimumWidth(36);
    progressPercentLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_progressBar, &QProgressBar::valueChanged, progressPercentLabel,
            [progressPercentLabel](int value) {
                progressPercentLabel->setText(QStringLiteral("%1 %").arg(value));
            });

    m_mainWindow->statusBar()->addPermanentWidget(m_currentStepLabel);
    m_mainWindow->statusBar()->addPermanentWidget(m_progressBar);
    m_mainWindow->statusBar()->addPermanentWidget(progressPercentLabel);
}

//mise à jour de l'indicateur de position
void WorkspaceStatusBar::updatePairSummary(int currentPairOneBased, int totalPairs, int dubiousCount, int failedCount)
{
    if (m_pairViewLabel) {
        m_pairViewLabel->setText(tr("Vue Paire · %1 / %2")
                                     .arg(currentPairOneBased)
                                     .arg(totalPairs));
    }
    if (m_dubiousSummaryLabel) {
        m_dubiousSummaryLabel->setText(
            tr("%1 paire(s) douteuse(s), %2 en échec").arg(dubiousCount).arg(failedCount));
        m_dubiousSummaryLabel->setStyleSheet(
            (dubiousCount > 0 || failedCount > 0)
                ? QStringLiteral("color:#e69138; font-weight:600;")
                : QStringLiteral("color:#66c98a; font-weight:600;"));
    }
}
