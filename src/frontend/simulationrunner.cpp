/*
 * .cpp : le POURQUOI
 * - choix de conception : un QTimer simule l'avancement sans bloquer la boucle d'événements ;
 * - cas limite : un second lancement est refusé tant que le premier n'est pas terminé ;
 * - unité ou repère : la progression est exprimée en pourcentage de 0 à 100 ;
 * - raison non évidente : les signaux permettent à l'interface de rester réactive pendant l'exécution.
 */
/*
 * Header : le QUOI
 * Simule le lancement et le suivi d'une exécution.
 * - ce que ça fait : démarre une progression temporisée et informe l'interface de son avancement ;
 * - ce que ça renvoie : true ou false selon l'état de l'exécution, avec des signaux de progression ;
 * - quand ça lève une exception : si la création ou le démarrage du minuteur échoue.
 */
//  simule le lancement et le suivi dune execution
#include "simulationrunner.h"

#include <QTimer>
#include <QImage>

SimulationRunner::SimulationRunner(QObject *parent)
    : QObject(parent)
{
}

bool SimulationRunner::isRunning() const
{
    return running;
}

void SimulationRunner::startRun(int firstStep)
{
    if (running) {
        emit statusChanged(tr("Une démonstration de progression est déjà en cours."));
        return;
    }

    const QStringList STEPS = {
        tr("1. Import"),
        tr("2. Masques"),
        tr("3. Correspondance"),
        tr("4. Optimisation"),
        tr("5. Calage local"),
        tr("6. Égalisation"),
        tr("7. Ligne de couture"),
        tr("8. Fusion et export")
    };

    firstStepNumber = firstStep;
    progress = 0;
    running = true;

    // L'interface envoie un firstStep qui commence à 1, on le convertit en index 0
    int startIndex = qBound(0, firstStep - 1, STEPS.size() - 1);

    emit runStarted();
    emit stepChanged(startIndex + 1, STEPS.at(startIndex) + " (demo)");
    emit statusChanged(
        tr("Démonstration de progression — le backend de calcul n'est pas connecté."));

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this, STEPS, startIndex]() {
        progress = qMin(100, progress + 4);
        emit progressAdvanced(progress);

        int stepsToRun = STEPS.size() - startIndex;
        int currentIndex = startIndex + ((progress * stepsToRun) / 100);
        currentIndex = qMin(STEPS.size() - 1, currentIndex);

        emit logMessage(tr("Calcul de l'étape %1 à %2%...").arg(STEPS.at(currentIndex)).arg(progress));
        emit stepChanged(currentIndex + 1, STEPS.at(currentIndex) + " (demo)");

        if (progress >= 100)
            finishRun();
    });
    timer->start(100);

    QTimer::singleShot(150, this, [this]() {
        if (running && timer && timer->isActive())
            emit cancellationAvailable();
    });
}

void SimulationRunner::cancelRun()
{
    if (!running)
        return;

    running = false;
    if (timer) {
        timer->stop();
        timer->deleteLater();
        timer = nullptr;
    }
    emit stepChanged(0, tr("Annulée"));
    emit statusChanged(tr("Démonstration annulée."));
    emit runCancelled();
}

void SimulationRunner::showComparison()
{
    emit comparisonRequested();
}

void SimulationRunner::toggleComparison()
{
    comparisonVisible = !comparisonVisible;
    emit comparisonVisibilityChanged(comparisonVisible);
    emit statusChanged(comparisonVisible
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
    if (timer) {
        timer->stop();
        timer->deleteLater();
        timer = nullptr;
    }
    running = false;
    emit stepChanged(0, tr("Terminé"));
    emit statusChanged(
        tr("Démonstration terminée (aucun calcul d'image effectué). Étape de départ : %1.")
            .arg(firstStepNumber));

    // Création d'une fausse image de mosaïque
    QImage dummyImage(800, 600, QImage::Format_RGB32);
    dummyImage.fill(Qt::green);
    emit mosaicReady(dummyImage);

    emit runFinished();
}
