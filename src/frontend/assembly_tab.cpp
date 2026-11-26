// src/frontend/assembly_tab.cpp
#include "assembly_tab.h"
#include "frontendutils.h"
#include "mainwindow.h"
#include "projectgrid.h"

#include "core/io/unicode_path.h"

#include <algorithm>
#include <filesystem>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace {

// Nom affiché d'une entrée sans la mention « indisponible », pour pouvoir la remettre ou l'enlever.
constexpr int BASE_TEXT_ROLE = Qt::UserRole + 1;

// Ajoute une entrée dont la donnée est le nom du step (ou de la méthode) envoyé au backend.
void addMethod(QComboBox *combo, const QString &text, const QString &data)
{
    combo->addItem(text, data);
    combo->setItemData(combo->count() - 1, text, BASE_TEXT_ROLE);
}

// Rend une entrée choisissable ou non. Indisponible, elle est grisée, marquée et ne peut pas être sélectionnée ;
// la raison est dans l'infobulle.
void setMethodAvailable(QComboBox *combo, int row, bool available, const QString &reason)
{
    auto *model = qobject_cast<QStandardItemModel *>(combo->model());
    if (!model || row < 0 || row >= model->rowCount())
        return;
    QStandardItem *item = model->item(row);
    const QString baseText = item->data(BASE_TEXT_ROLE).toString();
    item->setEnabled(available);
    item->setText(available ? baseText : baseText + QObject::tr("  (indisponible)"));
    item->setForeground(available ? QBrush() : QBrush(QColor(QStringLiteral("#6f6f6f"))));
    item->setToolTip(available ? QString() : reason);
}

void addUnavailableMethod(QComboBox *combo, const QString &text, const QString &data, const QString &reason)
{
    addMethod(combo, text, data);
    setMethodAvailable(combo, combo->count() - 1, false, reason);
}

void selectData(QComboBox *combo, const std::string &value)
{
    const int index = combo->findData(QString::fromStdString(value));
    if (index >= 0)
        combo->setCurrentIndex(index);
}

std::string selectedData(const QComboBox *combo)
{
    return combo->currentData().toString().toStdString();
}

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

}

AssemblyTab::AssemblyTab(MainWindow *mainWindow, ProjectGrid *projectGrid, QWidget *parent)
    : QWidget(parent), m_mainWindow(mainWindow), m_projectGrid(projectGrid)
{
    auto *chainLayout = new QVBoxLayout(this);
    chainLayout->setContentsMargins(6, 8, 6, 8);
    setupMethodControls(chainLayout);
    chainLayout->addSpacing(12);
    setupStepList(chainLayout);
    chainLayout->addStretch(1);

    updateChoices(m_projectGrid->state().pipelineChoices);
}

QComboBox *AssemblyTab::addStage(const QString &title)
{
    auto *combo = new QComboBox(m_methodsForm->parentWidget());
    // Largeur imposée par le panneau, pas par l'entrée la plus longue du menu.
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(6);
    m_methodsForm->addRow(title, combo);
    return combo;
}

