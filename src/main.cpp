#include "mainwindow.h"

#include <QtCore/QString>
#include <QtWidgets/QApplication>

int main(int argumentCount, char *arguments[]) {
  QApplication application(argumentCount, arguments); // NOLINT(misc-include-cleaner)
  application.setApplicationName(QStringLiteral("KIK-IRPA Assemblage")); // NOLINT(misc-include-cleaner)
  application.setOrganizationName(QStringLiteral("KIK-IRPA")); // NOLINT(misc-include-cleaner)

  MainWindow window;
  window.show();
  return application.exec();
}
