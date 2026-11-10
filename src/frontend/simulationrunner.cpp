/*
 * .cpp : le POURQUOI
 * - choix de conception :
 *   1. Exécuter PipelineClass::run() via QtConcurrent::run + QFutureWatcher évite de bloquer la
 *      boucle d'événements Qt, ce qui permet à l'utilisateur de cliquer sur « Annuler » à tout moment.
 *   2. L'annulation s'appuie sur PipelineClass::requestPause() (std::atomic<bool> thread-safe)
 *      plutôt que d'interrompre brutalement le thread : l'étape en cours se termine proprement,
 *      son checkpoint est validé et le script JSON est réécrit avec le statut « paused ».
 *   3. Le mode démonstration par QTimer est conservé comme repli lorsqu'aucun script backend
 *      n'est configuré, et supporte lui aussi la mise en pause puis la reprise.
 * - cas limite :
 *   - Si l'utilisateur ferme l'application pendant qu'une étape backend tourne encore, le
 *     destructeur demande la pause et attend la fin du QFutureWatcher pour éviter tout accès
 *     concurrent à un objet détruit.
 *   - Si « Annuler » est cliqué plusieurs fois pendant la fin d'une étape longue, un drapeau
 *     cancellationPending empêche les demandes redondantes.
 */
#include "simulationrunner.h"

#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/pipeline/run_state.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QtGlobal>

#include <exception>
#include <filesystem>
#include <string>

