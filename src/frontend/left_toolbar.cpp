// src/frontend/left_toolbar.cpp
#include "left_toolbar.h"

#include <QVBoxLayout>
#include <QToolButton>
#include <QStringList>

namespace {
constexpr int kToolbarWidth = 42;
constexpr int kToolButtonSize = 32;
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

    const QStringList icons = {QStringLiteral("✋"), QStringLiteral("⌕"), QStringLiteral("↖"),
                               QStringLiteral("✎"), QStringLiteral("⌫"), QStringLiteral("⬠"),
                               QStringLiteral("▭"), QStringLiteral("＋"), QStringLiteral("⌖"),
                               QStringLiteral("✂")};

    for (int i = 0; i < m_toolNames.size(); ++i) {
        auto *tool = new QToolButton(this);
        tool->setText(icons.at(i));
        tool->setToolTip(m_toolNames.at(i));
        tool->setFixedSize(kToolButtonSize, kToolButtonSize);
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
