#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPaintEvent>
#include <QStringList>
#include <QVector>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    struct PipelineSnapshot
    {
        int stepNumber = 1;
        int methodIndex = 0;
        QString description;
        QStringList parameterValues;
    };

    void paintEvent(QPaintEvent *event) override;
    void saveCurrentStepParameters();
    void displayStepParameters(int stepIndex);
    PipelineSnapshot captureCurrentSnapshot(int stepNumber) const;
    void displaySnapshot(const PipelineSnapshot &snapshot);
    void restoreSnapshot(const PipelineSnapshot &snapshot);

    Ui::MainWindow *ui;
    QVector<PipelineSnapshot> executionSnapshots;
    QVector<QStringList> stepParameterValues;
    int currentParameterStepIndex = -1;
    int lastRestoredStepNumber = 0;
};
#endif // MAINWINDOW_H