namespace {

namespace fs = std::filesystem;

// Convertit un chemin Qt en std::filesystem::path en préservant les caractères Unicode sous Windows.
fs::path toFsPath(const QString &qtPath)
{
#ifdef Q_OS_WIN
    return fs::path(qtPath.toStdWString());
#else
    return fs::path(qtPath.toStdString());
#endif
}

QString classifyBackendLogLevel(const QString &message)
{
    if (message.contains(QStringLiteral("failed"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("cannot "), Qt::CaseInsensitive)) {
        return QStringLiteral("Erreur");
    }
    if (message.contains(QStringLiteral("warning"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("paused"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("missing or outdated"), Qt::CaseInsensitive)) {
        return QStringLiteral("Avertissement");
    }
    return QStringLiteral("Info");
}

int computePercentage(int completedSteps, int totalSteps)
{
    if (totalSteps <= 0)
        return 0;
    return qBound(0, qRound(100.0 * static_cast<double>(completedSteps) / totalSteps), 100);
}

} // namespace

SimulationRunner::SimulationRunner(QObject *parent)
    : QObject(parent),
      backendWatcher(new QFutureWatcher<bool>(this))
{
    connect(backendWatcher, &QFutureWatcher<bool>::finished,
            this, &SimulationRunner::onBackendRunFinished);
}

SimulationRunner::~SimulationRunner()
{
    // Demander l'arrêt propre si le worker backend tourne encore lors de la fermeture,
    // puis attendre la fin du thread avant de détruire le QFutureWatcher.
    if (backendRunning && backendWatcher && backendWatcher->isRunning()) {
        std::shared_ptr<PipelineNs::PipelineClass> pipeline;
        {
            QMutexLocker locker(&pipelineMutex);
            pipeline = activePipeline;
        }
        if (pipeline)
            pipeline->requestPause();
        backendWatcher->waitForFinished();
    }
}

bool SimulationRunner::isRunning() const
{
    return running || backendRunning;
}

bool SimulationRunner::isPaused() const
{
    return paused;
}

bool SimulationRunner::canResume() const
{
    return resumeAvailable;
}

bool SimulationRunner::isCancellationPending() const
{
    return cancellationPending;
}

int SimulationRunner::currentProgress() const
{
    return progress;
}

QString SimulationRunner::backendScriptPath() const
{
    return scriptPath;
}

void SimulationRunner::setBackendScriptPath(const QString &newScriptPath)
{
    scriptPath = newScriptPath;
    if (isRunning())
        return;

    int completedSteps = 0;
    int totalSteps = 0;
    const bool resumable = inspectScriptResumable(scriptPath, &completedSteps, &totalSteps);
    paused = resumable;
    setResumeAvailable(resumable);
    if (resumable && totalSteps > 0) {
        progress = computePercentage(completedSteps, totalSteps);
        emit progressAdvanced(progress);
        emit stepChanged(tr("En pause (%1/%2)").arg(completedSteps).arg(totalSteps));
    }
}

void SimulationRunner::clearBackendScriptPath()
{
    scriptPath.clear();
    if (isRunning())
        return;

    paused = false;
    progress = 0;
    setResumeAvailable(false);
}

void SimulationRunner::startRun(int firstStep)
{
    if (isRunning()) {
        emit statusChanged(tr("Un assemblage est déjà en cours."));
        return;
    }

    // Si un script backend a déjà été configuré pour le projet, on lance le vrai pipeline.
    if (!scriptPath.isEmpty()) {
        startBackendRun(scriptPath, firstStep);
        return;
    }

    firstStepNumber = firstStep;
    progress = 0;
    paused = false;
    cancellationPending = false;
    setResumeAvailable(false);
    startDemoTimer(false);
}

void SimulationRunner::startBackendRun(const QString &targetScriptPath, int firstStep)
{
    if (isRunning()) {
        emit statusChanged(tr("Un assemblage est déjà en cours."));
        return;
    }

    scriptPath = targetScriptPath;
    const StartMode mode = firstStep > 1 ? StartMode::FromStep : StartMode::FromBeginning;
    launchBackendPipeline(targetScriptPath, mode, firstStep);
}

void SimulationRunner::resumeRun()
{
    if (isRunning()) {
        emit statusChanged(tr("Un assemblage est déjà en cours."));
        return;
    }

    if (!canResume()) {
        emit statusChanged(tr("Aucun assemblage en pause à reprendre."));
        return;
    }

    // Cas 1 : reprise d'un vrai pipeline backend depuis son script JSON et ses checkpoints.
    if (!scriptPath.isEmpty()) {
        launchBackendPipeline(scriptPath, StartMode::Resume, 1);
        return;
    }

    // Cas 2 : reprise du mode démonstration à la progression mémorisée.
    paused = false;
    setResumeAvailable(false);
    startDemoTimer(true);
}

void SimulationRunner::cancelRun()
{
    if (!isRunning())
        return;

    // Cas 1 : exécution backend en cours — demande une pause coopérative entre deux étapes.
    if (backendRunning) {
        if (cancellationPending)
            return;

        std::shared_ptr<PipelineNs::PipelineClass> pipeline;
        {
            QMutexLocker locker(&pipelineMutex);
            pipeline = activePipeline;
        }
        if (pipeline) {
            cancellationPending = true;
            // Le backend vérifiera ce drapeau atomique juste avant d'entamer l'étape suivante.
            pipeline->requestPause();
            emit cancellationRequested();
            emit stepChanged(tr("Annulation demandée…"));
            emit statusChanged(
                tr("Annulation demandée — l'assemblage s'arrêtera proprement après l'étape en cours."));
        }
        return;
    }

    // Cas 2 : démonstration temporisée — mise en pause immédiate sans perdre la progression.
    running = false;
    if (timer) {
        timer->stop();
        timer->deleteLater();
        timer = nullptr;
    }
    paused = true;
    const bool resumable = progress > 0 && progress < 100;
    setResumeAvailable(resumable);
    emit stepChanged(tr("En pause (%1 %)").arg(progress));
    emit statusChanged(resumable
                           ? tr("Démonstration mise en pause à %1 %. Vous pouvez la reprendre.").arg(progress)
                           : tr("Démonstration annulée."));
    if (resumable)
        emit runPaused();
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

void SimulationRunner::launchBackendPipeline(const QString &targetScriptPath, StartMode mode, int firstStep)
{
    if (targetScriptPath.isEmpty()) {
        const QString error = tr("Aucun script backend n'a été généré pour ce projet.");
        emit statusChanged(error);
        emit runFailed(error);
        return;
    }

    const fs::path scriptFsPath = toFsPath(targetScriptPath);
    std::shared_ptr<PipelineNs::PipelineClass> pipeline;
    try {
        // Construction et validation synchrones du pipeline sur le thread UI pour remonter
        // immédiatement toute erreur de configuration avant de démarrer un thread de calcul.
        pipeline = std::make_shared<PipelineNs::PipelineClass>(
            PipelineNs::buildPipelineFromFile(scriptFsPath));

        PipelineNs::RunState runState = pipeline->getRunState();
        const auto &steps = pipeline->getSteps();
        const int totalSteps = static_cast<int>(steps.size());

        // Garantit la présence d'un dossier de checkpoints pour rendre toute pause reprenable.
        if (runState.checkpointDirectory.empty()) {
            const fs::path defaultCheckpoints = scriptFsPath.parent_path() / "checkpoints";
            runState.checkpointDirectory = defaultCheckpoints.string();
        }

        if (mode == StartMode::Resume) {
            // Reprise : on conserve completedSteps du script et on laisse resolveStart()
            // restaurer les sorties depuis les checkpoints valides.
            runState.skipCompletedSteps = true;
            runState.startFromStep.clear();
        } else if (mode == StartMode::FromStep) {
            if (firstStep < 1 || firstStep > totalSteps) {
                throw std::invalid_argument(
                    "cannot restart at step " + std::to_string(firstStep)
                    + ": pipeline has " + std::to_string(totalSteps) + " step(s)");
            }
            const auto &targetStep = steps.at(static_cast<std::size_t>(firstStep - 1));
            const std::string targetName =
                targetStep->getConfigJson().is_object()
                    ? targetStep->getConfigJson().value("name", "")
                    : "";
            if (targetName.empty()) {
                throw std::invalid_argument("selected step has no configured name");
            }
            runState.skipCompletedSteps = true;
            runState.startFromStep = targetName;
        } else {
            // Lancement complet : on force la réexécution depuis la première étape.
            runState.skipCompletedSteps = false;
            runState.startFromStep.clear();
        }

        pipeline->setRunState(runState);
        // Réécrit automatiquement l'état "run" (status, completedSteps) dans le script après
        // chaque étape et lors d'une pause, afin qu'une reprise fonctionne même après fermeture.
        pipeline->setScriptPath(scriptFsPath);
    } catch (const std::exception &exception) {
        const QString error = QString::fromUtf8(exception.what());
        emit statusChanged(tr("Impossible de préparer le pipeline : %1").arg(error));
        emit runFailed(error);
        return;
    }

    const int totalSteps = static_cast<int>(pipeline->getSteps().size());
    const int initialCompleted =
        mode == StartMode::Resume ? static_cast<int>(pipeline->getRunState().completedSteps.size()) : 0;

    firstStepNumber = firstStep;
    progress = computePercentage(initialCompleted, totalSteps);
    backendRunning = true;
    paused = false;
    cancellationPending = false;
    setResumeAvailable(false);

    {
        QMutexLocker locker(&pipelineMutex);
        activePipeline = pipeline;
        lastBackendError.clear();
    }

    emit runStarted();
    emit progressAdvanced(progress);
    emit stepChanged(mode == StartMode::Resume
                         ? tr("Reprise de l'assemblage…")
                         : tr("Assemblage en cours…"));
    emit statusChanged(mode == StartMode::Resume
                           ? tr("Reprise du pipeline backend en cours…")
                           : tr("Exécution du pipeline backend en cours…"));
    // Le bouton « Annuler » est disponible immédiatement dès le lancement réel du backend.
    emit cancellationAvailable();

    QPointer<SimulationRunner> self(this);
    const auto logger = [self, totalSteps](const std::string &rawMessage) {
        if (!self)
            return;
        const QString message =
            QString::fromUtf8(rawMessage.c_str(), static_cast<int>(rawMessage.size()));
        // Le logger est invoqué depuis le thread worker : on bascule sur le thread UI.
        QMetaObject::invokeMethod(
            self.data(),
            [self, message, totalSteps]() {
                if (self)
                    self->handleBackendLogMessage(message, totalSteps);
            },
            Qt::QueuedConnection);
    };

    backendWatcher->setFuture(QtConcurrent::run([this, pipeline, logger]() -> bool {
        try {
            return pipeline->run(logger);
        } catch (const std::exception &exception) {
            QMutexLocker locker(&pipelineMutex);
            lastBackendError = QString::fromUtf8(exception.what());
            return false;
        }
    }));
}

void SimulationRunner::onBackendRunFinished()
{
    const bool succeeded = backendWatcher->result();

    std::shared_ptr<PipelineNs::PipelineClass> pipeline;
    QString errorMessage;
    {
        QMutexLocker locker(&pipelineMutex);
        pipeline = activePipeline;
        activePipeline.reset();
        errorMessage = lastBackendError;
    }

    backendRunning = false;
    cancellationPending = false;

    if (!pipeline) {
        emit runFailed(tr("État du pipeline indisponible à la fin de l'exécution."));
        return;
    }

    const PipelineNs::RunState finalState = pipeline->getRunState();
    const int completedCount = static_cast<int>(finalState.completedSteps.size());
    const int totalSteps = static_cast<int>(pipeline->getSteps().size());
    progress = computePercentage(completedCount, totalSteps);

    if (finalState.status == PipelineNs::RunStatus::Paused) {
        paused = true;
        setResumeAvailable(true);
        emit progressAdvanced(progress);
        emit stepChanged(tr("En pause (%1/%2)").arg(completedCount).arg(totalSteps));
        emit statusChanged(
            tr("Assemblage annulé et mis en pause (%1/%2 étape(s) conservée(s)). Vous pouvez reprendre.")
                .arg(completedCount)
                .arg(totalSteps));
        emit runPaused();
        emit runCancelled();
        return;
    }

    if (succeeded && finalState.status == PipelineNs::RunStatus::Completed) {
        paused = false;
        progress = 100;
        setResumeAvailable(false);
        emit progressAdvanced(100);
        emit stepChanged(tr("Terminé (%1/%2)").arg(totalSteps).arg(totalSteps));
        emit statusChanged(tr("Assemblage terminé avec succès (%1 étape(s)).").arg(totalSteps));
        emit runFinished();
        return;
    }

    paused = false;
    // Même après un échec, le backend conserve les checkpoints des étapes précédentes :
    // si au moins une étape a réussi, on autorise la reprise.
    const bool resumableAfterFailure = completedCount > 0;
    setResumeAvailable(resumableAfterFailure);
    emit progressAdvanced(progress);
    emit stepChanged(tr("Échec (%1/%2)").arg(completedCount).arg(totalSteps));
    const QString summary = errorMessage.isEmpty()
                                ? tr("Le pipeline s'est arrêté à l'étape %1/%2.")
                                      .arg(completedCount + 1)
                                      .arg(totalSteps)
                                : errorMessage;
    emit statusChanged(summary);
    emit runFailed(summary);
}

void SimulationRunner::handleBackendLogMessage(const QString &message, int totalSteps)
{
    emit backendLogReceived(message, classifyBackendLogLevel(message));

    static const QRegularExpression stepOkRegex(QStringLiteral(R"(^Step (\d+)/(\d+) OK\b)"));
    static const QRegularExpression resumingRegex(QStringLiteral(R"(^resuming at step (\d+)/(\d+)\b)"));
    static const QRegularExpression pausedRegex(QStringLiteral(R"(^Step (\d+)/(\d+) paused\b)"));

    const QRegularExpressionMatch okMatch = stepOkRegex.match(message);
    if (okMatch.hasMatch()) {
        const int done = okMatch.captured(1).toInt();
        const int total = qMax(totalSteps, okMatch.captured(2).toInt());
        progress = computePercentage(done, total);
        emit progressAdvanced(progress);
        if (!cancellationPending)
            emit stepChanged(tr("Étape %1/%2 terminée").arg(done).arg(total));
        return;
    }

    const QRegularExpressionMatch resumeMatch = resumingRegex.match(message);
    if (resumeMatch.hasMatch()) {
        const int nextStep = resumeMatch.captured(1).toInt();
        const int total = qMax(totalSteps, resumeMatch.captured(2).toInt());
        progress = computePercentage(nextStep - 1, total);
        emit progressAdvanced(progress);
        emit stepChanged(tr("Reprise étape %1/%2").arg(nextStep).arg(total));
        emit statusChanged(tr("Reprise à l'étape %1/%2 (étapes précédentes restaurées).").arg(nextStep).arg(total));
        return;
    }

    const QRegularExpressionMatch pauseMatch = pausedRegex.match(message);
    if (pauseMatch.hasMatch()) {
        const int interruptedAt = pauseMatch.captured(1).toInt();
        const int total = qMax(totalSteps, pauseMatch.captured(2).toInt());
        emit stepChanged(tr("En pause (%1/%2)").arg(interruptedAt - 1).arg(total));
    }
}

void SimulationRunner::startDemoTimer(bool resuming)
{
    running = true;
    emit runStarted();
    emit progressAdvanced(progress);
    emit stepChanged(resuming ? tr("Démonstration (reprise)") : tr("Démonstration"));
    emit statusChanged(
        resuming
            ? tr("Reprise de la démonstration à %1 % (aucun calcul d'image).").arg(progress)
            : tr("Démonstration de progression — aucune tuile/script backend configuré."));

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        progress = qMin(100, progress + 4);
        emit progressAdvanced(progress);
        if (progress >= 100)
            finishDemoRun();
    });
    timer->start(100);

    QTimer::singleShot(150, this, [this]() {
        if (running && timer && timer->isActive())
            emit cancellationAvailable();
    });
}

void SimulationRunner::finishDemoRun()
{
    if (timer) {
        timer->stop();
        timer->deleteLater();
        timer = nullptr;
    }
    running = false;
    paused = false;
    setResumeAvailable(false);
    emit stepChanged(tr("Terminé"));
    emit statusChanged(
        tr("Démonstration terminée (aucun calcul d'image effectué). Étape de départ : %1.")
            .arg(firstStepNumber));
    emit runFinished();
}

bool SimulationRunner::inspectScriptResumable(const QString &targetScriptPath,
                                              int *completedStepsOut,
                                              int *totalStepsOut) const
{
    if (targetScriptPath.isEmpty() || !QFileInfo::exists(targetScriptPath))
        return false;

    QFile file(targetScriptPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return false;

    const QJsonObject root = document.object();
    const QJsonArray steps = root.value(QStringLiteral("pipeline"))
                                 .toObject()
                                 .value(QStringLiteral("steps"))
                                 .toArray();
    const QJsonObject run = root.value(QStringLiteral("run")).toObject();
    const QString status = run.value(QStringLiteral("status")).toString();
    const QJsonArray completed = run.value(QStringLiteral("completedSteps")).toArray();

    if (completedStepsOut)
        *completedStepsOut = completed.size();
    if (totalStepsOut)
        *totalStepsOut = steps.size();

    if (status == QStringLiteral("paused"))
        return true;
    return status == QStringLiteral("failed") && !completed.isEmpty();
}

void SimulationRunner::setResumeAvailable(bool available)
{
    if (resumeAvailable == available)
        return;
    resumeAvailable = available;
    emit resumeAvailabilityChanged(resumeAvailable);
}
