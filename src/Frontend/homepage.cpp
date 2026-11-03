/*
 * .cpp : le POURQUOI
 * - choix de conception : conserver les projets récents simplifie le retour vers un projet existant ;
 * - cas limite : un fichier récent peut avoir été déplacé ou supprimé depuis sa dernière utilisation ;
 * - unité ou repère : les chemins sont interprétés selon le système de fichiers de la machine ;
 * - raison non évidente : les signaux séparent la présentation de l'accueil de la logique de la fenêtre principale.
 */
/*
 * Header : le QUOI
 * Implémente la page d'accueil et la gestion des projets récents.
 * - ce que ça fait : construit l'écran d'accueil et permet de créer, ouvrir ou sélectionner un projet ;
 * - ce que ça renvoie : aucune valeur directement, les actions sont transmises par des signaux ;
 * - quand ça lève une exception : si la lecture, l'écriture ou la création d'un projet échoue.
 */
//  implemente la page daccueil et les projets recents
/**
 * @brief Implémentation de la page d'accueil de l'application
 */
#include "homepage.h"

#include "frontendutils.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *side = new QWidget(this);
    side->setFixedWidth(245);
    side->setStyleSheet(QStringLiteral("background:#202020;"));
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(20, 24, 20, 20);
    auto *brand = new QLabel(tr("KIK-IRPA Assemblage"), side);
    brand->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;color:#d5d5d5;"));
    sideLayout->addWidget(brand);
    sideLayout->addSpacing(14);
    sideLayout->addWidget(FrontendUtils::mutedLabel(
        tr("Assemblage de tuiles haute résolution\n"
           "de peintures planes."), side));
    sideLayout->addSpacing(16);
    auto *newProject = FrontendUtils::makeButton(tr("+ Nouveau projet…"), side, true);
    sideLayout->addWidget(newProject);
    auto *open = FrontendUtils::makeButton(tr("Importer un projet…"), side);
    sideLayout->addWidget(open);
    sideLayout->addStretch();
    sideLayout->addWidget(FrontendUtils::mutedLabel(
        tr("Manuel d'utilisation · Raccourcis clavier · À propos"), side));
    layout->addWidget(side);

    auto *recentArea = new QWidget(this);
    auto *recentLayout = new QVBoxLayout(recentArea);
    recentLayout->setContentsMargins(30, 26, 30, 24);
    recentLayout->addWidget(new QLabel(tr("Projets récents"), recentArea));
    recentEmptyLabel = FrontendUtils::mutedLabel(tr("Aucun projet récent."), recentArea);
    recentLayout->addWidget(recentEmptyLabel);
    auto *cardsContainer = new QWidget(recentArea);
    recentCardsLayout = new QHBoxLayout(cardsContainer);
    recentCardsLayout->setContentsMargins(0, 0, 0, 0);
    recentCardsLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    recentLayout->addWidget(cardsContainer);
    recentLayout->addStretch();
    layout->addWidget(recentArea, 1);

    connect(newProject, &QPushButton::clicked, this, &HomePage::createProject);
    connect(open, &QPushButton::clicked, this, &HomePage::openProject);
    refreshRecentProjects();
}

