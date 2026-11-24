// src/frontend/export_mode.cpp
#include "mainwindow.h"
#include "artworkcanvas.h"
#include "frontendutils.h"
#include "workspace_ui.h"

#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/pyramidal_tiff_writer.h"
#include "core/io/unicode_path.h"
#include "core/pipeline/pipeline_report.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

#include <nlohmann/json.hpp>

#include <QByteArray>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIODevice>
#include <QLabel>
#include <QMessageBox>
#include <QLineEdit>
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
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Exporter le projet ou un rapport"));
    QVBoxLayout layout(&dialog);

    layout.addWidget(new QLabel(tr("Rapport d'une exécution archivée"), &dialog));
    auto *executions = new QComboBox(&dialog);
    QFileInfoList runDirectories;
    if (!outputDirectory.isEmpty()) {
        const QDir runsDirectory(QDir(outputDirectory).filePath(QStringLiteral("runs")));
        runDirectories = runsDirectory.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    }
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

    auto *exportReport = new QPushButton(tr("Enregistrer le rapport HTML…"), &dialog);
    exportReport->setEnabled(executions->count() > 0);
    layout.addWidget(exportReport);
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

    layout.addWidget(new QLabel(tr("Exporter la mosaïque TIFF"), &dialog));
    layout.addWidget(new QLabel(tr("Fichier de destination de la mosaïque :"), &dialog));
    auto *imagePathLayout = new QHBoxLayout();
    auto *imagePathEdit = new QLineEdit(&dialog);
    const QString defaultImageName = projectName.isEmpty() ? QStringLiteral("mosaic.tif")
                                                           : (projectName + QStringLiteral(".tif"));
    const QString defaultImagePath = outputDirectory.isEmpty()
        ? defaultImageName
        : QDir(outputDirectory).filePath(defaultImageName);
    imagePathEdit->setText(defaultImagePath);
    auto *browseButton = new QPushButton(tr("Parcourir…"), &dialog);
    imagePathLayout->addWidget(imagePathEdit);
    imagePathLayout->addWidget(browseButton);
    layout.addLayout(imagePathLayout);

    connect(browseButton, &QPushButton::clicked, &dialog, [this, &dialog, imagePathEdit]() {
        const QString path = QFileDialog::getSaveFileName(
            &dialog,
            tr("Choisir l'emplacement de l'image finale"),
            imagePathEdit->text(),
            tr("Image TIFF (*.tif *.tiff);;Tous les fichiers (*.*)"));
        if (!path.isEmpty()) {
            imagePathEdit->setText(path);
        }
    });

    auto *format = new QComboBox(&dialog);
    format->addItems({tr("TIFF 16 bits"), tr("TIFF 8 bits, perte de précision"),
                      tr("PSB"), tr("JPEG, perte de précision")});
    layout.addWidget(new QLabel(tr("Format de sortie"), &dialog));
    layout.addWidget(format);
    layout.addWidget(new QLabel(tr("Profil ICC des tuiles conservé"), &dialog));
    const bool mosaicAvailable = !mosaicFilePath.isEmpty() && QFileInfo(mosaicFilePath).isFile();
    if (!mosaicAvailable)
        layout.addWidget(new QLabel(
            tr("Aucune mosaïque finale n'est disponible pour l'export TIFF."), &dialog));
    layout.addWidget(FrontendUtils::mutedLabel(
        tr("L'export de la mosaïque en pleine résolution sera généré par le backend."),
        &dialog));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *exportButton = buttons->addButton(tr("Exporter…"),
                                            QDialogButtonBox::AcceptRole);
    exportButton->setEnabled(mosaicAvailable);
    layout.addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    connect(exportButton, &QPushButton::clicked, this, [this, &dialog, format, imagePathEdit]() {
        if (format->currentIndex() != 0) {
            QMessageBox::information(&dialog, tr("Format non disponible"),
                tr("Seul l'export TIFF 16 bits est actuellement pris en charge."));
            return;
        }
        const QString chosenPath = imagePathEdit->text().trimmed();
        if (chosenPath.isEmpty()) {
            QMessageBox::warning(&dialog, tr("Champ requis"),
                tr("Veuillez renseigner le nom ou le chemin du fichier image."));
            return;
        }

        if (mosaicFilePath.isEmpty() || !QFileInfo(mosaicFilePath).isFile()) {
            QMessageBox::warning(&dialog, tr("Export impossible"),
                tr("Aucun TIFF final disponible. Lancez un calcul complet avant l'export."));
            return;
        }

        QString destination = chosenPath;
        if (QFileInfo(destination).suffix().isEmpty()) {
            destination += QStringLiteral(".tif");
        }

        const QFileInfo requestedFile(destination);
        const QString extension = requestedFile.suffix();

        if (extension.compare(QStringLiteral("tif"), Qt::CaseInsensitive) != 0 &&
            extension.compare(QStringLiteral("tiff"), Qt::CaseInsensitive) != 0) {
            QMessageBox::warning(&dialog, tr("Format non pris en charge"),
                tr("Cet export produit un TIFF. Choisissez une extension .tif ou .tiff."));
            return;
        }

        QDir destinationDirectory(requestedFile.absolutePath());
        if (!destinationDirectory.exists() &&
            !QDir().mkpath(destinationDirectory.absolutePath())) {
            QMessageBox::warning(&dialog, tr("Export impossible"),
                tr("Impossible de créer le dossier de destination."));
            return;
        }

        destination = requestedFile.absoluteFilePath();

        int counter = 1;
        while (QFileInfo::exists(destination)) {
            const QString numberedName =
                requestedFile.completeBaseName() +
                QStringLiteral(" (%1).").arg(counter) + extension;
            destination = destinationDirectory.filePath(numberedName);
            ++counter;
        }

        auto *canvas = dynamic_cast<ArtworkCanvas *>(workspaceUI ? workspaceUI->canvas() : nullptr);
        if (canvas && !canvas->isCropped() && canvas->hasActiveCropSelection())
            canvas->applyCrop();
        const bool isCropped = canvas && canvas->isCropped();

        if (isCropped) {
            const QRectF normalizedCropRect = canvas->cropNormalizedRect();
            IONs::PyramidalTiffReaderClass reader(IONs::pathFromUtf8(mosaicFilePath.toStdString()));
            int fullWidth = 0;
            int fullHeight = 0;
            std::string readError;
            if (!reader.getLevelDimensions(0, fullWidth, fullHeight, readError)) {
                QMessageBox::warning(&dialog, tr("Export impossible"),
                    tr("Impossible de lire les dimensions de la mosaïque source : %1")
                        .arg(QString::fromStdString(readError)));
                return;
            }

            const int cropX = qBound(0, static_cast<int>(normalizedCropRect.x() * fullWidth), fullWidth - 1);
            const int cropY = qBound(0, static_cast<int>(normalizedCropRect.y() * fullHeight), fullHeight - 1);
            const int cropWidth = qBound(1, static_cast<int>(normalizedCropRect.width() * fullWidth), fullWidth - cropX);
            const int cropHeight = qBound(1, static_cast<int>(normalizedCropRect.height() * fullHeight), fullHeight - cropY);

            cv::Rect cropRegion(cropX, cropY, cropWidth, cropHeight);
            cv::Mat croppedImage;
            if (!reader.readRegion(0, cropRegion, croppedImage, readError)) {
                QMessageBox::warning(&dialog, tr("Export impossible"),
                    tr("Impossible d'extraire la zone recadrée : %1")
                        .arg(QString::fromStdString(readError)));
                return;
            }

            std::string writeError;
            const auto destinationPath = IONs::pathFromUtf8(destination.toStdString());
            if (!IONs::createPyramidalTiledTiff(croppedImage, destinationPath, writeError)) {
                if (!IONs::writeImage(destinationPath, croppedImage)) {
                    QMessageBox::warning(&dialog, tr("Export impossible"),
                        tr("Impossible d'enregistrer le TIFF recadré : %1")
                            .arg(QString::fromStdString(writeError)));
                    return;
                }
            }
        } else {
            QFile sourceFile(mosaicFilePath);
            if (!sourceFile.copy(destination)) {
                QMessageBox::warning(&dialog, tr("Export impossible"),
                    tr("Impossible de copier le TIFF : %1")
                        .arg(sourceFile.errorString()));
                return;
            }
        }

        const QFileInfo fileInfo(destination);
        outputDirectory = fileInfo.absolutePath();
        markProjectModified();

        updateStatus(tr("TIFF exporté : %1")
                         .arg(fileInfo.absoluteFilePath()));
        dialog.accept();
    });
    dialog.exec();
}
