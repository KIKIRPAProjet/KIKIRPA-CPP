#ifndef LEFT_TOOLBAR_H
#define LEFT_TOOLBAR_H

#include <QWidget>

class LeftToolbar : public QWidget
{
    Q_OBJECT
public:
    explicit LeftToolbar(QWidget *parent = nullptr);

signals:
    // Émis quand l'utilisateur clique sur un outil (index dans la liste, nom affichable).
    void toolSelected(int index, const QString &name);
};

#endif // LEFT_TOOLBAR_H
