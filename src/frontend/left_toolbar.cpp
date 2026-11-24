// src/frontend/left_toolbar.cpp
#include "left_toolbar.h"

#include <QButtonGroup>
#include <QIcon>
#include <QKeySequence>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QVBoxLayout>
#include <QToolButton>
#include <QStringList>

namespace {
constexpr int kToolbarWidth = 42;
constexpr int kToolButtonSize = 32;
constexpr int kIconSize = 20;

QIcon makeToolIcon(int index)
{
    QPixmap pm(kIconSize, kIconSize);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(QStringLiteral("#e2e2e2")), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (index) {
    case 0: { // Main (pan)
        p.drawLine(QPointF(6.5, 9.5), QPointF(6.5, 4.8));
        p.drawLine(QPointF(9.2, 9.0), QPointF(9.2, 3.6));
        p.drawLine(QPointF(11.9, 9.0), QPointF(11.9, 4.2));
        p.drawLine(QPointF(14.5, 10.0), QPointF(14.5, 6.2));
        QPainterPath palm;
        palm.moveTo(6.5, 9.5);
        palm.lineTo(4.6, 11.2);
        palm.quadTo(6.2, 16.2, 10.5, 16.4);
        palm.quadTo(14.8, 16.4, 15.0, 10.2);
        p.drawPath(palm);
        break;
    }
    case 1: { // Zoom (loupe)
        p.drawEllipse(QRectF(4.0, 4.0, 9.0, 9.0));
        p.drawLine(QPointF(11.8, 11.8), QPointF(16.2, 16.2));
        break;
    }
    case 2: { // Sélection (flèche)
        QPolygonF arrow;
        arrow << QPointF(5.5, 3.5) << QPointF(5.5, 15.0) << QPointF(8.8, 12.0)
              << QPointF(11.4, 16.5) << QPointF(13.2, 15.5) << QPointF(10.6, 11.1)
              << QPointF(14.8, 10.8);
        p.setBrush(QColor(QStringLiteral("#e2e2e2")));
        p.drawPolygon(arrow);
        break;
    }
    case 3: { // Pinceau / Bic
        p.drawLine(QPointF(14.8, 4.2), QPointF(7.2, 11.8));
        p.drawLine(QPointF(16.2, 5.6), QPointF(8.6, 13.2));
        p.drawLine(QPointF(14.8, 4.2), QPointF(16.2, 5.6));
        QPolygonF tip;
        tip << QPointF(7.2, 11.8) << QPointF(8.6, 13.2) << QPointF(4.4, 15.6);
        p.setBrush(QColor(QStringLiteral("#e2e2e2")));
        p.drawPolygon(tip);
        break;
    }
    case 4: { // Gomme
        QPolygonF body;
        body << QPointF(10.5, 4.2) << QPointF(16.0, 9.7)
             << QPointF(9.6, 16.0) << QPointF(4.1, 10.5);
        p.drawPolygon(body);
        p.drawLine(QPointF(7.3, 7.4), QPointF(12.8, 12.9));
        p.drawLine(QPointF(9.6, 16.0), QPointF(15.8, 16.0));
        break;
    }
    case 5: { // Lasso
        QPen dashPen(QColor(QStringLiteral("#e2e2e2")), 1.5, Qt::DashLine, Qt::RoundCap);
        p.setPen(dashPen);
        p.drawEllipse(QRectF(4.2, 4.2, 11.6, 9.4));
        p.setPen(pen);
        p.drawLine(QPointF(10.0, 13.6), QPointF(8.2, 16.6));
        break;
    }
    case 6: { // Rectangle
        QPen dashPen(QColor(QStringLiteral("#e2e2e2")), 1.5, Qt::DashLine, Qt::SquareCap);
        p.setPen(dashPen);
        p.drawRect(QRectF(4.2, 5.2, 11.6, 9.6));
        break;
    }
    case 7: { // Point (cible)
        p.drawLine(QPointF(10.0, 4.0), QPointF(10.0, 16.0));
        p.drawLine(QPointF(4.0, 10.0), QPointF(16.0, 10.0));
        p.drawEllipse(QRectF(7.2, 7.2, 5.6, 5.6));
        break;
    }
    case 8: { // Mesure (règle)
        p.drawLine(QPointF(4.2, 13.8), QPointF(15.8, 6.2));
        p.drawLine(QPointF(6.2, 12.5), QPointF(7.2, 14.0));
        p.drawLine(QPointF(9.8, 10.1), QPointF(11.1, 12.1));
        p.drawLine(QPointF(13.5, 7.7), QPointF(14.5, 9.2));
        break;
    }
    case 9: { // Recadrer (deux coins de cadre)
        p.drawLine(QPointF(6.0, 3.5), QPointF(6.0, 14.0));
        p.drawLine(QPointF(6.0, 14.0), QPointF(16.5, 14.0));
        p.drawLine(QPointF(3.5, 6.0), QPointF(14.0, 6.0));
        p.drawLine(QPointF(14.0, 6.0), QPointF(14.0, 16.5));
        break;
    }
    default:
        break;
    }

    return QIcon(pm);
}
}

LeftToolbar::LeftToolbar(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(kToolbarWidth);
    auto *toolLayout = new QVBoxLayout(this);
    toolLayout->setContentsMargins(4, 5, 4, 5);

    m_toolNames = {tr("Main (H)"), tr("Zoom (Z)"), tr("Sélection (V)"),
        tr("Pinceau (B)"), tr("Gomme (E)"), tr("Lasso (L)"), tr("Rectangle (M)"),
        tr("Point (P)"), tr("Mesure (I)"), tr("Recadrer (C)")};

    const QList<Qt::Key> shortcuts = {Qt::Key_H, Qt::Key_Z, Qt::Key_V,
                                      Qt::Key_B, Qt::Key_E, Qt::Key_L,
                                      Qt::Key_M, Qt::Key_P, Qt::Key_I, Qt::Key_C};

    auto *toolGroup = new QButtonGroup(this);
    toolGroup->setExclusive(true);

    for (int i = 0; i < m_toolNames.size(); ++i) {
        auto *tool = new QToolButton(this);
        tool->setIcon(makeToolIcon(i));
        tool->setIconSize(QSize(18, 18));
        tool->setToolTip(m_toolNames.at(i));
        tool->setShortcut(QKeySequence(shortcuts.at(i)));
        tool->setFixedSize(kToolButtonSize, kToolButtonSize);
        tool->setCheckable(true);
        if (i == 0)
            tool->setChecked(true);
        toolGroup->addButton(tool, i);
        if (i == kCropToolIndex) {
            tool->setVisible(false); // L'outil de recadrage est masqué par défaut hors du mode Image finale
        }
        toolLayout->addWidget(tool);
        m_toolButtons.append(tool);

        connect(tool, &QToolButton::clicked, this, [this, i]() {
            emit toolSelected(i, m_toolNames.at(i));
        });
    }

    toolLayout->addStretch();
}

void LeftToolbar::selectTool(int index)
{
    if (index >= 0 && index < m_toolButtons.size()) {
        m_toolButtons.at(index)->animateClick();
    }
}

void LeftToolbar::setToolVisible(int index, bool visible)
{
    if (index >= 0 && index < m_toolButtons.size()) {
        m_toolButtons.at(index)->setVisible(visible);
    }
}
