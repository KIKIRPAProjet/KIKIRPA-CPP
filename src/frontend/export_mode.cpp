// src/frontend/export_mode.cpp
/*
 * .cpp : le POURQUOI
 * - choix de conception : centraliser les options d'export dans une boîte de dialogue ;
 * - cas limite : le backend d'export peut ne pas être connecté dans le prototype ;
 * - unité ou repère : les formats et profils sont ceux du projet courant ;
 * - raison non évidente : le rapport est proposé séparément pour permettre un export sans rapport.
 */
/*
 * Header : le QUOI
 * Gère l'export du projet et la création du rapport d'assemblage.
 * - ce que ça fait : affiche les options d'export et prépare la génération du fichier de sortie ;
 * - ce que ça renvoie : aucune valeur directement, les résultats sont transmis à l'interface ;
 * - quand ça lève une exception : si la boîte de dialogue ou l'enregistrement du fichier échoue.
 */
//  gere l'export du projet et la creation du rapport
// Code historique de la fenêtre d'export non encore découpé selon les seuils clang-tidy (CONVENTIONS.md §9).
// NOLINTBEGIN
#include "mainwindow.h"
#include "artworkcanvas.h"
#include "frontendutils.h"
#include "projectgrid.h"
#include "workspace_ui.h"

#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/pyramidal_tiff_writer.h"
#include "core/io/unicode_path.h"
#include "steps/export_step.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>


#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIODevice>
#include <QMessageBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QtConcurrentRun>
#include <QVBoxLayout>
#include <QFile>

