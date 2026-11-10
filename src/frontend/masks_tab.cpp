#include "masks_tab.h"
#include "mainwindow.h"
#include "artworkcanvas.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QSlider>
#include <QtMath>
MasksTab::MasksTab(MainWindow *mainWindow, ArtworkCanvas *canvas, QWidget *parent)
    : QWidget(parent), m_mainWindow(mainWindow), m_canvas(canvas)
{
    auto *layersLayout = new QVBoxLayout(this);
    layersLayout->addWidget(new QLabel(tr("Calques")));
    const QStringList layerNames = {tr("Lignes de couture"), tr("Erreur d'alignement"),
                                    tr("Tuiles"), tr("Mosaïque"), tr("Masques")};
    for (const QString &name : layerNames) {
        auto *check = new QCheckBox(name);
        check->setChecked(true);
        layersLayout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this, name](bool checked) {
            m_mainWindow->updateStatus(tr("Calque « %1 » %2.").arg(name, checked ? tr("affiché") : tr("masqué")));
            if (name == tr("Masques")) {
                m_mainWindow->maskVisible = checked;
                m_canvas->setMaskVisible(checked);
            }
            else if(name == tr("Lignes de couture")){
                m_canvas->setSeamVisible(checked);
            }
        });
    }
    layersLayout->addWidget(
    new QLabel(tr("Images superposées"), this)
);

    auto *imageControls = new QWidget(this);
    m_imageLayersLayout = new QVBoxLayout(imageControls);
    m_imageLayersLayout->setContentsMargins(0, 0, 0, 0);

    layersLayout->addWidget(imageControls);
    layersLayout->addStretch();
}


void MasksTab::setImageLayers(const QVector<ImageLayer> &layers)
{
    // Transmettre les couches au canvas
    m_canvas->setImageLayers(layers);

    // Supprimer les anciens contrôles
    while (QLayoutItem *item = m_imageLayersLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    // Créer les contrôles de chaque couche
    for (const ImageLayer &layer : layers) {
        auto *check = new QCheckBox(layer.name, this);
        check->setChecked(layer.visible);

        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(0, 100);
        slider->setValue(
            qRound(qBound(qreal(0), layer.opacity, qreal(1)) * 100)
        );
        slider->setEnabled(layer.visible);

        m_imageLayersLayout->addWidget(check);
        m_imageLayersLayout->addWidget(slider);

        // Identifier la couche concernée
        const QString id = layer.id;

        // Afficher ou masquer la couche
        connect(check, &QCheckBox::toggled,
                m_canvas, [canvas = m_canvas, id](bool visible) {
                    canvas->setImageLayerVisible(id, visible);
                });

        // Modifier son opacité
        connect(slider, &QSlider::valueChanged,
                m_canvas, [canvas = m_canvas, id](int value) {
                    canvas->setImageLayerOpacity(id, value / 100.0);
                });

        // Désactiver le curseur si la couche est masquée
        connect(check, &QCheckBox::toggled,
                slider, &QWidget::setEnabled);
    }
}
