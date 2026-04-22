#include <QApplication>
#include <QFileDialog>

#include "fontviewer.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QString path;
    if (argc > 1) {
        path = QString::fromLocal8Bit(argv[1]);
    }
    else {
        path = QFileDialog::getOpenFileName(
            nullptr, "Open Font", {}, "Font Files (*.ttf *.otf *.otc *.ttc)");
    }
    if (path.isEmpty()) {
        return 0;
    }

    ViewOptionsPrivate d;
    d.dpr                                    = 1;
    d.theme                                  = 0;
    d.path                                   = path;
    d.viewer_type                            = "fontviewer";
    d.extras[ViewOptionsKeys::kKeyPluginCmd] = "-p 50";

    ViewOptions opts;
    opts.d_ptr = &d;

    FontViewer viewer;
    viewer.setWindowTitle(path);
    viewer.load(nullptr, &opts);
    viewer.resize(viewer.getContentSize());
    viewer.show();
    return a.exec();
}
