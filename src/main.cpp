#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>
#include "mainwindow.h"
#include "theme.h"

int main(int argc, char *argv[]) {
    QElapsedTimer startup;
    startup.start();

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("JooseClip"));
    app.setOrganizationName(QStringLiteral("Dr-Bongjoose"));
    const qint64 afterQApp = startup.nsecsElapsed(); // includes plugin scan
    app.setStyleSheet(Theme::appStyleSheet()); // JooseBooks palette

    MainWindow w;
    const qint64 afterWindow = startup.nsecsElapsed(); // + docks/UI/fx panel
    w.show();

    // Fires on the first event-loop pass — the moment the window is up and
    // responsive: the number the user perceives as "how long it took to load".
    // fprintf (not qWarning) so it lands in the launching terminal directly.
    QTimer::singleShot(0, [&]() {
        const qint64 now = startup.nsecsElapsed();
        fprintf(stderr, "[perf] startup: qapp+plugins %lld ms; window-built %lld ms; "
                        "first-frame %lld ms (total)\n",
                afterQApp / 1000000, (afterWindow - afterQApp) / 1000000,
                now / 1000000);
    });

    return app.exec();
}