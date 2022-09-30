#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QFontDatabase>
#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(int wnd_index, const QString &p, QWidget *parent = nullptr);
    ~MainWindow();

    bool init();

protected:
    virtual bool nativeEvent(const QByteArray &eventType,
                             void *message,
                             long *result) override;
    virtual bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI(const QStringList &names);

    void doResize(const QSize &sz);
    void onDPIChanged(qreal dpi);
    void onThemeChanged(int theme);
    void sendMsg2Seer(int sub_type, const QByteArray &d);
    QVariant getDataFromSeerMsg(const QByteArray &ba) const;

    void updatePreview();
    void updateInfo();
    void onNameChanged();
    void onStyleChanged();
    void onTabChanged();

    QList<ushort> m_char_indexes;
    QFontDatabase m_fdb;

    const int m_wnd_index;
    const QString m_path;

    Ui::MainWindow *ui;
};
#endif  // MAINWINDOW_H
