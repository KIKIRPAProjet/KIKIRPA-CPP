#include "mainwindow.h"

#include <QApplication>
#include <QString>

int main(int argumentCount, char *arguments[]) {
  QApplication application(argumentCount, arguments);
  application.setApplicationName(QStringLiteral("KIK-IRPA Assemblage"));
  application.setOrganizationName(QStringLiteral("KIK-IRPA"));

  MainWindow window;
  window.show();
  return application.exec();
}
