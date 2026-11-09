#include "masks_tab.h"
#include "mainwindow.h"
#include "artworkcanvas.h"

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
            if (name == tr("Tuiles")) {
                m_mainWindow->tileContoursVisible = checked;
                m_canvas->setTileContoursVisible(checked);
            }
            if (name == tr("Masques")) {
                m_mainWindow->maskVisible = checked;
                m_canvas->setMaskVisible(checked);
            }
        });
        if (name == tr("Tuiles"))
            check->setChecked(m_mainWindow->tileContoursVisible);
        else if (name == tr("Masques"))
            check->setChecked(m_mainWindow->maskVisible);
    }
    layersLayout->addStretch();
}