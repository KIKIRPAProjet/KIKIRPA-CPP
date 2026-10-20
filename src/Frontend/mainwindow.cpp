#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QAbstractItemView>

namespace {
const QStringList modeNames = {
    QObject::tr("Projet"), QObject::tr("Tuiles"), QObject::tr("Masques"),
    QObject::tr("Alignement"), QObject::tr("Assemblage"), QObject::tr("Export")
};

QPushButton *makeButton(const QString &text, QWidget *parent, bool primary = false)
{
    auto *button = new QPushButton(text, parent);
    button->setProperty("primary", primary);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QLabel *mutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("muted", true);
    return label;
}

class ArtworkCanvas final : public QWidget
{
public:
    explicit ArtworkCanvas(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(360, 260);
        setMouseTracking(true);
    }

    void setMode(int mode) { m_mode = mode; update(); }
    void setZoom(int zoom) { m_zoom = zoom; update(); }
    void toggleComparison() { m_comparison = !m_comparison; update(); }
    void toggleMask() { m_mask = !m_mask; update(); }
    void setMaskVisible(bool visible) { m_mask = visible; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#484848"));

        if (m_mode == 1) {
            drawGrid(p);
            return;
        }

        const bool pair = m_mode == 3;
        const int gutter = pair ? 22 : 0;
        const int maxW = pair ? (width() - 90) / 2 : width() - 130;
        const int maxH = height() - 80;
        const double aspect = pair ? 0.78 : 0.70;
        int imageH = qMin(maxH, int(maxW / aspect));
        int imageW = int(imageH * aspect);
        if (imageW > maxW) {
            imageW = maxW;
            imageH = int(imageW / aspect);
        }
        imageW = qMax(130, imageW);
        imageH = qMax(180, imageH);
        const int totalW = pair ? imageW * 2 + gutter : imageW;
        const int x0 = (width() - totalW) / 2;
        const int y0 = (height() - imageH) / 2;

        drawPainting(p, QRect(x0, y0, imageW, imageH));
        if (pair) {
            drawPainting(p, QRect(x0 + imageW + gutter, y0, imageW, imageH));
            p.setPen(QPen(QColor("#39b9e8"), 2));
            for (int i = 0; i < 10; ++i) {
                const QPoint a(x0 + imageW - 10 + (i * 31) % qMax(1, imageW / 3),
                               y0 + imageH / 3 + i * imageH / 20);
                const QPoint b(x0 + imageW + gutter + 15 + (i * 37) % qMax(1, imageW / 3),
                               a.y() + (i % 3 - 1) * 8);
                p.drawEllipse(a, 3, 3);
                p.drawEllipse(b, 3, 3);
                p.drawLine(a, b);
            }
            p.setPen(QColor("#eeeeee"));
            p.drawText(QRect(x0, y0 - 24, imageW, 20), Qt::AlignLeft, tr("Tuile A  r00_c00"));
            p.drawText(QRect(x0 + imageW + gutter, y0 - 24, imageW, 20),
                       Qt::AlignLeft, tr("Tuile B  r00_c01"));
            return;
        }

        if (m_mode == 2 && m_mask) {
            QPainterPath mask;
            mask.moveTo(x0 + imageW * 0.10, y0);
            mask.lineTo(x0 + imageW * 0.98, y0);
            mask.lineTo(x0 + imageW * 0.98, y0 + imageH * 0.06);
            mask.lineTo(x0 + imageW * 0.12, y0 + imageH * 0.10);
            mask.closeSubpath();
            p.fillPath(mask, QColor(185, 39, 36, 150));
            p.setPen(QPen(QColor("#ef5149"), 2));
            p.drawPath(mask);
            p.setPen(QColor("#eeeeee"));
            p.drawText(QRect(x0, y0 - 24, imageW, 20), Qt::AlignLeft,
                       tr("Masque d'exclusion — proposition"));
        } else if (m_mode == 4 || m_mode == 5) {
            p.setPen(QPen(QColor("#df4038"), 2));
            QPainterPath seam;
            seam.moveTo(x0 + imageW * 0.72, y0);
            seam.cubicTo(x0 + imageW * 0.68, y0 + imageH * .20,
                         x0 + imageW * .83, y0 + imageH * .29,
                         x0 + imageW * .65, y0 + imageH * .44);
            seam.cubicTo(x0 + imageW * .58, y0 + imageH * .70,
                         x0 + imageW * .76, y0 + imageH * .76,
                         x0 + imageW * .69, y0 + imageH);
            p.drawPath(seam);
            if (m_comparison) {
                p.setPen(QPen(QColor("#f0cf36"), 2));
                p.drawLine(x0 + imageW / 2, y0, x0 + imageW / 2, y0 + imageH);
                p.fillRect(QRect(x0, y0, imageW / 2, imageH), QColor(225, 188, 40, 30));
            }
            p.setPen(QPen(QColor("#e4ca37"), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRect(x0, y0, imageW, imageH));
            p.drawRect(QRect(x0 + imageW * .58, y0, imageW * .20, imageH));
        }

        p.setPen(QColor("#dddddd"));
        p.drawText(QRect(14, height() - 30, 160, 20), Qt::AlignLeft,
                   QString::number(m_zoom) + QStringLiteral(" %"));
    }

private:
    void drawGrid(QPainter &p)
    {
        const int cols = 2;
        const int rows = 3;
        const int cellW = qMin(190, (width() - 120) / cols);
        const int cellH = qMin(125, (height() - 100) / rows);
        const int left = (width() - cols * (cellW + 18)) / 2;
        const int top = (height() - rows * (cellH + 20)) / 2;
        for (int i = 0; i < 6; ++i) {
            const int row = i / cols;
            const int col = i % cols;
            QRect r(left + col * (cellW + 18), top + row * (cellH + 20), cellW, cellH);
            drawPainting(p, r);
            p.fillRect(QRect(r.left(), r.bottom() - 20, r.width(), 20), QColor(0, 0, 0, 125));
            p.setPen(Qt::white);
            p.drawText(r.adjusted(7, 4, -4, -4), Qt::AlignBottom | Qt::AlignLeft,
                       QStringLiteral("r%1_c%2   %3.tif").arg(row, 2, 10, QLatin1Char('0'))
                           .arg(col, 2, 10, QLatin1Char('0')).arg(i + 1));
            p.setPen(QPen(QColor("#e8c638"), 1, Qt::DashLine));
            p.drawRect(r.adjusted(0, 0, -1, -1));
            p.fillRect(QRect(r.left(), r.bottom() - 32, r.width(), 12), QColor(39, 175, 112, 80));
        }
        p.setPen(QColor("#eeeeee"));
        p.drawText(QRect(18, 12, width() - 36, 24), Qt::AlignLeft,
                   tr("Vue Grille — glisser une vignette pour échanger les tuiles"));
    }

    void drawPainting(QPainter &p, const QRect &r)
    {
        p.save();
        p.setClipRect(r);
        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0, QColor("#b6b5af"));
        bg.setColorAt(.55, QColor("#777872"));
        bg.setColorAt(1, QColor("#343532"));
        p.fillRect(r, bg);
        p.fillRect(r.adjusted(0, 0, 0, -int(r.height() * .80)), QColor("#171817"));

        const double sx = r.width() / 240.0;
        const double sy = r.height() / 360.0;
        p.translate(r.topLeft());
        p.scale(sx, sy);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#393a37"));
        QPainterPath body;
        body.moveTo(34, 360);
        body.cubicTo(38, 260, 64, 214, 101, 208);
        body.cubicTo(158, 198, 205, 276, 218, 360);
        body.closeSubpath();
        p.drawPath(body);
        p.setBrush(QColor("#b8b7b0"));
        p.drawEllipse(QRectF(88, 92, 68, 96));
        p.setBrush(QColor("#222321"));
        QPainterPath hair;
        hair.moveTo(84, 142);
        hair.cubicTo(69, 76, 82, 28, 132, 32);
        hair.cubicTo(184, 36, 193, 94, 168, 147);
        hair.cubicTo(166, 99, 140, 69, 104, 91);
        hair.closeSubpath();
        p.drawPath(hair);
        p.setBrush(QColor("#d4d1c7"));
        QPainterPath hat;
        hat.moveTo(68, 88);
        hat.cubicTo(75, 38, 158, 31, 181, 75);
        hat.lineTo(201, 102);
        hat.cubicTo(151, 85, 105, 83, 68, 101);
        hat.closeSubpath();
        p.drawPath(hat);
        p.setPen(QPen(QColor("#484945"), 2));
        p.drawLine(103, 137, 119, 135);
        p.drawLine(136, 135, 151, 137);
        p.drawLine(120, 140, 116, 159);
        p.drawLine(115, 169, 132, 170);
        p.setPen(QPen(QColor("#aaa9a2"), 1));
        for (int y = 210; y < 350; y += 14) {
            p.drawLine(48, y, 198, y - 8);
        }
        p.restore();

        p.setPen(QPen(QColor("#101010"), 5));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
    }

