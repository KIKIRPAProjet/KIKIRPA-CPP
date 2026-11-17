
#include "frontend/image_layer_loader.h"

#include "core/pipeline/pipeline.h"
#include "core/pipeline/pipeline_builder.h"
#include "core/data/tile.h"
#include "core/data/algorithm_types.h"

#include <nlohmann/json.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSlider>
#include <QTimer>
#include <QTransform>
#include <QVBoxLayout>
#include <QWidget>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Vérifier que le dossier des images est fourni.
    if (argc < 2) {
        std::cerr
            << "Utilisation : demo_real_image_layers <dossier_images>\n";
        return 1;
    }

    try {
        // =====================================================
        // 1. CONFIGURATION DU PIPELINE
        // =====================================================

        const std::string imageDirectory = argv[1];

        // ImportStep exige :
        // groupe 1 = colonne
        // groupe 2 = ligne
        //
        // Les fichiers ont la forme NOM_r00_c01.tif.
        // Le lookahead permet de capturer d'abord la colonne.

        const std::string filenamePattern =
            R"((?=.*_c([0-9]+)\.tiff?$).*_r([0-9]+)_c[0-9]+\.tiff?)";

        using Json = nlohmann::json;

        const Json script = {
            {"pipeline", {
                {"steps", Json::array({
                    {
                        {"name", "import"},
                        {"parameters", Json::array({
                            {
                                {"valueType", "string"},
                                {"name", "path"},
                                {"value", imageDirectory}
                            },
                            {
                                {"valueType", "string"},
                                {"name", "format"},
                                {"value", filenamePattern}
                            }
                        })}
                    },
                    {
                        {"name", "phase-correlation"}
                    },
                    {
                        {"name", "absolute-pos"}
                    }
                })}
            }}
        };

        // =====================================================
        // 2. CONSTRUCTION DU PIPELINE
        // =====================================================

        std::cout
            << "\n=== Construction du pipeline ===\n";

        auto pipeline = PipelineNs::buildPipeline(script);

        // =====================================================
        // 3. EXECUTION DES ALGORITHMES
        // =====================================================

        std::cout
            << "\n=== Execution du pipeline ===\n";

        const bool success = pipeline.run(
            [](const std::string &message) {
                std::cout << message << std::endl;
            }
        );

        if (!success) {
            throw std::runtime_error(
                "Le pipeline de positionnement a echoue"
            );
        }

        // =====================================================
        // 4. RECUPERATION DES RESULTATS
        // =====================================================

        const auto &store = pipeline.getDataStore();

        const auto &tiles =
            store.get<DataNs::TileList>("tiles");

        const auto &positions =
            store.get<std::vector<DataNs::TilePositionData>>(
                "positions"
            );

        std::cout
            << "\n=== Positions calculees ===\n";

        for (const auto &position : positions) {
            std::cout
                << position.tileId
                << " : x = " << position.position.x
                << ", y = " << position.position.y
                << ", rotation = "
                << position.rotationRadians
                << '\n';
        }

        // =====================================================
        // 5. CREATION DES COUCHES BASSE RESOLUTION
        // =====================================================

        constexpr int previewLevel = 3;

        QVector<ImageLayer> layers =
            buildImageLayers(
                tiles,
                positions,
                previewLevel
            );

        if (layers.isEmpty()) {
            throw std::runtime_error(
                "Aucune couche d'image creee"
            );
        }

        std::cout
            << "\nNombre de couches : "
            << layers.size()
            << '\n';

        // =====================================================
        // 6. CREATION DE LA FENETRE QT
        // =====================================================

        QWidget window;

        window.setWindowTitle(
            "KIKIRPA - Superposition des TIFF"
        );

        auto *mainLayout = new QVBoxLayout(&window);

        auto *title = new QLabel(
            "Superposition des images avec positions calculees"
        );

        mainLayout->addWidget(title);

        // Scene contenant les images.
        auto *scene = new QGraphicsScene(&window);

        // Zone graphique qui affiche la scene.
        auto *view = new QGraphicsView(scene);

        view->setMinimumSize(850, 600);

        view->setRenderHint(
            QPainter::SmoothPixmapTransform
        );

        view->setDragMode(
            QGraphicsView::ScrollHandDrag
        );

        mainLayout->addWidget(view);

        // =====================================================
        // 7. AFFICHAGE DES COUCHES
        // =====================================================

        for (const ImageLayer &layer : layers) {

            // Convertir QImage en QPixmap.
            const QPixmap pixmap =
                QPixmap::fromImage(layer.image);

            if (pixmap.isNull()) {
                throw std::runtime_error(
                    "Impossible de convertir une couche en QPixmap"
                );
            }

            // Ajouter l'image a la scene.
            QGraphicsPixmapItem *item =
                scene->addPixmap(pixmap);

            // Position calculee dans le repere basse resolution.
            item->setPos(
                layer.worldRect.x(),
                layer.worldRect.y()
            );

            // Adapter l'echelle du pixmap au rectangle de la couche.
            const double scaleX =
                layer.worldRect.width() / pixmap.width();

            const double scaleY =
                layer.worldRect.height() / pixmap.height();

            item->setTransform(
                QTransform::fromScale(scaleX, scaleY)
            );

            item->setOpacity(layer.opacity);
            item->setVisible(layer.visible);

            // =================================================
            // 8. CONTROLES D'OPACITE ET DE VISIBILITE
            // =================================================

            auto *controls = new QHBoxLayout();

            auto *checkbox = new QCheckBox(layer.name);
            checkbox->setChecked(layer.visible);

            auto *slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, 100);
            slider->setValue(
                static_cast<int>(layer.opacity * 100.0)
            );

            auto *percentage = new QLabel(
                QString::number(slider->value()) + " %"
            );

            // Changer l'opacite.
            QObject::connect(
                slider,
                &QSlider::valueChanged,
                &window,
                [item, percentage](int value) {
                    item->setOpacity(value / 100.0);

                    percentage->setText(
                        QString::number(value) + " %"
                    );
                }
            );

            // Afficher ou masquer la couche.
            QObject::connect(
                checkbox,
                &QCheckBox::toggled,
                &window,
                [item](bool visible) {
                    item->setVisible(visible);
                }
            );

            controls->addWidget(checkbox);
            controls->addWidget(slider);
            controls->addWidget(percentage);

            mainLayout->addLayout(controls);
        }

        // =====================================================
        // 9. CADRAGE DE LA MOSAIQUE
        // =====================================================

        scene->setSceneRect(
            scene->itemsBoundingRect().adjusted(
                -20, -20, 20, 20
            )
        );

        window.resize(1100, 850);
        window.show();

        // Adapter le zoom a la scene apres affichage.
        QTimer::singleShot(
            0,
            &window,
            [view, scene]() {
                view->fitInView(
                    scene->sceneRect(),
                    Qt::KeepAspectRatio
                );
            }
        );

        // =====================================================
        // 10. LANCEMENT DE QT
        // =====================================================

        return app.exec();

    } catch (const std::exception &error) {

        std::cerr
            << "\nErreur : "
            << error.what()
            << '\n';

        return 1;
    }
}
