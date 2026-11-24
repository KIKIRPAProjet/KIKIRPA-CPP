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
#include "mainwindow.h"
#include "artworkcanvas.h"
#include "frontendutils.h"
#include "projectgrid.h"
#include "workspace_ui.h"

#include "core/io/pyramidal_tiff_reader.h"
#include "core/io/tiled_tiff_writer.h"
#include "core/io/unicode_path.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>


#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIODevice>
#include <QMessageBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
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

    connect(exportButton, &QPushButton::clicked, this, [this, &dialog, format, report, imagePathEdit]() {
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

            const cv::Rect cropRegion(cropX, cropY, cropWidth, cropHeight);

            // Recopie bloc par bloc (IONs::copyRegionToTiledTiff) : lire la zone recadrée d'un seul coup
            // demanderait des dizaines de Go sur une grande mosaïque, alors qu'un bloc suffit. Le fichier
            // produit a le même format que l'export non recadré, qui est une simple copie.
            std::string writeError;
            if (!IONs::copyRegionToTiledTiff(IONs::pathFromUtf8(mosaicFilePath.toStdString()), cropRegion,
                                             IONs::pathFromUtf8(destination.toStdString()), writeError)) {
                QMessageBox::warning(&dialog, tr("Export impossible"),
                    tr("Impossible d'enregistrer le TIFF recadré : %1")
                        .arg(QString::fromStdString(writeError)));
                return;
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

        updateStatus(tr("TIFF exporté : %1")
                         .arg(fileInfo.absoluteFilePath()));
        dialog.accept();
    });
    dialog.exec();
}
