#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QDialog>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QListWidget>
#include <memory>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #f5f0e6; }"
        "QWidget { background: #f5f0e6; color: #302b25; }"
        "QWidget#centralwidget, QStackedWidget, QWidget#homePage, QWidget#imageLoadingPage,"
        " QWidget#resumePage, QWidget#pipelineModificationPage, QWidget#executionSummaryPage"
        " { background: transparent; }"
        "QPushButton { background: #d8c6a8; color: #302b25; border: 1px solid #a9906d;"
        " border-radius: 4px; padding: 10px; }"
        "QPushButton:hover { background: #e5d5ba; }"
        "QPushButton:pressed { background: #c5af8c; }"
        "QComboBox, QLineEdit { background: #fffdf8; color: #302b25;"
        " border: 1px solid #bda98a; border-radius: 4px; padding: 7px; }"
        "QLineEdit[readOnly=\"true\"] { background: #e4dfd5; color: #625d55;"
        " border-color: #b0a99d; }"
        "QLabel#homeTitleLabel { color: #765b3c; font-size: 28px;"
        " font-weight: bold; padding: 18px; }"
        "QLabel#executionSummaryTitleLabel { color: #765b3c; font-size: 24px;"
        " font-weight: bold; padding: 14px; }"
        "QLabel#executionSummaryHintLabel { color: #806f59; padding: 6px; }"
        "QLabel#restorationStartLabel { color: #765b3c; font-weight: bold; padding: 8px; }"
        "QLabel#executionRestoredStateLabel { color: #765b3c; padding: 8px; }"
        "QLabel#executionStepsLabel { color: #765b3c; font-weight: bold; padding-top: 8px; }"
        "QListWidget { background: #fffdf8; color: #302b25; border: 1px solid #bda98a;"
        " border-radius: 4px; padding: 6px; }"
        "QListWidget::item { padding: 10px; border-bottom: 1px solid #e3d8c6; }"
        "QListWidget::item:selected { background: #d8c6a8; color: #302b25; }"
        "QProgressBar { background: #fffdf8; border: 1px solid #bda98a;"
        " border-radius: 3px; text-align: center; }"
        "QProgressBar::chunk { background: #8b9c64; border-radius: 2px; }"));

    stepParameterValues.resize(5);
    const int stepValueCounts[] = {4, 3, 4, 5, 5};
    for (int stepIndex = 0; stepIndex < stepParameterValues.size(); ++stepIndex) {
        for (int valueIndex = 0; valueIndex < stepValueCounts[stepIndex]; ++valueIndex) {
            stepParameterValues[stepIndex].append(QString());
        }
    }
    connect(ui->methodComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int stepIndex) {
                saveCurrentStepParameters();
                displayStepParameters(stepIndex);
            });
    displayStepParameters(ui->methodComboBox->currentIndex());

    connect(ui->loadImageButton, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentWidget(ui->imageLoadingPage);
    });

    connect(ui->backButton, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentWidget(ui->homePage);
    });

    connect(ui->resumeButton, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentWidget(ui->resumePage);
    });

    connect(ui->pipelineModificationButton, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentWidget(ui->pipelineModificationPage);
    });

    connect(ui->resumeBackButton, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentWidget(ui->homePage);
    });

    connect(ui->pipelineModificationBackButton, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentWidget(ui->homePage);
    });

    connect(ui->executionSummaryFinishButton, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentWidget(ui->homePage);
    });

    connect(ui->executionStepsListWidget, &QListWidget::currentRowChanged, this,
            [this](int row) {
                if (row >= 0 && row < executionSnapshots.size()) {
                    displaySnapshot(executionSnapshots.at(row));
                }
            });

    connect(ui->executionSummaryRestoreButton, &QPushButton::clicked, this, [this]() {
        const int row = ui->executionStepsListWidget->currentRow();
        if (row >= 0 && row < executionSnapshots.size()) {
            const PipelineSnapshot &snapshot = executionSnapshots.at(row);
            restoreSnapshot(snapshot);
            ui->restorationStartLabel->setText(
                QStringLiteral("Vous êtes en restauration à partir de l'étape %1.")
                    .arg(lastRestoredStepNumber));
            ui->restorationStartLabel->show();
            ui->resumeExecutionButton->setEnabled(true);
            ui->stackedWidget->setCurrentWidget(ui->imageLoadingPage);
        }
    });

    const auto startExecution = [this](int startingStep) {
        saveCurrentStepParameters();
        ui->executeButton->setEnabled(false);
        ui->resumeExecutionButton->setEnabled(false);
        if (startingStep == 1) {
            lastRestoredStepNumber = 0;
            ui->restorationStartLabel->hide();
            displayStepParameters(ui->methodComboBox->currentIndex());
        }

        while (executionSnapshots.size() >= startingStep) {
            executionSnapshots.removeLast();
            delete ui->executionStepsListWidget->takeItem(
                ui->executionStepsListWidget->count() - 1);
        }

        auto *progressDialog = new QDialog(this);
        progressDialog->setWindowTitle(QStringLiteral("Exécution"));
        progressDialog->setWindowModality(Qt::WindowModal);
        progressDialog->setAttribute(Qt::WA_DeleteOnClose);

        auto *layout = new QVBoxLayout(progressDialog);
        auto *stepLabel = new QLabel(progressDialog);
        auto *globalLabel = new QLabel(QStringLiteral("Progression globale"), progressDialog);
        auto *globalProgress = new QProgressBar(progressDialog);
        auto *stepLabelTitle = new QLabel(QStringLiteral("Progression de l'étape"), progressDialog);
        auto *stepProgress = new QProgressBar(progressDialog);
        auto *pauseButton = new QPushButton(QStringLiteral("Pause"), progressDialog);
        auto currentStep = std::make_shared<int>(startingStep);
        auto stepElapsed = std::make_shared<int>(0);

        globalProgress->setRange(0, 100);
        stepProgress->setRange(0, 100);
        globalProgress->setValue((startingStep - 1) * 20);
        stepLabel->setText(QStringLiteral("Étape %1...").arg(startingStep));
        layout->addWidget(stepLabel);
        layout->addWidget(globalLabel);
        layout->addWidget(globalProgress);
        layout->addWidget(stepLabelTitle);
        layout->addWidget(stepProgress);
        layout->addWidget(pauseButton);

        auto *progressTimer = new QTimer(progressDialog);
        progressTimer->setInterval(100);
        connect(progressTimer, &QTimer::timeout, progressDialog, [this, progressDialog,
                                                                   progressTimer, stepLabel,
                                                                   globalProgress, stepProgress,
                                                                   currentStep, stepElapsed]() {
            *stepElapsed += progressTimer->interval();
            stepProgress->setValue(*stepElapsed * 100 / 2000);
            globalProgress->setValue((*currentStep - 1) * 20 + *stepElapsed / 100);
            stepLabel->setText(QStringLiteral("Étape %1...").arg(*currentStep));

            if (*stepElapsed >= 2000) {
                executionSnapshots.append(captureCurrentSnapshot(*currentStep));
                ui->executionStepsListWidget->addItem(
                    QStringLiteral("Étape %1").arg(*currentStep));

                if (*currentStep == 5) {
                    progressTimer->stop();
                    progressDialog->close();
                    ui->executeButton->setEnabled(true);
                    ui->executionStepsListWidget->setCurrentRow(
                        ui->executionStepsListWidget->count() - 1);
                    ui->stackedWidget->setCurrentWidget(ui->executionSummaryPage);
                    return;
                }

                ++*currentStep;
                *stepElapsed = 0;
                stepProgress->setValue(0);
            }
        });

        connect(pauseButton, &QPushButton::clicked, progressDialog, [progressTimer,
                                                                      pauseButton,
                                                                      stepProgress,
                                                                      stepElapsed]() {
            if (progressTimer->isActive()) {
                progressTimer->stop();
                pauseButton->setText(QStringLiteral("Reprendre"));
                *stepElapsed = 0;
                stepProgress->setValue(0);
            } else {
                pauseButton->setText(QStringLiteral("Pause"));
                progressTimer->start();
            }
        });

        progressDialog->show();
        progressTimer->start(100);
    };

    connect(ui->executeButton, &QPushButton::clicked, this,
            [startExecution]() { startExecution(1); });
    connect(ui->resumeExecutionButton, &QPushButton::clicked, this,
            [this, startExecution]() {
                if (lastRestoredStepNumber > 0) {
                    startExecution(lastRestoredStepNumber);
                }
            });
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::saveCurrentStepParameters()
{
    if (currentParameterStepIndex < 0 || currentParameterStepIndex >= stepParameterValues.size()) {
        return;
    }

    QStringList &values = stepParameterValues[currentParameterStepIndex];
    values.clear();
    const auto fields = ui->methodParametersContainer->findChildren<QLineEdit *>();
    for (const QLineEdit *field : fields) {
        values.append(field->text());
    }
}

