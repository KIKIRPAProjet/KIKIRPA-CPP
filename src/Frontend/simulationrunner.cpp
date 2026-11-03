//  simule le lancement et le suivi dune execution
#include "simulationrunner.h"

#include <QTimer>

SimulationRunner::SimulationRunner(QObject *parent)
    : QObject(parent)
{
}

bool SimulationRunner::isRunning() const
{
    return m_running;
}

void SimulationRunner::startRun(int firstStep)
{
    if (m_running) {
        emit statusChanged(tr("Une démonstration de progression est déjà en cours."));
        return;
    }

    m_firstStep = firstStep;
    m_progress = 0;
    m_running = true;
    emit runStarted();
    emit stepChanged(tr("Démonstration"));
    emit statusChanged(
        tr("Démonstration de progression — le backend de calcul n'est pas connecté."));

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        m_progress = qMin(100, m_progress + 4);
        emit progressAdvanced(m_progress);
        if (m_progress >= 100)
            finishRun();
    });
    m_timer->start(100);

    QTimer::singleShot(150, this, [this]() {
        if (m_running && m_timer && m_timer->isActive())
            emit cancellationAvailable();
    });
}

void SimulationRunner::cancelRun()
{
    if (!m_running)
        return;

    m_running = false;
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }
    emit stepChanged(tr("Annulée"));
    emit statusChanged(tr("Démonstration annulée."));
    emit runCancelled();
}

void SimulationRunner::showComparison()
{
    emit comparisonRequested();
}

void SimulationRunner::toggleComparison()
{
    m_comparisonVisible = !m_comparisonVisible;
    emit comparisonVisibilityChanged(m_comparisonVisible);
    emit statusChanged(m_comparisonVisible
        ? tr("Comparaison affichée — curseur de séparation actif.")
        : tr("Comparaison côte à côte affichée."));
}

void SimulationRunner::keepComparisonA()
{
    emit statusChanged(tr("Résultat A conservé; les étapes suivantes sont à refaire."));
}

void SimulationRunner::keepComparisonB()
{
    emit statusChanged(tr("Résultat B conservé; les étapes suivantes sont à refaire."));
}

void SimulationRunner::finishRun()
{
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }
    m_running = false;
    emit stepChanged(tr("Terminé"));
    emit statusChanged(
        tr("Démonstration terminée (aucun calcul d'image effectué). Étape de départ : %1.")
            .arg(m_firstStep));
    emit runFinished();
}
