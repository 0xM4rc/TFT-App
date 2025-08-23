#include <QApplication>
#include <QTimer>
#include <QDebug>
#include "core/controller.h"
#include "gui/mainwindow.h"
#include <QCoreApplication>


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow w;
    w.show();

    return app.exec();
}

