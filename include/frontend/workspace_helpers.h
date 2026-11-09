#ifndef WORKSPACE_HELPERS_H
#define WORKSPACE_HELPERS_H

#include <QLabel>
#include <QPushButton>
#include <QStringList>

inline const QStringList &workspaceModeNames()
{
    static const QStringList names = {
        QObject::tr("Projet"), QObject::tr("Tuiles"), QObject::tr("Masques"),
        QObject::tr("Alignement"), QObject::tr("Assemblage"), QObject::tr("Export")
    };
    return names;
}

inline QPushButton *makeWorkspaceButton(const QString &text, QWidget *parent,
                                        bool primary = false)
{
    auto *button = new QPushButton(text, parent);
    button->setProperty("primary", primary);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

inline QLabel *makeMutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("muted", true);
    return label;
}

#endif
