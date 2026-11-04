#pragma once

#include "project_setup_model.h"

#include <QMainWindow>
#include <QWidget>

#include <string>
#include <vector>

class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QVBoxLayout;

namespace FrontendNs {

class ProjectSetupWindowClass : public QMainWindow {

public:
  explicit ProjectSetupWindowClass(QWidget *parent = nullptr);

private:
  QGroupBox *createProjectGroup(QWidget *parent);
  QGroupBox *createSettingsGroup(QWidget *parent);
  QGroupBox *createTilesGroup(QWidget *parent);
  void addGridDimensions(QFormLayout *form, QWidget *parent);
  void addFooter(QVBoxLayout *layout, QWidget *parent);
  void connectDimensionControls();
  void chooseInputDirectory();
  void chooseOutputDirectory();
  void importTiles();
  void moveSelectedTile(int offset);
  void generateJson();
  void synchronizeModel();
  void refreshGrid();
  void showValidation(const std::vector<std::string> &errors);

  ProjectSetupModelClass model;
  QLineEdit *projectNameEdit = nullptr;
  QLineEdit *inputDirectoryEdit = nullptr;
  QLineEdit *outputDirectoryEdit = nullptr;
  QLineEdit *filenameFormatEdit = nullptr;
  QSpinBox *rowCountSpinBox = nullptr;
  QSpinBox *columnCountSpinBox = nullptr;
  QDoubleSpinBox *overlapSpinBox = nullptr;
  QSpinBox *pyramidLevelSpinBox = nullptr;
  QTableWidget *tileGrid = nullptr;
  QLabel *statusLabel = nullptr;
  QPushButton *generateButton = nullptr;
};

} // namespace FrontendNs
