#include "Frontend/workspace/workspace_center.h"
#include "Frontend/app/mainwindow.h"
#include "Frontend/common/frontendutils.h"
#include "Frontend/canvas/artworkcanvas.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>

WorkspaceCenter::WorkspaceCenter(MainWindow *mainWindow, QWidget *parent)
    :QWidget(parent), m_mainWindow(mainWindow)
{
    auto *centerLayout = new QVBoxLayout(this);
    centerLayout->setContentsMargins(0,0,0,0);

    createProjectMode();
    centerLayout->addWidget(m_projectInfoPanel,1);

    createCanvasAndControls();

    m_projectInfoPanel->setVisible(false);
}

void WorkspaceCenter::createProjectMode()
{
    m_projectInfoPanel = new QWidget(this);
    auto *projectInfoLayout = new QVBoxLayout(m_projectInfoPanel);
    projectInfoLayout->setContentsMargins(32, 32, 32, 32);
    projectInfoLayout->setSpacing(14);

    auto *projectInfoTitle = new QLabel(tr("Informations du projet"), m_projectInfoPanel);
    projectInfoTitle->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
    projectInfoLayout->addWidget(projectInfoTitle);

    auto *projectNameForm = new QFormLayout;
    m_projectNameEdit = new QLineEdit(m_projectInfoPanel);
    m_projectNameEdit->setObjectName(QStringLiteral("projectNameEdit"));

    auto *renameProject = FrontendUtils::makeButton(tr("Renommer"), m_projectInfoPanel);
    auto *projectNameRow = new QHBoxLayout;
    projectNameRow->addWidget(m_projectNameEdit, 1);
    projectNameRow->addWidget(renameProject);
    projectNameForm->addRow(tr("Nom du projet"), projectNameRow);

    connect(renameProject, &QPushButton::clicked, this, [this]() {
        const QString newName = m_projectNameEdit->text().trimmed();
        if (newName.isEmpty()) {
            QMessageBox::warning(m_mainWindow, tr("Nom invalide"),
                tr("Le nom du projet ne peut pas être vide."));
            return;
        }
        m_mainWindow->projectName = newName;
        m_mainWindow->refreshProjectInfo();
        m_mainWindow->updateStatus(
            tr("Projet renommé : %1").arg(m_mainWindow->projectName));
    });

    projectInfoLayout->addLayout(projectNameForm);

    m_projectFolderLabel = FrontendUtils::mutedLabel(QString(), m_projectInfoPanel);
    m_projectFolderLabel->setWordWrap(true);
    projectInfoLayout->addWidget(m_projectFolderLabel);

    m_projectFormatLabel = new QLabel(m_projectInfoPanel);
    m_projectFormatLabel->setWordWrap(true);
    projectInfoLayout->addWidget(m_projectFormatLabel);

    projectInfoLayout->addStretch();
}

void WorkspaceCenter::createCanvasAndControls()
{
    m_canvas = new ArtworkCanvas(this);
    static_cast<QVBoxLayout *>(layout())->addWidget(m_canvas,1);

    auto *canvasControls = new QWidget;
    canvasControls->setObjectName(QStringLiteral("canvasControls"));
    auto *canvasControlsLayout = new QHBoxLayout(canvasControls);
    canvasControlsLayout->setContentsMargins(8, 3, 8, 3);
    
    auto *minus = FrontendUtils::makeButton(QStringLiteral("−"), canvasControls);
    auto *plus = FrontendUtils::makeButton(QStringLiteral("+"), canvasControls);
    
    m_zoomLabel = new QLabel(QStringLiteral("12,4 %"));
    m_zoomLabel->setMinimumWidth(48);

    auto *fit = FrontendUtils::makeButton(tr("Ajuster"), canvasControls);
    auto *oneToOne = FrontendUtils::makeButton(QStringLiteral("100 %"), canvasControls);
    
    canvasControlsLayout->addWidget(minus);
    canvasControlsLayout->addWidget(plus);
    canvasControlsLayout->addWidget(m_zoomLabel);
    canvasControlsLayout->addWidget(fit);
    canvasControlsLayout->addWidget(oneToOne);
    canvasControlsLayout->addStretch();
    static_cast<QVBoxLayout *>(layout())->addWidget(canvasControls);

    connect(minus, &QPushButton::clicked, this, [this]() {
        m_mainWindow->zoomPercent = qMax(5, m_mainWindow->zoomPercent - 5);
        m_zoomLabel->setText(QString::number(m_mainWindow->zoomPercent) + "%");
        m_canvas->setZoom(m_mainWindow->zoomPercent);
    });
    connect(plus, &QPushButton::clicked, this, [this]() {
        m_mainWindow->zoomPercent = qMin(1600, m_mainWindow->zoomPercent + 5);
        m_zoomLabel->setText(QString::number(m_mainWindow->zoomPercent) + "%");
        m_canvas->setZoom(m_mainWindow->zoomPercent);
    });
    connect(fit, &QPushButton::clicked, this, [this]() {
        m_zoomLabel->setText(tr("Ajuster"));
        m_canvas->setZoom(100);
    });
    connect(oneToOne, &QPushButton::clicked, this, [this]() {
        m_mainWindow->zoomPercent = 100;
        m_zoomLabel->setText(QStringLiteral("100 %"));
        m_canvas->setZoom(m_mainWindow->zoomPercent);
    });

}

void WorkspaceCenter::updateProjectInfo(const QString &projectName, const ProjectGrid::State &gridState)
{
    m_projectNameEdit->setText(projectName);

    m_projectFolderLabel->setText(gridState.tileDirectory.isEmpty()
            ? tr("Dossier sélectionné : aucun dossier")
            : tr("Dossier sélectionné :\n%1").arg(gridState.tileDirectory));
    
    const QFileInfoList folderFiles = gridState.tileDirectory.isEmpty()
            ? QFileInfoList()
            : QDir(gridState.tileDirectory).entryInfoList(
                  QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        
    int validFiles = 0;
    for (const QFileInfo &file : folderFiles) {
        const QString suffix = file.suffix().toLower();
            if (suffix == QStringLiteral("tif") || suffix == QStringLiteral("tiff"))
                ++validFiles;
        }   
    const bool allFilesValid = !folderFiles.isEmpty() && validFiles == folderFiles.size();
    
    m_projectFormatLabel->setText(allFilesValid
            ? tr("Format : tous les éléments sont au format TIFF compatible (%1 fichier(s)).")
                  .arg(validFiles)
            : tr("Format : %1 fichier(s) TIFF compatible(s) sur %2 ; certains éléments "
                "ne sont pas au bon format.")
                  .arg(validFiles).arg(folderFiles.size()));

    m_projectFormatLabel->setStyleSheet(allFilesValid
            ? QStringLiteral("color:#66c98a;")
            : QStringLiteral("color:#e0a45f;"));
}