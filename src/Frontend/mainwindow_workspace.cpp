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
#include <QFrame>
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
#include <QStatusBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>


void MainWindow::createWorkspace()
{
    workspace = new QWidget;
    auto *root = new QVBoxLayout(workspace);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *toolbar = new QWidget;
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 5, 8, 5);
    auto *title = new QLabel(tr("VanDerVeken — Infrarouge — KIK-IRPA Assemblage"));
    title->setObjectName(QStringLiteral("workspaceTitle"));
    toolbarLayout->addWidget(title);
    toolbarLayout->addStretch();
    auto *save = makeWorkspaceButton(tr("Enregistrer"), toolbar);
    toolbarLayout->addWidget(save);
    connect(save, &QPushButton::clicked, this, &MainWindow::saveProject);
    root->addWidget(toolbar);

    auto *stages = new QWidget;
    auto *stageLayout = new QHBoxLayout(stages);
    stageLayout->setContentsMargins(8, 4, 8, 4);
    stageLayout->setSpacing(4);
    auto *modeGroup = new QButtonGroup(stages);
    modeGroup->setExclusive(true);
    for (int i = 0; i < workspaceModeNames().size(); ++i) {
        auto *button = makeWorkspaceButton(QStringLiteral("%1   %2").arg(i + 1).arg(workspaceModeNames().at(i)), stages);
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("mode%1").arg(i));
        modeGroup->addButton(button, i);
        stageLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i]() { setMode(i); });
    }
    auto *run = makeWorkspaceButton(tr("▶ Tout lancer"), stages, true);
    stageLayout->addWidget(run);
    connect(run, &QPushButton::clicked, this, [this]() { startRun(1); });
    auto *compare = makeWorkspaceButton(tr("Comparer…"), stages);
    stageLayout->addWidget(compare);
    connect(compare, &QPushButton::clicked, this, &MainWindow::showComparison);
    root->addWidget(stages);

    auto *modeOptions = new QWidget;
    auto *optionsLayout = new QHBoxLayout(modeOptions);
    optionsLayout->setContentsMargins(10, 3, 10, 3);
    auto *modeLabel = new QLabel(tr("Calques : mosaïque, tuiles, coutures, erreur d'alignement"));
    modeLabel->setObjectName(QStringLiteral("modeDescription"));
    optionsLayout->addWidget(modeLabel);
    optionsLayout->addStretch();
    arrangementCombo = new QComboBox;
    arrangementCombo->addItems({tr("Automatique (par lignes)"),
                                tr("Serpentin vertical, bas gauche"),
                                tr("Serpentin vertical, haut droite"),
                                tr("Serpentin horizontal"), tr("Par lignes"), tr("Par colonnes")});
    arrangementCombo->setObjectName(QStringLiteral("arrangementCombo"));
    optionsLayout->addWidget(arrangementCombo);
    connect(arrangementCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int value) {
        captureOrder = value;
        gridValidated = false;
        refreshGrid();
        updateStatus(tr("Ordre de prise de vue : %1").arg(arrangementCombo->currentText()));
    });
    root->addWidget(modeOptions);

    auto *body = new QSplitter(Qt::Horizontal);
    body->setHandleWidth(5);
    body->setChildrenCollapsible(false);
    auto *tools = new QWidget;
    tools->setFixedWidth(42);
    auto *toolLayout = new QVBoxLayout(tools);
    toolLayout->setContentsMargins(4, 5, 4, 5);
    const QStringList toolNames = {tr("Main (H)"), tr("Zoom (Z)"), tr("Sélection (V)"),
        tr("Pinceau (B)"), tr("Gomme (E)"), tr("Lasso (L)"), tr("Rectangle (M)"),
        tr("Point (P)"), tr("Mesure (I)")};
    for (int i = 0; i < toolNames.size(); ++i) {
        auto *tool = new QToolButton(tools);
        tool->setText(QStringList{QStringLiteral("✋"), QStringLiteral("⌕"), QStringLiteral("↖"),
                                  QStringLiteral("✎"), QStringLiteral("⌫"), QStringLiteral("⬠"),
                                  QStringLiteral("▭"), QStringLiteral("＋"), QStringLiteral("⌁")}.at(i));
        tool->setToolTip(toolNames.at(i));
        tool->setFixedSize(32, 32);
        toolLayout->addWidget(tool);
        connect(tool, &QToolButton::clicked, this, [this, toolNames, i]() {
            updateStatus(tr("Outil actif : %1").arg(toolNames.at(i)));
            if (currentMode == 2 && i >= 3 && i <= 6) {
                static_cast<ArtworkCanvas *>(canvas)->toggleMask();
            }
        });
    }
    toolLayout->addStretch();
    body->addWidget(tools);

    auto *center = new QWidget;
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    createProjectMode(center);
    canvas = new ArtworkCanvas(center);
    centerLayout->addWidget(canvas, 1);
    auto *canvasControls = new QWidget;
    canvasControls->setObjectName(QStringLiteral("canvasControls"));
    auto *canvasControlsLayout = new QHBoxLayout(canvasControls);
    canvasControlsLayout->setContentsMargins(8, 3, 8, 3);
    auto *minus = makeWorkspaceButton(QStringLiteral("−"), canvasControls);
    auto *plus = makeWorkspaceButton(QStringLiteral("+"), canvasControls);
    zoomLabel = new QLabel(QStringLiteral("12,4 %"));
    auto *fit = makeWorkspaceButton(tr("Ajuster"), canvasControls);
    auto *oneToOne = makeWorkspaceButton(QStringLiteral("100 %"), canvasControls);
    canvasControlsLayout->addWidget(minus);
    canvasControlsLayout->addWidget(plus);
    canvasControlsLayout->addWidget(zoomLabel);
    canvasControlsLayout->addWidget(fit);
    canvasControlsLayout->addWidget(oneToOne);
    canvasControlsLayout->addStretch();
    centerLayout->addWidget(canvasControls);
    projectInfoPanel->setVisible(false);
    connect(minus, &QPushButton::clicked, this, [this]() {
        zoomPercent = qMax(5, zoomPercent - 5);
        zoomLabel->setText(QString::number(zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    connect(plus, &QPushButton::clicked, this, [this]() {
        zoomPercent = qMin(1600, zoomPercent + 5);
        zoomLabel->setText(QString::number(zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    connect(fit, &QPushButton::clicked, this, [this]() {
        zoomLabel->setText(tr("Ajuster"));
        static_cast<ArtworkCanvas *>(canvas)->setZoom(100);
    });
    connect(oneToOne, &QPushButton::clicked, this, [this]() {
        zoomPercent = 100;
        zoomLabel->setText(QStringLiteral("100 %"));
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    body->addWidget(center);

    rightTabs = new QTabWidget;
    rightTabs->setMinimumWidth(218);

    gridTabPage = createTilesMode();
    rightTabs->addTab(createMasksMode(), tr("Calques"));
    rightTabs->addTab(createAssemblyMode(), tr("Chaîne"));
    rightTabs->addTab(createAlignmentMode(), tr("Infos"));
    body->addWidget(rightTabs);
    body->setStretchFactor(0, 0);
    body->setStretchFactor(1, 1);
    body->setStretchFactor(2, 0);
    body->setSizes({42, 760, 280});

    bottomTabs = new QTabWidget;
    bottomTabs->setMinimumHeight(80);
    pairsTable = new QTableWidget(4, 8, bottomTabs);
    pairsTable->setHorizontalHeaderLabels({tr("Paire"), tr("Sens"), tr("Statut"), tr("Confiance"),
        tr("Contrôle NCC"), tr("dx (px)"), tr("dy (px)"), tr("Erreur")});
    const QStringList pairRows[] = {
        {QStringLiteral("r00_c00 → r00_c01"), tr("horizontal"), tr("douteuse"), QStringLiteral("0,91"), QStringLiteral("0,37"), QStringLiteral("4 065,5"), QStringLiteral("-20,7"), QStringLiteral("3,95 px")},
        {QStringLiteral("r00_c00 → r01_c00"), tr("vertical"), tr("fiable"), QStringLiteral("0,99"), QStringLiteral("0,76"), QStringLiteral("23,5"), QStringLiteral("4 086,7"), QStringLiteral("0,04 px")},
        {QStringLiteral("r00_c01 → r01_c01"), tr("vertical"), tr("fiable"), QStringLiteral("0,99"), QStringLiteral("0,92"), QStringLiteral("23,8"), QStringLiteral("4 133,7"), QStringLiteral("0,04 px")},
        {QStringLiteral("r01_c00 → r01_c01"), tr("horizontal"), tr("fiable"), QStringLiteral("0,98"), QStringLiteral("0,65"), QStringLiteral("4 064,7"), QStringLiteral("29,9"), QStringLiteral("0,02 px")}
    };
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 8; ++col)
            pairsTable->setItem(row, col, new QTableWidgetItem(pairRows[row].at(col)));
    }
    pairsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    pairsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    pairsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bottomTabs->addTab(pairsTable, tr("Paires (4)"));
    auto *journal = new QListWidget;
    journal->addItems({tr("14:12  Assemblage terminé — une paire à vérifier"),
                       tr("14:10  4 tuiles importées"),
                       tr("14:02  Projet ouvert")});
    bottomTabs->addTab(journal, tr("Journal"));
    auto *positions = new QTableWidget(4, 3);
    positions->setHorizontalHeaderLabels({tr("Tuile"), tr("X"), tr("Y")});
    for (int i = 0; i < 4; ++i) {
        positions->setItem(i, 0, new QTableWidgetItem(QStringLiteral("r%1_c%2").arg(i / 2).arg(i % 2)));
        positions->setItem(i, 1, new QTableWidgetItem(QString::number((i % 2) * 4065)));
        positions->setItem(i, 2, new QTableWidgetItem(QString::number((i / 2) * 4080)));
    }
    bottomTabs->addTab(positions, tr("Positions"));
    connect(pairsTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { setMode(3); });
    auto *contentSplitter = new QSplitter(Qt::Vertical);
    contentSplitter->setHandleWidth(5);
    contentSplitter->setChildrenCollapsible(false);
    contentSplitter->addWidget(body);
    contentSplitter->addWidget(bottomTabs);
    contentSplitter->setStretchFactor(0, 1);
    contentSplitter->setStretchFactor(1, 0);
    contentSplitter->setSizes({560, 165});
    root->addWidget(contentSplitter, 1);

    auto *status = new QWidget;
    auto *statusLayout = new QHBoxLayout(status);
    statusLayout->setContentsMargins(10, 3, 10, 3);
    zoomLabel->setMinimumWidth(48);
    statusLayout->addWidget(makeMutedLabel(tr("x 1 812  y 2 310 px    Tuile r00_c01"), status));
    statusLayout->addStretch();
    statusLabel = new QLabel(tr("Affichage ajusté (contraste)"));
    statusLayout->addWidget(statusLabel);
    root->addWidget(status);

    currentStepLabel = new QLabel(tr("Aucune étape"), statusBar());
    progressBar = new QProgressBar(statusBar());
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setFixedWidth(160);
    statusBar()->addPermanentWidget(currentStepLabel);
    statusBar()->addPermanentWidget(progressBar);

    pages->addWidget(workspace);
    refreshGrid();
    refreshProjectInfo();
    connect(pairsTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        updateStatus(tr("Paire sélectionnée : %1").arg(pairsTable->item(row, 0)->text()));
    });
}
