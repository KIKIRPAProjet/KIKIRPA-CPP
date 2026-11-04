#include "project_setup_window.h"

#include "project_setup_model.h"

#include <QAbstractItemView>
#include <QChar>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace FrontendNs {

namespace {

constexpr int WINDOW_WIDTH = 980;
constexpr int WINDOW_HEIGHT = 760;
constexpr int CONTROL_SPACING = 20;
constexpr double MINIMUM_OVERLAP_PERCENT = 0.1;
constexpr double MAXIMUM_OVERLAP_PERCENT = 99.9;
constexpr double PERCENT_FACTOR = 100.0;

QHBoxLayout *directoryRow(QLineEdit *&lineEdit, const QString &buttonText,
                          QWidget *parent,
                          const std::function<void()> &chooseDirectory) {
  auto *layout = new QHBoxLayout();
  lineEdit = new QLineEdit(parent);
  lineEdit->setReadOnly(true);
  auto *button = new QPushButton(buttonText, parent);
  layout->addWidget(lineEdit, 1);
  layout->addWidget(button);
  QObject::connect(button, &QPushButton::clicked, parent, chooseDirectory);
  return layout;
}

} // namespace

ProjectSetupWindowClass::ProjectSetupWindowClass(QWidget *parent)
    : QMainWindow(parent) {
  setWindowTitle(tr("KIK-IRPA — Nouveau projet"));
  resize(WINDOW_WIDTH, WINDOW_HEIGHT);

  auto *centralWidget = new QWidget(this);
  auto *mainLayout = new QVBoxLayout(centralWidget);
  auto *title = new QLabel(tr("Nouveau projet d'assemblage"), centralWidget);
  title->setStyleSheet(QStringLiteral("font-size:22px;font-weight:600;"));
  mainLayout->addWidget(title);
  mainLayout->addWidget(
      new QLabel(tr("Sélectionnez les dossiers, organisez les tuiles puis "
                    "générez le JSON du pipeline."),
                 centralWidget));

  mainLayout->addWidget(createProjectGroup(centralWidget));
  mainLayout->addWidget(createSettingsGroup(centralWidget));
  mainLayout->addWidget(createTilesGroup(centralWidget), 1);
  addFooter(mainLayout, centralWidget);
  setCentralWidget(centralWidget);
  connectDimensionControls();
  refreshGrid();
}

QGroupBox *ProjectSetupWindowClass::createProjectGroup(QWidget *parent) {
  auto *projectGroup = new QGroupBox(tr("1. Projet et dossiers"), parent);
  auto *projectForm = new QFormLayout(projectGroup);
  projectNameEdit = new QLineEdit(projectGroup);
  projectNameEdit->setPlaceholderText(tr("Nom du projet"));
  projectForm->addRow(tr("Projet"), projectNameEdit);
  projectForm->addRow(tr("Dossier d'entrée"),
                      directoryRow(inputDirectoryEdit, tr("Choisir…"),
                                   projectGroup,
                                   [this] { chooseInputDirectory(); }));
  projectForm->addRow(tr("Dossier de sortie"),
                      directoryRow(outputDirectoryEdit, tr("Choisir…"),
                                   projectGroup,
                                   [this] { chooseOutputDirectory(); }));
  return projectGroup;
}

void ProjectSetupWindowClass::addGridDimensions(QFormLayout *form,
                                                QWidget *parent) {
  auto *dimensions = new QHBoxLayout();
  rowCountSpinBox = new QSpinBox(parent);
  rowCountSpinBox->setRange(1, MAX_GRID_DIMENSION);
  rowCountSpinBox->setValue(3);
  columnCountSpinBox = new QSpinBox(parent);
  columnCountSpinBox->setRange(1, MAX_GRID_DIMENSION);
  columnCountSpinBox->setValue(2);
  dimensions->addWidget(new QLabel(tr("Lignes"), parent));
  dimensions->addWidget(rowCountSpinBox);
  dimensions->addSpacing(CONTROL_SPACING);
  dimensions->addWidget(new QLabel(tr("Colonnes"), parent));
  dimensions->addWidget(columnCountSpinBox);
  dimensions->addStretch();
  form->addRow(tr("Dimensions"), dimensions);
}

