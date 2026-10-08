#include "MainWindow.h"
#include "algorithms/Factory.h"
#include "app/PipelineBuilder.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QtConcurrent/QtConcurrentRun>

namespace stitching::gui {

namespace {
// Remplit une liste avec les noms enregistrés dans la Factory
QComboBox* comboFrom(const std::vector<std::string>& names) {
    auto* box = new QComboBox;
    for (const auto& n : names) box->addItem(QString::fromStdString(n));
    return box;
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle("Stitching");
    PipelineSettings defaults;

    dirEdit = new QLineEdit(QString::fromStdString(defaults.imageDir.string()));
    auto* browseButton = new QPushButton("…");
    auto* dirRow = new QHBoxLayout;
    dirRow->addWidget(dirEdit);
    dirRow->addWidget(browseButton);

    patternEdit = new QLineEdit(QString::fromStdString(defaults.pattern));
    matcherBox  = comboFrom(featureMatcherNames());
    rejectorBox = comboFrom(outlierRejectorNames());
    seamBox     = comboFrom(seamFinderNames());
    matcherBox->setCurrentText(QString::fromStdString(defaults.matcher));
    rejectorBox->setCurrentText(QString::fromStdString(defaults.rejector));
    seamBox->setCurrentText(QString::fromStdString(defaults.seamFinder));

    directionBox = new QComboBox;
    directionBox->addItem("B right of A",  static_cast<int>(Direction::Right));
    directionBox->addItem("B below A",     static_cast<int>(Direction::Bottom));
    directionBox->addItem("B left of A",   static_cast<int>(Direction::Left));
    directionBox->addItem("B above A",     static_cast<int>(Direction::Top));

    overlapSpin = new QDoubleSpinBox;
    overlapSpin->setRange(0.0, 1.0);
    overlapSpin->setSingleStep(0.05);
    overlapSpin->setValue(defaults.overlap);

    auto* form = new QFormLayout;
    form->addRow("Image folder", dirRow);
    form->addRow("File pattern", patternEdit);
    form->addRow("Feature matcher", matcherBox);
    form->addRow("Outlier rejection", rejectorBox);
    form->addRow("Seamline", seamBox);
    form->addRow("Direction", directionBox);
    form->addRow("Overlap", overlapSpin);

    runButton = new QPushButton("Run");
    log = new QPlainTextEdit;
    log->setReadOnly(true);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(runButton);
    layout->addWidget(log, 1);
    resize(520, 480);

    connect(browseButton, &QPushButton::clicked, this, [this] { browse(); });
    connect(runButton, &QPushButton::clicked, this, [this] { run(); });
    connect(&watcher, &QFutureWatcher<bool>::finished, this, [this] {
        appendLog(watcher.result() ? "Pipeline OK" : "Pipeline KO");
        runButton->setEnabled(true);
    });
}

void MainWindow::browse() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Image folder", dirEdit->text());
    if (!dir.isEmpty()) dirEdit->setText(dir);
}

void MainWindow::appendLog(const QString& line) {
    log->appendPlainText(line);
}

void MainWindow::run() {
    PipelineSettings s;
    s.imageDir   = dirEdit->text().toStdString();
    s.pattern    = patternEdit->text().toStdString();
    s.matcher    = matcherBox->currentText().toStdString();
    s.rejector   = rejectorBox->currentText().toStdString();
    s.seamFinder = seamBox->currentText().toStdString();
    s.direction  = static_cast<Direction>(directionBox->currentData().toInt());
    s.overlap    = static_cast<float>(overlapSpin->value());

    log->clear();
    runButton->setEnabled(false);

    // Le pipeline tourne hors du thread GUI ; les logs y sont renvoyés
    auto post = [this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, line = QString::fromStdString(msg)] {
            appendLog(line);
        }, Qt::QueuedConnection);
    };
    watcher.setFuture(QtConcurrent::run([s, post] {
        try {
            Tuile a, b;
            Pipeline p = buildPipeline(s, a, b);
            return p.run(post);
        } catch (const std::exception& e) {
            post(std::string("Error: ") + e.what());
            return false;
        }
    }));
}

} // namespace stitching::gui
