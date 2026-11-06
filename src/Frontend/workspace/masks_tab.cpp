#include "Frontend/workspace/masks_tab.h"
#include "Frontend/app/mainwindow.h"
#include "Frontend/canvas/artworkcanvas.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QCheckBox>

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
                static_cast<ArtworkCanvas *>(m_canvas)->setMaskVisible(checked);
            }
        });
    }
    layersLayout->addStretch();
}