// Une ligne par étape qui peut être remplie par plusieurs steps du registre. Les données des entrées sont les noms du
// registre (ou du paramètre method d'intensity-correction) : c'est ce que le script envoie au backend.
void AssemblyTab::setupMethodControls(QVBoxLayout *layout)
{
    layout->addWidget(sectionTitle(tr("Méthodes"), layout->parentWidget()));
    m_methodsForm = new QFormLayout;
    m_methodsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(m_methodsForm);

    m_overlapCombo = addStage(tr("Recouvrement"));
    addMethod(m_overlapCombo, tr("Réglage de la grille"), QString());
    addMethod(m_overlapCombo, tr("Mesuré"), QStringLiteral("overlap-estimation"));

    m_matchingCombo = addStage(tr("Appariement"));
    addMethod(m_matchingCombo, tr("Corrélation de phase"), QStringLiteral("phase-correlation"));
    addMethod(m_matchingCombo, QStringLiteral("SIFT"), QStringLiteral("sift"));
    addMethod(m_matchingCombo, QStringLiteral("LightGlue"), QStringLiteral("lightglue"));
    // Sans modèle, LightGlue échouerait au moment du calcul : mieux vaut ne pas le proposer.
    const std::filesystem::path modelPath = FrontendUtils::lightGlueModelPath();
    m_isLightGlueAvailable = std::filesystem::is_regular_file(modelPath);
    setMethodAvailable(m_matchingCombo, m_matchingCombo->findData(QStringLiteral("lightglue")), m_isLightGlueAvailable,
                       tr("Modèle introuvable : %1. Il est téléchargé dans le dossier de build quand les tests sont "
                          "activés.").arg(QString::fromStdString(IONs::pathToUtf8(modelPath))));

    // Le Vote 2D suit l'appariement (voir usesVote2D) : rien à choisir, seulement une note.
    m_voteNote = FrontendUtils::mutedLabel(tr("+ Vote 2D (automatique)"), m_methodsForm->parentWidget());
    m_voteNote->setToolTip(tr("Écarte les correspondances aberrantes. Pas avec la corrélation de phase."));
    m_methodsForm->addRow(QString(), m_voteNote);

    m_intensityCombo = addStage(tr("Égalisation"));
    addMethod(m_intensityCombo, tr("Gains et éclairage"), QStringLiteral("gain-and-field"));
    addMethod(m_intensityCombo, tr("Gains seuls"), QStringLiteral("gain"));

    m_seamCombo = addStage(tr("Couture"));
    addMethod(m_seamCombo, tr("Coupe de graphe"), QStringLiteral("graphcut"));
    addMethod(m_seamCombo, tr("Programmation dynamique"), QStringLiteral("dynamic-programming-dijkstra"));

    m_verificationCombo = addStage(tr("Vérification"));
    addMethod(m_verificationCombo, tr("Aucune"), QString());
    addUnavailableMethod(m_verificationCombo, tr("Image de référence"), QStringLiteral("final-verification"),
                         tr("Demande une image de référence, pas encore réglable ici."));

    m_exportCombo = addStage(tr("Export"));
    addMethod(m_exportCombo, tr("TIFF pleine résolution"), QStringLiteral("tiff"));
    addMethod(m_exportCombo, tr("JPEG aperçu réduit"), QStringLiteral("jpeg"));
    addMethod(m_exportCombo, tr("TIFF + JPEG aperçu"), QStringLiteral("both"));

    // Le menu ouvert montre les entrées en entier, même si le menu fermé est étroit.
    for (QComboBox *combo : {m_overlapCombo, m_matchingCombo, m_intensityCombo, m_seamCombo, m_verificationCombo, m_exportCombo}) {
        int widest = 0;
        for (int row = 0; row < combo->count(); ++row)
            widest = std::max(widest, combo->fontMetrics().horizontalAdvance(combo->itemText(row)));
        combo->view()->parentWidget()->setMinimumWidth(widest + 40);   // conteneur du menu ouvert
    }
    // Connectés une fois tous les menus remplis : ajouter une entrée change l'index courant.
    for (QComboBox *combo : {m_overlapCombo, m_matchingCombo, m_intensityCombo, m_seamCombo, m_exportCombo})
        connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
                &AssemblyTab::applyChoices);
}

void AssemblyTab::setupStepList(QVBoxLayout *layout)
{
    layout->addWidget(sectionTitle(tr("Étapes"), layout->parentWidget()));
    m_stepList = new QListWidget(layout->parentWidget());
    m_stepList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stepList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stepList->setTextElideMode(Qt::ElideRight);
    m_stepList->setToolTip(tr("Double-clic : relancer à partir de cette étape"));
    layout->addWidget(m_stepList);
    auto *relaunch = FrontendUtils::makeButton(tr("Relancer à partir d'ici"), layout->parentWidget());
    layout->addWidget(relaunch);

    // Un double-clic sur une étape déclenche immédiatement la relance à partir de celle-ci.
    connect(m_stepList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        m_mainWindow->requestRun(item->data(Qt::UserRole).toInt());
    });
    // Relance le pipeline à partir de l'étape sélectionnée en exploitant les checkpoints existants.
    connect(relaunch, &QPushButton::clicked, this, [this]() {
        const int row = std::max(0, m_stepList->currentRow());
        m_mainWindow->requestRun(row + 1);
    });
}

void AssemblyTab::applyChoices()
{
    FrontendNs::PipelineChoices choices = m_projectGrid->state().pipelineChoices;   // garde les corrections manuelles
    choices.estimateOverlap = selectedData(m_overlapCombo) == "overlap-estimation";
    choices.matchingStep = selectedData(m_matchingCombo);
    choices.intensityMethod = selectedData(m_intensityCombo);
    choices.seamStep = selectedData(m_seamCombo);
    choices.exportFormat = selectedData(m_exportCombo);
    if (choices == m_projectGrid->state().pipelineChoices)
        return;
    m_projectGrid->setPipelineChoices(choices);   // le projet prévient l'interface, qui rappelle updateChoices
    m_mainWindow->updateStatus(tr("Méthodes modifiées, les étapes suivantes sont à refaire."));
}