    int m_mode = 4;
    int m_zoom = 12;
    bool m_mask = true;
    bool m_comparison = false;
};
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("KIK-IRPA Assemblage"));
    resize(1228, 780);
    setMinimumSize(920, 600);
    setStyleSheet(QStringLiteral(
        "QMainWindow,QWidget { background:#303030; color:#d5d5d5; font-size:12px; }"
        "QMenuBar,QMenu { background:#242424; color:#dedede; }"
        "QMenuBar::item:selected,QMenu::item:selected { background:#3d6e9d; }"
        "QToolBar { background:#2c2c2c; border:0; spacing:4px; }"
        "QPushButton,QToolButton,QComboBox,QSpinBox,QLineEdit { background:#3b3b3b;"
        " color:#dedede; border:1px solid #4a4a4a; border-radius:3px; padding:5px 8px; }"
        "QPushButton:hover,QToolButton:hover { background:#4a4a4a; }"
        "QPushButton[primary=\"true\"] { background:#397fb8; border-color:#438bca; color:white; }"
        "QPushButton[primary=\"true\"]:hover { background:#4b91cd; }"
        "QTabWidget::pane { border:1px solid #424242; }"
        "QTabBar::tab { background:#303030; color:#aaa; padding:7px 12px; }"
        "QTabBar::tab:selected { background:#3b3b3b; color:white; border-bottom:2px solid #438bca; }"
        "QTableWidget,QListWidget { background:#303030; alternate-background-color:#363636;"
        " gridline-color:#484848; selection-background-color:#36536d; }"
        "QHeaderView::section { background:#303030; color:#bdbdbd; border:0;"
        " border-bottom:1px solid #505050; padding:5px; }"
        "QProgressBar { background:#242424; border:0; text-align:center; height:8px; }"
        "QProgressBar::chunk { background:#43a56c; }"
        "QSplitter::handle { background:#202020; }"
        "QSplitter::handle:hover { background:#438bca; }"
        "QLabel[muted=\"true\"] { color:#999; }"
        "QCheckBox { spacing:7px; }"));

    pages = new QStackedWidget(this);
    setCentralWidget(pages);
    createHomePage();
    createWorkspace();

    auto *fileMenu = menuBar()->addMenu(tr("Fichier"));
    auto *newAction = fileMenu->addAction(tr("Nouveau projet…"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Nouveau projet"),
            tr("Nom du projet :"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            projectName = name.trimmed();
            tilePaths.clear();
            projectPath.clear();
            pages->setCurrentWidget(workspace);
            setMode(1);
            refreshProjectInfo();
        }
    });
    auto *openAction = fileMenu->addAction(tr("Ouvrir un projet…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openProject);
    auto *importAction = fileMenu->addAction(tr("Importer des tuiles…"));
    importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(importAction, &QAction::triggered, this, &MainWindow::importTiles);
    auto *saveAction = fileMenu->addAction(tr("Enregistrer le projet"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveProject);
    fileMenu->addSeparator();
    auto *exportAction = fileMenu->addAction(tr("Exporter / rapport…"));
    exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(exportAction, &QAction::triggered, this, &MainWindow::showExportDialog);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Quitter"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto *editMenu = menuBar()->addMenu(tr("Édition"));
    editMenu->addAction(tr("Annuler"), QKeySequence::Undo, this,
                        [this]() { updateStatus(tr("Dernière action annulée.")); });
    editMenu->addAction(tr("Rétablir"), QKeySequence::Redo, this,
                        [this]() { updateStatus(tr("Action rétablie.")); });
    auto *viewMenu = menuBar()->addMenu(tr("Affichage"));
    auto *togglePanels = viewMenu->addAction(tr("Afficher / masquer les panneaux"));
    connect(togglePanels, &QAction::triggered, this, [this]() {
        panelsVisible = !panelsVisible;
        rightTabs->setVisible(panelsVisible);
        bottomTabs->setVisible(panelsVisible);
    });
    auto *zoomIn = viewMenu->addAction(tr("Zoom avant"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(zoomIn, &QAction::triggered, this, [this]() {
        zoomPercent = qMin(1600, zoomPercent + 10);
        zoomLabel->setText(QString::number(zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    auto *zoomOut = viewMenu->addAction(tr("Zoom arrière"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOut, &QAction::triggered, this, [this]() {
        zoomPercent = qMax(5, zoomPercent - 10);
        zoomLabel->setText(QString::number(zoomPercent) + "%");
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    auto *fit = viewMenu->addAction(tr("Ajuster à la fenêtre"));
    fit->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(fit, &QAction::triggered, this, [this]() {
        zoomPercent = 100;
        zoomLabel->setText(QStringLiteral("Ajuster"));
        static_cast<ArtworkCanvas *>(canvas)->setZoom(zoomPercent);
    });
    auto *toolsMenu = menuBar()->addMenu(tr("Tuiles"));
    toolsMenu->addAction(tr("Importer des tuiles…"), this, &MainWindow::importTiles);
    toolsMenu->addAction(tr("Organiser la grille"), this, [this]() {
        setMode(1);
        updateStatus(tr("Grille réorganisée selon l'ordre sélectionné."));
    });
    auto *masksMenu = menuBar()->addMenu(tr("Masques"));
    masksMenu->addAction(tr("Détecter les bords noirs"), this, [this]() {
        setMode(2);
        static_cast<ArtworkCanvas *>(canvas)->setMaskVisible(true);
        updateStatus(tr("Proposition de masque affichée — à valider par l'opérateur."));
    });
    auto *assemblyMenu = menuBar()->addMenu(tr("Assemblage"));
    assemblyMenu->addAction(tr("Tout lancer"), QKeySequence(Qt::Key_F5), this,
                            [this]() { startRun(1); });
    assemblyMenu->addAction(tr("Relancer à partir de…"), this, [this]() { startRun(6); });
    assemblyMenu->addAction(tr("Comparer deux exécutions…"), this, &MainWindow::showComparison);
    auto *windowMenu = menuBar()->addMenu(tr("Fenêtre"));
    windowMenu->addAction(togglePanels);
    auto *helpMenu = menuBar()->addMenu(tr("Aide"));
    helpMenu->addAction(tr("À propos"), this, [this]() {
        QMessageBox::about(this, tr("À propos"),
            tr("KIK-IRPA Assemblage — prototype d'interface.\n"
               "Les traitements d'image nécessitent le backend, qui n'est pas encore connecté."));
    });
}

void MainWindow::createHomePage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *side = new QWidget;
    side->setFixedWidth(245);
    side->setStyleSheet(QStringLiteral("background:#202020;"));
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(20, 24, 20, 20);
    auto *brand = new QLabel(tr("KIK-IRPA Assemblage"));
    brand->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;color:#d5d5d5;"));
    sideLayout->addWidget(brand);
    sideLayout->addSpacing(14);
    sideLayout->addWidget(mutedLabel(tr("Assemblage de tuiles haute résolution\n"
                                        "de peintures planes."), side));
    sideLayout->addSpacing(16);
    auto *newProject = makeButton(tr("+ Nouveau projet…"), side, true);
    sideLayout->addWidget(newProject);
    auto *open = makeButton(tr("Ouvrir un projet…"), side);
    sideLayout->addWidget(open);
    auto *importJson = makeButton(tr("Importer une chaîne de traitement (JSON)…"), side);
    sideLayout->addWidget(importJson);
    sideLayout->addStretch();
    sideLayout->addWidget(mutedLabel(tr("Manuel d'utilisation · Raccourcis clavier · À propos"), side));
    layout->addWidget(side);

    auto *recentArea = new QWidget;
    auto *recentLayout = new QVBoxLayout(recentArea);
    recentLayout->setContentsMargins(30, 26, 30, 24);
    recentLayout->addWidget(new QLabel(tr("Projets récents")));
    auto *cards = new QHBoxLayout;
    const QStringList recentNames = {
        QStringLiteral("VanDerVeken.kikproj"), QStringLiteral("Savery.kikproj"),
        tr("Dernier assemblage")
    };
    for (int i = 0; i < recentNames.size(); ++i) {
        auto *card = new QPushButton;
        card->setMinimumSize(210, 180);
        card->setMaximumWidth(260);
        card->setStyleSheet(QStringLiteral("text-align:left;padding:0;background:#252525;"
            "border:1px solid #383838;border-radius:5px;"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 10);
        auto *preview = new ArtworkCanvas(card);
        preview->setMinimumSize(180, 120);
        preview->setMaximumHeight(130);
        cardLayout->addWidget(preview);
        auto *caption = new QLabel(recentNames.at(i) + QStringLiteral("\n")
            + (i == 0 ? tr("Infrarouge · 4 tuiles · assemblage terminé")
                      : tr("Projet récent · ouvrir pour continuer")), card);
        caption->setContentsMargins(10, 3, 8, 0);
        cardLayout->addWidget(caption);
        cards->addWidget(card);
        connect(card, &QPushButton::clicked, this, [this, i]() {
            projectName = i == 1 ? QStringLiteral("Savery") : QStringLiteral("VanDerVeken");
            tilePaths = {QStringLiteral("r00_c00.tif"), QStringLiteral("r00_c01.tif"),
                         QStringLiteral("r01_c00.tif"), QStringLiteral("r01_c01.tif")};
            pages->setCurrentWidget(workspace);
            setMode(4);
            refreshProjectInfo();
            updateStatus(tr("Projet récent ouvert. Les données illustrées sont une démonstration."));
        });
    }
    cards->addStretch();
    recentLayout->addLayout(cards, 0);
    recentLayout->addStretch();
    layout->addWidget(recentArea, 1);
    pages->addWidget(page);
    connect(newProject, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Nouveau projet"),
            tr("Nom du projet :"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            projectName = name.trimmed();
            tilePaths.clear();
            projectPath.clear();
            pages->setCurrentWidget(workspace);
            setMode(1);
            refreshProjectInfo();
        }
    });
    connect(open, &QPushButton::clicked, this, &MainWindow::openProject);
    connect(importJson, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Importer une chaîne"),
            QString(), tr("Projet JSON (*.json *.kikproj)"));
        if (!path.isEmpty()) {
            projectPath = path;
            projectName = QFileInfo(path).completeBaseName();
            pages->setCurrentWidget(workspace);
            setMode(4);
            updateStatus(tr("Chaîne chargée. Vérifiez les chemins de tuiles avant tout traitement."));
        }
    });
}

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
    toolbarLayout->addWidget(title);
    toolbarLayout->addStretch();
    auto *save = makeButton(tr("Enregistrer"), toolbar);
    toolbarLayout->addWidget(save);
    connect(save, &QPushButton::clicked, this, &MainWindow::saveProject);
    root->addWidget(toolbar);

    auto *stages = new QWidget;
    auto *stageLayout = new QHBoxLayout(stages);
    stageLayout->setContentsMargins(8, 4, 8, 4);
    stageLayout->setSpacing(4);
    auto *modeGroup = new QButtonGroup(stages);
    modeGroup->setExclusive(true);
    for (int i = 0; i < modeNames.size(); ++i) {
        auto *button = makeButton(QStringLiteral("%1   %2").arg(i + 1).arg(modeNames.at(i)), stages);
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("mode%1").arg(i));
        modeGroup->addButton(button, i);
        stageLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i]() { setMode(i); });
    }
    auto *run = makeButton(tr("▶ Tout lancer"), stages, true);
    stageLayout->addWidget(run);
    connect(run, &QPushButton::clicked, this, [this]() { startRun(1); });
    auto *compare = makeButton(tr("Comparer…"), stages);
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
    arrangementCombo->addItems({tr("Serpentin vertical, bas gauche"), tr("Automatique"),
                                tr("Serpentin horizontal"), tr("Par lignes"), tr("Par colonnes")});
    arrangementCombo->setObjectName(QStringLiteral("arrangementCombo"));
    optionsLayout->addWidget(arrangementCombo);
    connect(arrangementCombo, &QComboBox::currentTextChanged, this, [this](const QString &value) {
        updateStatus(tr("Ordre de prise de vue : %1").arg(value));
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
    canvas = new ArtworkCanvas(center);
    centerLayout->addWidget(canvas, 1);
    auto *canvasControls = new QWidget;
    auto *canvasControlsLayout = new QHBoxLayout(canvasControls);
    canvasControlsLayout->setContentsMargins(8, 3, 8, 3);
    auto *minus = makeButton(QStringLiteral("−"), canvasControls);
    auto *plus = makeButton(QStringLiteral("+"), canvasControls);
    zoomLabel = new QLabel(QStringLiteral("12,4 %"));
    auto *fit = makeButton(tr("Ajuster"), canvasControls);
    auto *oneToOne = makeButton(QStringLiteral("100 %"), canvasControls);
    canvasControlsLayout->addWidget(minus);
    canvasControlsLayout->addWidget(plus);
    canvasControlsLayout->addWidget(zoomLabel);
    canvasControlsLayout->addWidget(fit);
    canvasControlsLayout->addWidget(oneToOne);
    canvasControlsLayout->addStretch();
    centerLayout->addWidget(canvasControls);
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
    auto *layersPage = new QWidget;
    auto *layersLayout = new QVBoxLayout(layersPage);
    layersLayout->addWidget(new QLabel(tr("Calques")));
    const QStringList layerNames = {tr("Lignes de couture"), tr("Erreur d'alignement"),
                                    tr("Tuiles"), tr("Mosaïque"), tr("Masques")};
    for (const QString &name : layerNames) {
        auto *check = new QCheckBox(name);
        check->setChecked(true);
        layersLayout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this, name](bool checked) {
            updateStatus(tr("Calque « %1 » %2.").arg(name, checked ? tr("affiché") : tr("masqué")));
            if (name == tr("Masques")) {
                maskVisible = checked;
                static_cast<ArtworkCanvas *>(canvas)->setMaskVisible(checked);
            }
        });
    }
    layersLayout->addStretch();
    rightTabs->addTab(layersPage, tr("Calques"));

    auto *chainPage = new QWidget;
    auto *chainLayout = new QVBoxLayout(chainPage);
    chainLayout->setContentsMargins(6, 8, 6, 8);
    chainLayout->addWidget(new QLabel(tr("Chaîne de traitement")));
    const QStringList steps = {tr("1  Import"), tr("2  Masques"), tr("3  Correspondance"),
        tr("4  Optimisation"), tr("5  Calage local"), tr("6  Égalisation"),
        tr("7  Ligne de couture"), tr("8  Fusion et export")};
    auto *stepList = new QListWidget(chainPage);
    for (int i = 0; i < steps.size(); ++i) {
        auto *item = new QListWidgetItem((i == 2 ? QStringLiteral("⚠  ") : QStringLiteral("●  "))
                                         + steps.at(i), stepList);
        item->setData(Qt::UserRole, i + 1);
    }
    chainLayout->addWidget(stepList);
    connect(stepList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        startRun(item->data(Qt::UserRole).toInt());
    });
    auto *relaunch = makeButton(tr("Relancer à partir d'ici"), chainPage);
    chainLayout->addWidget(relaunch);
    connect(relaunch, &QPushButton::clicked, this, [this, stepList]() {
        const int row = qMax(0, stepList->currentRow());
        startRun(row + 1);
    });
    auto *selectAlgo = new QComboBox(chainPage);
    selectAlgo->addItems({tr("Coupe de graphe"), tr("Programmation dynamique"), tr("Dijkstra")});
    chainLayout->addWidget(selectAlgo);
    connect(selectAlgo, &QComboBox::currentTextChanged, this, [this](const QString &algorithm) {
        updateStatus(tr("Algorithme sélectionné : %1 — étapes suivantes à refaire.").arg(algorithm));
    });
    rightTabs->addTab(chainPage, tr("Chaîne"));

    auto *infoPage = new QWidget;
    auto *infoLayout = new QVBoxLayout(infoPage);
    infoLayout->addWidget(new QLabel(tr("Projet sélectionné")));
    projectNameLabel = new QLabel;
    projectNameLabel->setWordWrap(true);
    infoLayout->addWidget(projectNameLabel);
    tileCountLabel = new QLabel;
    infoLayout->addWidget(tileCountLabel);
    infoLayout->addWidget(mutedLabel(tr("Modalité : infrarouge\nÉchelle : à renseigner"), infoPage));
    auto *maskButton = makeButton(tr("Détecter les bords noirs"), infoPage);
    infoLayout->addWidget(maskButton);
    connect(maskButton, &QPushButton::clicked, this, [this]() {
        setMode(2);
        static_cast<ArtworkCanvas *>(canvas)->setMaskVisible(true);
        updateStatus(tr("Masque proposé. Vérifiez-le avant validation."));
    });
    auto *pairButton = makeButton(tr("Ouvrir la vue Paire"), infoPage);
    infoLayout->addWidget(pairButton);
    connect(pairButton, &QPushButton::clicked, this, [this]() { setMode(3); });
    auto *validateButton = makeButton(tr("Valider la paire"), infoPage, true);
    infoLayout->addWidget(validateButton);
    connect(validateButton, &QPushButton::clicked, this, [this]() {
        updateStatus(tr("Paire validée par l'opérateur; les étapes suivantes sont à refaire."));
    });
    auto *adjustButton = makeButton(tr("Saisir un décalage…"), infoPage);
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
    rightTabs->addTab(infoPage, tr("Infos"));
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
    statusLayout->addWidget(mutedLabel(tr("x 1 812  y 2 310 px    Tuile r00_c01"), status));
    statusLayout->addStretch();
    statusLabel = new QLabel(tr("Affichage ajusté (contraste)"));
    statusLayout->addWidget(statusLabel);
    progressBar = new QProgressBar(status);
    progressBar->setRange(0, 100);
    progressBar->setFixedWidth(130);
    progressBar->hide();
    statusLayout->addWidget(progressBar);
    root->addWidget(status);

    pages->addWidget(workspace);
    connect(pairsTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        updateStatus(tr("Paire sélectionnée : %1").arg(pairsTable->item(row, 0)->text()));
    });
}

void MainWindow::setMode(int mode)
{
    currentMode = qBound(0, mode, 5);
    pages->setCurrentWidget(workspace);
    if (auto *button = workspace->findChild<QPushButton *>(QStringLiteral("mode%1").arg(currentMode)))
        button->setChecked(true);
    const QStringList descriptions = {
        tr("Projet — aperçu général"), tr("Vue Grille de tuiles — organiser et vérifier l'import"),
        tr("Tuile r00_c00 — rouge : bords sombres proposés · bleu : lasso en cours"),
        tr("Vue Paire — points de contrôle et correction"), tr("Calques : mosaïque, tuiles, coutures, erreur d'alignement"),
        tr("Export — TIFF 16 bits, profil ICC conservé")
    };
    if (auto *label = workspace->findChild<QLabel *>(QStringLiteral("modeDescription")))
        label->setText(descriptions.at(currentMode));
    if (auto *canvasView = dynamic_cast<ArtworkCanvas *>(canvas))
        canvasView->setMode(currentMode);
    arrangementCombo->setVisible(currentMode == 1);
    if (currentMode == 1)
        bottomTabs->setCurrentIndex(1);
    if (currentMode == 3)
        bottomTabs->setCurrentIndex(0);
    updateStatus(tr("Mode : %1").arg(modeNames.at(currentMode)));
}

void MainWindow::refreshProjectInfo()
{
    if (projectNameLabel)
        projectNameLabel->setText(projectName);
    if (tileCountLabel)
        tileCountLabel->setText(tr("%1 tuile(s) chargée(s)").arg(tilePaths.size()));
    setWindowTitle(projectName + QStringLiteral(" — KIK-IRPA Assemblage"));
}

void MainWindow::importTiles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Importer des tuiles"),
        QString(), tr("Images TIFF (*.tif *.tiff);;Images (*.png *.jpg *.jpeg *.bmp);;Tous les fichiers (*)"));
    if (paths.isEmpty())
        return;
    tilePaths.append(paths);
    refreshProjectInfo();
    setMode(1);
    updateStatus(tr("%1 fichier(s) ajouté(s). Les images source ne sont pas modifiées.")
                 .arg(paths.size()));
}

