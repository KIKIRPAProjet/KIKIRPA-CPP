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
#include "projectgrid.h"
#include "frontendutils.h"
#include "artworkcanvas.h"
#include "workspaceui.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>

void MainWindow::showExportDialog()
{
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
    format->addItems({tr("TIFF 16 bits"), tr("TIFF 8 bits — perte de précision"),
                      tr("PSB"), tr("JPEG — perte de précision")});
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
        const QString chosenPath = imagePathEdit->text().trimmed();
        if (chosenPath.isEmpty()) {
            QMessageBox::warning(&dialog, tr("Champ requis"),
                tr("Veuillez renseigner le nom ou le chemin du fichier image."));
            return;
        }

        const QFileInfo fileInfo(chosenPath);
        if (!fileInfo.dir().exists()) {
            QDir().mkpath(fileInfo.absolutePath());
        }
        outputDirectory = fileInfo.absolutePath();

        auto *canvas = workspaceUI ? dynamic_cast<ArtworkCanvas*>(workspaceUI->canvas()) : nullptr;
        bool mosaicSaved = false;
        if (canvas && !canvas->mosaicPreview().isNull()) {
            if (!canvas->mosaicPreview().save(fileInfo.absoluteFilePath())) {
                QMessageBox::warning(&dialog, tr("Export impossible"),
                    tr("L'image mosaïque n'a pas pu être enregistrée."));
                return;
            }
            mosaicSaved = true;
        }

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
                    "<h1>Rapport d'assemblage — %1</h1><p>Généré le %2.</p><p>Format demandé : %3</p>"
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

        if (mosaicSaved) {
            updateStatus(tr("Mosaïque et rapport exportés."));
        } else {
            updateStatus(tr("Paramètres d'export enregistrés."));
        }
        dialog.accept();
    });
    dialog.exec();
}
