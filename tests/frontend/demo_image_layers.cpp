
#include "artworkcanvas.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Première image : fond bleu
    QImage base(500, 320, QImage::Format_ARGB32);
    base.fill(QColor("#2864A0"));

    {
        QPainter p(&base);
        p.setPen(QPen(Qt::white, 6));
        p.drawEllipse(120, 60, 220, 180);
    }

    // Deuxième image : fond orange
    QImage overlay(500, 320, QImage::Format_ARGB32);
    overlay.fill(QColor("#E58B34"));

    {
        QPainter p(&overlay);
        p.setPen(QPen(Qt::black, 6));
        p.drawLine(40, 40, 450, 270);
    }

    ImageLayer layer1;
    layer1.id = "base";
    layer1.name = "Couche bleue";
    layer1.image = base;
    layer1.worldRect = QRectF(0, 0, 500, 320);
    layer1.opacity = 1.0;

    ImageLayer layer2;
    layer2.id = "overlay";
    layer2.name = "Couche orange";
    layer2.image = overlay;
    layer2.worldRect = QRectF(150, 70, 500, 320);
    layer2.opacity = 0.5;

    QVector<ImageLayer> layers = {layer1, layer2};

    QWidget window;
    window.setWindowTitle("Test - Superposition des couches");

    auto *layout = new QVBoxLayout(&window);

    auto *canvas = new ArtworkCanvas(&window);
    canvas->setMinimumSize(650, 450);
    canvas->setSeamVisible(false);
    canvas->setImageLayers(layers);

    layout->addWidget(canvas);

    auto *label = new QLabel("Opacité couche orange : 50 %", &window);
    layout->addWidget(label);

    auto *slider = new QSlider(Qt::Horizontal, &window);
    slider->setRange(0, 100);
    slider->setValue(50);
    layout->addWidget(slider);

    auto *visibleCheck = new QCheckBox(
        "Afficher la couche orange", &window
    );
    visibleCheck->setChecked(true);
    layout->addWidget(visibleCheck);

    QObject::connect(
        slider, &QSlider::valueChanged,
        &window, [canvas, label, &layers](int value) {
            layers[1].opacity = value / 100.0;
            canvas->setImageLayers(layers);
            label->setText(
                QString("Opacité couche orange : %1 %").arg(value)
            );
        }
    );

    QObject::connect(
        visibleCheck, &QCheckBox::toggled,
        &window, [canvas, &layers](bool visible) {
            layers[1].visible = visible;
            canvas->setImageLayers(layers);
        }
    );

    window.resize(850, 650);
    window.show();

    return app.exec();
}
