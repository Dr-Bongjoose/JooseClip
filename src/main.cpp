#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("JooseClip"));
    MainWindow w;
    w.show();
    return app.exec();
}