void MainWindow::saveProject()
{
    QString path = projectPath;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, tr("Enregistrer le projet"),
            projectName + QStringLiteral(".kikproj"), tr("Projet KIK-IRPA (*.kikproj);;JSON (*.json)"));
    if (path.isEmpty())
        return;
    QJsonObject project;
    project.insert(QStringLiteral("format_version"), QStringLiteral("1.0"));
    project.insert(QStringLiteral("project_name"), projectName);
    project.insert(QStringLiteral("saved_at"), QDateTime::currentDateTime().toString(Qt::ISODate));
    project.insert(QStringLiteral("mode"), currentMode);
    QJsonArray tiles;
    for (const QString &tile : tilePaths)
        tiles.append(tile);
    project.insert(QStringLiteral("tiles"), tiles);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(project).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        QMessageBox::warning(this, tr("Enregistrement impossible"),
            tr("Le projet n'a pas pu être enregistré à cet emplacement."));
        return;
    }
    projectPath = path;
    updateStatus(tr("Projet enregistré : %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::openProject()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Ouvrir un projet"),
        QString(), tr("Projet KIK-IRPA (*.kikproj *.json);;Tous les fichiers (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Ouverture impossible"), tr("Le fichier ne peut pas être lu."));
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, tr("Projet invalide"),
            tr("Ce fichier n'est pas un projet JSON valide."));
        return;
    }
    const QJsonObject project = document.object();
    projectName = project.value(QStringLiteral("project_name")).toString(QFileInfo(path).completeBaseName());
    tilePaths.clear();
    for (const QJsonValue &tile : project.value(QStringLiteral("tiles")).toArray())
        tilePaths.append(tile.toString());
    projectPath = path;
    refreshProjectInfo();
    pages->setCurrentWidget(workspace);
    setMode(project.value(QStringLiteral("mode")).toInt(4));
    updateStatus(tr("Projet ouvert sans recalcul. Vérifiez les chemins des tuiles."));
}

