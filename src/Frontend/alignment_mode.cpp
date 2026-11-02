#include "mainwindow.h"
#include "artworkcanvas.h"
#include "frontendutils.h"
#include "workspace_helpers.h"

#include <QButtonGroup>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

QWidget *MainWindow::createAlignmentMode()
{
    auto *infoPage = new QWidget;
    auto *infoLayout = new QVBoxLayout(infoPage);
    infoLayout->addWidget(new QLabel(tr("Projet sélectionné")));
    projectNameLabel = new QLabel;
    projectNameLabel->setWordWrap(true);
    infoLayout->addWidget(projectNameLabel);
    tileCountLabel = new QLabel;
    infoLayout->addWidget(tileCountLabel);
    auto *projectMetadata = makeMutedLabel(QString(), infoPage);
    projectMetadata->setObjectName(QStringLiteral("projectMetadataLabel"));
    infoLayout->addWidget(projectMetadata);
    auto *maskButton = makeWorkspaceButton(tr("Détecter les bords noirs"), infoPage);
    infoLayout->addWidget(maskButton);
    connect(maskButton, &QPushButton::clicked, this, [this]() {
        setMode(2);
        static_cast<ArtworkCanvas *>(canvas)->setMaskVisible(true);
        updateStatus(tr("Masque proposé. Vérifiez-le avant validation."));
    });
    auto *pairButton = makeWorkspaceButton(tr("Ouvrir la vue Paire"), infoPage);
    infoLayout->addWidget(pairButton);
    connect(pairButton, &QPushButton::clicked, this, [this]() { setMode(3); });
    auto *validateButton = makeWorkspaceButton(tr("Valider la paire"), infoPage, true);
    infoLayout->addWidget(validateButton);
    connect(validateButton, &QPushButton::clicked, this, [this]() {
        updateStatus(tr("Paire validée par l'opérateur; les étapes suivantes sont à refaire."));
    });
    auto *adjustButton = makeWorkspaceButton(tr("Saisir un décalage…"), infoPage);
    infoLayout->addWidget(adjustButton);
    connect(adjustButton, &QPushButton::clicked, this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Correction manuelle"));
        QFormLayout form(&dialog);
        auto *dx = new QDoubleSpinBox(&dialog);
        auto *dy = new QDoubleSpinBox(&dialog);
        dx->setRange(-100000, 100000); dx->setDecimals(1); dx->setValue(4065.5);
        dy->setRange(-100000, 100000); dy->setDecimals(1); dy->setValue(-20.7);
        form.addRow(tr("Décalage horizontal (px)"), dx);
        form.addRow(tr("Décalage vertical (px)"), dy);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted)
            updateStatus(tr("Correction enregistrée : dx %1 px, dy %2 px.")
                         .arg(dx->value()).arg(dy->value()));
    });
    infoLayout->addStretch();
    return infoPage;
}