QGroupBox *ProjectSetupWindowClass::createSettingsGroup(QWidget *parent) {
  auto *settingsGroup = new QGroupBox(tr("2. Grille et paramètres"), parent);
  auto *settingsLayout = new QFormLayout(settingsGroup);
  addGridDimensions(settingsLayout, settingsGroup);
  filenameFormatEdit =
      new QLineEdit(QStringLiteral(R"(img_C(\d+)_R(\d+)\.tif)"), settingsGroup);
  settingsLayout->addRow(tr("Format des noms"), filenameFormatEdit);
  overlapSpinBox = new QDoubleSpinBox(settingsGroup);
  overlapSpinBox->setRange(MINIMUM_OVERLAP_PERCENT, MAXIMUM_OVERLAP_PERCENT);
  overlapSpinBox->setDecimals(1);
  overlapSpinBox->setSuffix(tr(" %"));
  overlapSpinBox->setValue(DEFAULT_OVERLAP * PERCENT_FACTOR);
  settingsLayout->addRow(tr("Recouvrement"), overlapSpinBox);
  pyramidLevelSpinBox = new QSpinBox(settingsGroup);
  pyramidLevelSpinBox->setRange(0, MAX_PYRAMID_LEVEL);
  pyramidLevelSpinBox->setValue(0);
  settingsLayout->addRow(tr("Niveau de pyramide"), pyramidLevelSpinBox);
  return settingsGroup;
}

QGroupBox *ProjectSetupWindowClass::createTilesGroup(QWidget *parent) {
  auto *tilesGroup = new QGroupBox(tr("3. Organisation des tuiles"), parent);
  auto *tilesLayout = new QVBoxLayout(tilesGroup);
  auto *tileActions = new QHBoxLayout();
  auto *importButton = new QPushButton(tr("Importer les TIFF"), tilesGroup);
  auto *moveBeforeButton = new QPushButton(tr("Déplacer avant"), tilesGroup);
  auto *moveAfterButton = new QPushButton(tr("Déplacer après"), tilesGroup);
  tileActions->addWidget(importButton);
  tileActions->addWidget(moveBeforeButton);
  tileActions->addWidget(moveAfterButton);
  tileActions->addStretch();
  tilesLayout->addLayout(tileActions);
  tileGrid = new QTableWidget(3, 2, tilesGroup);
  tileGrid->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tileGrid->setSelectionMode(QAbstractItemView::SingleSelection);
  tileGrid->setSelectionBehavior(QAbstractItemView::SelectItems);
  tileGrid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  tileGrid->verticalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  tilesLayout->addWidget(tileGrid);
  connect(importButton, &QPushButton::clicked, this, [this] { importTiles(); });
  connect(moveBeforeButton, &QPushButton::clicked, this,
          [this] { moveSelectedTile(-1); });
  connect(moveAfterButton, &QPushButton::clicked, this,
          [this] { moveSelectedTile(1); });
  return tilesGroup;
}

void ProjectSetupWindowClass::addFooter(QVBoxLayout *layout, QWidget *parent) {
  auto *footer = new QHBoxLayout();
  statusLabel = new QLabel(tr("Aucune tuile importée."), parent);
  statusLabel->setWordWrap(true);
  generateButton = new QPushButton(tr("Valider et générer le JSON"), parent);
  generateButton->setDefault(true);
  footer->addWidget(statusLabel, 1);
  footer->addWidget(generateButton);
  layout->addLayout(footer);
  connect(generateButton, &QPushButton::clicked, this,
          [this] { generateJson(); });
}

void ProjectSetupWindowClass::connectDimensionControls() {
  connect(rowCountSpinBox, &QSpinBox::valueChanged, this, [this](int) {
    synchronizeModel();
    refreshGrid();
  });
  connect(columnCountSpinBox, &QSpinBox::valueChanged, this, [this](int) {
    synchronizeModel();
    refreshGrid();
  });
}

void ProjectSetupWindowClass::chooseInputDirectory() {
  const QString directory = QFileDialog::getExistingDirectory(
      this, tr("Choisir le dossier des tuiles"), inputDirectoryEdit->text());
  if (!directory.isEmpty())
    inputDirectoryEdit->setText(directory);
}

void ProjectSetupWindowClass::chooseOutputDirectory() {
  const QString directory = QFileDialog::getExistingDirectory(
      this, tr("Choisir le dossier de sortie"), outputDirectoryEdit->text());
  if (!directory.isEmpty())
    outputDirectoryEdit->setText(directory);
}