void AssemblyTab::updateChoices(const FrontendNs::PipelineChoices &requestedChoices)
{
    FrontendNs::PipelineChoices choices = requestedChoices;
    // Un projet rouvert peut avoir choisi LightGlue sur un poste où le modèle manque : on revient à la méthode par
    // défaut, dans le projet aussi, pour que le menu affiché et le script envoyé restent d'accord.
    if (choices.matchingStep == "lightglue" && !m_isLightGlueAvailable) {
        choices.matchingStep = FrontendNs::PipelineChoices{}.matchingStep;
        m_projectGrid->setPipelineChoices(choices);   // peut rappeler updateChoices avec ces choix : sans effet de plus
        m_mainWindow->updateStatus(tr("Modèle LightGlue introuvable : appariement par corrélation de phase."));
    }
    const QSignalBlocker overlapBlocker(m_overlapCombo);
    const QSignalBlocker matchingBlocker(m_matchingCombo);
    const QSignalBlocker intensityBlocker(m_intensityCombo);
    const QSignalBlocker seamBlocker(m_seamCombo);
    const QSignalBlocker exportBlocker(m_exportCombo);
    selectData(m_overlapCombo, choices.estimateOverlap ? "overlap-estimation" : "");
    selectData(m_matchingCombo, choices.matchingStep);
    selectData(m_intensityCombo, choices.intensityMethod);
    selectData(m_seamCombo, choices.seamStep);
    selectData(m_exportCombo, choices.exportFormat);
    refreshVoteNote(choices);
    refreshStepList(choices);
    // La note du Vote 2D ou une étape a pu apparaître : la zone de défilement du panneau (RightPanel) recalcule la
    // hauteur de l'onglet, ce qu'elle ne fait pas d'elle-même quand le contenu change de taille.
    for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if (auto *scrollArea = qobject_cast<QScrollArea *>(ancestor)) {
            QCoreApplication::postEvent(scrollArea, new QEvent(QEvent::LayoutRequest));
            break;
        }
    }
}

void AssemblyTab::refreshVoteNote(const FrontendNs::PipelineChoices &choices)
{
    m_methodsForm->setRowVisible(m_voteNote, FrontendNs::usesVote2D(choices));
}

void AssemblyTab::refreshStepList(const FrontendNs::PipelineChoices &choices)
{
    const auto steps = FrontendUtils::pipelineSteps(choices);
    // Mêmes étapes : la liste est gardée telle quelle, avec l'étape sélectionnée pour « Relancer à partir d'ici ».
    bool isUnchanged = m_stepList->count() == static_cast<int>(steps.size());
    for (int row = 0; isUnchanged && row < m_stepList->count(); ++row)
        isUnchanged = m_stepList->item(row)->text() == steps[static_cast<std::size_t>(row)].displayName;
    if (isUnchanged)
        return;
    m_stepList->clear();
    for (std::size_t i = 0; i < steps.size(); ++i) {
        auto *item = new QListWidgetItem(steps[i].displayName, m_stepList);
        item->setData(Qt::UserRole, static_cast<int>(i + 1));
    }
    // Toutes les étapes visibles sans barre de défilement : c'est l'onglet entier qui défile.
    int height = 2 * m_stepList->frameWidth();
    for (int row = 0; row < m_stepList->count(); ++row)
        height += m_stepList->sizeHintForRow(row);
    m_stepList->setFixedHeight(height + 4);
    setActiveStep(m_activeStep);
}

void AssemblyTab::setActiveStep(int stepNumber)
{
    m_activeStep = stepNumber;
    if (!m_stepList) return;

    // Si aucune étape n'est active (ex. calcul terminé ou inactif), désélectionne la liste.
    if (stepNumber <= 0 || stepNumber > m_stepList->count()) {
        m_stepList->clearSelection();
        m_stepList->setCurrentRow(-1);
        return;
    }

    // Sélectionne et fait défiler automatiquement jusqu'à l'étape active.
    m_stepList->setCurrentRow(stepNumber - 1);
    m_stepList->scrollToItem(m_stepList->item(stepNumber - 1));
}
