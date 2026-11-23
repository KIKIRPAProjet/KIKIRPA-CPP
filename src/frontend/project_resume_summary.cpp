// src/frontend/project_resume_summary.cpp
#include "project_serializer.h"

#include "core/checkpoint/checkpoint.h"
#include "core/io/unicode_path.h"
#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>

namespace {

// run.json est plus récent que le projet, mais n'est réutilisable que pour la même configuration.
nlohmann::json latestScript(const ProjectData &project)
{
    auto script = project.backendScript;
    const std::string directory = script.value("run", nlohmann::json::object())
                                       .value("checkpointDirectory", std::string());
    QFile file(QDir(QString::fromStdString(directory)).filePath(QStringLiteral("run.json")));
    if (directory.empty() || !file.exists()) return script;
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot read run.json");
    const auto saved = nlohmann::json::parse(file.readAll().toStdString());
    // Le backend exporte aussi les paramètres par défaut : comparer les configurations normalisées.
    const auto expectedPipeline = PipelineNs::buildPipeline(script).exportConfig();
    const auto savedPipeline = PipelineNs::buildPipeline(saved).exportConfig();
    if (savedPipeline.at("pipeline") != expectedPipeline.at("pipeline"))
        throw std::runtime_error("run.json belongs to a different pipeline configuration");
    script["run"] = saved.at("run");
    return script;
}

void inspectSteps(const PipelineNs::PipelineClass &pipeline, ProjectResumeSummary &summary)
{
    const auto &state = pipeline.getRunState();
    summary.runStatus = QString::fromStdString(nlohmann::json(state.status).get<std::string>());
    CheckpointNs::CheckpointClass checkpoint(IONs::pathFromUtf8(state.checkpointDirectory));
    bool invalidPrefix = false;
    const auto &steps = pipeline.getSteps();
    for (std::size_t index = 0; index < steps.size(); ++index) {
        const std::string name = steps[index]->getConfigJson().at("name");
        const QString label = QString::fromStdString(name);
        if (index >= state.completedSteps.size() || state.completedSteps[index] != name) {
            summary.neverRunSteps.append(label);
            invalidPrefix = true;
        } else if (!invalidPrefix && checkpoint.valid(index, *steps[index])) {
            summary.upToDateSteps.append(label);
        } else {
            summary.stepsToVerify.append(label);
            invalidPrefix = true;
        }
    }
}

}

ProjectResumeSummary ProjectSerializer::summarize(const ProjectData &projectData)
{
    ProjectResumeSummary summary;
    if (!projectData.backendScript.is_object()) {
        summary.issues.append(QStringLiteral("Ancien projet : aucun script backend enregistré. Configurez puis lancez le pipeline."));
        return summary;
    }
    for (const QString &path : projectData.gridState.tilePaths)
        if (!QFileInfo(path).isFile())
            summary.issues.append(QStringLiteral("Image absente : %1").arg(path));
    const QFileInfo output(projectData.outputDirectory);
    if (!output.isDir() || !output.isWritable())
        summary.issues.append(QStringLiteral("Dossier de sortie inaccessible : %1").arg(projectData.outputDirectory));
    try {
        const auto pipeline = PipelineNs::buildPipeline(latestScript(projectData));
        inspectSteps(pipeline, summary);
        summary.canResume = summary.issues.isEmpty();
    } catch (const std::exception &exception) {
        summary.issues.append(QString::fromUtf8(exception.what()));
    }
    return summary;
}

QString ProjectResumeSummary::text() const
{
    return QStringLiteral("État : %1\nÀ jour : %2\nÀ recalculer : %3\nNon terminées : %4\n%5")
        .arg(runStatus, upToDateSteps.join(QStringLiteral(", ")),
             stepsToVerify.join(QStringLiteral(", ")), neverRunSteps.join(QStringLiteral(", ")),
             issues.join(QLatin1Char('\n')));
}
