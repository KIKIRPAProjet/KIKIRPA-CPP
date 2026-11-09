#include "dialog_helpers.h"

#include "frontendutils.h"
#include "simulationrunner.h"

#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
QString translate(const char *text)
{
    return QCoreApplication::translate("DialogHelpers", text);
}
}

void DialogHelpers::showReferenceComparisonDialog(QWidget *parent,
                                                   const QString &referencePath,
                                                   const QString &tilePath,
                                                   const QImage &referenceImage,
                                                   const QImage &tileImage)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(translate("Comparer avec l'image de référence"));
    dialog.resize(980, 620);
    auto *layout = new QVBoxLayout(&dialog);
    auto *imagesLayout = new QHBoxLayout;
    const auto addImageColumn = [&imagesLayout](const QString &title, const QString &path,
                                                const QImage &image) {
        auto *column = new QWidget;
        auto *columnLayout = new QVBoxLayout(column);
        auto *name = new QLabel(title + QStringLiteral("\n") + QFileInfo(path).fileName(),
                                column);
        name->setWordWrap(true);
        auto *preview = new QLabel(column);
        preview->setAlignment(Qt::AlignCenter);
        preview->setMinimumSize(400, 440);
        preview->setStyleSheet(QStringLiteral("background:#202020;"));
        preview->setPixmap(QPixmap::fromImage(image));
        columnLayout->addWidget(name);
        columnLayout->addWidget(preview, 1);
        imagesLayout->addWidget(column, 1);
    };
    addImageColumn(translate("Image de référence"), referencePath, referenceImage);
    addImageColumn(translate("Tuile sélectionnée"), tilePath, tileImage);
    layout->addLayout(imagesLayout, 1);
    layout->addWidget(FrontendUtils::mutedLabel(
        translate("Comparaison visuelle côte à côte ; aucune similarité automatique n'est calculée."),
        &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void DialogHelpers::showComparisonDialog(QWidget *parent,
                                         SimulationRunner *simulationRunner)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(translate("Comparer deux exécutions"));
    dialog.resize(440, 240);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        translate("Comparer les sorties de deux algorithmes de couture."), &dialog));
    auto *algorithms = new QHBoxLayout;
    auto *a = new QComboBox(&dialog);
    auto *b = new QComboBox(&dialog);
    const QStringList choices = {
        translate("Programmation dynamique"),
        translate("Coupe de graphe"),
        translate("Dijkstra")
    };
    a->addItems(choices);
    b->addItems(choices);
    b->setCurrentIndex(1);
    algorithms->addWidget(a);
    algorithms->addWidget(b);
    layout->addLayout(algorithms);
    layout->addWidget(new QLabel(
        translate("Mesures : saut à la couture · détails conservés · durée"), &dialog));
    auto *buttons = new QDialogButtonBox(&dialog);
    auto *show = buttons->addButton(translate("Comparer"), QDialogButtonBox::ActionRole);
    auto *keepA = buttons->addButton(translate("Garder A"), QDialogButtonBox::ActionRole);
    auto *keepB = buttons->addButton(translate("Garder B"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    QObject::connect(show, &QPushButton::clicked, &dialog, [&dialog, simulationRunner]() {
        simulationRunner->toggleComparison();
        dialog.accept();
    });
    QObject::connect(keepA, &QPushButton::clicked, &dialog, [&dialog, simulationRunner]() {
        simulationRunner->keepComparisonA();
        dialog.accept();
    });
    QObject::connect(keepB, &QPushButton::clicked, &dialog, [&dialog, simulationRunner]() {
        simulationRunner->keepComparisonB();
        dialog.accept();
    });
    dialog.exec();
}
