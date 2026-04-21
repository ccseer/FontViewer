#include <QApplication>
#include <QFile>

#include "fontviewer.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    FontViewer viewer;
    auto p = new ViewOptions();
    p->d_ptr = new ViewOptionsPrivate();
    p->d_ptr->dpr   = 1;
    p->d_ptr->theme = 1;
    p->d_ptr->path  = "D:/2.ttf";
    p->d_ptr->viewer_type = viewer.name();
    viewer.setWindowTitle(p->d_ptr->path);
    viewer.load(nullptr, p);
    viewer.resize(viewer.getContentSize());
    viewer.show();
    return a.exec();
}
