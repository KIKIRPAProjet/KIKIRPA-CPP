// include/frontend/left_toolbar.h
#ifndef LEFT_TOOLBAR_H
#define LEFT_TOOLBAR_H

#include <QStringList>
#include <QVector>
#include <QWidget>

class QToolButton;

class LeftToolbar : public QWidget
{
    Q_OBJECT
public:
    // Index standard de l'outil de recadrage dans la barre d'outils.
    static constexpr int kCropToolIndex = 9;

    explicit LeftToolbar(QWidget *parent = nullptr);

    // Sélectionne programmatiquement un outil par son index dans la liste.
    void selectTool(int index);

    // Affiche ou masque un outil de la barre par son index.
    void setToolVisible(int index, bool visible);

signals:
    // Émis quand l'utilisateur clique sur un outil (index dans la liste, nom affichable).
    void toolSelected(int index, const QString &name);

private:
    QVector<QToolButton *> m_toolButtons;
    QStringList m_toolNames;
};

#endif // LEFT_TOOLBAR_H