void HomePage::refreshRecentProjects()
{
    if (!recentCardsLayout || !recentEmptyLabel)
        return;

    while (QLayoutItem *item = recentCardsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    QSettings settings(QStringLiteral("KIK-IRPA"), QStringLiteral("Assemblage"));
    QStringList recentPaths = settings.value(QStringLiteral("recentProjects")).toStringList();
    QStringList validPaths;
    for (const QString &path : recentPaths) {
        const QString absolutePath = QFileInfo(path).absoluteFilePath();
        if (QFileInfo::exists(absolutePath) && !validPaths.contains(absolutePath))
            validPaths.append(absolutePath);
    }
    if (validPaths != recentPaths) {
        recentPaths = validPaths;
        settings.setValue(QStringLiteral("recentProjects"), recentPaths);
    }

    const auto formatModifiedDate = [](const QDateTime &dateTime) {
        return dateTime.isValid()
            ? dateTime.toString(QStringLiteral("dd/MM/yyyy HH:mm"))
            : QObject::tr("Date inconnue");
    };

    int cardCount = 0;
    if (currentProjectCreated && currentProjectPath.isEmpty()) {
        auto *card = FrontendUtils::makeButton(QString(), recentCardsLayout->parentWidget());
        card->setMinimumSize(210, 100);
        card->setMaximumWidth(260);
        card->setStyleSheet(QStringLiteral("text-align:left;padding:12px;background:#252525;"
            "border:1px solid #383838;border-radius:5px;"));
        auto *cardLayout = new QVBoxLayout(card);
        auto *name = new QLabel(currentProjectName, card);
        name->setStyleSheet(QStringLiteral("font-size:14px;font-weight:600;"));
        auto *details = new QLabel(tr("%1 tuile(s)\nModifié le : %2")
                                       .arg(currentTileCount)
                                       .arg(tr("Non enregistré")), card);
        details->setProperty("muted", true);
        details->setWordWrap(true);
        cardLayout->addWidget(name);
        cardLayout->addWidget(details);
        recentCardsLayout->addWidget(card);
        connect(card, &QPushButton::clicked, this, &HomePage::currentProjectRequested);
        ++cardCount;
    }

    for (const QString &path : recentPaths) {
        QFile file(path);
        QString recentName = QFileInfo(path).completeBaseName();
        int recentTileCount = 0;
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
            if (document.isObject()) {
                const QJsonObject project = document.object();
                recentName = project.contains(QStringLiteral("projectName"))
                                 ? project.value(QStringLiteral("projectName")).toString(recentName)
                                 : project.value(QStringLiteral("project_name"))
                                       .toString(recentName);
                const QJsonArray paths = project.value(QStringLiteral("tiles")).toArray();
                recentTileCount = paths.size();
            }
        }

        auto *card = FrontendUtils::makeButton(QString(), recentCardsLayout->parentWidget());
        card->setMinimumSize(210, 100);
        card->setMaximumWidth(260);
        card->setStyleSheet(QStringLiteral("text-align:left;padding:12px;background:#252525;"
            "border:1px solid #383838;border-radius:5px;"));
        auto *cardLayout = new QVBoxLayout(card);
        auto *name = new QLabel(recentName, card);
        name->setStyleSheet(QStringLiteral("font-size:14px;font-weight:600;"));
        name->setWordWrap(true);
        auto *details = new QLabel(tr("%1 tuile(s)\nModifié le %2")
                                       .arg(recentTileCount)
                                       .arg(formatModifiedDate(QFileInfo(path).lastModified())), card);
        details->setProperty("muted", true);
        details->setWordWrap(true);
        cardLayout->addWidget(name);
        cardLayout->addWidget(details);
        recentCardsLayout->addWidget(card);
        connect(card, &QPushButton::clicked, this, [this, path]() {
            loadProjectFromPath(path);
        });
        ++cardCount;
    }

    recentCardsLayout->addStretch();
    recentEmptyLabel->setVisible(cardCount == 0);
}

void HomePage::rememberRecentProject(const QString &path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (!QFileInfo::exists(absolutePath))
        return;

    QSettings settings(QStringLiteral("KIK-IRPA"), QStringLiteral("Assemblage"));
    QStringList recentPaths = settings.value(QStringLiteral("recentProjects")).toStringList();
    recentPaths.removeAll(absolutePath);
    recentPaths.prepend(absolutePath);
    while (recentPaths.size() > 10)
        recentPaths.removeLast();
    settings.setValue(QStringLiteral("recentProjects"), recentPaths);
    refreshRecentProjects();
}

void HomePage::setCurrentProjectInfo(bool created, const QString &path,
                                     const QString &name, int tileCount)
{
    currentProjectCreated = created;
    currentProjectPath = path;
    currentProjectName = name;
    currentTileCount = tileCount;
    refreshRecentProjects();
}

void HomePage::createProject()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nouveau projet"),
        tr("Nom du projet :"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    const QString selectedTileDirectory = QFileDialog::getExistingDirectory(
        this, tr("Choisir le dossier des tuiles"));
    if (selectedTileDirectory.isEmpty())
        return;

    const QFileInfoList tileFiles = QDir(selectedTileDirectory).entryInfoList(
        {QStringLiteral("*.tif"), QStringLiteral("*.tiff")},
        QDir::Files, QDir::Name);
    QStringList tilePaths;
    for (const QFileInfo &tileFile : tileFiles)
        tilePaths.append(tileFile.absoluteFilePath());

    emit projectCreationRequested(name, selectedTileDirectory, tilePaths);
}

void HomePage::openProject()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Ouvrir un projet"),
        QString(), tr("Projet KIK-IRPA (*.kikproj *.json);;Tous les fichiers (*)"));
    if (!path.isEmpty())
        loadProjectFromPath(path);
}

void HomePage::loadProjectFromPath(const QString &path)
{
    emit projectLoaded(path);
}