void ProjectSetupWindowClass::synchronizeModel() {
  ProjectSetup setup = model.getSetup();
  setup.projectName = projectNameEdit->text().trimmed().toStdString();
  setup.inputDirectory = inputDirectoryEdit->text().toStdString();
  setup.outputDirectory = outputDirectoryEdit->text().toStdString();
  setup.filenameFormat = filenameFormatEdit->text().toStdString();
  setup.overlap = overlapSpinBox->value() / PERCENT_FACTOR;
  setup.pyramidLevel = pyramidLevelSpinBox->value();
  model.setSetup(std::move(setup));
  model.resizeGrid(rowCountSpinBox->value(), columnCountSpinBox->value());
}

void ProjectSetupWindowClass::importTiles() {
  try {
    synchronizeModel();
    model.importTiles();
    refreshGrid();
    statusLabel->setText(
        tr("%1 tuile(s) importée(s). Organisez-les puis générez le JSON.")
            .arg(model.getSetup().tiles.size()));
  } catch (const std::exception &exception) {
    QMessageBox::warning(this, tr("Import impossible"),
                         QString::fromStdString(exception.what()));
  }
}

void ProjectSetupWindowClass::moveSelectedTile(int offset) {
  const int row = tileGrid->currentRow();
  const int column = tileGrid->currentColumn();
  if (row < 0 || column < 0)
    return;
  const int index = row * columnCountSpinBox->value() + column;
  const int target = index + offset;
  if (target < 0 || target >= static_cast<int>(model.getSetup().tiles.size()))
    return;
  model.swapTiles(static_cast<std::size_t>(index),
                  static_cast<std::size_t>(target));
  refreshGrid();
  tileGrid->setCurrentCell(target / columnCountSpinBox->value(),
                           target % columnCountSpinBox->value());
}

void ProjectSetupWindowClass::refreshGrid() {
  tileGrid->setRowCount(rowCountSpinBox->value());
  tileGrid->setColumnCount(columnCountSpinBox->value());
  tileGrid->clearContents();
  const auto &tiles = model.getSetup().tiles;
  for (std::size_t index = 0; index < tiles.size(); ++index) {
    const int row = static_cast<int>(index) / columnCountSpinBox->value();
    const int column = static_cast<int>(index) % columnCountSpinBox->value();
    if (row >= tileGrid->rowCount())
      break;
    auto *item = new QTableWidgetItem(
        QFileInfo(QString::fromStdString(tiles[index].path.string()))
            .fileName());
    item->setToolTip(QString::fromStdString(tiles[index].path.string()));
    tileGrid->setItem(row, column, item);
  }
  for (int column = 0; column < tileGrid->columnCount(); ++column)
    tileGrid->setHorizontalHeaderItem(
        column, new QTableWidgetItem(tr("Colonne %1").arg(column + 1)));
  for (int row = 0; row < tileGrid->rowCount(); ++row)
    tileGrid->setVerticalHeaderItem(
        row, new QTableWidgetItem(tr("Ligne %1").arg(row + 1)));
}

void ProjectSetupWindowClass::showValidation(
    const std::vector<std::string> &errors) {
  QString message;
  for (const std::string &error : errors)
    message += QStringLiteral("• ") + QString::fromStdString(error) +
               QLatin1Char('\n');
  statusLabel->setText(message.trimmed());
  QMessageBox::warning(this, tr("Configuration à corriger"), message.trimmed());
}

void ProjectSetupWindowClass::generateJson() {
  try {
    synchronizeModel();
    const std::vector<std::string> errors = model.validate();
    if (!errors.empty()) {
      showValidation(errors);
      return;
    }
    const std::filesystem::path path = model.writeBackendScript();
    statusLabel->setText(
        tr("JSON généré : %1").arg(QString::fromStdString(path.string())));
    QMessageBox::information(
        this, tr("Projet prêt"),
        tr("Le JSON a été validé par le backend et enregistré dans :\n%1")
            .arg(QString::fromStdString(path.string())));
  } catch (const std::exception &exception) {
    QMessageBox::critical(this, tr("Génération impossible"),
                          QString::fromStdString(exception.what()));
  }
}

} // namespace FrontendNs
