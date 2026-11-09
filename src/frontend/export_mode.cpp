#include "mainwindow.h"

#include "core/pipeline/pipeline_report.h"

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
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path filesystemPath(const QString &path)
{
    return std::filesystem::u8path(path.toUtf8().constData());
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
    for (const QFileInfo &runDirectory : runDirectories) {
        const QString jsonPath = QDir(runDirectory.absoluteFilePath())
                                     .filePath(QStringLiteral("execution.json"));
        QFile file(jsonPath);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const auto report = nlohmann::json::parse(file.readAll().constData(), nullptr, false);
        if (report.is_discarded() || !report.is_object() || !report.contains("metadata"))
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
                throw std::runtime_error("L'archive ne contient pas les étapes attendues.");
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
            PipelineNs::writeHtmlReport(filesystemPath(htmlPath), report);
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
