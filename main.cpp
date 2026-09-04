#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("File XOR Processor");
    app.setOrganizationName("FileXORProcessor");

    MainWindow window;
    window.show();

    return app.exec();
}
