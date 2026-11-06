#include "Frontend/workspace/alignment_tab.h"
#include "Frontend/app/mainwindow.h"
#include "Frontend/canvas/artworkcanvas.h"
#include "Frontend/common/frontendutils.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialog>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>

AlignmentTab::AlignmentTab(MainWindow *mainWindow, ArtworkCanvas *canvas, QWidget *parent)
    : QWidget(parent), m_mainWindow(mainWindow), m_canvas(canvas)
{
    auto *infoLayout = new QVBoxLayout(this);

    infoLayout->addWidget(new QLabel(tr("Projet sélectionné"), this));
    m_projectNameLabel = new QLabel;
    m_projectNameLabel->setWordWrap(true);
    infoLayout->addWidget(m_projectNameLabel);

    m_tileCountLabel = new QLabel;
    infoLayout->addWidget(m_tileCountLabel);

    m_projectMetadataLabel = FrontendUtils::mutedLabel(QString(), this);
    m_projectMetadataLabel->setObjectName(QStringLiteral("projectMetadataLabel"));
    infoLayout->addWidget(m_projectMetadataLabel);

    auto *maskButton = FrontendUtils::makeButton(tr("Détecter les bords noirs"), this);
    auto *pairButton = FrontendUtils::makeButton(tr("Ouvrir la vue Paire"), this);
    auto *validateButton = FrontendUtils::makeButton(tr("Valider la paire"), this, true);
    auto *adjustButton = FrontendUtils::makeButton(tr("Saisir un décalage…"), this);

    infoLayout->addWidget(pairButton);
    infoLayout->addWidget(maskButton);
    infoLayout->addWidget(validateButton);
    infoLayout->addWidget(adjustButton);
    infoLayout->addStretch();

    connect(maskButton, &QPushButton::clicked, this, [this]() {
        m_mainWindow->setMode(2);
        static_cast<ArtworkCanvas *>(m_canvas)->setMaskVisible(true);
        m_mainWindow->updateStatus(tr("Masque proposé. Vérifiez-le avant validation."));
    });
    connect(pairButton, &QPushButton::clicked, this, [this]() { m_mainWindow->setMode(3); });
    connect(validateButton, &QPushButton::clicked, this, [this]() {
        m_mainWindow->updateStatus(tr("Paire validée par l'opérateur; les étapes suivantes sont à refaire."));
    });
    
    connect(adjustButton, &QPushButton::clicked,
            this, &AlignmentTab::showManualAdjustmentDialog);
}

void AlignmentTab::showManualAdjustmentDialog()
{
    QDialog dialog(m_mainWindow);
    dialog.setWindowTitle(tr("Correction manuelle"));
    QFormLayout form(&dialog);

    auto *dx = new QDoubleSpinBox(&dialog);
    auto *dy = new QDoubleSpinBox(&dialog);  
    dx->setRange(-100000, 100000); 
    dx->setDecimals(1); 
    dx->setValue(4065.5);
    
    dy->setRange(-100000, 100000); 
    dy->setDecimals(1); 
    dy->setValue(-20.7);
        
    form.addRow(tr("Décalage horizontal (px)"), dx);
    form.addRow(tr("Décalage vertical (px)"), dy);
        
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(buttons);
    
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    
    if (dialog.exec() == QDialog::Accepted){
            m_mainWindow->updateStatus(tr("Correction enregistrée : dx %1 px, dy %2 px.")
                         .arg(dx->value()).arg(dy->value()));}
}

void AlignmentTab::updateInfo(const QString &projectName, const ProjectGrid::State &gridState)
{
    m_projectNameLabel->setText(projectName);
    m_tileCountLabel->setText(
            tr("%1 tuile(s) chargée(s)").arg(gridState.tilePaths.size()));

    const QString scaleText = gridState.scaleMmPerPixel > 0.0
        ? tr("%1 mm/pixel").arg(gridState.scaleMmPerPixel, 0, 'g', 8)
        : tr("à renseigner");

    m_projectMetadataLabel->setText(tr("Modalité : %1\nÉchelle : %2\nRecouvrement : %3 % / %4 %")
                              .arg(gridState.modality, scaleText)
                              .arg(gridState.horizontalOverlap).arg(gridState.verticalOverlap));
}
