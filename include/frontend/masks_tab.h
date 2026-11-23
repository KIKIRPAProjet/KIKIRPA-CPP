#ifndef MASKS_TAB_H
#define MASKS_TAB_H

#include <QWidget>

class MainWindow;
class ArtworkCanvas;

class MasksTab : public QWidget
{
    Q_OBJECT
public:
    explicit MasksTab(MainWindow *mainWindow, ArtworkCanvas *canvas, QWidget *parent = nullptr);

private:
    MainWindow *m_mainWindow;
    ArtworkCanvas *m_canvas;
};

#endif // MASKS_TAB_H