void MainWindow::startRun(int firstStep)
{
    if (progressBar->isVisible()) {
        updateStatus(tr("Une démonstration de progression est déjà en cours."));
        return;
    }
    if (tilePaths.isEmpty()) {
        const auto answer = QMessageBox::question(this, tr("Aucune tuile importée"),
            tr("Aucune tuile n'est chargée. Lancer quand même la démonstration d'interface ?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return;
    }
    if (firstStep > 1) {
        QDialog confirmation(this);
        confirmation.setWindowTitle(tr("Relancer à partir de l'étape %1").arg(firstStep));
        auto *layout = new QVBoxLayout(&confirmation);
        layout->addWidget(new QLabel(tr("Les étapes précédentes seront réutilisées sans recalcul."), &confirmation));
        auto *reused = new QListWidget(&confirmation);
        for (int step = 1; step < firstStep; ++step)
            reused->addItem(tr("✓  Étape %1 — résultat conservé").arg(step));
        layout->addWidget(reused);
        auto *recalculated = new QListWidget(&confirmation);
        for (int step = firstStep; step <= 8; ++step)
            recalculated->addItem(tr("↻  Étape %1 — résultat recalculé").arg(step));
        layout->addWidget(recalculated);
        layout->addWidget(mutedLabel(
            tr("Les résultats actuels seront conservés dans l'historique. "
               "Cette maquette ne lance pas de calcul backend."), &confirmation));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok,
                                             &confirmation);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Lancer"));
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &confirmation, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &confirmation, &QDialog::reject);
        if (confirmation.exec() != QDialog::Accepted)
            return;
    }
    progressBar->setValue(0);
    progressBar->show();
    updateStatus(tr("Démonstration de progression — le backend de calcul n'est pas connecté."));
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this, timer, firstStep]() {
        progressBar->setValue(progressBar->value() + 4);
        if (progressBar->value() >= 100) {
            timer->stop();
            timer->deleteLater();
            progressBar->hide();
            updateStatus(tr("Démonstration terminée (aucun calcul d'image effectué). Étape de départ : %1.")
                         .arg(firstStep));
        }
    });
    timer->start(100);
    QTimer::singleShot(150, this, [this, timer]() {
        if (timer->isActive()) {
            auto *cancel = makeButton(tr("Annuler"), this);
            cancel->setObjectName(QStringLiteral("cancelRunButton"));
            statusBar()->addPermanentWidget(cancel);
            connect(cancel, &QPushButton::clicked, this, [this, timer, cancel]() {
                timer->stop();
                timer->deleteLater();
                progressBar->hide();
                cancel->deleteLater();
                updateStatus(tr("Démonstration annulée."));
            });
            connect(timer, &QTimer::destroyed, cancel, &QPushButton::deleteLater);
        }
    });
}

