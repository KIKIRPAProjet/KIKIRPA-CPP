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

// src/frontend/pipeline_runner.cpp
#include "pipeline_runner.h"

#include "pipeline_worker.h"

#include <QImage>
#include <QThread>

#include <nlohmann/json.hpp>

#include <exception>
#include <stdexcept>

#include <string>
#include <utility>

PipelineRunnerClass::PipelineRunnerClass(QObject* parent)
    : QObject(parent) {
    // Les résultats traversent la frontière entre le thread du worker et celui de l'interface (connexion en file).
    qRegisterMetaType<PipelineResults>();
}

PipelineRunnerClass::~PipelineRunnerClass() {
    // Interruption et attente de terminaison du thread avant destruction complète (principe RAII).
    cancelRun();
    cleanUpThread();
}

void PipelineRunnerClass::cleanUpThread() {
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
void PipelineRunnerClass::setupProgressConnections() {
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
void PipelineRunnerClass::setupStateConnections() {
    connect(worker, &PipelineWorkerClass::mosaicFileReady, this, &PipelineRunnerClass::mosaicFileReady);
    connect(worker, &PipelineWorkerClass::resultsReady, this, &PipelineRunnerClass::resultsReady);
    // Prise en compte de la mise en pause : libère le thread et informe les boutons de contrôle.
    connect(worker, &PipelineWorkerClass::pipelinePaused, this, [this](int atStep, const QString& name) {
        running = false;
        paused = true;
        pausedStepNumber = atStep;
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
        if (cropRun)
            emit cropFinished(false, QImage());
        emit runCancelled();
        cleanUpThread();
    });

    // Réception du résultat final ou de l'erreur à la fin du pipeline.
    connect(worker, &PipelineWorkerClass::pipelineFinished, this,
            &PipelineRunnerClass::handleWorkerFinished);
}

void PipelineRunnerClass::handleWorkerFinished(
    bool success, const QString& errorText, const QImage& previewImage)
{
    running = false;
    paused = false;
    emit pausedStateChanged(false);

    if (success) {
        emit stepChanged(0, tr("Terminé"));
        emit statusChanged(tr("Calcul du pipeline terminé avec succès."));
    }
    else {
        emit stepChanged(0, tr("Erreur"));
        emit statusChanged(tr("Erreur lors de l'exécution : %1").arg(errorText));
    }

    if (cropRun) {
        emit cropFinished(success, previewImage);
    }
    else if (success && !previewImage.isNull()) {
        emit mosaicReady(previewImage);
    }

    emit runFinished();
    cleanUpThread();
}

void PipelineRunnerClass::startCropRun(
    const QRect& rectangle, const QString& cropOutputDirectory)
{
    if (running || paused) {
        emit statusChanged(
            tr("Terminez ou annulez le calcul courant avant de lancer un rognage."));
        return;
    }

    if (rectangle.x() < 0 || rectangle.y() < 0 ||
        rectangle.width() <= 0 || rectangle.height() <= 0 ||
        cropOutputDirectory.trimmed().isEmpty()) {
        emit statusChanged(tr("Rectangle ou dossier de rognage invalide."));
        return;
    }

    try {
        auto script = nlohmann::json::parse(scriptJson.toStdString());
        auto& steps = script.at("pipeline").at("steps");
        if (!steps.is_array())
            throw std::runtime_error("pipeline.steps must be an array");

        const auto run = script.value("run", nlohmann::json::object());
        if (run.value("checkpointDirectory", std::string()).empty())
            throw std::runtime_error("crop execution requires a checkpoint directory");

        // Retirer l'ancien rognage pour qu'un nouvel essai reparte de fusion.
        auto updatedSteps = nlohmann::json::array();
        for (const auto& step : steps) {
            if (step.at("name").get<std::string>() != "mosaic-crop")
                updatedSteps.push_back(step);
        }

        int fusionIndex = -1;
        int exportIndex = -1;
        int stepIndex = 0;
        for (const auto& step : updatedSteps) {
            const auto name = step.at("name").get<std::string>();
            if (name == "fusion") {
                if (fusionIndex >= 0)
                    throw std::runtime_error("multiple fusion steps are not supported");
                fusionIndex = stepIndex;
            }
            if (name == "export") {
                if (exportIndex >= 0)
                    throw std::runtime_error("multiple export steps are not supported");
                exportIndex = stepIndex;
            }
            ++stepIndex;
        }

        if (fusionIndex < 0 || exportIndex != fusionIndex + 1)
            throw std::runtime_error("crop requires fusion immediately before export");

        const auto integerParameter = [](const char* name, int value) {
            return nlohmann::json{
                {"name", name}, {"valueType", "int"}, {"value", value}
            };
            };

        const nlohmann::json cropStep = {
            {"name", "mosaic-crop"},
            {"parameters", nlohmann::json::array({
                integerParameter("x", rectangle.x()),
                integerParameter("y", rectangle.y()),
                integerParameter("width", rectangle.width()),
                integerParameter("height", rectangle.height()),
                {
                    {"name", "outputDirectory"},
                    {"valueType", "string"},
                    {"value", cropOutputDirectory.toStdString()}
                }
            })}
        };

        // Le dialogue frontend publiera le TIFF à l'emplacement choisi.
        // Ne pas créer un export intermédiaire dans le dossier du projet.
        updatedSteps.erase(updatedSteps.begin() + exportIndex);
        updatedSteps.insert(updatedSteps.begin() + exportIndex, cropStep);
        steps = std::move(updatedSteps);

        // La sélection correspond à la fusion déjà affichée.
        // Refuser la reprise si une étape précédente doit être recalculée.
        script["run"]["startFromStep"] = "mosaic-crop";
        script["run"]["skipCompletedSteps"] = true;

        scriptJson = QString::fromStdString(script.dump(2));
        startFromStepName.clear();
        cropRun = true;
        startRun(exportIndex + 1, true);
    }
    catch (const std::exception& exception) {
        cropRun = false;
        emit statusChanged(
            tr("Impossible de préparer le rognage : %1")
            .arg(QString::fromUtf8(exception.what())));
    }
}

void PipelineRunnerClass::startRun(int firstStep, bool isResume) {
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
        connect(workerThread, &QThread::started, worker, [this, script, startStep, isResume]() {
        worker->runPipeline(script, startStep, true, isResume);
    });

    workerThread->start();
}

void PipelineRunnerClass::pauseRun() {
    // Demande au moteur backend d'interrompre le calcul dès la fin de l'étape courante.
    if (worker && running) {
        emit statusChanged(tr("Demande de mise en pause en cours..."));
        worker->requestPause();
    }
}

void PipelineRunnerClass::resumeRun() {
    // Relance le pipeline : les checkpoints existants permettent d'éviter le recalcul des étapes déjà faites.
    if (!paused) return;
    startFromStepName.clear();
    startRun(firstStepNumber, true);
}

void PipelineRunnerClass::cancelRun()
{
    if (!running && !paused)
        return;

    if (worker && running) {
        emit statusChanged(tr("Interruption du calcul en cours..."));
        worker->requestCancel();
    }
    else if (paused) {
        resetPausedState();
        emit pausedStateChanged(false);
        emit stepChanged(0, tr("Annulée"));
        emit statusChanged(tr("Exécution annulée."));

        // Réactiver les commandes et abandonner l'export différé.
        if (cropRun)
            emit cropFinished(false, QImage());

        emit runCancelled();
        cleanUpThread();
    }
}

void PipelineRunnerClass::showComparison() {
    emit comparisonRequested();
}

void PipelineRunnerClass::toggleComparison() {
    comparisonVisible = !comparisonVisible;
    emit comparisonVisibilityChanged(comparisonVisible);
    emit statusChanged(comparisonVisible
        ? tr("Comparaison affichée : curseur de séparation actif.")
        : tr("Comparaison côte à côte affichée."));
}

void PipelineRunnerClass::keepComparisonA() {
    emit statusChanged(tr("Résultat A conservé ; les étapes suivantes sont à refaire."));
}

void PipelineRunnerClass::keepComparisonB() {
    emit statusChanged(tr("Résultat B conservé ; les étapes suivantes sont à refaire."));
}
