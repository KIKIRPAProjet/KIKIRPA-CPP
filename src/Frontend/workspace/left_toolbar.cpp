#include "Frontend/workspace/left_toolbar.h"

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

    const QStringList toolNames = {tr("Main (H)"), tr("Zoom (Z)"), tr("Sélection (V)"),
        tr("Pinceau (B)"), tr("Gomme (E)"), tr("Lasso (L)"), tr("Rectangle (M)"),
        tr("Point (P)"), tr("Mesure (I)")};

    const QStringList icons = {QStringLiteral("✋"), QStringLiteral("⌕"), QStringLiteral("↖"),
                               QStringLiteral("✎"), QStringLiteral("⌫"), QStringLiteral("⬠"),
                               QStringLiteral("▭"), QStringLiteral("＋"), QStringLiteral("⌖")};

    for (int i = 0; i < toolNames.size(); ++i) {
        auto *tool = new QToolButton(this);
        tool->setText(icons.at(i));
        tool->setToolTip(toolNames.at(i));
        tool->setFixedSize(kToolButtonSize, kToolButtonSize);
        toolLayout->addWidget(tool);

        connect(tool, &QToolButton::clicked, this, [this, toolNames, i]() {
            emit toolSelected(i, toolNames.at(i));
        });
    }

    toolLayout->addStretch();
}
