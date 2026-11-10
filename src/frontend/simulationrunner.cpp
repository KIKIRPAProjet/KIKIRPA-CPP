/*
 * .cpp : le POURQUOI
 * - choix de conception : un PipelineWorkerClass s'exécute dans un QThread dédié pour piloter
 *   le moteur C++ (PipelineNs::PipelineClass) sans bloquer la boucle d'événements de l'UI (GUI thread) ;
 * - cas limite : un second lancement est refusé tant que le run est actif ; la pause s'effectue
 *   proprement entre deux étapes ; le destructeur attend l'arrêt complet du thread pour éviter un crash (RAII) ;
 * - unité ou repère : progression de 0 à 100 % ; étapes numérotées de 1 à N ;
 * - raison non évidente : cette classe sert de façade stable conservant l'API historique des signaux
 *   afin de découpler l'interface Qt des mécanismes internes de multithreading du backend.
 */
/*
 * Header : le QUOI
 * Façade de pilotage et de suivi asynchrone du pipeline de calcul.
 * - ce que ça fait : démarre, met en pause, reprend ou annule l'exécution du pipeline et relaie ses flux ;
 * - ce que ça renvoie : signaux Qt (progression globale, étape active, messages du journal, mosaïque finale) ;
 * - quand ça lève une exception : ne lève pas d'exception ; les erreurs d'exécution backend sont capturées
 *   et transmises sous forme de signaux et de messages d'état vers l'interface.
 */

// src/frontend/simulationrunner.cpp
#include "simulationrunner.h"

#include "pipeline_worker.h"

#include <QImage>
#include <QThread>

SimulationRunner::SimulationRunner(QObject* parent)
    : QObject(parent) {
}

SimulationRunner::~SimulationRunner() {
    // Interruption et attente de terminaison du thread avant destruction complète (principe RAII).
    cancelRun();
    cleanUpThread();
}

void SimulationRunner::cleanUpThread() {
    // Quitte la boucle d'événements du QThread et attend son arrêt effectif.
    if (workerThread) {
        workerThread->quit();
        workerThread->wait();
        delete workerThread;
        workerThread = nullptr;
    }
    if (worker) {
        delete worker;
        worker = nullptr;
    }
}

// Branche les signaux de calcul continu émis par le worker (avancement en %, étape courante, logs horodatés) pour les relayer en direct à la barre de progression et au journal de bord de l'interface.
void SimulationRunner::setupProgressConnections() {
    // Relais des notifications de début de calcul et d'avancement vers l'interface.
    connect(worker, &PipelineWorkerClass::started, this, [this]() {
        emit runStarted();
        emit cancellationAvailable();
        emit statusChanged(tr("Exécution asynchrone du pipeline backend..."));
    });

    // Mise à jour de l'étape active dans la chaîne de traitement (AssemblyTab) et la barre de statut.
    connect(worker, &PipelineWorkerClass::stepStarted, this, [this](int stepIdx, int total, const QString& name) {
        const QString title = name.isEmpty() ? tr("Étape %1/%2").arg(stepIdx).arg(total) : name;
        emit stepChanged(stepIdx, title);
    });

    connect(worker, &PipelineWorkerClass::stepCompleted, this, [this](int stepIdx, int total, const QString& name) {
        const QString title = name.isEmpty() ? tr("Étape %1/%2 OK").arg(stepIdx).arg(total) : name;
        emit stepChanged(stepIdx, title);
    });

    // Progression globale transmise à la barre d'avancement de WorkspaceUI.
    connect(worker, &PipelineWorkerClass::progressChanged, this, [this](int percentage) {
        progress = percentage;
        emit progressAdvanced(percentage);
    });

    // Transmission des messages du Logger backend vers BottomTabs et LogPanel.
    connect(worker, &PipelineWorkerClass::logMessageReceived, this, [this](const QString& msg, const QString& sev) {
        emit logMessage(msg);
        emit logMessageWithSeverity(msg, sev);
    });
}

