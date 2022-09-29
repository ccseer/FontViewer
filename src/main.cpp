#include <QApplication>
#include <QFile>

#include "mainwindow.h"
#include "oitvar.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    if (argc != 3) {
        return ERR_BAD_ARG;
    }
    const auto args = a.arguments();
    const auto p    = args[2];
    if (!QFile::exists(p)) {
        return ERR_FILE_NOT_FOUND;
    }

    a.setOrganizationName("Corey");
    a.setApplicationName("Seer");
    a.setOrganizationDomain("http://1218.io");
    a.setApplicationName("FontViewer");
    a.setApplicationDisplayName("FontViewer");

    MainWindow w(args[1].toInt(), p);
    if (!w.init()) {
        return ERR_PROCESS;
    }
    return a.exec();
}
