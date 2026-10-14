#include "MainWindow.h"
#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    stitching::gui::MainWindow window;
    window.show();
    return app.exec();
}