void MainWindow::showExportDialog()
{
    if (mosaicFilePath.isEmpty() || !QFileInfo(mosaicFilePath).isFile()) {
        QMessageBox::information(this, tr("Image non disponible"),
            tr("L'image finale n'est pas encore prête. Lancez le calcul complet avant de pouvoir l'exporter."));
        return;
    }

    auto *canvas = dynamic_cast<ArtworkCanvas *>(workspaceUI ? workspaceUI->canvas() : nullptr);
    if (canvas && !canvas->isCropped() && canvas->hasActiveCropSelection()) {
        canvas->applyCrop();
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Exporter le projet"));
    QVBoxLayout layout(&dialog);

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

    auto *format = new QComboBox(&dialog);
    format->addItems({tr("TIFF 16 bits"), tr("TIFF 8 bits, perte de précision"),
                      tr("PSB"), tr("JPEG, perte de précision")});
    constexpr int JPEG_FORMAT_INDEX = 3;

    connect(browseButton, &QPushButton::clicked, &dialog, [this, &dialog, imagePathEdit, format]() {
        const QString filter = format->currentIndex() == JPEG_FORMAT_INDEX
            ? tr("Image JPEG (*.jpg *.jpeg);;Tous les fichiers (*.*)")
            : tr("Image TIFF (*.tif *.tiff);;Tous les fichiers (*.*)");
        const QString path = QFileDialog::getSaveFileName(
            &dialog,
            tr("Choisir l'emplacement de l'image finale"),
            imagePathEdit->text(),
            filter);
        if (!path.isEmpty()) {
            imagePathEdit->setText(path);
        }
    });

    connect(format, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), &dialog,
            [imagePathEdit](int index) {
        QString path = imagePathEdit->text().trimmed();
        if (path.isEmpty()) return;
        const QString targetExt = (index == JPEG_FORMAT_INDEX) ? QStringLiteral(".jpg") : QStringLiteral(".tif");
        const QFileInfo info(path);
        if (!info.suffix().isEmpty()) {
            path.chop(info.suffix().size() + 1);
        }
        imagePathEdit->setText(path + targetExt);
    });

    auto *report = new QCheckBox(tr("Générer le rapport d'assemblage (HTML)"), &dialog);
    report->setChecked(true);
    layout.addWidget(new QLabel(tr("Format de sortie"), &dialog));
    layout.addWidget(format);
    layout.addWidget(new QLabel(tr("Profil ICC des tuiles conservé"), &dialog));
    layout.addWidget(report);
    layout.addWidget(FrontendUtils::mutedLabel(
        tr("L'export de la mosaïque en pleine résolution sera généré par le backend."),
        &dialog));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *exportButton = buttons->addButton(tr("Exporter…"),
                                            QDialogButtonBox::AcceptRole);
    layout.addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    connect(exportButton, &QPushButton::clicked, &dialog, [this, &dialog, format, report, imagePathEdit]() {
        const bool isJpeg = format->currentIndex() == JPEG_FORMAT_INDEX;
        if (format->currentIndex() != 0 && !isJpeg) {
            QMessageBox::information(&dialog, tr("Format non disponible"),
                tr("Seuls les exports TIFF 16 bits et JPEG sont actuellement pris en charge."));
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
            destination += isJpeg ? QStringLiteral(".jpg") : QStringLiteral(".tif");
        }

        const QFileInfo requestedFile(destination);
        const QString extension = requestedFile.suffix();
        const bool validExtension = isJpeg
            ? (extension.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0 ||
               extension.compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0)
            : (extension.compare(QStringLiteral("tif"), Qt::CaseInsensitive) == 0 ||
               extension.compare(QStringLiteral("tiff"), Qt::CaseInsensitive) == 0);

        if (!validExtension) {
            QMessageBox::warning(&dialog, tr("Format non pris en charge"),
                isJpeg
                    ? tr("Cet export produit un JPEG. Choisissez une extension .jpg ou .jpeg.")
                    : tr("Cet export produit un TIFF. Choisissez une extension .tif ou .tiff."));
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
        const bool isCropped = canvas && canvas->isCropped();
        cv::Rect cropRegion;

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
            cropRegion = cv::Rect(cropX, cropY, cropWidth, cropHeight);
        }

        if (isJpeg) {
            const auto sourcePath = IONs::pathFromUtf8(mosaicFilePath.toStdString());
            const auto destinationPath = IONs::pathFromUtf8(destination.toStdString());
            dialog.setEnabled(false);
            QFutureWatcher<QString> watcher;
            QEventLoop loop(&dialog);
            connect(&watcher, &QFutureWatcher<QString>::finished, &loop, &QEventLoop::quit);
            connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
            watcher.setFuture(QtConcurrent::run([sourcePath, cropRegion, destinationPath]() {
                std::string writeError;
                if (StepsNs::exportTiffRegionToBaselineJpeg(sourcePath, cropRegion, destinationPath, writeError))
                    return QString();
                return writeError.empty() ? QStringLiteral("erreur inconnue")
                                          : QString::fromStdString(writeError);
            }));
            loop.exec();
            if (!watcher.isFinished()) {
                watcher.waitForFinished();
                return;
            }
            dialog.setEnabled(true);
            const QString errorText = watcher.result();
            if (!errorText.isEmpty()) {
                QMessageBox::warning(&dialog, tr("Export impossible"),
                    tr("Impossible d'enregistrer le JPEG : %1").arg(errorText));
                return;
            }
        } else if (isCropped) {
            IONs::PyramidalTiffReaderClass reader(IONs::pathFromUtf8(mosaicFilePath.toStdString()));
            std::string readError;
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

        if (report->isChecked()) {
            const QString reportName = projectName + QStringLiteral("_rapport.html");
            const QString defaultPath = outputDirectory.isEmpty()
                ? reportName : QDir(outputDirectory).filePath(reportName);
            const QString path = QFileDialog::getSaveFileName(&dialog, tr("Enregistrer le rapport"),
                defaultPath, tr("Rapport HTML (*.html)"));
            if (!path.isEmpty()) {
                QSaveFile file(path);
                const QString html = QStringLiteral(
                    "<!doctype html><meta charset=\"utf-8\"><title>Rapport KIK-IRPA</title>"
                    "<h1>Rapport d'assemblage : %1</h1><p>Généré le %2.</p><p>Format demandé : %3</p>"
                    "<p>Fichier image : %4</p><p>Tuiles référencées : %5</p><p>Les mesures présentées sont celles de la maquette.</p>")
                    .arg(projectName.toHtmlEscaped(),
                         QDateTime::currentDateTime().toString(Qt::ISODate).toHtmlEscaped(),
                         format->currentText().toHtmlEscaped(),
                         fileInfo.fileName().toHtmlEscaped())
                    .arg(projectGrid ? projectGrid->state().tilePaths.size() : 0);
                if (!file.open(QIODevice::WriteOnly) || file.write(html.toUtf8()) < 0 || !file.commit()) {
                    QMessageBox::warning(&dialog, tr("Export impossible"),
                        tr("Le rapport n'a pas pu être enregistré."));
                    return;
                }
            }
        }

        updateStatus((isJpeg ? tr("JPEG exporté : %1") : tr("TIFF exporté : %1"))
                         .arg(fileInfo.absoluteFilePath()));
        dialog.accept();
    });
    dialog.exec();
}
// NOLINTEND
