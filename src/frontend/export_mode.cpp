// src/frontend/export_mode.cpp
#include "mainwindow.h"

#include "core/pipeline/pipeline_report.h"
#include "core/io/unicode_path.h"

#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path toFilesystemPath(const QString &path)
{
    return IONs::pathFromUtf8(path.toStdString());
}

}

void MainWindow::showExportDialog()
{
    if (outputDirectory.isEmpty()) {
        QMessageBox::information(this, tr("Aucune exécution"),
            tr("Le projet ne possède pas encore de dossier de sortie ni d'exécution archivée."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Exporter un rapport d'assemblage"));
    QVBoxLayout layout(&dialog);

    auto *executions = new QComboBox(&dialog);
    const QDir runsDirectory(QDir(outputDirectory).filePath(QStringLiteral("runs")));
    const QFileInfoList runDirectories = runsDirectory.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    QString summaryCacheWarning;
    for (const QFileInfo &runDirectory : runDirectories) {
        const QString jsonPath = QDir(runDirectory.absoluteFilePath()).filePath(QStringLiteral("execution.json"));
        const QString summaryPath = QDir(runDirectory.absoluteFilePath())
                                        .filePath(QStringLiteral("execution-summary.json"));
        if (!QFileInfo::exists(jsonPath)) continue;

        nlohmann::json report;
        if (QFileInfo::exists(summaryPath)) {
            QFile summaryFile(summaryPath);
            if (summaryFile.open(QIODevice::ReadOnly))
                report = nlohmann::json::parse(summaryFile.readAll().constData(), nullptr, false);
        }
        const bool validSummary = !report.is_discarded() && report.is_object() &&
                                 report.contains("metadata") && report["metadata"].is_object() &&
                                 report.contains("progress") && report["progress"].is_object();
        if (!validSummary) {
            QFile archiveFile(jsonPath);
            if (!archiveFile.open(QIODevice::ReadOnly)) continue;
            report = nlohmann::json::parse(archiveFile.readAll().constData(), nullptr, false);
            if (!report.is_discarded() && report.is_object() &&
                report.contains("metadata") && report["metadata"].is_object() &&
                report.contains("steps") && report["steps"].is_array()) {
                try {
                    PipelineNs::writeReportSummary(toFilesystemPath(jsonPath), report);
                } catch (const std::exception &exception) {
                    if (summaryCacheWarning.isEmpty())
                        summaryCacheWarning = QString::fromUtf8(exception.what());
                }
            }
        }
        const bool hasSummary = report.is_object() && report.contains("progress") &&
                                report["progress"].is_object();
        const bool hasArchivedSteps = report.is_object() && report.contains("steps") &&
                                      report["steps"].is_array();
        if (report.is_discarded() || !report.is_object() || !report.contains("metadata") ||
            !report["metadata"].is_object() || (!hasSummary && !hasArchivedSteps))
            continue;

        const auto &metadata = report["metadata"];
        const QString archivedProject = QString::fromUtf8(
            metadata.value("projectName", std::string("projet inconnu")).c_str());
        const QString executionId = QString::fromUtf8(
            metadata.value("executionId", std::string()).c_str());
        const QString startedAt = QString::fromUtf8(
            metadata.value("startedAt", std::string("date inconnue")).c_str());
        const QString status = QString::fromUtf8(
            metadata.value("status", std::string("statut inconnu")).c_str());
        const QString label = tr("%1 — %2 — %3 — %4")
                                  .arg(archivedProject, startedAt, executionId, status);
        executions->addItem(label, jsonPath);
    }

    layout.addWidget(new QLabel(tr("Exécution à exporter"), &dialog));
    layout.addWidget(executions);
    if (!summaryCacheWarning.isEmpty())
        layout.addWidget(new QLabel(
            tr("Certaines anciennes archives n'ont pas pu être mises en cache : %1")
                .arg(summaryCacheWarning), &dialog));
    if (executions->count() == 0) {
        executions->setEnabled(false);
        layout.addWidget(new QLabel(
            tr("Aucune exécution archivée n'a été trouvée dans le dossier du projet."),
            &dialog));
    } else {
        layout.addWidget(new QLabel(
            tr("Le rapport est généré à partir des résultats enregistrés, sans relancer le calcul."),
            &dialog));
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *exportReport = buttons->addButton(
        tr("Enregistrer le rapport HTML…"), QDialogButtonBox::AcceptRole);
    exportReport->setEnabled(executions->count() > 0);
    layout.addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(exportReport, &QPushButton::clicked, this, [this, &dialog, executions]() {
        const QString executionJsonPath = executions->currentData().toString();
        QFile file(executionJsonPath);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(&dialog, tr("Rapport indisponible"),
                tr("L'archive de cette exécution n'a pas pu être lue."));
            return;
        }

        const QByteArray contents = file.readAll();
        nlohmann::json report;
        try {
            report = nlohmann::json::parse(contents.constData());
            if (!report.is_object() || !report.contains("metadata") ||
                !report.contains("steps") || !report["steps"].is_array())
                throw std::runtime_error("archive does not contain the expected steps");
        } catch (const std::exception &exception) {
            QMessageBox::warning(&dialog, tr("Archive invalide"),
                tr("Le rapport ne peut pas être exporté :\n%1")
                    .arg(QString::fromUtf8(exception.what())));
            return;
        }

        const QString archivedProject = QString::fromUtf8(
            report["metadata"].value("projectName", std::string("projet")).c_str());
        const QString executionId = QString::fromUtf8(
            report["metadata"].value("executionId", std::string("execution")).c_str());
        const QString fileName = archivedProject + QStringLiteral("_rapport_") +
                                 executionId + QStringLiteral(".html");
        const QString defaultPath = outputDirectory.isEmpty()
            ? fileName : QDir(outputDirectory).filePath(fileName);
        const QString htmlPath = QFileDialog::getSaveFileName(
            &dialog, tr("Enregistrer le rapport"), defaultPath,
            tr("Rapport HTML (*.html)"));
        if (htmlPath.isEmpty())
            return;

        try {
            PipelineNs::writeHtmlReport(toFilesystemPath(htmlPath), report);
        } catch (const std::exception &exception) {
            QMessageBox::warning(&dialog, tr("Export impossible"),
                tr("Le rapport HTML n'a pas pu être enregistré :\n%1")
                    .arg(QString::fromUtf8(exception.what())));
            return;
        }

        updateStatus(tr("Rapport de l'exécution %1 exporté.").arg(executionId));
        dialog.accept();
    });
    dialog.exec();
}
