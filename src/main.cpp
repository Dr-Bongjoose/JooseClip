#include <QApplication>
#include "mainwindow.h"
#include "theme.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("JooseClip"));
    app.setOrganizationName(QStringLiteral("Dr-Bongjoose"));
    app.setStyleSheet(Theme::appStyleSheet()); // JooseBooks palette
    MainWindow w;
    w.show();
    return app.exec();
}