void MainWindow::showComparison()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Comparer deux exécutions"));
    dialog.resize(440, 240);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Comparer les sorties de deux algorithmes de couture."), &dialog));
    auto *algorithms = new QHBoxLayout;
    auto *a = new QComboBox(&dialog);
    auto *b = new QComboBox(&dialog);
    const QStringList choices = {tr("Programmation dynamique"), tr("Coupe de graphe"), tr("Dijkstra")};
    a->addItems(choices);
    b->addItems(choices);
    b->setCurrentIndex(1);
    algorithms->addWidget(a);
    algorithms->addWidget(b);
    layout->addLayout(algorithms);
    layout->addWidget(new QLabel(tr("Mesures : saut à la couture · détails conservés · durée"), &dialog));
    auto *buttons = new QDialogButtonBox(&dialog);
    auto *show = buttons->addButton(tr("Comparer"), QDialogButtonBox::ActionRole);
    auto *keepA = buttons->addButton(tr("Garder A"), QDialogButtonBox::ActionRole);
    auto *keepB = buttons->addButton(tr("Garder B"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(show, &QPushButton::clicked, this, [this, &dialog]() {
        comparisonVisible = !comparisonVisible;
        static_cast<ArtworkCanvas *>(canvas)->toggleComparison();
        updateStatus(comparisonVisible ? tr("Comparaison affichée — curseur de séparation actif.")
                                       : tr("Comparaison côte à côte affichée."));
        dialog.accept();
    });
    connect(keepA, &QPushButton::clicked, this, [this, &dialog]() {
        updateStatus(tr("Résultat A conservé; les étapes suivantes sont à refaire."));
        dialog.accept();
    });
    connect(keepB, &QPushButton::clicked, this, [this, &dialog]() {
        updateStatus(tr("Résultat B conservé; les étapes suivantes sont à refaire."));
        dialog.accept();
    });
    dialog.exec();
}

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
    layout.addWidget(mutedLabel(tr("L'export de la mosaïque nécessite le backend, non connecté dans ce prototype."), &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *exportReport = buttons->addButton(tr("Enregistrer le rapport…"), QDialogButtonBox::AcceptRole);
    layout.addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(exportReport, &QPushButton::clicked, this, [this, &dialog, format, report]() {
        if (!report->isChecked()) {
            QMessageBox::information(&dialog, tr("Backend requis"),
                tr("La génération de l'image mosaïque sera disponible une fois le backend connecté."));
            return;
        }
        const QString path = QFileDialog::getSaveFileName(&dialog, tr("Enregistrer le rapport"),
            projectName + QStringLiteral("_rapport.html"), tr("Rapport HTML (*.html)"));
        if (path.isEmpty())
            return;
        QSaveFile file(path);
        const QString html = QStringLiteral("<!doctype html><meta charset=\"utf-8\"><title>Rapport KIK-IRPA</title>"
            "<h1>Rapport d'assemblage — %1</h1><p>Généré le %2.</p><p>Format demandé : %3</p>"
            "<p>Tuiles référencées : %4</p><p>Les mesures présentées sont celles de la maquette.</p>")
            .arg(projectName.toHtmlEscaped(),
                 QDateTime::currentDateTime().toString(Qt::ISODate).toHtmlEscaped(),
                 format->currentText().toHtmlEscaped()).arg(tilePaths.size());
        if (!file.open(QIODevice::WriteOnly) || file.write(html.toUtf8()) < 0 || !file.commit()) {
            QMessageBox::warning(&dialog, tr("Export impossible"), tr("Le rapport n'a pas pu être enregistré."));
            return;
        }
        updateStatus(tr("Rapport HTML exporté."));
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::updateStatus(const QString &message)
{
    if (statusLabel)
        statusLabel->setText(message);
    statusBar()->showMessage(message, 5000);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Tab) {
        panelsVisible = !panelsVisible;
        rightTabs->setVisible(panelsVisible);
        bottomTabs->setVisible(panelsVisible);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
