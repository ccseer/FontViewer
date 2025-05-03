#include <QApplication>
#include <QFile>

#include "fontviewer.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    FontViewer viewer;
    auto p      = std::make_unique<ViewOptions>();
    p->d->dpr   = 1;
    p->d->theme = 1;
    p->d->path  = "C:/D/2.ttf";
    p->d->path  = "D:/2.ttf";
    // p->d->path  = "C:/D/1.eot";
    p->d->type  = viewer.name();
    viewer.setWindowTitle(p->d->path);
    viewer.load(nullptr, std::move(p));
    viewer.resize(viewer.getContentSize());
    viewer.show();
    return a.exec();
}
