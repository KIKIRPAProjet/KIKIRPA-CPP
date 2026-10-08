#pragma once
#include <QFutureWatcher>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace stitching::gui {

class MainWindow : public QWidget {
public:
    explicit MainWindow(QWidget* parent = nullptr);
private:
    void browse();
    void run();
    void appendLog(const QString& line);

    QLineEdit* dirEdit;
    QLineEdit* formatEdit;
    QComboBox* matcherBox;
    QComboBox* rejectorBox;
    QComboBox* seamBox;
    QComboBox* directionBox;
    QDoubleSpinBox* overlapSpin;
    QPushButton* runButton;
    QPlainTextEdit* log;
    QFutureWatcher<bool> watcher;
};

} // namespace stitching::gui
