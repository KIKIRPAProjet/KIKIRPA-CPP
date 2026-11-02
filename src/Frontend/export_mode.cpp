#include "mainwindow.h"
#include "projectgrid.h"
#include "workspace_helpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>

void MainWindow::showExportDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Exporter le projet"));
    QVBoxLayout layout(&dialog);
    auto *format = new QComboBox(&dialog);
    format->addItems({tr("TIFF 16 bits"), tr("TIFF 8 bits — perte de précision"),
                      tr("PSB"), tr("JPEG — perte de précision")});
    auto *report = new QCheckBox(tr("Générer le rapport d'assemblage (HTML)"), &dialog);
    report->setChecked(true);
    layout.addWidget(new QLabel(tr("Format de sortie"), &dialog));
    layout.addWidget(format);
    layout.addWidget(new QLabel(tr("Profil ICC des tuiles conservé"), &dialog));
    layout.addWidget(report);
    layout.addWidget(makeMutedLabel(
        tr("L'export de la mosaïque nécessite le backend, non connecté dans ce prototype."),
        &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *exportReport = buttons->addButton(tr("Enregistrer le rapport…"),
                                            QDialogButtonBox::AcceptRole);
    layout.addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(exportReport, &QPushButton::clicked, this, [this, &dialog, format, report]() {
        if (!report->isChecked()) {
            QMessageBox::information(&dialog, tr("Backend requis"),
                tr("La génération de l'image mosaïque sera disponible une fois le backend connecté."));
            return;
        }
        const QString reportName = projectName + QStringLiteral("_rapport.html");
        const QString defaultPath = outputDirectory.isEmpty()
            ? reportName : QDir(outputDirectory).filePath(reportName);
        const QString path = QFileDialog::getSaveFileName(&dialog, tr("Enregistrer le rapport"),
            defaultPath, tr("Rapport HTML (*.html)"));
        if (path.isEmpty())
            return;
        QSaveFile file(path);
        const QString html = QStringLiteral(
            "<!doctype html><meta charset=\"utf-8\"><title>Rapport KIK-IRPA</title>"
            "<h1>Rapport d'assemblage — %1</h1><p>Généré le %2.</p><p>Format demandé : %3</p>"
            "<p>Tuiles référencées : %4</p><p>Les mesures présentées sont celles de la maquette.</p>")
            .arg(projectName.toHtmlEscaped(),
                 QDateTime::currentDateTime().toString(Qt::ISODate).toHtmlEscaped(),
                 format->currentText().toHtmlEscaped())
                .arg(projectGrid->state().tilePaths.size());
        if (!file.open(QIODevice::WriteOnly) || file.write(html.toUtf8()) < 0 || !file.commit()) {
            QMessageBox::warning(&dialog, tr("Export impossible"),
                tr("Le rapport n'a pas pu être enregistré."));
            return;
        }
        updateStatus(tr("Rapport HTML exporté."));
        dialog.accept();
    });
    dialog.exec();
}