void MainWindow::displayStepParameters(int stepIndex)
{
    QFormLayout *parametersLayout =
        qobject_cast<QFormLayout *>(ui->methodParametersContainer->layout());

    while (QLayoutItem *item = parametersLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    currentParameterStepIndex = stepIndex;
    const int valueCounts[] = {4, 3, 4, 5, 5};
    const int numberOfValues = valueCounts[stepIndex];
    const QString stepName = ui->methodComboBox->itemText(stepIndex);
    const bool readOnly = lastRestoredStepNumber > 0 && stepIndex + 1 < lastRestoredStepNumber;

    for (int valueIndex = 0; valueIndex < numberOfValues; ++valueIndex) {
        auto *label = new QLabel(
            QStringLiteral("%1 - valeur %2 :").arg(stepName).arg(valueIndex + 1),
            ui->methodParametersContainer);
        auto *lineEdit = new QLineEdit(ui->methodParametersContainer);
        lineEdit->setObjectName(
            QStringLiteral("step%1Value%2").arg(stepIndex + 1).arg(valueIndex + 1));
        lineEdit->setReadOnly(readOnly);
        if (readOnly) {
            lineEdit->setCursor(Qt::ForbiddenCursor);
            lineEdit->setToolTip(QStringLiteral("Cette étape est déjà exécutée et ne peut pas être modifiée."));
        }
        if (valueIndex < stepParameterValues[stepIndex].size()) {
            lineEdit->setText(stepParameterValues[stepIndex].at(valueIndex));
        }
        parametersLayout->addRow(label, lineEdit);
    }
}

MainWindow::PipelineSnapshot MainWindow::captureCurrentSnapshot(int stepNumber) const
{
    PipelineSnapshot snapshot;
    snapshot.stepNumber = stepNumber;
    snapshot.methodIndex = stepNumber - 1;

    const QString descriptions[] = {
        QStringLiteral("Préparation des données d'entrée et vérification du pipeline."),
        QStringLiteral("Application du prétraitement et normalisation des paramètres."),
        QStringLiteral("Exécution de la transformation principale sur les données."),
        QStringLiteral("Contrôle intermédiaire et ajustement des résultats."),
        QStringLiteral("Finalisation du pipeline et préparation du résultat.")
    };
    snapshot.description = descriptions[stepNumber - 1];

    snapshot.parameterValues = stepParameterValues.at(snapshot.methodIndex);

    return snapshot;
}

void MainWindow::displaySnapshot(const PipelineSnapshot &snapshot)
{
    QStringList values;
    for (int index = 0; index < snapshot.parameterValues.size(); ++index) {
        values.append(QStringLiteral("valeur %1 : %2")
                          .arg(index + 1)
                          .arg(snapshot.parameterValues.at(index)));
    }

    ui->executionRestoredStateLabel->setText(
        QStringLiteral("Description : %1\n"
                       "Étape : %2\n%3")
            .arg(snapshot.description,
                 QStringLiteral("Étape %1").arg(snapshot.stepNumber),
                 values.join(QStringLiteral("\n"))));
}

void MainWindow::restoreSnapshot(const PipelineSnapshot &snapshot)
{
    saveCurrentStepParameters();
    for (const PipelineSnapshot &savedSnapshot : executionSnapshots) {
        stepParameterValues[savedSnapshot.methodIndex] = savedSnapshot.parameterValues;
    }
    stepParameterValues[snapshot.methodIndex] = snapshot.parameterValues;

    lastRestoredStepNumber = snapshot.stepNumber;
    currentParameterStepIndex = -1;
    ui->methodComboBox->setCurrentIndex(snapshot.methodIndex);
    if (currentParameterStepIndex == -1) {
        displayStepParameters(snapshot.methodIndex);
    }
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(245, 240, 230));
    painter.setPen(QPen(QColor(122, 101, 73, 58), 1));
    painter.setBrush(Qt::NoBrush);

    const QRect area = rect().adjusted(18, 18, -18, -18);
    const int left = area.left();
    const int right = area.right();
    const int top = area.top();
    const int bottom = area.bottom();

    painter.drawArc(QRect(left - 155, top - 110, 300, 300), 270 * 16, 90 * 16);
    painter.drawArc(QRect(left - 108, top - 64, 208, 208), 270 * 16, 90 * 16);
    painter.drawArc(QRect(right - 145, bottom - 145, 300, 300), 90 * 16, 90 * 16);
    painter.drawArc(QRect(right - 100, bottom - 100, 208, 208), 90 * 16, 90 * 16);
    painter.drawLine(left + 34, top + 34, left + 88, top + 34);
    painter.drawLine(left + 34, top + 34, left + 34, top + 88);
    painter.drawLine(right - 34, bottom - 34, right - 88, bottom - 34);
    painter.drawLine(right - 34, bottom - 34, right - 34, bottom - 88);

}