// Branche les signaux de fin ou d'interruption du worker (mise en pause, annulation, succès final avec la mosaïque ou erreur) pour actualiser l'état global et libérer le thread.
void SimulationRunner::setupStateConnections() {
    // Prise en compte de la mise en pause : libère le thread et informe les boutons de contrôle.
    connect(worker, &PipelineWorkerClass::pipelinePaused, this, [this](int atStep, const QString& name) {
        running = false;
        paused = true;
        emit pausedStateChanged(true);
        emit statusChanged(tr("Pipeline en pause après l'étape %1 (%2).").arg(atStep).arg(name));
        cleanUpThread();
    });

    // Prise en compte de l'annulation manuelle demandée par l'opérateur.
    connect(worker, &PipelineWorkerClass::pipelineCancelled, this, [this]() {
        running = false;
        paused = false;
        emit pausedStateChanged(false);
        emit stepChanged(0, tr("Annulée"));
        emit statusChanged(tr("Exécution annulée."));
        emit runCancelled();
        cleanUpThread();
    });

    // Réception du résultat final ou de l'erreur à la fin du pipeline.
    connect(worker, &PipelineWorkerClass::pipelineFinished, this,
            &SimulationRunner::handleWorkerFinished);
}

void SimulationRunner::handleWorkerFinished(bool success, const QString& errorText, const QImage& previewImage) {
    running = false;
    paused = false;
    emit pausedStateChanged(false);

    if (success) {
        emit stepChanged(0, tr("Terminé"));
        emit statusChanged(tr("Calcul du pipeline terminé avec succès."));
        // Émission de l'image finale pour basculer automatiquement en mode Export sur le canevas.
        if (!previewImage.isNull()) emit mosaicReady(previewImage);
        emit runFinished();
    } else {
        emit stepChanged(0, tr("Erreur"));
        emit statusChanged(tr("Erreur lors de l'exécution : %1").arg(errorText));
        emit runFinished();
    }
    cleanUpThread();
}

void SimulationRunner::startRun(int firstStep) {
    if (running) {
        emit statusChanged(tr("Une exécution du pipeline est déjà en cours."));
        return;
    }

    firstStepNumber = firstStep;
    progress = 0;
    running = true;
    paused = false;

    cleanUpThread();

    // Création du QThread et déplacement du worker vers ce thread dédié.
    workerThread = new QThread(this);
    worker = new PipelineWorkerClass();
    worker->moveToThread(workerThread);

    setupProgressConnections();
    setupStateConnections();

    // Déclenchement de runPipeline dès que la boucle d'événements du QThread démarre.
    const QString script = scriptJson;
    const QString startStep = startFromStepName;
    connect(workerThread, &QThread::started, worker, [this, script, startStep]() {
        worker->runPipeline(script, startStep, true);
    });

    workerThread->start();
}

void SimulationRunner::pauseRun() {
    // Demande au moteur backend d'interrompre le calcul dès la fin de l'étape courante.
    if (worker && running) {
        emit statusChanged(tr("Demande de mise en pause en cours..."));
        worker->requestPause();
    }
}

void SimulationRunner::resumeRun() {
    // Relance le pipeline : les checkpoints existants permettent d'éviter le recalcul des étapes déjà faites.
    if (!paused) return;
    startFromStepName.clear();
    startRun(firstStepNumber);
}

void SimulationRunner::cancelRun() {
    // Interrompt d'urgence le calcul en cours.
    if (!running) return;
    if (worker) {
        emit statusChanged(tr("Demande d'annulation en cours..."));
        worker->requestCancel();
    }
}

void SimulationRunner::showComparison() {
    emit comparisonRequested();
}

void SimulationRunner::toggleComparison() {
    comparisonVisible = !comparisonVisible;
    emit comparisonVisibilityChanged(comparisonVisible);
    emit statusChanged(comparisonVisible
        ? tr("Comparaison affichée — curseur de séparation actif.")
        : tr("Comparaison côte à côte affichée."));
}

void SimulationRunner::keepComparisonA() {
    emit statusChanged(tr("Résultat A conservé ; les étapes suivantes sont à refaire."));
}

void SimulationRunner::keepComparisonB() {
    emit statusChanged(tr("Résultat B conservé ; les étapes suivantes sont à refaire."));
}
