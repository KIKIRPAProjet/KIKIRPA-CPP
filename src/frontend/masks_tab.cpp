// src/frontend/masks_tab.cpp
#include "masks_tab.h"

#include "artworkcanvas.h"
#include "mainwindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QVBoxLayout>

namespace {

    constexpr int MAX_OPACITY_PERCENT = 100;

}

MasksTab::MasksTab(MainWindow *mainWindow, ArtworkCanvas *canvas, QWidget *parent)
    : QWidget(parent), m_mainWindow(mainWindow), m_canvas(canvas)
{
    auto *layersLayout = new QVBoxLayout(this);
    layersLayout->addWidget(new QLabel(tr("Calques")));
    const QStringList layerNames = {tr("Lignes de couture"), tr("Erreur d'alignement"),
                                    tr("Masques")};
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
            else if (name == tr("Erreur d'alignement")) {
                m_canvas->setAlignmentErrorVisible(checked);
            }
        });
    }
    imageLayerControls = new QGroupBox(tr("Images superposées"), this);
    auto *opacityForm = new QFormLayout(imageLayerControls);
    fusedMosaicCheck = new QCheckBox(tr("Afficher la mosaïque fusionnée"), imageLayerControls);
    fusedMosaicCheck->setObjectName(QStringLiteral("fusedMosaicCheck"));
    globalOpacitySlider = new QSlider(Qt::Horizontal, imageLayerControls);
    globalOpacitySlider->setObjectName(QStringLiteral("globalOpacitySlider"));
    globalOpacitySlider->setRange(0, MAX_OPACITY_PERCENT);
    globalOpacitySlider->setValue(MAX_OPACITY_PERCENT);
    layerSelector = new QComboBox(imageLayerControls);
    layerSelector->setObjectName(QStringLiteral("layerSelector"));
    layerSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    layerSelector->setPlaceholderText(tr("Aucune tuile"));
    layerOpacitySlider = new QSlider(Qt::Horizontal, imageLayerControls);
    layerOpacitySlider->setObjectName(QStringLiteral("layerOpacitySlider"));
    layerOpacitySlider->setRange(0, MAX_OPACITY_PERCENT);
    layerVisibilityCheck = new QCheckBox(tr("Afficher la tuile"), imageLayerControls);
    layerVisibilityCheck->setObjectName(QStringLiteral("layerVisibilityCheck"));

    opacityForm->addRow(fusedMosaicCheck);
    opacityForm->addRow(tr("Opacité globale"), globalOpacitySlider);
    opacityForm->addRow(tr("Tuile"), layerSelector);
    opacityForm->addRow(tr("Opacité de la tuile"), layerOpacitySlider);
    opacityForm->addRow(layerVisibilityCheck);
    layersLayout->addWidget(imageLayerControls);
    layersLayout->addStretch();
    imageLayerControls->setEnabled(false);
    syncSelectedLayerControls();

    connect(fusedMosaicCheck, &QCheckBox::toggled, this, [this](bool fused) {
        m_canvas->setFusedMosaicVisible(fused);
        syncSelectedLayerControls();
    });
    connect(globalOpacitySlider, &QSlider::valueChanged, this, [this](int percent) {
        m_canvas->setGlobalImageLayerOpacity(static_cast<qreal>(percent) / MAX_OPACITY_PERCENT);
    });
    connect(layerSelector, &QComboBox::currentIndexChanged, this, &MasksTab::syncSelectedLayerControls);
    connect(layerOpacitySlider, &QSlider::valueChanged, this, [this](int percent) {
        if (layerSelector->currentIndex() >= 0) {
            m_canvas->setImageLayerOpacity(layerSelector->currentData().toString(),
                                          static_cast<qreal>(percent) / MAX_OPACITY_PERCENT);
        }
    });
    connect(layerVisibilityCheck, &QCheckBox::toggled, this, [this](bool visible) {
        if (layerSelector->currentIndex() >= 0) {
            m_canvas->setImageLayerVisible(layerSelector->currentData().toString(), visible);
            layerOpacitySlider->setEnabled(visible);
        }
    });
}

void MasksTab::setImageLayers(const QVector<ImageLayer> &layers, const QRectF &bounds)
{
    m_canvas->setImageLayers(layers, bounds);

    const QSignalBlocker blockSelection(layerSelector);
    const QSignalBlocker blockGlobalOpacity(globalOpacitySlider);
    layerSelector->clear();
    for (const ImageLayer &layer : layers) {
        layerSelector->addItem(layer.name, layer.id);
    }

    fusedMosaicCheck->setChecked(false);
    globalOpacitySlider->setValue(MAX_OPACITY_PERCENT);
    layerSelector->setCurrentIndex(layers.isEmpty() ? -1 : 0);
    imageLayerControls->setEnabled(!layers.isEmpty());
    syncSelectedLayerControls();
}

void MasksTab::syncSelectedLayerControls()
{
    // Une nouvelle sélection relit les valeurs du canvas sans les réécrire.
    const ImageLayer *layer = layerSelector->currentIndex() >= 0
        ? m_canvas->findImageLayer(layerSelector->currentData().toString())
        : nullptr;
    const QSignalBlocker blockOpacity(layerOpacitySlider);
    const QSignalBlocker blockVisibility(layerVisibilityCheck);
    const bool visible = layer && layer->visible;
    const bool adjustingLayers = !fusedMosaicCheck->isChecked();
    globalOpacitySlider->setEnabled(adjustingLayers);
    layerSelector->setEnabled(adjustingLayers);
    layerVisibilityCheck->setEnabled(adjustingLayers && layer != nullptr);
    layerVisibilityCheck->setChecked(visible);
    layerOpacitySlider->setEnabled(adjustingLayers && visible);
    const qreal opacity = layer ? qBound(qreal(0), layer->opacity, qreal(1)) : 1.0;
    layerOpacitySlider->setValue(qRound(opacity * MAX_OPACITY_PERCENT));
}
