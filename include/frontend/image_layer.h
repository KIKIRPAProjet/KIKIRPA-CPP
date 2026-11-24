
#ifndef IMAGE_LAYER_H
#define IMAGE_LAYER_H

#include <QImage>
#include <QRectF>
#include <QString>

struct ImageLayer
{
    QString id;                // Identifiant unique
    QString name;              // Nom affiché dans le panneau
    QImage image;              // Aperçu basse résolution
    QRectF worldRect;          // Position dans le repère commun
    qreal opacity = 1.0;       // Entre 0.0 et 1.0
    bool visible = true;       // Afficher ou masquer
};

#endif // IMAGE_LAYER_H
