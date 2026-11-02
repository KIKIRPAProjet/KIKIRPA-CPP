#include "mainwindow.h"
#include "artworkcanvas.h"
#include "frontendutils.h"
#include "workspace_helpers.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
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
#include <QToolButton>
#include <QVBoxLayout>

void MainWindow::createProjectMode(QWidget *center)
{
    projectInfoPanel = new QWidget(center);
    auto *projectInfoLayout = new QVBoxLayout(projectInfoPanel);
    projectInfoLayout->setContentsMargins(32, 32, 32, 32);
    projectInfoLayout->setSpacing(14);
    auto *projectInfoTitle = new QLabel(tr("Informations du projet"), projectInfoPanel);
    projectInfoTitle->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
    projectInfoLayout->addWidget(projectInfoTitle);

    auto *projectNameForm = new QFormLayout;
    projectNameEdit = new QLineEdit(projectInfoPanel);
    projectNameEdit->setObjectName(QStringLiteral("projectNameEdit"));
    auto *renameProject = makeWorkspaceButton(tr("Renommer"), projectInfoPanel);
    auto *projectNameRow = new QHBoxLayout;
    projectNameRow->addWidget(projectNameEdit, 1);
    projectNameRow->addWidget(renameProject);
    projectNameForm->addRow(tr("Nom du projet"), projectNameRow);
    connect(renameProject, &QPushButton::clicked, this, [this]() {
        const QString newName = projectNameEdit->text().trimmed();
        if (newName.isEmpty()) {
            QMessageBox::warning(this, tr("Nom invalide"),
                tr("Le nom du projet ne peut pas être vide."));
            return;
        }
        projectName = newName;
        refreshProjectInfo();
        updateStatus(tr("Projet renommé : %1").arg(projectName));
    });
    projectInfoLayout->addLayout(projectNameForm);

    projectFolderLabel = makeMutedLabel(QString(), projectInfoPanel);
    projectFolderLabel->setWordWrap(true);
    projectInfoLayout->addWidget(projectFolderLabel);
    projectFormatLabel = new QLabel(projectInfoPanel);
    projectFormatLabel->setWordWrap(true);
    projectInfoLayout->addWidget(projectFormatLabel);
    projectInfoLayout->addStretch();
    static_cast<QVBoxLayout *>(center->layout())->addWidget(projectInfoPanel, 1);

}
